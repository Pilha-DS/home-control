package com.home.backend.auth;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Limitador de tentativas de login em memoria (janela deslizante).
 * <p>
 * Chave = IP + usuario. Simples e sem dependencia externa; suficiente para
 * uma unica instancia. Em cluster, troque por um store compartilhado
 * (Redis/Bucket4j) — o contrato {@link #tryAcquire} continua o mesmo.
 */
@Component
public class LoginRateLimiter {

    private final int maxAttempts;
    private final long windowMs;
    private final Map<String, Deque<Long>> attempts = new ConcurrentHashMap<>();

    public LoginRateLimiter(
            @Value("${app.login-rate-limit.max-attempts:10}") int maxAttempts,
            @Value("${app.login-rate-limit.window-seconds:60}") long windowSeconds) {
        this.maxAttempts = Math.max(1, maxAttempts);
        this.windowMs = Math.max(1, windowSeconds) * 1000L;
    }

    /** Registra uma tentativa. Retorna false se o limite da janela ja foi excedido. */
    public synchronized boolean tryAcquire(String key) {
        long now = System.currentTimeMillis();
        Deque<Long> times = attempts.computeIfAbsent(key, k -> new ArrayDeque<>());
        prune(times, now);
        if (times.size() >= maxAttempts) {
            return false;
        }
        times.addLast(now);
        return true;
    }

    /** Zera o contador (usado apos login bem-sucedido). */
    public void reset(String key) {
        attempts.remove(key);
    }

    /** Segundos ate a proxima tentativa ser permitida (apenas informativo). */
    public long retryAfterSeconds(String key) {
        Deque<Long> times = attempts.get(key);
        if (times == null || times.isEmpty()) {
            return 0;
        }
        long oldest = times.peekFirst();
        long remaining = (oldest + windowMs) - System.currentTimeMillis();
        return Math.max(0, (remaining + 999) / 1000);
    }

    private void prune(Deque<Long> times, long now) {
        while (!times.isEmpty() && now - times.peekFirst() > windowMs) {
            times.pollFirst();
        }
    }
}
