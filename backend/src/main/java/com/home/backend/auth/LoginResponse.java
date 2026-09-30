package com.home.backend.auth;

public record LoginResponse(
        String token,
        String username,
        String role,
        String espSecret) {
}
