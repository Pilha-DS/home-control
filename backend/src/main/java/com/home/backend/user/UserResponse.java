package com.home.backend.user;

import java.time.Instant;

public record UserResponse(
        String username,
        String role,
        boolean enabled,
        Instant createdAt) {

    public static UserResponse from(User user) {
        return new UserResponse(
                user.getUsername(),
                user.getRole(),
                user.isEnabled(),
                user.getCreatedAt());
    }
}
