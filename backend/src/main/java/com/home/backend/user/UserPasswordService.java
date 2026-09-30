package com.home.backend.user;

import com.home.backend.auth.CurrentUser;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/**
 * Troca de senha: usuario altera a propria senha (valida a atual);
 * admin pode redefinir a senha de outro usuario sem saber a atual.
 */
@Service
public class UserPasswordService {

    private final UserRepository userRepository;
    private final PasswordEncoder passwordEncoder;
    private final CurrentUser currentUser;

    public UserPasswordService(UserRepository userRepository,
                               PasswordEncoder passwordEncoder,
                               CurrentUser currentUser) {
        this.userRepository = userRepository;
        this.passwordEncoder = passwordEncoder;
        this.currentUser = currentUser;
    }

    /** Usuario logado altera a propria senha (exige a senha atual). */
    @Transactional
    public void changeOwnPassword(String currentPassword, String newPassword) {
        User user = requireUser(currentUser.username());
        if (currentPassword == null || currentPassword.isEmpty()
                || !passwordEncoder.matches(currentPassword, user.getPassword())) {
            throw new IllegalArgumentException("senha_atual_incorreta");
        }
        applyPassword(user, newPassword);
    }

    /** Admin redefine a senha de outro usuario (sem saber a atual). */
    @Transactional
    public void resetPasswordByAdmin(String username, String newPassword) {
        if (!currentUser.isAdmin()) {
            throw new IllegalArgumentException("acesso_negado");
        }
        User user = requireUser(username);
        applyPassword(user, newPassword);
    }

    private void applyPassword(User user, String newPassword) {
        if (newPassword == null || newPassword.trim().length() < 4) {
            throw new IllegalArgumentException("senha_muito_curta");
        }
        user.setPassword(passwordEncoder.encode(newPassword.trim()));
        userRepository.save(user);
    }

    private User requireUser(String username) {
        return userRepository.findByUsername(username)
                .orElseThrow(() -> new IllegalArgumentException("usuario_nao_encontrado"));
    }
}
