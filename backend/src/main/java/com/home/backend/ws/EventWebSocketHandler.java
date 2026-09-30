package com.home.backend.ws;

import com.home.backend.auth.JwtService;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

/**
 * Recebe a conexao WebSocket do app. O username (user_id) vem dos atributos
 * da sessao (preenchidos no handshake apos validar o token).
 */
@Component
public class EventWebSocketHandler extends TextWebSocketHandler {

    public static final String ATTR_USERNAME = "username";

    private final WsSessionRegistry registry;

    public EventWebSocketHandler(WsSessionRegistry registry) {
        this.registry = registry;
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) throws Exception {
        String username = (String) session.getAttributes().get(ATTR_USERNAME);
        if (username != null && !username.isBlank()) {
            registry.register(username, session);
        } else {
            session.close(CloseStatus.NOT_ACCEPTABLE.withReason("token invalido"));
        }
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        String username = (String) session.getAttributes().get(ATTR_USERNAME);
        if (username != null) registry.unregister(username, session);
    }

    @Override
    protected void handleTextMessage(WebSocketSession session, TextMessage message) {
        // O canal e unidirecional (server -> app). Ignora mensagens de entrada;
        // pode responder pong se o cliente enviar ping.
        String payload = message.getPayload();
        if ("ping".equalsIgnoreCase(payload.trim())) {
            try {
                session.sendMessage(new TextMessage("pong"));
            } catch (Exception ignored) {
            }
        }
    }
}
