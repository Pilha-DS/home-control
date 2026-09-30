package com.home.backend.device;

import org.springframework.stereotype.Component;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;

/**
 * Fila de comandos/config pendentes para o ESP buscar via poll HTTP.
 * Chave = usuario + UUID (UUID pode repetir entre contas).
 *
 * TTL: comando com idade &gt;= 5s nao e entregue (nao roda).
 * Nao permanece na fila por mais de 8s.
 */
@Component
public class DeviceCommandQueue {

    /** Idade maxima para entregar ao ESP — acima disso o comando e descartado. */
    public static final long DELIVER_MAX_AGE_MS = 5_000L;

    /** Tempo maximo que um item pode permanecer enfileirado. */
    public static final long QUEUE_MAX_AGE_MS = 8_000L;

    public record PendingCommand(String type, String payload, long enqueuedAtMs) {
        public PendingCommand(String type, String payload) {
            this(type, payload, System.currentTimeMillis());
        }

        public long ageMs(long now) {
            return Math.max(0L, now - enqueuedAtMs);
        }
    }

    private final ConcurrentHashMap<String, ConcurrentLinkedQueue<PendingCommand>> byDevice =
            new ConcurrentHashMap<>();

    /** Ate quando este device esta ocupado pelo timeMs do ultimo cmd aceito. */
    private final ConcurrentHashMap<String, Long> cmdBusyUntilMs = new ConcurrentHashMap<>();

    private static String key(String userId, String uuid) {
        String u = userId == null ? "" : userId.trim().toLowerCase(Locale.ROOT);
        String id = uuid == null ? "" : uuid.trim().toLowerCase(Locale.ROOT);
        return u + "/" + id;
    }

    public void enqueue(String userId, String uuid, String type, String payloadJson) {
        String k = key(userId, uuid);
        if (k.equals("/")) return;
        ConcurrentLinkedQueue<PendingCommand> q =
                byDevice.computeIfAbsent(k, ignored -> new ConcurrentLinkedQueue<>());
        purgeExpired(q, System.currentTimeMillis());
        // 1o comando vence: nao substitui cmd pendente (evita cancelar o anterior).
        if ("cmd".equals(type)) {
            for (PendingCommand c : q) {
                if ("cmd".equals(c.type())) {
                    return;
                }
            }
        }
        q.add(new PendingCommand(type, payloadJson));
    }

    /**
     * Recusa novo cmd se o timeMs do anterior ainda nao passou.
     * Qualquer poke (0 ou 1) e bloqueado enquanto o ciclo estiver ativo.
     */
    public void requireCmdNotBusy(String userId, String uuid) {
        long rem = remainingBusyMs(userId, uuid);
        if (rem > 0) throw new CommandBusyException(rem);
    }

    public void markCmdBusy(String userId, String uuid, int poke, Long timeMs) {
        String k = key(userId, uuid);
        if (k.equals("/")) return;
        long t = timeMs == null ? 0L : timeMs;
        // poke=0 permanente nao arma busy; mas tambem nao limpa um ciclo
        // ja em andamento (senao um OFF em seguida liberaria a fila cedo).
        if (poke == 0 || t <= 0) {
            return;
        }
        cmdBusyUntilMs.put(k, System.currentTimeMillis() + t);
    }

    public long remainingBusyMs(String userId, String uuid) {
        String k = key(userId, uuid);
        Long until = cmdBusyUntilMs.get(k);
        if (until == null) return 0L;
        long rem = until - System.currentTimeMillis();
        if (rem <= 0) {
            cmdBusyUntilMs.remove(k, until);
            return 0L;
        }
        return rem;
    }

    /**
     * Remove e retorna apenas comandos ainda validos (idade &lt; 5s).
     * Itens com 5s+ sao descartados sem entregar; 8s+ sao limpos da fila.
     */
    public List<PendingCommand> pollAll(String userId, String uuid) {
        ConcurrentLinkedQueue<PendingCommand> q = byDevice.get(key(userId, uuid));
        if (q == null || q.isEmpty()) {
            return List.of();
        }
        long now = System.currentTimeMillis();
        List<PendingCommand> out = new ArrayList<>();
        PendingCommand cmd;
        while ((cmd = q.poll()) != null) {
            long age = cmd.ageMs(now);
            if (age >= QUEUE_MAX_AGE_MS) {
                continue; // lixo: nao entrega
            }
            if (age >= DELIVER_MAX_AGE_MS) {
                continue; // stale: nao roda
            }
            out.add(cmd);
        }
        return out;
    }

    public boolean hasPending(String userId, String uuid) {
        ConcurrentLinkedQueue<PendingCommand> q = byDevice.get(key(userId, uuid));
        if (q == null || q.isEmpty()) return false;
        long now = System.currentTimeMillis();
        purgeExpired(q, now);
        for (PendingCommand c : q) {
            if (c.ageMs(now) < DELIVER_MAX_AGE_MS) return true;
        }
        return false;
    }

    public void clear(String userId, String uuid) {
        ConcurrentLinkedQueue<PendingCommand> q = byDevice.get(key(userId, uuid));
        if (q != null) q.clear();
    }

    /** Remove da fila apenas o que passou de 8s. */
    private static void purgeExpired(ConcurrentLinkedQueue<PendingCommand> q, long now) {
        q.removeIf(c -> c.ageMs(now) >= QUEUE_MAX_AGE_MS);
    }
}