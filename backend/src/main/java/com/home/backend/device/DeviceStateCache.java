package com.home.backend.device;

import org.springframework.stereotype.Component;

import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Estado em memoria por usuario + UUID (UUID pode repetir entre contas).
 */
@Component
public class DeviceStateCache {

    private final Map<String, Map<String, Object>> states = new ConcurrentHashMap<>();

    public static String key(String userId, String uuid) {
        String u = userId == null ? "" : userId.trim();
        String id = uuid == null ? "" : uuid.trim().toLowerCase(Locale.ROOT);
        return u + "/" + id;
    }

    public void update(String userId, String uuid, String field, Object value) {
        Map<String, Object> s = states.computeIfAbsent(key(userId, uuid), k -> new ConcurrentHashMap<>());
        s.put(field, value);
        s.put("lastSeen", System.currentTimeMillis());
    }

    public void merge(String userId, String uuid, Map<String, Object> values) {
        Map<String, Object> s = states.computeIfAbsent(key(userId, uuid), k -> new ConcurrentHashMap<>());
        s.putAll(values);
        s.put("lastSeen", System.currentTimeMillis());
    }

    public Map<String, Object> get(String userId, String uuid) {
        return states.getOrDefault(key(userId, uuid), Map.of());
    }

    /** Copia superficial de todos os estados ao vivo (monitor admin). Chave: userId/uuid. */
    public Map<String, Map<String, Object>> snapshotAll() {
        Map<String, Map<String, Object>> out = new ConcurrentHashMap<>();
        states.forEach((k, v) -> out.put(k, Map.copyOf(v)));
        return out;
    }

    public void remove(String userId, String uuid) {
        states.remove(key(userId, uuid));
    }
}
