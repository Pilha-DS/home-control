package com.home.backend.device;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.eclipse.paho.client.mqttv3.IMqttClient;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Component;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Publica comandos e configuracoes nos topicos MQTT ja usados pelo firmware
 * (home/&lt;user_id&gt;/&lt;uuid&gt;/cmd e /config).
 */
@Component
@ConditionalOnProperty(name = "app.transport", havingValue = "mqtt")
public class MqttPublisher {

    private final IMqttClient mqttClient;
    private final ObjectMapper objectMapper;

    public MqttPublisher(IMqttClient mqttClient, ObjectMapper objectMapper) {
        this.mqttClient = mqttClient;
        this.objectMapper = objectMapper;
    }

    public void publishCommand(String userId, String uuid, CommandRequest cmd, String deviceToken) {
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("poke", cmd.poke());
        payload.put("time", cmd.timeMs() == null ? 0 : cmd.timeMs());
        if (cmd.pin() != null) {
            payload.put("pin", cmd.pin());
        }
        if (deviceToken != null && !deviceToken.isBlank()) {
            payload.put("USER_SECRET", deviceToken);
            payload.put("token", deviceToken);
        }
        publish("home/" + userId + "/" + uuid + "/cmd", payload);
    }

    public void publishConfig(String userId, String uuid, ConfigRequest cfg, String deviceToken) {
        Map<String, Object> payload = new LinkedHashMap<>();
        payload.put("PASS", cfg.adminPass());
        if (deviceToken != null && !deviceToken.isBlank()) {
            payload.put("USER_SECRET", deviceToken);
            payload.put("token", deviceToken);
        }
        putIfPresent(payload, "UUID", cfg.newUuid());
        putIfPresent(payload, "USER_ID", cfg.userId());
        putIfPresent(payload, "MODELO", cfg.modelo());
        putIfPresent(payload, "WIFI_SSID", cfg.wifiSsid());
        putIfPresent(payload, "WIFI_PASS", cfg.wifiPass());
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
        publish("home/" + userId + "/" + uuid + "/config", payload);
    }

    private void putIfPresent(Map<String, Object> map, String key, String value) {
        if (value != null && !value.isBlank()) map.put(key, value);
    }

    private void publish(String topic, Map<String, Object> payload) {
        try {
            String json = objectMapper.writeValueAsString(payload);
            MqttMessage msg = new MqttMessage(json.getBytes());
            msg.setQos(1);
            mqttClient.publish(topic, msg);
        } catch (JsonProcessingException | org.eclipse.paho.client.mqttv3.MqttException e) {
            throw new IllegalStateException("Falha ao publicar em " + topic + ": " + e.getMessage(), e);
        }
    }
}
