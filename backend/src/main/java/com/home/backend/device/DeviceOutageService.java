package com.home.backend.device;

import com.home.backend.auth.CurrentUser;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Detecta e persiste quedas (ESP sem poll/telemetria por {@link DevicePresence#ONLINE_MS}).
 * Identidade da queda = {@code user_id} + {@code uuid}.
 *
 * <p>API: gerais = só a última queda; por dispositivo = página (limit/offset).
 */
@Service
public class DeviceOutageService {

    public static final int DEFAULT_PAGE_SIZE = 10;
    public static final int MAX_PAGE_SIZE = 50;

    private final DeviceOutageRepository outageRepository;
    private final DeviceRepository deviceRepository;
    private final DeviceStateCache stateCache;
    private final CurrentUser currentUser;

    /** UUIDs (por usuario) que ja estiveram online neste processo. */
    private final Set<String> seenOnline = ConcurrentHashMap.newKeySet();

    public DeviceOutageService(DeviceOutageRepository outageRepository,
                               DeviceRepository deviceRepository,
                               DeviceStateCache stateCache,
                               CurrentUser currentUser) {
        this.outageRepository = outageRepository;
        this.deviceRepository = deviceRepository;
        this.stateCache = stateCache;
        this.currentUser = currentUser;
    }

    @Transactional
    public void markOnline(Device device) {
        if (device == null) return;
        markOnline(device.getUserId(), device.getUuid());
    }

    @Transactional
    public void markOnline(String userId, String uuid) {
        if (userId == null || uuid == null) return;
        seenOnline.add(key(userId, uuid));
        closeOpenOutage(userId, uuid);
    }

    @Scheduled(fixedDelay = 15_000L)
    @Transactional
    public void scanOffline() {
        long now = System.currentTimeMillis();
        for (Device d : deviceRepository.findAll()) {
            if (!d.isActive()) continue;
            long lastSeen = lastSeenMs(d.getUserId(), d.getUuid());
            if (lastSeen <= 0L) continue;
            boolean online = (now - lastSeen) <= DevicePresence.ONLINE_MS;
            String k = key(d.getUserId(), d.getUuid());
            if (online) {
                seenOnline.add(k);
                closeOpenOutage(d.getUserId(), d.getUuid());
            } else if (seenOnline.contains(k)) {
                openOutage(d);
            }
        }
    }

    /**
     * Sem uuid: só a última queda da conta (lista com 0 ou 1 item).
     * Com uuid: página desse dispositivo ({@code limit}/{@code offset}).
     */
    public List<DeviceOutageSummaryResponse> query(
            String requestedUserId, String uuid, int limit, int offset) {
        String owner = resolveOwner(requestedUserId);
        if (uuid != null && !uuid.isBlank()) {
            return List.of(summaryFor(owner, uuid.trim(), limit, offset));
        }
        return latestForUser(owner);
    }

    public DeviceOutageSummaryResponse queryOne(
            String requestedUserId, String uuid, int limit, int offset) {
        return summaryFor(resolveOwner(requestedUserId), uuid, limit, offset);
    }

    @Transactional
    public void deleteForDevice(String userId, String uuid) {
        outageRepository.deleteByUserIdAndDeviceUuid(userId, uuid);
    }

    private String resolveOwner(String requestedUserId) {
        if (currentUser.isAdmin()) {
            throw new IllegalArgumentException("admin_sem_acesso_dispositivos");
        }
        String actor = currentUser.username();
        if (requestedUserId == null || requestedUserId.isBlank()) {
            return actor;
        }
        if (!requestedUserId.equals(actor)) {
            throw new IllegalArgumentException("acesso_negado");
        }
        return actor;
    }

    /** Uma única queda — a mais recente da conta. */
    private List<DeviceOutageSummaryResponse> latestForUser(String userId) {
        Optional<DeviceOutage> latest =
                outageRepository.findFirstByUserIdOrderByStartedAtDesc(userId);
        if (latest.isEmpty()) {
            return List.of();
        }
        DeviceOutage o = latest.get();
        Optional<Device> device =
                deviceRepository.findByUserIdAndUuid(userId, o.getDeviceUuid());
        return List.of(buildSummary(
                userId,
                o.getDeviceUuid(),
                device.orElse(null),
                List.of(o),
                1L,
                DeviceOutageEventResponse.from(o, System.currentTimeMillis()).durationMs()));
    }

    private DeviceOutageSummaryResponse summaryFor(
            String userId, String uuid, int limit, int offset) {
        int pageSize = clampLimit(limit);
        int off = Math.max(0, offset);
        int page = off / pageSize;

        Optional<Device> device = deviceRepository.findByUserIdAndUuid(userId, uuid);
        long total = outageRepository.countByUserIdAndDeviceUuid(userId, uuid);
        if (device.isEmpty() && total == 0L) {
            throw new IllegalArgumentException("dispositivo_nao_encontrado");
        }

        Page<DeviceOutage> pageResult = outageRepository
                .findByUserIdAndDeviceUuidOrderByStartedAtDesc(
                        userId, uuid, PageRequest.of(page, pageSize));

        long now = System.currentTimeMillis();
        long totalDownMs = outageRepository.sumClosedDurationMs(userId, uuid);
        Optional<DeviceOutage> open =
                outageRepository.findFirstByUserIdAndDeviceUuidAndEndedAtIsNull(userId, uuid);
        if (open.isPresent() && open.get().getStartedAt() != null) {
            totalDownMs += Math.max(0L, now - open.get().getStartedAt().toEpochMilli());
        }

        return buildSummary(
                userId,
                uuid,
                device.orElse(null),
                pageResult.getContent(),
                total,
                totalDownMs);
    }

    private static int clampLimit(int limit) {
        if (limit <= 0) return DEFAULT_PAGE_SIZE;
        return Math.min(limit, MAX_PAGE_SIZE);
    }

    private DeviceOutageSummaryResponse buildSummary(
            String userId,
            String uuid,
            Device device,
            List<DeviceOutage> events,
            long totalCount,
            long totalDownMs) {
        long now = System.currentTimeMillis();
        String name = nameOf(device, events, uuid);
        boolean currentlyDown = events.stream().anyMatch(o -> o.getEndedAt() == null)
                || isCurrentlyDown(userId, uuid, now)
                || outageRepository
                        .findFirstByUserIdAndDeviceUuidAndEndedAtIsNull(userId, uuid)
                        .isPresent();
        long currentDownMs = 0L;
        List<DeviceOutageEventResponse> items = new ArrayList<>(events.size());
        for (DeviceOutage o : events) {
            DeviceOutageEventResponse item = DeviceOutageEventResponse.from(o, now);
            items.add(item);
            if (item.open()) currentDownMs = item.durationMs();
        }
        if (currentlyDown && currentDownMs == 0L) {
            Optional<DeviceOutage> open =
                    outageRepository.findFirstByUserIdAndDeviceUuidAndEndedAtIsNull(userId, uuid);
            if (open.isPresent() && open.get().getStartedAt() != null) {
                currentDownMs = Math.max(0L, now - open.get().getStartedAt().toEpochMilli());
            } else {
                long lastSeen = lastSeenMs(userId, uuid);
                if (lastSeen > 0L) currentDownMs = Math.max(0L, now - lastSeen);
            }
        }
        return new DeviceOutageSummaryResponse(
                name, uuid, userId, totalCount, currentlyDown, currentDownMs, totalDownMs, items);
    }

    private boolean isCurrentlyDown(String userId, String uuid, long now) {
        long lastSeen = lastSeenMs(userId, uuid);
        return lastSeen > 0L && (now - lastSeen) > DevicePresence.ONLINE_MS;
    }

    private static String nameOf(Device device, List<DeviceOutage> events, String uuid) {
        if (device != null && device.getName() != null && !device.getName().isBlank()) {
            return device.getName();
        }
        for (DeviceOutage o : events) {
            if (o.getDeviceName() != null && !o.getDeviceName().isBlank()) {
                return o.getDeviceName();
            }
        }
        return uuid;
    }

    private void openOutage(Device device) {
        Optional<DeviceOutage> open = outageRepository
                .findFirstByUserIdAndDeviceUuidAndEndedAtIsNull(device.getUserId(), device.getUuid());
        if (open.isPresent()) return;

        Instant start = Instant.ofEpochMilli(lastSeenMs(device.getUserId(), device.getUuid()));
        if (start.toEpochMilli() <= 0L) start = Instant.now();

        DeviceOutage o = new DeviceOutage();
        o.setUserId(device.getUserId());
        o.setDeviceUuid(device.getUuid());
        o.setDeviceName(device.getName() != null ? device.getName() : device.getUuid());
        o.setStartedAt(start);
        outageRepository.save(o);
    }

    private void closeOpenOutage(String userId, String uuid) {
        outageRepository.findFirstByUserIdAndDeviceUuidAndEndedAtIsNull(userId, uuid)
                .ifPresent(o -> {
                    Instant end = Instant.now();
                    o.setEndedAt(end);
                    long duration = Math.max(0L, end.toEpochMilli() - o.getStartedAt().toEpochMilli());
                    o.setDurationMs(duration);
                    outageRepository.save(o);
                });
    }

    private long lastSeenMs(String userId, String uuid) {
        Object ls = stateCache.get(userId, uuid).get("lastSeen");
        return ls instanceof Number n ? n.longValue() : 0L;
    }

    private static String key(String userId, String uuid) {
        return (userId == null ? "" : userId) + "\0" + (uuid == null ? "" : uuid);
    }
}
