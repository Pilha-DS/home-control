package com.home.backend.user;

import org.springframework.security.access.prepost.PreAuthorize;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import java.util.List;
import java.util.Map;

/**
 * Cadastro e gestao de usuarios (admin-only). O username e o user_id
 * dos topicos MQTT.
 */
@RestController
@RequestMapping("/api/users")
@Tag(name = "Usuários", description = "Gestão de logins (somente admin)")
public class UserController {

    private final UserRepository userRepository;
    private final PasswordEncoder passwordEncoder;
    private final UserSecretService userSecretService;
    private final UserAdminService userAdminService;
    private final UserPasswordService userPasswordService;

    public UserController(UserRepository userRepository,
                          PasswordEncoder passwordEncoder,
                          UserSecretService userSecretService,
                          UserAdminService userAdminService,
                          UserPasswordService userPasswordService) {
        this.userRepository = userRepository;
        this.passwordEncoder = passwordEncoder;
        this.userSecretService = userSecretService;
        this.userAdminService = userAdminService;
        this.userPasswordService = userPasswordService;
    }

    @GetMapping
    @PreAuthorize("hasRole('ADMIN')")
    @Operation(summary = "Listar usuários")
    public List<UserResponse> list() {
        return userAdminService.listAll();
    }

    @PostMapping
    @PreAuthorize("hasRole('ADMIN')")
    public ResponseEntity<?> create(@Valid @RequestBody CreateUserRequest request) {
        if (userRepository.existsByUsername(request.username())) {
            return ResponseEntity.badRequest().body(Map.of("error", "username_ja_existe"));
        }
        User user = new User();
        user.setUsername(request.username().trim());
        user.setPassword(passwordEncoder.encode(request.password()));
        String role = request.role() != null && request.role().equalsIgnoreCase("ADMIN")
                ? "ROLE_ADMIN" : "ROLE_USER";
        user.setRole(role);
        user.setEnabled(true);
        userRepository.save(user);
        userSecretService.ensureSecret(user);
        user = userRepository.findByUsername(user.getUsername()).orElseThrow();
        return ResponseEntity.ok(Map.of(
                "username", user.getUsername(),
                "role", user.getRole(),
                "espSecret", user.getEspSecretToken()));
    }

    @PatchMapping("/{username}")
    @PreAuthorize("hasRole('ADMIN')")
    public UserResponse setEnabled(@PathVariable String username,
                                   @Valid @RequestBody UpdateUserEnabledRequest request) {
        return userAdminService.setEnabled(username, request.enabled());
    }

    @DeleteMapping("/{username}")
    @PreAuthorize("hasRole('ADMIN')")
    public ResponseEntity<?> delete(@PathVariable String username) {
        userAdminService.delete(username);
        return ResponseEntity.ok(Map.of("ok", true));
    }

    /** Admin redefine a senha de outro usuario (nao precisa saber a atual). */
    @PostMapping("/{username}/reset-password")
    @PreAuthorize("hasRole('ADMIN')")
    public ResponseEntity<?> resetPassword(@PathVariable String username,
                                           @Valid @RequestBody AdminResetPasswordRequest request) {
        userPasswordService.resetPasswordByAdmin(username, request.newPassword());
        return ResponseEntity.ok(Map.of("ok", true, "message", "senha_redefinida"));
    }
}
