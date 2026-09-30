package com.home.backend.ws;

import org.springframework.stereotype.Component;
import org.springframework.web.socket.WebSocketSession;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArraySet;

/**
 * Associa cada sessao WebSocket ao user_id do token e roteia mensagens
 * apenas para os usuarios donos do dispositivo.
 */
@Component
public class WsSessionRegistry {

    private final ConcurrentHashMap<String, Set<WebSocketSession>> byUser = new ConcurrentHashMap<>();

    public void register(String username, WebSocketSession session) {
        byUser.computeIfAbsent(username, k -> new CopyOnWriteArraySet<>()).add(session);
    }

    public void unregister(String username, WebSocketSession session) {
        Set<WebSocketSession> sessions = byUser.get(username);
        if (sessions != null) {
            sessions.remove(session);
            if (sessions.isEmpty()) byUser.remove(username);
        }
    }

    public Set<WebSocketSession> sessionsFor(String username) {
        return byUser.getOrDefault(username, Set.of());
    }

    public int sessionCount() {
        return byUser.values().stream().mapToInt(Set::size).sum();
    }
}
