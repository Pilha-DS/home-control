package com.home.backend.config;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.security.SecurityRequirements;
import io.swagger.v3.oas.annotations.tags.Tag;
import org.springframework.boot.actuate.health.HealthComponent;
import org.springframework.boot.actuate.health.HealthEndpoint;
import org.springframework.boot.actuate.health.Status;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.Map;

/**
 * Health check enxuto usado pelo Docker/K8s. Diferente do /actuator/health
 * (que so mostra detalhes com JWT), aqui devolvemos apenas o status agregado,
 * mas {@code real}: se o banco cair, responde 503 e o container reinicia.
 */
@RestController
@Tag(name = "Health", description = "Monitoramento")
public class HealthController {

    private final HealthEndpoint healthEndpoint;

    public HealthController(HealthEndpoint healthEndpoint) {
        this.healthEndpoint = healthEndpoint;
    }

    @GetMapping("/health")
    @Operation(summary = "Health check (status agregado real)")
    @SecurityRequirements
    public ResponseEntity<Map<String, String>> health() {
        HealthComponent overall = healthEndpoint.health();
        Status status = overall.getStatus();
        HttpStatus http = Status.UP.equals(status) ? HttpStatus.OK : HttpStatus.SERVICE_UNAVAILABLE;
        return ResponseEntity.status(http).body(Map.of("status", status.getCode()));
    }
}
