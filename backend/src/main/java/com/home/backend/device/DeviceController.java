package com.home.backend.device;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/devices")
@Tag(name = "Dispositivos", description = "CRUD, comandos e config remota (JWT)")
public class DeviceController {

    private final DeviceService deviceService;
    private final DeviceOutageService outageService;

    public DeviceController(DeviceService deviceService, DeviceOutageService outageService) {
        this.deviceService = deviceService;
        this.outageService = outageService;
    }

    @GetMapping
    @Operation(summary = "Lista Arduinos da conta",
            description = "JWT. Retorna dispositivos ativos persistidos no banco. "
                    + "Novos ESPs entram sozinhos na primeira telemetria/poll com X-User-Secret.")
    public List<DeviceResponse> list() {
        return deviceService.list();
    }

    @GetMapping("/outages")
    @Operation(summary = "Quedas dos Arduinos",
            description = "JWT (SK). Sem uuid: só a última queda da conta. "
                    + "Com uuid: página desse dispositivo (limit/offset, padrão 10).")
    public List<DeviceOutageSummaryResponse> outages(
            @RequestParam(required = false) String userId,
            @RequestParam(required = false) String uuid,
            @RequestParam(required = false, defaultValue = "10") int limit,
            @RequestParam(required = false, defaultValue = "0") int offset) {
        return outageService.query(userId, uuid, limit, offset);
    }

    @GetMapping("/{uuid}/outages")
    @Operation(summary = "Quedas de um Arduino",
            description = "JWT. Histórico paginado (limit/offset, padrão 10). "
                    + "outageCount = total; outages = página atual.")
    public DeviceOutageSummaryResponse outagesOf(
            @PathVariable String uuid,
            @RequestParam(required = false, defaultValue = "10") int limit,
            @RequestParam(required = false, defaultValue = "0") int offset) {
        return outageService.queryOne(null, uuid, limit, offset);
    }

    @GetMapping("/{uuid}")
    public DeviceResponse get(@PathVariable String uuid) {
        return deviceService.get(uuid);
    }

    @PostMapping
    public Map<String, String> create(@Valid @RequestBody DeviceRequest request) {
        return deviceService.create(request);
    }

    @PutMapping("/{uuid}")
    public DeviceResponse update(@PathVariable String uuid,
                                 @RequestBody DeviceUpdateRequest request) {
        return deviceService.update(uuid, request);
    }

    @DeleteMapping("/{uuid}")
    public ResponseEntity<?> delete(@PathVariable String uuid) {
        deviceService.delete(uuid);
        return ResponseEntity.ok(Map.of("ok", true));
    }

    @PostMapping("/{uuid}/command")
    public ResponseEntity<?> command(@PathVariable String uuid,
                                     @Valid @RequestBody CommandRequest request) {
        deviceService.command(uuid, request);
        return ResponseEntity.ok(Map.of("ok", true));
    }

    @PostMapping("/{uuid}/config")
    @Operation(summary = "Config remota", description = "Enfileira config para o ESP (poll). Campos vazios não alteram.")
    public ResponseEntity<?> config(@PathVariable String uuid,
                                    @Valid @RequestBody ConfigRequest request) {
        deviceService.config(uuid, request);
        return ResponseEntity.ok(Map.of("ok", true));
    }
}
