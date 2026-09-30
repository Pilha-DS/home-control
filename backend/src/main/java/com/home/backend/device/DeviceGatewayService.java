package com.home.backend.device;

import com.home.backend.user.User;
import com.home.backend.user.UserRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.util.List;
import java.util.Optional;

/**
 * Autenticacao e roteamento ESP ↔ Java (sem MQTT).
 * O ESP identifica o usuario via {@code X-User-Secret} (secret unico por conta).
 * Na primeira telemetria/poll valida, o UUID e gravado em {@code devices} na conta do dono.
 */
@Service
public class DeviceGatewayService {

    private static final Logger log = LoggerFactory.getLogger(DeviceGatewayService.class);

    public record EspAuthResult(Device device, boolean newlyCreated) {}

    private final DeviceRepository deviceRepository;
    private final UserRepository userRepository;
    private final DeviceRegistryService deviceRegistry;
    private final DeviceTelemetryService telemetryService;
    private final DeviceCommandQueue commandQueue;
    private final DeviceEventBroadcaster broadcaster;
    private final DeviceStateCache stateCache;
    private final DeviceOutageService outageService;

    public DeviceGatewayService(DeviceRepository deviceRepository,
                                UserRepository userRepository,
                                DeviceRegistryService deviceRegistry,
                                DeviceTelemetryService telemetryService,
                                DeviceCommandQueue commandQueue,
                                DeviceEventBroadcaster broadcaster,
                                DeviceStateCache stateCache,
                                DeviceOutageService outageService) {
        this.deviceRepository = deviceRepository;
        this.userRepository = userRepository;
        this.deviceRegistry = deviceRegistry;
        this.telemetryService = telemetryService;
        this.commandQueue = commandQueue;
        this.broadcaster = broadcaster;
        this.stateCache = stateCache;
        this.outageService = outageService;
    }

    /**
     * Autentica o ESP pelo secret do usuario e auto-registra o UUID na conta.
     */
    @Transactional
    public Optional<EspAuthResult> authenticateEsp(String uuid, String secret) {
        if (uuid == null || uuid.isBlank()) return Optional.empty();
        if (secret == null || secret.isBlank()) return Optional.empty();

        String trimmed = secret.trim();
        String uuidTrim = uuid.trim();

        Optional<User> owner = userRepository.findByEspSecretToken(trimmed);
        if (owner.isPresent()) {
            if (!owner.get().isEnabled()) return Optional.empty();
            String username = owner.get().getUsername();
            return deviceRegistry.ensureFromTopic(username, uuidTrim)
                    .filter(r -> r.device().getUserId().equals(username))
                    .map(r -> new EspAuthResult(r.device(), r.newlyCreated()));
        }

        // Legado: token por dispositivo (sem auto-criar conta nova).
        // Reativa device soft-deletado (excluído no app): o usuário pode ter
        // excluído o registro, mas o ESP continua vivo com o token salvo — ao
        // voltar a conectar, o dispositivo reaparece sozinho (mesmo contrato
        // do ensureFromTopic, que reativa inativos com X-User-Secret).
        Optional<Device> legacy = deviceRepository.findByUuid(uuidTrim).stream()
                .filter(d -> trimmed.equals(d.getDeviceToken())
                        || trimmed.equals(d.getMqttPassword()))
                .findFirst();
        if (legacy.isPresent()) {
            Device d = legacy.get();
            if (!d.isActive()) {
                d.setActive(true);
                deviceRepository.save(d);
                log.info("Dispositivo reativado por token: uuid={} user={}", uuidTrim, d.getUserId());
                return Optional.of(new EspAuthResult(d, true));
            }
            return Optional.of(new EspAuthResult(d, false));
        }
        return Optional.empty();
    }

    /**
     * Valida secret + ownership. Auto-registra se for a primeira vez com X-User-Secret.
     */
    @Transactional
    public Optional<EspAuthResult> requireDeviceAuth(String uuid, String secret) {
        return authenticateEsp(uuid, secret);
    }

    public Optional<Device> requireDevice(String uuid, String secret) {
        return authenticateEsp(uuid, secret).map(EspAuthResult::device);
    }

    public void ingestEvent(Device device, boolean newlyDiscovered, String type, String payload) {
        telemetryService.ingest(device.getUserId(), device.getUuid(), type, payload);
        if (newlyDiscovered) {
            broadcaster.broadcastDiscovery(device.getUserId(), device.getUuid());
        }
    }

    public void notifyIfDiscovered(Device device, boolean newlyDiscovered) {
        if (newlyDiscovered) {
            broadcaster.broadcastDiscovery(device.getUserId(), device.getUuid());
        }
    }

    /** Atualiza lastSeen no cache (poll HTTP periodico do ESP). */
    public void touchPresence(Device device) {
        stateCache.update(device.getUserId(), device.getUuid(), "poll", "ok");
        outageService.markOnline(device);
    }

    public List<DeviceCommandQueue.PendingCommand> pollCommands(Device device) {
        return commandQueue.pollAll(device.getUserId(), device.getUuid());
    }
}
