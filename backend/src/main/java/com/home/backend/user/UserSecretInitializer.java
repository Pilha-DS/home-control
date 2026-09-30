package com.home.backend.user;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.CommandLineRunner;
import org.springframework.stereotype.Component;

/**
 * Gera {@code esp_secret_token} para usuarios existentes (pos-migracao V2).
 */
@Component
public class UserSecretInitializer implements CommandLineRunner {

    private static final Logger log = LoggerFactory.getLogger(UserSecretInitializer.class);

    private final UserRepository userRepository;
    private final UserSecretService userSecretService;

    public UserSecretInitializer(UserRepository userRepository,
                                 UserSecretService userSecretService) {
        this.userRepository = userRepository;
        this.userSecretService = userSecretService;
    }

    @Override
    public void run(String... args) {
        int count = 0;
        for (User user : userRepository.findAll()) {
            if (user.getEspSecretToken() == null || user.getEspSecretToken().isBlank()) {
                userSecretService.ensureSecret(user);
                count++;
            }
        }
        if (count > 0) {
            log.info("esp_secret_token gerado para {} usuario(s)", count);
        }
    }
}
