package com.home.backend.device;

import com.home.backend.auth.CurrentUser;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import org.springframework.security.access.prepost.PreAuthorize;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Monitor global de ESPs (somente admin) — dispositivos cadastrados + atividade recente.
 */
@RestController
@RequestMapping("/api/admin/esp-monitor")
@PreAuthorize("hasRole('ADMIN')")
@Tag(name = "ESP Monitor", description = "Visão global de Arduinos falando com o servidor (admin)")
public class AdminEspMonitorController {

    private static final long ONLINE_MS = DevicePresence.ONLINE_MS;

    private final DeviceRepository deviceRepository;
    private final DeviceStateCache stateCache;
    private final EspActivityLog activityLog;
    private final CurrentUser currentUser;

    public AdminEspMonitorController(DeviceRepository deviceRepository,
                                     DeviceStateCache stateCache,
                                     EspActivityLog activityLog,
                                     CurrentUser currentUser) {
        this.deviceRepository = deviceRepository;
        this.stateCache = stateCache;
        this.activityLog = activityLog;
        this.currentUser = currentUser;
    }

    @GetMapping
    @Operation(summary = "Snapshot global", description = "Lista devices + hits recentes do gateway ESP")
    public Map<String, Object> snapshot(
            @RequestParam(defaultValue = "120") int activityLimit) {

        if (!currentUser.isAdmin()) {
            throw new IllegalArgumentException("acesso_negado");
        }

        long now = System.currentTimeMillis();
        Map<String, Map<String, Object>> live = stateCache.snapshotAll();
        List<Device> registered = deviceRepository.findAll();
        Set<String> seen = new HashSet<>();

        List<Map<String, Object>> devices = new ArrayList<>();
        for (Device d : registered) {
            String cacheKey = DeviceStateCache.key(d.getUserId(), d.getUuid());
            seen.add(cacheKey);
            Map<String, Object> state = stateCache.get(d.getUserId(), d.getUuid());
            devices.add(deviceRow(d.getUuid(), d.getUserId(), d.getName(), d.getModelo(),
                    d.isActive(), true, state, now));
        }

        // UUIDs com telemetria em cache mas ainda nao (ou nunca) no banco
        for (Map.Entry<String, Map<String, Object>> e : live.entrySet()) {
            if (seen.contains(e.getKey())) continue;
            seen.add(e.getKey());
            String[] parts = splitCacheKey(e.getKey());
            devices.add(deviceRow(parts[1], parts[0], parts[1], null,
                    true, false, e.getValue(), now));
        }

        // UUIDs so no log de atividade (ex.: auth falhou)
        for (String uuid : activityLog.recentUuids(30 * 60_000L)) {
            if (seen.contains(uuid)) continue;
            seen.add(uuid);
            if (live.containsKey(uuid)) continue;
            devices.add(deviceRow(uuid, "", uuid, null, true, false, Map.of(), now));
        }

        devices.sort((a, b) -> Long.compare(
                ((Number) b.getOrDefault("lastSeenMs", 0L)).longValue(),
                ((Number) a.getOrDefault("lastSeenMs", 0L)).longValue()));

        List<Map<String, Object>> activity = activityLog.recent(activityLimit).stream()
                .map(h -> {
                    Map<String, Object> m = new LinkedHashMap<>();
                    m.put("at", h.at());
                    m.put("uuid", h.uuid());
                    m.put("action", h.action());
                    m.put("type", h.type());
                    m.put("ok", h.ok());
                    m.put("userId", h.userId());
                    m.put("detail", h.detail());
                    return m;
                })
                .toList();

        long online = devices.stream().filter(d -> Boolean.TRUE.equals(d.get("online"))).count();

        Map<String, Object> resp = new LinkedHashMap<>();
        resp.put("now", now);
        resp.put("onlineCount", online);
        resp.put("deviceCount", devices.size());
        resp.put("devices", devices);
        resp.put("activity", activity);
        return resp;
    }

    private static String[] splitCacheKey(String key) {
        int slash = key == null ? -1 : key.indexOf('/');
        if (slash < 0) return new String[] {"", key == null ? "" : key};
        return new String[] {key.substring(0, slash), key.substring(slash + 1)};
    }

    private static Map<String, Object> deviceRow(
            String uuid, String userId, String name, String modelo,
            boolean active, boolean registered, Map<String, Object> state, long now) {

        long lastSeen = 0L;
        Object ls = state.get("lastSeen");
        if (ls instanceof Number n) lastSeen = n.longValue();

        String ip = str(state.get("ip"));
        String status = str(state.get("status"));
        String lastType = "";
        // ultimo campo de telemetria conhecido (exceto lastSeen)
        for (String key : List.of("ack", "state", "status", "ip", "logs/basic", "logs/advance")) {
            if (state.containsKey(key)) {
                lastType = key;
                break;
            }
        }

        Map<String, Object> row = new LinkedHashMap<>();
        row.put("uuid", uuid);
        row.put("userId", userId == null ? "" : userId);
        row.put("name", name == null ? uuid : name);
        row.put("modelo", modelo);
        row.put("active", active);
        row.put("registered", registered);
        row.put("lastSeenMs", lastSeen);
        row.put("online", lastSeen > 0 && (now - lastSeen) <= ONLINE_MS);
        row.put("ip", ip);
        row.put("status", status);
        row.put("lastType", lastType);
        row.put("state", state);
        return row;
    }

    private static String str(Object v) {
        return v == null ? "" : v.toString().trim();
    }
}
