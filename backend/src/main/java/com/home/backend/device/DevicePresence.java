package com.home.backend.device;

/**
 * Limiar compartilhado: sem poll/telemetria neste intervalo o ESP e considerado offline.
 */
public final class DevicePresence {

    public static final long ONLINE_MS = 50_000L;

    private DevicePresence() {}
}
