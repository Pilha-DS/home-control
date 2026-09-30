package com.home.backend.device;

import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotNull;

/**
 * Comando de relé enviado ao dispositivo (equivale ao JSON do firmware).
 */
public record CommandRequest(
        @NotNull @Min(0) @Max(1) int poke,
        Integer pin,
        @Min(0) Long timeMs) {
}
