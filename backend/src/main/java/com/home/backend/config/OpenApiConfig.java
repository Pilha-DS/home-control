package com.home.backend.config;

import io.swagger.v3.oas.models.Components;
import io.swagger.v3.oas.models.OpenAPI;
import io.swagger.v3.oas.models.info.Contact;
import io.swagger.v3.oas.models.info.Info;
import io.swagger.v3.oas.models.security.SecurityRequirement;
import io.swagger.v3.oas.models.security.SecurityScheme;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

@Configuration
public class OpenApiConfig {

    @Bean
    public OpenAPI homeOpenApi() {
        final String bearer = "bearerAuth";
        final String espSecret = "espSecret";

        return new OpenAPI()
                .info(new Info()
                        .title("Automação HOME API")
                        .description("""
                                Backend Spring Boot — app Flutter (JWT) e ESP8266 (gateway Java).

                                **Autenticação app:** `POST /api/auth/login` → use o token em \
                                `Authorization: Bearer <token>`.

                                **Autenticação ESP:** header `X-User-Secret` (secret único do usuário).
                                """)
                        .version("1.0.0")
                        .contact(new Contact()
                                .name("HOME Automação")
                                .url("https://automacao.omny.app.br")))
                .components(new Components()
                        .addSecuritySchemes(bearer, new SecurityScheme()
                                .type(SecurityScheme.Type.HTTP)
                                .scheme("bearer")
                                .bearerFormat("JWT")
                                .description("JWT retornado por POST /api/auth/login"))
                        .addSecuritySchemes(espSecret, new SecurityScheme()
                                .type(SecurityScheme.Type.APIKEY)
                                .in(SecurityScheme.In.HEADER)
                                .name("X-User-Secret")
                                .description("Secret único do usuário para o ESP (alternativa legada: X-Device-Token)")))
                .addSecurityItem(new SecurityRequirement().addList(bearer));
    }
}
