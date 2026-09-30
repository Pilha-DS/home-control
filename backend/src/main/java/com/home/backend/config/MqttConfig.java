package com.home.backend.config;

import org.eclipse.paho.client.mqttv3.IMqttClient;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/**
 * Cliente MQTT do backend (conta de servico).
 * <p>
 * O firmware ESP usa {@code wss://host:443/} (WebSocket Seguro). O backend
 * deve usar o mesmo transporte para enxergar a telemetria dos devices.
 * <ul>
 *   <li>{@code MQTT_URL} — URI completa (sobrescreve tudo)</li>
 *   <li>Com {@code mqtt.path} preenchido → {@code wss://} / {@code ws://}</li>
 *   <li>Sem path → {@code ssl://} / {@code tcp://}</li>
 * </ul>
 */
@Configuration
@ConditionalOnProperty(name = "app.transport", havingValue = "mqtt")
public class MqttConfig {

    private static final Logger log = LoggerFactory.getLogger(MqttConfig.class);

    @Bean
    public MqttConnectOptions mqttConnectOptions(
            @Value("${mqtt.username}") String username,
            @Value("${mqtt.password}") String password) {

        MqttConnectOptions options = new MqttConnectOptions();
        options.setAutomaticReconnect(true);
        options.setCleanSession(true);
        options.setConnectionTimeout(20);
        options.setKeepAliveInterval(30);
        options.setMaxReconnectDelay(30_000);
        if (username != null && !username.isBlank()) {
            options.setUserName(username);
            options.setPassword(password == null ? new char[0] : password.toCharArray());
        }
        return options;
    }

    @Bean(destroyMethod = "disconnect")
    public IMqttClient mqttClient(
            MqttConnectOptions options,
            @Value("${mqtt.host}") String host,
            @Value("${mqtt.port}") int port,
            @Value("${mqtt.path:}") String path,
            @Value("${mqtt.use-ssl:true}") boolean useSsl,
            @Value("${mqtt.client-id-prefix:home_backend}") String clientIdPrefix,
            @Value("${MQTT_URL:}") String mqttUrl) throws MqttException {

        String serverURI = resolveServerUri(mqttUrl, host, port, path, useSsl);
        String clientId = clientIdPrefix + "_" + System.currentTimeMillis();
        MqttClient client = new MqttClient(serverURI, clientId, new MemoryPersistence());

        try {
            log.info("Conectando ao MQTT em {} (user={})", serverURI, options.getUserName());
            client.connect(options);
            log.info("MQTT conectado em {}", serverURI);
        } catch (MqttException e) {
            log.warn("Nao foi possivel conectar ao MQTT em {} (tentarei de novo): {}",
                    serverURI, e.getMessage());
        }
        return client;
    }

    static String resolveServerUri(String mqttUrl, String host, int port,
                                   String path, boolean useSsl) {
        if (mqttUrl != null && !mqttUrl.isBlank()) {
            return mqttUrl.trim();
        }

        String p = path == null ? "" : path.trim();
        if (!p.isEmpty() && !p.startsWith("/")) {
            p = "/" + p;
        }

        // Path preenchido = mesmo transporte do ESP (WebSocket / WSS).
        if (!p.isEmpty()) {
            return (useSsl ? "wss://" : "ws://") + host + ":" + port + p;
        }
        return (useSsl ? "ssl://" : "tcp://") + host + ":" + port;
    }
}
