package com.home.backend.device;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

/**
 * Processa telemetria recebida do ESP (via HTTP gateway Java).
 * Atualiza cache, persiste modelo e notifica o app.
 */
@Service
public class DeviceTelemetryService {

    private static final Logger log = LoggerFactory.getLogger(DeviceTelemetryService.class);

    private final DeviceStateCache stateCache;
    private final DeviceRegistryService deviceRegistry;
    private final DeviceEventBroadcaster broadcaster;
    private final DeviceOutageService outageService;

    public DeviceTelemetryService(DeviceStateCache stateCache,
                                  DeviceRegistryService deviceRegistry,
                                  DeviceEventBroadcaster broadcaster,
                                  DeviceOutageService outageService) {
        this.stateCache = stateCache;
        this.deviceRegistry = deviceRegistry;
        this.broadcaster = broadcaster;
        this.outageService = outageService;
    }

    /**
     * @param userId dono do dispositivo (do banco)
     * @param uuid   identidade do ESP
     * @param type   sufixo do evento (ex.: status, logs/basic, cmd)
     * @param payload corpo bruto (string ou JSON)
     * @return true se descoberta nova
     */
    public boolean ingest(String userId, String uuid, String type, String payload) {
        if (type == null || type.isBlank()) return false;

        stateCache.update(userId, uuid, type, payload);
        outageService.markOnline(userId, uuid);

        if ("logs/basic".equals(type)) {
            log.debug("logs/basic uuid={} payload={}", uuid,
                    payload != null && payload.length() > 180
                            ? payload.substring(0, 180) + "…" : payload);
            deviceRegistry.syncModeloFromBasic(userId, uuid, payload);
        }

        broadcaster.broadcast(userId, uuid, type, payload == null ? "" : payload);
        return false;
    }

    /**
     * Registra dispositivo na primeira telemetria e retorna se foi criado agora.
     */
    public boolean ensureAndIngest(String userId, String uuid, String type, String payload) {
        var ensured = deviceRegistry.ensureFromTopic(userId, uuid);
        if (ensured.isEmpty()) {
            log.warn("Telemetria descartada user={} uuid={} type={}", userId, uuid, type);
            return false;
        }
        String ownerId = ensured.get().device().getUserId();
        ingest(ownerId, uuid, type, payload);
        if (ensured.get().newlyCreated()) {
            log.info("Device descoberto via Java gateway: uuid={} user={}", uuid, ownerId);
            broadcaster.broadcastDiscovery(ownerId, uuid);
            return true;
        }
        return false;
    }
}
