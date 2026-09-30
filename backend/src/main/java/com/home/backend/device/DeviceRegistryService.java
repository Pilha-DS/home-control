package com.home.backend.device;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.home.backend.user.UserRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.security.SecureRandom;
import java.util.Optional;

/**
 * Persiste dispositivos por usuario ({@code user_id} + {@code uuid}).
 * Na primeira telemetria/poll com secret valido, cria o registro no banco.
 */
@Service
public class DeviceRegistryService {

    private static final Logger log = LoggerFactory.getLogger(DeviceRegistryService.class);
    private static final SecureRandom RANDOM = new SecureRandom();
    private static final ObjectMapper MAPPER = new ObjectMapper();

    public record EnsureResult(Device device, boolean newlyCreated) {}

    private final DeviceRepository deviceRepository;
    private final UserRepository userRepository;

    public DeviceRegistryService(DeviceRepository deviceRepository,
                                 UserRepository userRepository) {
        this.deviceRepository = deviceRepository;
        this.userRepository = userRepository;
    }

    /**
     * Garante que o dispositivo existe no banco na conta do {@code userId}.
     * Cria na primeira conexao; reativa se estava soft-deleted.
     */
    @Transactional
    public Optional<EnsureResult> ensureFromTopic(String userId, String uuid) {
        if (userId == null || userId.isBlank() || uuid == null || uuid.isBlank()) {
            return Optional.empty();
        }

        String user = userId.trim();
        String id = uuid.trim();

        Optional<Device> existing = deviceRepository.findByUserIdAndUuid(user, id);
        if (existing.isPresent()) {
            Device d = existing.get();
            boolean reactivated = false;
            boolean dirty = false;
            if (!d.isActive()) {
                d.setActive(true);
                log.info("Dispositivo reativado: uuid={} user={}", id, user);
                reactivated = true;
                dirty = true;
            }
            // Sem modelo salvo: grava o padrao ate o firmware mandar logs/basic.
            if (d.getModelo() == null || d.getModelo().isBlank()) {
                d.setModelo(DeviceResponse.DEFAULT_MODELO);
                dirty = true;
            }
            if (dirty) deviceRepository.save(d);
            return Optional.of(new EnsureResult(d, reactivated));
        }

        if (!userRepository.existsByUsername(user)) {
            log.warn("Telemetria para user_id desconhecido: {}", user);
            return Optional.empty();
        }

        Device d = new Device();
        d.setUuid(id);
        d.setUserId(user);
        d.setName(id);
        d.setModelo(DeviceResponse.DEFAULT_MODELO);
        d.setActive(true);
        d.setMqttUsername("dev_" + id);
        d.setMqttPassword(randomToken(24));
        d.setDeviceToken(randomToken(32));
        deviceRepository.save(d);
        log.info("Dispositivo auto-registrado: uuid={} user={} modelo={}", id, user, d.getModelo());
        return Optional.of(new EnsureResult(d, true));
    }

    /**
     * Atualiza {@code modelo} no banco a partir do JSON de {@code logs/basic}.
     */
    @Transactional
    public void syncModeloFromBasic(String userId, String uuid, String payload) {
        if (uuid == null || uuid.isBlank() || payload == null || payload.isBlank()) {
            return;
        }
        try {
            JsonNode root = MAPPER.readTree(payload);
            JsonNode m = root.get("modelo");
            if (m == null || m.isNull()) m = root.get("MODELO");
            if (m == null || !m.isTextual()) return;
            String modelo = m.asText().trim();
            if (modelo.isEmpty()) return;

            deviceRepository.findByUserIdAndUuid(userId, uuid).ifPresent(d -> {
                if (!modelo.equals(d.getModelo())) {
                    d.setModelo(modelo);
                    deviceRepository.save(d);
                    log.info("Modelo persistido uuid={} user={} modelo={}", uuid, userId, modelo);
                }
            });
        } catch (Exception e) {
            log.debug("Ignorando logs/basic sem modelo JSON: {}", e.getMessage());
        }
    }

    private String randomToken(int length) {
        final String alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        StringBuilder sb = new StringBuilder(length);
        for (int i = 0; i < length; i++) {
            sb.append(alphabet.charAt(RANDOM.nextInt(alphabet.length())));
        }
        return sb.toString();
    }
}
