-- Secret unico por usuario para autenticacao do ESP (HTTP gateway Java).
ALTER TABLE users
    ADD COLUMN esp_secret_token VARCHAR(128) NULL;

CREATE UNIQUE INDEX uq_users_esp_secret ON users (esp_secret_token);
