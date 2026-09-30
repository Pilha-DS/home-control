package com.home.backend.config;

import com.home.backend.auth.JwtService;
import com.home.backend.ws.EventWebSocketHandler;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.server.ServerHttpRequest;
import org.springframework.http.server.ServerHttpResponse;
import org.springframework.web.socket.WebSocketHandler;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;
import org.springframework.web.socket.server.HandshakeInterceptor;

import java.net.URI;
import java.util.Map;

/**
 * Registra o endpoint /ws/events. O token JWT e passado via query string
 * (?token=...) e validado no handshake; o username (user_id) e guardado nos
 * atributos da sessao.
 */
@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {

    private final EventWebSocketHandler eventHandler;
    private final JwtService jwtService;

    public WebSocketConfig(EventWebSocketHandler eventHandler, JwtService jwtService) {
        this.eventHandler = eventHandler;
        this.jwtService = jwtService;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(eventHandler, "/ws/events")
                .setAllowedOriginPatterns("*")
                .addInterceptors(new TokenHandshakeInterceptor(jwtService));
    }

    static class TokenHandshakeInterceptor implements HandshakeInterceptor {

        private final JwtService jwtService;

        TokenHandshakeInterceptor(JwtService jwtService) {
            this.jwtService = jwtService;
        }

        @Override
        public boolean beforeHandshake(ServerHttpRequest request, ServerHttpResponse response,
                                       WebSocketHandler wsHandler, Map<String, Object> attributes) {
            String token = extractToken(request.getURI());
            if (token == null) return false;
            try {
                String username = jwtService.extractUsername(token);
                if (username == null || username.isBlank()) return false;
                attributes.put(EventWebSocketHandler.ATTR_USERNAME, username);
                return true;
            } catch (Exception e) {
                return false;
            }
        }

        @Override
        public void afterHandshake(ServerHttpRequest request, ServerHttpResponse response,
                                   WebSocketHandler wsHandler, Exception exception) {
        }

        private String extractToken(URI uri) {
            String query = uri.getQuery();
            if (query == null) return null;
            for (String pair : query.split("&")) {
                String[] kv = pair.split("=", 2);
                if (kv.length == 2 && "token".equals(kv[0])) {
                    return java.net.URLDecoder.decode(kv[1], java.nio.charset.StandardCharsets.UTF_8);
                }
            }
            return null;
        }
    }
}
