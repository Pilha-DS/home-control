package com.home.backend.device;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.time.Instant;
import java.util.Map;

/**
 * Resposta de dispositivo. {@code modelo} e telemetria vem do cache MQTT
 * (estado ao vivo) — o backend nao inventa placa; so repassa o que o ESP
 * publicou em {@code logs/basic}. Ate o primeiro {@code logs/basic} chegar,
 * o modelo fica no {@link #DEFAULT_MODELO} (padrao).
 */
public record DeviceResponse(
        String uuid,
        String userId,
        String name,
        String modelo,
        boolean active,
        Instant createdAt,
        Map<String, Object> state) {

    /** Modelo assumido enquanto o firmware nao mandou {@code logs/basic}. */
    public static final String DEFAULT_MODELO = "ESP-01";

    private static final ObjectMapper MAPPER = new ObjectMapper();

    public static DeviceResponse from(Device d, Map<String, Object> state) {
        // Fonte da verdade: telemetria ao vivo. DB so como fallback raro.
        String modelo = extractModeloFromState(state);
        if (modelo == null || modelo.isBlank()) {
            modelo = d.getModelo();
        }
        // Ainda sem logs/basic nem valor salvo: assume o padrao. Assim que o
        // firmware enviar logs/basic, o modelo real substitui este.
        if (modelo == null || modelo.isBlank()) {
            modelo = DEFAULT_MODELO;
        }
        return new DeviceResponse(
                d.getUuid(),
                d.getUserId(),
                d.getName(),
                modelo,
                d.isActive(),
                d.getCreatedAt(),
                state);
    }

    static String extractModeloFromState(Map<String, Object> state) {
        if (state == null || state.isEmpty()) return null;
        // "logs/basic" e o canonical; "basic" cobre cache antigo (bug do parser).
        Object basic = state.get("logs/basic");
        if (basic == null) basic = state.get("basic");
        if (basic == null) return null;
        try {
            JsonNode root = MAPPER.readTree(basic.toString());
            JsonNode m = root.get("modelo");
            if (m == null || m.isNull()) m = root.get("MODELO");
            if (m != null && m.isTextual() && !m.asText().isBlank()) {
                return m.asText().trim();
            }
        } catch (Exception ignored) {
            // payload nao-JSON
        }
        return null;
    }
}
