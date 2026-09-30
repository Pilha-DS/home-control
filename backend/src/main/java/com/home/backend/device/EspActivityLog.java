package com.home.backend.device;

import org.springframework.stereotype.Component;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedDeque;

/**
 * Log em memoria dos hits HTTP do ESP (events/poll), inclusive falhas de auth.
 * Usado pelo monitor admin para ver quem esta falando com o servidor.
 */
@Component
public class EspActivityLog {

    public record Hit(
            long at,
            String uuid,
            String action,
            String type,
            boolean ok,
            String userId,
            String detail) {}

    private static final int MAX = 300;
    private final ConcurrentLinkedDeque<Hit> hits = new ConcurrentLinkedDeque<>();

    public void record(String uuid, String action, String type, boolean ok,
                       String userId, String detail) {
        hits.addFirst(new Hit(
                System.currentTimeMillis(),
                uuid == null ? "" : uuid,
                action == null ? "" : action,
                type == null ? "" : type,
                ok,
                userId == null ? "" : userId,
                detail == null ? "" : detail));
        while (hits.size() > MAX) {
            hits.pollLast();
        }
    }

    public List<Hit> recent(int limit) {
        int n = Math.max(1, Math.min(limit, MAX));
        List<Hit> out = new ArrayList<>(n);
        int i = 0;
        for (Hit h : hits) {
            out.add(h);
            if (++i >= n) break;
        }
        return Collections.unmodifiableList(out);
    }

    /** UUIDs vistos recentemente no cache de atividade (mesmo sem device no banco). */
    public List<String> recentUuids(long sinceMs) {
        long cutoff = System.currentTimeMillis() - sinceMs;
        java.util.LinkedHashSet<String> set = new java.util.LinkedHashSet<>();
        for (Hit h : hits) {
            if (h.at() < cutoff) break;
            if (h.uuid() != null && !h.uuid().isBlank()) set.add(h.uuid());
        }
        return List.copyOf(set);
    }
}
