package com.home.backend.device;

/**
 * Comando recusado: o timer do comando anterior deste device ainda nao acabou.
 */
public class CommandBusyException extends RuntimeException {

    private final long remainingMs;

    public CommandBusyException(long remainingMs) {
        super("comando_em_andamento");
        this.remainingMs = Math.max(0L, remainingMs);
    }

    public long remainingMs() {
        return remainingMs;
    }
}
