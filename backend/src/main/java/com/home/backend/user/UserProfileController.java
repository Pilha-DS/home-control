package com.home.backend.user;

import com.home.backend.auth.CurrentUser;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.Map;

/**
 * Perfil do usuario logado — secret do ESP (rotacionavel) e senha.
 */
@RestController
@RequestMapping("/api/users/me")
@Tag(name = "Perfil", description = "Dados do usuário logado")
public class UserProfileController {

    private final UserRepository userRepository;
    private final UserSecretService userSecretService;
    private final UserPasswordService userPasswordService;
    private final CurrentUser currentUser;

    public UserProfileController(UserRepository userRepository,
                                 UserSecretService userSecretService,
                                 UserPasswordService userPasswordService,
                                 CurrentUser currentUser) {
        this.userRepository = userRepository;
        this.userSecretService = userSecretService;
        this.userPasswordService = userPasswordService;
        this.currentUser = currentUser;
    }

    /**
     * Usuario logado troca a propria senha. Exige a senha atual correta.
     */
    @PostMapping("/password")
    public ResponseEntity<?> changePassword(@Valid @RequestBody ChangePasswordRequest request) {
        userPasswordService.changeOwnPassword(request.currentPassword(), request.newPassword());
        return ResponseEntity.ok(Map.of("ok", true, "message", "senha_alterada"));
    }

    @GetMapping("/esp-secret")
    public ResponseEntity<?> getEspSecret() {
        User user = requireCurrentUser();
        userSecretService.ensureSecret(user);
        user = userRepository.findByUsername(user.getUsername()).orElseThrow();
        return ResponseEntity.ok(Map.of("espSecret", user.getEspSecretToken()));
    }

    /**
     * Invalida o secret anterior e gera outro aleatorio.
     * ESPs precisam receber o novo valor (config USER_SECRET ou portal).
     */
    @PostMapping("/esp-secret/refresh")
    public ResponseEntity<?> refreshEspSecret() {
        String username = currentUser.username();
        String fresh = userSecretService.refresh(username);
        return ResponseEntity.ok(Map.of(
                "espSecret", fresh,
                "message", "Secret anterior invalidado. Atualize todos os ESPs."));
    }

    private User requireCurrentUser() {
        return userRepository.findByUsername(currentUser.username())
                .orElseThrow(() -> new IllegalArgumentException("usuario_nao_encontrado"));
    }
}
