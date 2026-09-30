package com.home.backend.device;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Component;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Enfileira cmd/config para o ESP buscar em GET /api/esp/{uuid}/poll.
 */
@Component
@ConditionalOnProperty(name = "app.transport", havingValue = "java", matchIfMissing = true)
public class JavaDeviceCommandPublisher implements DeviceCommandPublisher {

    private final DeviceCommandQueue commandQueue;
    private final ObjectMapper objectMapper;

    public JavaDeviceCommandPublisher(DeviceCommandQueue commandQueue, ObjectMapper objectMapper) {
        this.commandQueue = commandQueue;
        this.objectMapper = objectMapper;
    }

    @Override
    public void publishCommand(String userId, String uuid, CommandRequest cmd, String deviceToken) {
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("poke", cmd.poke());
        payload.put("time", cmd.timeMs() == null ? 0 : cmd.timeMs());
        if (cmd.pin() != null) payload.put("pin", cmd.pin());
        // Nao embute USER_SECRET/token no cmd: o poll HTTP ja autenticou o ESP.
        // Secret no JSON quebrava ESPs com token truncado (tokenOk falhava).
        commandQueue.enqueue(userId, uuid, "cmd", toJson(payload));
    }

    @Override
    public void publishConfig(String userId, String uuid, ConfigRequest cfg, String deviceToken) {
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("PASS", cfg.adminPass());
        putUserSecret(payload, deviceToken);
        putIfPresent(payload, "UUID", cfg.newUuid());
        putIfPresent(payload, "USER_ID", cfg.userId());
        putIfPresent(payload, "MODELO", cfg.modelo());
        putIfPresent(payload, "WIFI_SSID", cfg.wifiSsid());
        putIfPresent(payload, "WIFI_PASS", cfg.wifiPass());
        putIfPresent(payload, "JAVA_HOST", cfg.javaHost());
        if (cfg.javaPort() != null && cfg.javaPort() > 0) payload.put("JAVA_PORT", cfg.javaPort());
        // Compat: firmware antigo ainda le MQTT_* na config remota
        putIfPresent(payload, "MQTT_HOST", cfg.mqttHost());
        if (cfg.mqttPort() != null && cfg.mqttPort() > 0) payload.put("MQTT_PORT", cfg.mqttPort());
        putIfPresent(payload, "MQTT_USER", cfg.mqttUser());
        putIfPresent(payload, "MQTT_PASS", cfg.mqttPass());
        putIfPresent(payload, "MQTT_PATH", cfg.mqttPath());
        if (cfg.logicaDoRele() != null && (cfg.logicaDoRele() == 1 || cfg.logicaDoRele() == 2)) {
            payload.put("LOGICA_DO_RELE", cfg.logicaDoRele());
        }
        putIfPresent(payload, "NEW_PASS", cfg.newAdminPass());
        if (cfg.apOpen() != null && (cfg.apOpen() == 0 || cfg.apOpen() == 1)) {
            payload.put("AP_OPEN", cfg.apOpen());
        }
        commandQueue.enqueue(userId, uuid, "config", toJson(payload));
    }

    private void putIfPresent(Map<String, Object> map, String key, String value) {
        if (value != null && !value.isBlank()) map.put(key, value);
    }

    /** Secret do usuario — firmware grava em EEPROM e envia em X-User-Secret. */
    private void putUserSecret(Map<String, Object> payload, String secret) {
        if (secret == null || secret.isBlank()) return;
        payload.put("USER_SECRET", secret);
        payload.put("token", secret);
    }

    private String toJson(Map<String, Object> payload) {
        try {
            return objectMapper.writeValueAsString(payload);
        } catch (JsonProcessingException e) {
            throw new IllegalStateException("Falha ao serializar comando: " + e.getMessage(), e);
        }
    }
}
