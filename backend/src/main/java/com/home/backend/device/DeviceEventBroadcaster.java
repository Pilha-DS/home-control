package com.home.backend.device;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.home.backend.ws.WsSessionRegistry;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Encaminha eventos de dispositivos ao app via WebSocket (mesmo envelope do MQTT).
 */
@Component
public class DeviceEventBroadcaster {

    private static final Logger log = LoggerFactory.getLogger(DeviceEventBroadcaster.class);

    private final WsSessionRegistry wsRegistry;
    private final ObjectMapper objectMapper;

    public DeviceEventBroadcaster(WsSessionRegistry wsRegistry, ObjectMapper objectMapper) {
        this.wsRegistry = wsRegistry;
        this.objectMapper = objectMapper;
    }

    public void broadcastDiscovery(String userId, String uuid) {
        send(userId, "device/discovered", uuid, "");
    }

    public void broadcast(String userId, String uuid, String type, String payload) {
        send(userId, type, uuid, payload);
    }

    private void send(String userId, String type, String uuid, String payload) {
        try {
            Map<String, Object> envelope = new LinkedHashMap<>();
            envelope.put("type", type);
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
            log.warn("Falha ao encaminhar evento {} uuid={}: {}", type, uuid, e.getMessage());
        }
    }
}
