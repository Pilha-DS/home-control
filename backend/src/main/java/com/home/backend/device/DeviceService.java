package com.home.backend.device;

import com.home.backend.auth.CurrentUser;
import com.home.backend.user.User;
import com.home.backend.user.UserRepository;
import com.home.backend.user.UserSecretService;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.security.SecureRandom;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Regras de negocio dos dispositivos: valida ownership (user_id) em toda
 * operacao e gera a credencial MQTT (token) exclusiva por dispositivo.
 */
@Service
public class DeviceService {

    private final DeviceRepository deviceRepository;
    private final DeviceStateCache stateCache;
    private final DeviceCommandPublisher commandPublisher;
    private final DeviceCommandQueue commandQueue;
    private final CurrentUser currentUser;
    private final UserRepository userRepository;
    private final UserSecretService userSecretService;

    private static final SecureRandom RANDOM = new SecureRandom();

    public DeviceService(DeviceRepository deviceRepository,
                         DeviceStateCache stateCache,
                         DeviceCommandPublisher commandPublisher,
                         DeviceCommandQueue commandQueue,
                         CurrentUser currentUser,
                         UserRepository userRepository,
                         UserSecretService userSecretService) {
        this.deviceRepository = deviceRepository;
        this.stateCache = stateCache;
        this.commandPublisher = commandPublisher;
        this.commandQueue = commandQueue;
        this.currentUser = currentUser;
        this.userRepository = userRepository;
        this.userSecretService = userSecretService;
    }

    /**
     * Lista dispositivos ativos da conta logada (persistidos no banco).
     * Novos Arduinos entram sozinhos na primeira telemetria/poll com X-User-Secret.
     */
    public List<DeviceResponse> list() {
        if (currentUser.isAdmin()) {
            return List.of();
        }
        String user = currentUser.username();
        return deviceRepository.findByUserIdAndActiveTrue(user).stream()
                .map(d -> DeviceResponse.from(d, stateCache.get(d.getUserId(), d.getUuid())))
                .toList();
    }

    public DeviceResponse get(String uuid) {
        Device d = requireOwned(uuid);
        return DeviceResponse.from(d, stateCache.get(d.getUserId(), d.getUuid()));
    }

    @Transactional
    public Map<String, String> create(DeviceRequest request) {
        rejectAdminDeviceAccess();
        String user = currentUser.username();
        String uuid = request.uuid().trim();

        Optional<Device> existing = deviceRepository.findByUserIdAndUuid(user, uuid);
        if (existing.isPresent()) {
            Device d = existing.get();
            if (d.isActive()) {
                throw new IllegalArgumentException("uuid_ja_existe");
            }
            // Soft-deleted: reativa e devolve o secret da conta
            d.setActive(true);
            if (request.name() != null && !request.name().isBlank()) {
                d.setName(request.name().trim());
            }
            if (request.modelo() != null && !request.modelo().isBlank()) {
                d.setModelo(request.modelo().trim());
            }
            deviceRepository.save(d);
            User owner = userRepository.findByUsername(user).orElseThrow();
            userSecretService.ensureSecret(owner);
            owner = userRepository.findByUsername(user).orElseThrow();
            return Map.of(
                    "uuid", d.getUuid(),
                    "esp_secret", owner.getEspSecretToken(),
                    "mqtt_username", d.getMqttUsername() == null ? "" : d.getMqttUsername(),
                    "mqtt_password", d.getMqttPassword() == null ? "" : d.getMqttPassword());
        }

        Device d = new Device();
        d.setUuid(uuid);
        d.setUserId(user);
        d.setName(normalizeName(request.name(), uuid));
        // Sem modelo informado: grava o padrao ate o logs/basic informar a placa.
        String modelo = request.modelo() != null ? request.modelo().trim() : "";
        d.setModelo(modelo.isEmpty() ? DeviceResponse.DEFAULT_MODELO : modelo);
        d.setMqttUsername("dev_" + uuid);
        d.setMqttPassword(randomToken(24));
        d.setDeviceToken(randomToken(32));
        d.setActive(true);
        deviceRepository.save(d);

        User owner = userRepository.findByUsername(user).orElseThrow();
        userSecretService.ensureSecret(owner);
        owner = userRepository.findByUsername(user).orElseThrow();

        return Map.of(
                "uuid", d.getUuid(),
                "esp_secret", owner.getEspSecretToken(),
                "mqtt_username", d.getMqttUsername(),
                "mqtt_password", d.getMqttPassword());
    }

    @Transactional
    public DeviceResponse update(String uuid, DeviceUpdateRequest request) {
        Device d = requireOwned(uuid);
        if (request.name() != null && !request.name().isBlank()) {
            d.setName(request.name().trim());
        }
        if (request.modelo() != null && !request.modelo().isBlank()) {
            d.setModelo(request.modelo().trim());
        }
        deviceRepository.save(d);
        return DeviceResponse.from(d, stateCache.get(d.getUserId(), d.getUuid()));
    }

    /** Soft-delete: some do GET /api/devices, mas o UUID pode voltar ao reconectar. */
    @Transactional
    public void delete(String uuid) {
        Device d = requireOwned(uuid);
        d.setActive(false);
        deviceRepository.save(d);
        stateCache.remove(d.getUserId(), d.getUuid());
    }

    public void command(String uuid, CommandRequest cmd) {
        Device d = requireOwned(uuid);
        commandQueue.requireCmdNotBusy(d.getUserId(), d.getUuid());
        commandPublisher.publishCommand(d.getUserId(), d.getUuid(), cmd, userAuthSecret(d));
        commandQueue.markCmdBusy(d.getUserId(), d.getUuid(), cmd.poke(), cmd.timeMs());
    }

    public void config(String uuid, ConfigRequest cfg) {
        Device d = requireOwned(uuid);
        if (cfg.adminPass() == null || cfg.adminPass().isBlank()) {
            throw new IllegalArgumentException("PASS_obrigatorio");
        }
        commandPublisher.publishConfig(d.getUserId(), d.getUuid(), cfg, userAuthSecret(d));
    }

    private Device requireOwned(String uuid) {
        rejectAdminDeviceAccess();
        String user = currentUser.username();
        Device d = deviceRepository.findByUserIdAndUuid(user, uuid)
                .filter(Device::isActive)
                .orElseThrow(() -> new IllegalArgumentException("dispositivo_nao_encontrado"));
        return d;
    }

    private void rejectAdminDeviceAccess() {
        if (currentUser.isAdmin()) {
            throw new IllegalArgumentException("admin_sem_acesso_dispositivos");
        }
    }

    /** Secret unico do usuario dono do dispositivo (ESP). */
    private String userAuthSecret(Device device) {
        User user = userRepository.findByUsername(device.getUserId()).orElse(null);
        if (user == null) return device.getDeviceToken();
        userSecretService.ensureSecret(user);
        user = userRepository.findByUsername(device.getUserId()).orElse(user);
        String secret = user.getEspSecretToken();
        return (secret != null && !secret.isBlank()) ? secret : device.getDeviceToken();
    }

    private String normalizeName(String name, String fallback) {
        return (name == null || name.isBlank()) ? fallback : name.trim();
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
