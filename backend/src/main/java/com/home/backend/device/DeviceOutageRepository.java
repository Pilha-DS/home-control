package com.home.backend.device;

import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;

import java.util.Optional;

public interface DeviceOutageRepository extends JpaRepository<DeviceOutage, Long> {

    Optional<DeviceOutage> findFirstByUserIdAndDeviceUuidAndEndedAtIsNull(String userId, String deviceUuid);

    /** Última queda da conta (mais recente por started_at). */
    Optional<DeviceOutage> findFirstByUserIdOrderByStartedAtDesc(String userId);

    Page<DeviceOutage> findByUserIdAndDeviceUuidOrderByStartedAtDesc(
            String userId, String deviceUuid, Pageable pageable);

    long countByUserIdAndDeviceUuid(String userId, String deviceUuid);

    @Query("select coalesce(sum(o.durationMs), 0) from DeviceOutage o "
            + "where o.userId = ?1 and o.deviceUuid = ?2 and o.endedAt is not null")
    long sumClosedDurationMs(String userId, String deviceUuid);

    void deleteByUserIdAndDeviceUuid(String userId, String deviceUuid);
}
