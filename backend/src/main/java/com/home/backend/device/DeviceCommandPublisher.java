package com.home.backend.device;

/**
 * Publica comandos/config para o ESP (MQTT ou fila Java).
 */
public interface DeviceCommandPublisher {

    void publishCommand(String userId, String uuid, CommandRequest cmd, String deviceToken);

    void publishConfig(String userId, String uuid, ConfigRequest cfg, String deviceToken);
}
