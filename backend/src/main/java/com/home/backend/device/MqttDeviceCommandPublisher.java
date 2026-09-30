package com.home.backend.device;

import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Component;

/**
 * Modo legado: publica cmd/config no broker MQTT.
 */
@Component
@ConditionalOnProperty(name = "app.transport", havingValue = "mqtt")
public class MqttDeviceCommandPublisher implements DeviceCommandPublisher {

    private final MqttPublisher mqttPublisher;

    public MqttDeviceCommandPublisher(MqttPublisher mqttPublisher) {
        this.mqttPublisher = mqttPublisher;
    }

    @Override
    public void publishCommand(String userId, String uuid, CommandRequest cmd, String deviceToken) {
        mqttPublisher.publishCommand(userId, uuid, cmd, deviceToken);
    }

    @Override
    public void publishConfig(String userId, String uuid, ConfigRequest cfg, String deviceToken) {
        mqttPublisher.publishConfig(userId, uuid, cfg, deviceToken);
    }
}
