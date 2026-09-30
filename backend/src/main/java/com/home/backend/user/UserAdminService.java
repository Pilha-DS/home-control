package com.home.backend.user;

import com.home.backend.auth.CurrentUser;
import com.home.backend.device.DeviceRepository;
import com.home.backend.device.DeviceStateCache;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.util.Comparator;
import java.util.List;

@Service
public class UserAdminService {

    private final UserRepository userRepository;
    private final DeviceRepository deviceRepository;
    private final DeviceStateCache deviceStateCache;
    private final CurrentUser currentUser;

    public UserAdminService(UserRepository userRepository,
                            DeviceRepository deviceRepository,
                            DeviceStateCache deviceStateCache,
                            CurrentUser currentUser) {
        this.userRepository = userRepository;
        this.deviceRepository = deviceRepository;
        this.deviceStateCache = deviceStateCache;
        this.currentUser = currentUser;
    }

    public List<UserResponse> listAll() {
        requireAdmin();
        return userRepository.findAll().stream()
                .sorted(Comparator.comparing(User::getCreatedAt).reversed())
                .map(UserResponse::from)
                .toList();
    }

    @Transactional
    public UserResponse setEnabled(String username, boolean enabled) {
        requireAdmin();
        User user = requireUser(username);
        rejectSelfMutation(user.getUsername(), "nao_pode_alterar_proprio_usuario");
        user.setEnabled(enabled);
        userRepository.save(user);
        return UserResponse.from(user);
    }

    @Transactional
    public void delete(String username) {
        requireAdmin();
        User user = requireUser(username);
        rejectSelfMutation(user.getUsername(), "nao_pode_excluir_proprio_usuario");

        deviceRepository.findByUserId(user.getUsername()).forEach(d -> {
            deviceStateCache.remove(d.getUserId(), d.getUuid());
            deviceRepository.delete(d);
        });
        userRepository.delete(user);
    }

    private User requireUser(String username) {
        return userRepository.findByUsername(username.trim())
                .orElseThrow(() -> new IllegalArgumentException("usuario_nao_encontrado"));
    }

    private void rejectSelfMutation(String targetUsername, String errorCode) {
        if (targetUsername.equalsIgnoreCase(currentUser.username())) {
            throw new IllegalArgumentException(errorCode);
        }
    }

    private void requireAdmin() {
        if (!currentUser.isAdmin()) {
            throw new IllegalArgumentException("acesso_negado");
        }
    }
}
