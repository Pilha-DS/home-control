package com.home.backend.device;

import java.time.Instant;

public record DeviceOutageEventResponse(
        long id,
        String name,
        String uuid,
        String userId,
        Instant startedAt,
        Instant endedAt,
        long durationMs,
        boolean open) {

    public static DeviceOutageEventResponse from(DeviceOutage o, long nowMs) {
        long duration;
        if (o.getDurationMs() != null) {
            duration = o.getDurationMs();
        } else if (o.getStartedAt() != null) {
            duration = Math.max(0L, nowMs - o.getStartedAt().toEpochMilli());
        } else {
            duration = 0L;
        }
        return new DeviceOutageEventResponse(
                o.getId() == null ? 0L : o.getId(),
                o.getDeviceName() == null ? o.getDeviceUuid() : o.getDeviceName(),
                o.getDeviceUuid(),
                o.getUserId(),
                o.getStartedAt(),
                o.getEndedAt(),
                duration,
                o.getEndedAt() == null);
    }
}
