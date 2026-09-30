package com.home.backend.device;

import java.util.List;

public record DeviceOutageSummaryResponse(
        String name,
        String uuid,
        String userId,
        long outageCount,
        boolean currentlyDown,
        long currentDownMs,
        long totalDownMs,
        List<DeviceOutageEventResponse> outages) {
}
