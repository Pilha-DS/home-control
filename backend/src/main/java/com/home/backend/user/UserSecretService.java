package com.home.backend.user;

import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.security.SecureRandom;

/**
 * Secret unico por usuario ({@code esp_secret_token}). O ESP envia no header
 * {@code X-User-Secret} — o backend resolve o dono e valida ownership do UUID.
 */
@Service
public class UserSecretService {

    private static final SecureRandom RANDOM = new SecureRandom();
    private static final String ALPHABET =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    private final UserRepository userRepository;

    public UserSecretService(UserRepository userRepository) {
        this.userRepository = userRepository;
    }

    @Transactional
    public User ensureSecret(User user) {
        if (user.getEspSecretToken() != null && !user.getEspSecretToken().isBlank()) {
            return user;
        }
        user.setEspSecretToken(generateUniqueToken());
        return userRepository.save(user);
    }

    @Transactional
    public String refresh(String username) {
        User user = userRepository.findByUsername(username)
                .orElseThrow(() -> new IllegalArgumentException("usuario_nao_encontrado"));
        user.setEspSecretToken(generateUniqueToken());
        userRepository.save(user);
        return user.getEspSecretToken();
    }

    public String secretForUsername(String username) {
        return userRepository.findByUsername(username)
                .map(User::getEspSecretToken)
                .filter(s -> s != null && !s.isBlank())
                .orElse(null);
    }

    private String generateUniqueToken() {
        for (int attempt = 0; attempt < 20; attempt++) {
            String token = randomToken(32);
            if (userRepository.findByEspSecretToken(token).isEmpty()) {
                return token;
            }
        }
        throw new IllegalStateException("Falha ao gerar esp_secret_token unico");
    }

    static String randomToken(int length) {
        StringBuilder sb = new StringBuilder(length);
        for (int i = 0; i < length; i++) {
            sb.append(ALPHABET.charAt(RANDOM.nextInt(ALPHABET.length())));
        }
        return sb.toString();
    }
}
