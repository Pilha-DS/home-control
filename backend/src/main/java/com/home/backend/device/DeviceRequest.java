package com.home.backend.device;

import jakarta.validation.constraints.NotBlank;

/**
 * Requisicao para provisionar/atualizar um dispositivo.
 */
public record DeviceRequest(
        @NotBlank String uuid,
        String name,
        String modelo) {
}
