package com.home.backend.device;

import org.springframework.data.jpa.repository.JpaRepository;

import java.util.List;
import java.util.Optional;

public interface DeviceRepository extends JpaRepository<Device, Long> {
    Optional<Device> findByUserIdAndUuid(String userId, String uuid);
    List<Device> findByUuid(String uuid);
    List<Device> findByUserId(String userId);
    List<Device> findByUserIdAndActiveTrue(String userId);
    boolean existsByUserIdAndUuid(String userId, String uuid);
}
