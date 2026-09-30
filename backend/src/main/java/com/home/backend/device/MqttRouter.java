package com.home.backend.device;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.home.backend.ws.WsSessionRegistry;
import jakarta.annotation.PostConstruct;
import org.eclipse.paho.client.mqttv3.IMqttClient;
import org.eclipse.paho.client.mqttv3.MqttCallback;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Recebe telemetria MQTT (modo legado app.transport=mqtt).
 * No modo java o ESP fala HTTP direto ({@link EspGatewayController}).
 */
@Component
@ConditionalOnProperty(name = "app.transport", havingValue = "mqtt")
public class MqttRouter implements MqttCallback {

    private static final Logger log = LoggerFactory.getLogger(MqttRouter.class);

    private final IMqttClient mqttClient;
    private final MqttConnectOptions connectOptions;
    private final DeviceStateCache stateCache;
    private final DeviceRegistryService deviceRegistry;
    private final DeviceOutageService outageService;
    private final WsSessionRegistry wsRegistry;
    private final ObjectMapper objectMapper;
    private final String[] subscribeTopics;
    private volatile boolean subscribed = false;

    public MqttRouter(IMqttClient mqttClient,
                      MqttConnectOptions connectOptions,
                      DeviceStateCache stateCache,
                      DeviceRegistryService deviceRegistry,
                      DeviceOutageService outageService,
                      WsSessionRegistry wsRegistry,
                      ObjectMapper objectMapper,
                      @Value("${mqtt.subscribe-topics}") String subscribeTopics) {
        this.mqttClient = mqttClient;
        this.connectOptions = connectOptions;
        this.stateCache = stateCache;
        this.deviceRegistry = deviceRegistry;
        this.outageService = outageService;
        this.wsRegistry = wsRegistry;
        this.objectMapper = objectMapper;
        this.subscribeTopics = subscribeTopics.split(",");
    }

    @PostConstruct
    public void init() {
        mqttClient.setCallback(this);
        trySubscribe();
    }

    @Scheduled(fixedDelay = 10000)
    public void ensureConnection() {
        if (!mqttClient.isConnected()) {
            tryConnect();
        }
        if (mqttClient.isConnected() && !subscribed) {
            trySubscribe();
        }
    }

    private synchronized void tryConnect() {
        if (mqttClient.isConnected()) return;
        try {
            // Evita estado "Connect already in progress" apos falha parcial.
            try {
                mqttClient.disconnectForcibly(500, 500);
            } catch (Exception ignored) {
                // ok se nunca conectou
            }
            log.info("Tentando conectar MQTT em {}...", mqttClient.getServerURI());
            mqttClient.connect(connectOptions);
            subscribed = false;
            log.info("MQTT conectado em {}", mqttClient.getServerURI());
        } catch (Exception e) {
            log.warn("MQTT ainda indisponivel: {}", e.getMessage());
        }
    }

    private synchronized void trySubscribe() {
        if (subscribed || !mqttClient.isConnected()) return;
        try {
            for (String topic : subscribeTopics) {
                String t = topic.trim();
                if (t.isEmpty()) continue;
                mqttClient.subscribe(t, 1);
                log.info("Assinando topico MQTT: {}", t);
            }
            subscribed = true;
        } catch (Exception e) {
            log.warn("Falha ao assinar topicos MQTT: {}", e.getMessage());
            subscribed = false;
        }
    }

    @Override
    public void connectionLost(Throwable cause) {
        log.warn("Conexao MQTT perdida: {}", cause != null ? cause.getMessage() : "desconhecida");
        subscribed = false;
    }

    @Override
    public void deliveryComplete(org.eclipse.paho.client.mqttv3.IMqttDeliveryToken token) {
        // Entrega confirmada (QoS 1).
    }

    @Override
    public void messageArrived(String topic, MqttMessage message) {
        String payload = new String(message.getPayload());
        TopicParser.ParsedTopic parsed = TopicParser.parse(topic);
        if (parsed == null) {
            log.debug("Topico MQTT ignorado (formato): {}", topic);
            return;
        }

        var ensured = deviceRegistry.ensureFromTopic(parsed.userId(), parsed.uuid());
        if (ensured.isEmpty()) {
            log.warn("Telemetria descartada topic={} user={} uuid={} suffix={}",
                    topic, parsed.userId(), parsed.uuid(), parsed.suffix());
            return;
        }

        // Broadcast sempre para o dono do banco (pode divergir do user_id do topico).
        String ownerId = ensured.get().device().getUserId();

        stateCache.update(ownerId, parsed.uuid(), parsed.suffix(), payload);
        outageService.markOnline(ownerId, parsed.uuid());
        if ("logs/basic".equals(parsed.suffix())) {
            log.info("logs/basic uuid={} payload={}", parsed.uuid(),
                    payload.length() > 180 ? payload.substring(0, 180) + "…" : payload);
            deviceRegistry.syncModeloFromBasic(ownerId, parsed.uuid(), payload);
        }
        broadcast(ownerId, parsed.uuid(), parsed.suffix(), payload);

        if (ensured.get().newlyCreated()) {
            log.info("Device descoberto via MQTT: uuid={} user={}", parsed.uuid(), ownerId);
            broadcastDiscovery(ownerId, parsed.uuid());
        }
    }

    private void broadcastDiscovery(String userId, String uuid) {
        try {
            Map<String, Object> envelope = new LinkedHashMap<>();
            envelope.put("type", "device/discovered");
            envelope.put("uuid", uuid);
            envelope.put("payload", "");
            String json = objectMapper.writeValueAsString(envelope);

            for (WebSocketSession session : wsRegistry.sessionsFor(userId)) {
                if (session.isOpen()) {
                    synchronized (session) {
                        session.sendMessage(new TextMessage(json));
                    }
                }
            }
        } catch (Exception e) {
            log.warn("Falha ao notificar descoberta de dispositivo: {}", e.getMessage());
        }
    }

    private void broadcast(String userId, String uuid, String suffix, String payload) {
        try {
            Map<String, Object> envelope = new LinkedHashMap<>();
            envelope.put("type", suffix);
            envelope.put("uuid", uuid);
            envelope.put("payload", payload);
            String json = objectMapper.writeValueAsString(envelope);

            for (WebSocketSession session : wsRegistry.sessionsFor(userId)) {
                if (session.isOpen()) {
                    synchronized (session) {
                        session.sendMessage(new TextMessage(json));
                    }
                }
            }
        } catch (Exception e) {
            log.warn("Falha ao encaminhar telemetria: {}", e.getMessage());
        }
    }
}
