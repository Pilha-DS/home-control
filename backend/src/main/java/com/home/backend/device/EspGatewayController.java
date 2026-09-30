package com.home.backend.device;

import com.fasterxml.jackson.databind.ObjectMapper;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.security.SecurityRequirement;
import io.swagger.v3.oas.annotations.tags.Tag;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * API HTTP para o ESP8266 falar direto com o Java (sem broker MQTT).
 *
 * Autenticacao: header {@code X-User-Secret} (secret unico do usuario).
 * {@code X-Device-Token} ainda aceito por compatibilidade.
 */
@RestController
@RequestMapping("/api/esp/{uuid}")
@ConditionalOnProperty(name = "app.transport", havingValue = "java", matchIfMissing = true)
@Tag(name = "ESP Gateway", description = "HTTP direto ESP ↔ backend (header X-User-Secret)")
@SecurityRequirement(name = "espSecret")
public class EspGatewayController {

    private final DeviceGatewayService gatewayService;
    private final EspActivityLog activityLog;
    private final ObjectMapper objectMapper;

    public EspGatewayController(DeviceGatewayService gatewayService,
                                EspActivityLog activityLog,
                                ObjectMapper objectMapper) {
        this.gatewayService = gatewayService;
        this.activityLog = activityLog;
        this.objectMapper = objectMapper;
    }

    @PostMapping("/events")
    @Operation(summary = "Telemetria ESP",
            description = "ESP envia state, logs, ack, etc. Com X-User-Secret valido, "
                    + "o UUID e gravado automaticamente na conta do usuario.")
    public ResponseEntity<?> ingest(
            @PathVariable String uuid,
            @RequestHeader(value = "X-User-Secret", required = false) String userSecret,
            @RequestHeader(value = "X-Device-Token", required = false) String legacyToken,
            @RequestBody EspEventRequest body) {

        String type = body != null && body.type() != null ? body.type().trim() : "";
        var auth = gatewayService.authenticateEsp(uuid, resolveSecret(userSecret, legacyToken));
        if (auth.isEmpty()) {
            activityLog.record(uuid, "events", type, false, "", "token_invalido");
            return ResponseEntity.status(HttpStatus.UNAUTHORIZED)
                    .body(Map.of("error", "token_invalido"));
        }
        if (type.isBlank()) {
            activityLog.record(uuid, "events", "", false, auth.get().device().getUserId(), "type_obrigatorio");
            return ResponseEntity.badRequest().body(Map.of("error", "type_obrigatorio"));
        }

        var result = auth.get();
        gatewayService.ingestEvent(
                result.device(), result.newlyCreated(), type, body.payload());
        String detail = result.newlyCreated() ? "discovered" : "ok";
        activityLog.record(uuid, "events", type, true, result.device().getUserId(), detail);
        return ResponseEntity.ok(Map.of("ok", true));
    }

    @GetMapping("/poll")
    @Operation(summary = "Poll de comandos", description = "ESP busca cmd/config enfileirados pelo app. "
            + "Opcional: wait=1..20 (segundos) para long-poll.")
    public ResponseEntity<?> poll(
            @PathVariable String uuid,
            @RequestParam(value = "wait", defaultValue = "0") int waitSec,
            @RequestHeader(value = "X-User-Secret", required = false) String userSecret,
            @RequestHeader(value = "X-Device-Token", required = false) String legacyToken)
            throws InterruptedException {

        var auth = gatewayService.requireDeviceAuth(uuid, resolveSecret(userSecret, legacyToken));
        if (auth.isEmpty()) {
            activityLog.record(uuid, "poll", "", false, "", "token_invalido");
            return ResponseEntity.status(HttpStatus.UNAUTHORIZED)
                    .body(Map.of("error", "token_invalido"));
        }

        Device device = auth.get().device();
        gatewayService.notifyIfDiscovered(device, auth.get().newlyCreated());

        int waitMs = Math.min(Math.max(waitSec, 0), 20) * 1000;
        List<DeviceCommandQueue.PendingCommand> cmds = gatewayService.pollCommands(device);
        if (cmds.isEmpty() && waitMs > 0) {
            long deadline = System.currentTimeMillis() + waitMs;
            while (System.currentTimeMillis() < deadline) {
                Thread.sleep(40);
                cmds = gatewayService.pollCommands(device);
                if (!cmds.isEmpty()) break;
            }
        }
        gatewayService.touchPresence(device);

        List<Map<String, Object>> items = new ArrayList<>();
        for (DeviceCommandQueue.PendingCommand c : cmds) {
            Map<String, Object> item = new LinkedHashMap<>();
            item.put("type", c.type());
            try {
                item.put("payload", objectMapper.readTree(c.payload()));
            } catch (Exception e) {
                item.put("payload", c.payload());
            }
            items.add(item);
        }

        if (!items.isEmpty()) {
            activityLog.record(uuid, "poll", "commands", true, device.getUserId(),
                    items.size() + " cmd(s)");
        } else if (auth.get().newlyCreated()) {
            activityLog.record(uuid, "poll", "discovered", true, device.getUserId(), "ok");
        }

        Map<String, Object> resp = new LinkedHashMap<>();
        resp.put("commands", items);
        return ResponseEntity.ok(resp);
    }

    private static String resolveSecret(String userSecret, String legacyToken) {
        if (userSecret != null && !userSecret.isBlank()) return userSecret;
        return legacyToken;
    }

    public record EspEventRequest(String type, String payload) {}
}
