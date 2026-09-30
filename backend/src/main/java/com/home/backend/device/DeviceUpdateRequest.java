package com.home.backend.device;

/**
 * Atualizacao parcial de dispositivo (nome / modelo). UUID vem do path.
 */
public record DeviceUpdateRequest(String name, String modelo) {
}
