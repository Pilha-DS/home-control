package com.home.backend.config;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.core.env.Environment;
import org.springframework.stereotype.Component;

import java.util.ArrayList;
import java.util.List;

/**
 * Guarda de segredos na subida.
 * <p>
 * Em desenvolvimento apenas avisa (WARN), para nao atrapalhar o
 * {@code docker compose up}. Em producao (perfil {@code prod} ou
 * {@code APP_STRICT_SECRETS=true}) <b>falha a inicializacao</b> se ainda
 * estiver usando os valores padrao do repositorio — evitando subir um
 * servidor com JWT previsivel e senha de admin {@code admin123}.
 */
@Component
public class SecurityStartupValidator implements ApplicationRunner {

    private static final Logger log = LoggerFactory.getLogger(SecurityStartupValidator.class);

    private static final String DEFAULT_JWT_SECRET = "change-me-to-a-long-random-secret-at-least-32-chars";
    private static final String DEFAULT_ADMIN_PASSWORD = "admin123";
    private static final String DEFAULT_MQTT_PASSWORD = "C6m8n4d2d3";

    private final Environment environment;
    private final String jwtSecret;
    private final String adminPassword;
    private final boolean strictSecrets;
    private final String transport;
    private final String mqttPassword;

    public SecurityStartupValidator(
            Environment environment,
            @Value("${app.jwt.secret:}") String jwtSecret,
            @Value("${app.admin.password:}") String adminPassword,
            @Value("${app.security.strict-secrets:false}") boolean strictSecrets,
            @Value("${app.transport:java}") String transport,
            @Value("${mqtt.password:}") String mqttPassword) {
        this.environment = environment;
        this.jwtSecret = jwtSecret;
        this.adminPassword = adminPassword;
        this.strictSecrets = strictSecrets;
        this.transport = transport;
        this.mqttPassword = mqttPassword;
    }

    @Override
    public void run(ApplicationArguments args) {
        List<String> problems = new ArrayList<>();

        if (jwtSecret == null || jwtSecret.isBlank()) {
            problems.add("JWT_SECRET vazio");
        } else if (DEFAULT_JWT_SECRET.equals(jwtSecret.trim())) {
            problems.add("JWT_SECRET ainda e o valor padrao do repositorio");
        } else if (jwtSecret.trim().length() < 32) {
            problems.add("JWT_SECRET tem menos de 32 caracteres");
        }

        if (DEFAULT_ADMIN_PASSWORD.equals(adminPassword)) {
            problems.add("ADMIN_PASSWORD ainda e 'admin123'");
        }

        if ("mqtt".equalsIgnoreCase(transport)
                && (DEFAULT_MQTT_PASSWORD.equals(mqttPassword) || mqttPassword == null || mqttPassword.isBlank())) {
            problems.add("MQTT_PASS ainda e o valor padrao do repositorio");
        }

        if (problems.isEmpty()) {
            return;
        }

        boolean prod = strictSecrets || List.of(environment.getActiveProfiles()).contains("prod");
        if (prod) {
            throw new IllegalStateException(
                    "Segredos inseguros detectados (perfil prod). Corrija no .env: " + String.join("; ", problems));
        }

        for (String p : problems) {
            log.warn("[seguranca] {} — defina no .env antes de ir para producao.", p);
        }
    }
}
