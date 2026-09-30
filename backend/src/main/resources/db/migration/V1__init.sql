-- V1__init.sql
-- Schema inicial do backend intermediario.
-- username = user_id dos topicos MQTT (home/<user_id>/<uuid>/...).

CREATE TABLE users (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY,
    username    VARCHAR(40)  NOT NULL,
    password    VARCHAR(100) NOT NULL,
    role        VARCHAR(32)  NOT NULL DEFAULT 'ROLE_USER',
    enabled     BOOLEAN      NOT NULL DEFAULT TRUE,
    created_at  TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uq_users_username UNIQUE (username)
);

CREATE TABLE devices (
    id            BIGINT AUTO_INCREMENT PRIMARY KEY,
    uuid          VARCHAR(64)  NOT NULL,
    user_id       VARCHAR(40)  NOT NULL,
    name          VARCHAR(64),
    modelo        VARCHAR(30),
    mqtt_username VARCHAR(64),           -- token do dispositivo (conta MQTT propria)
    mqtt_password VARCHAR(128),          -- segredo do dispositivo
    device_token  VARCHAR(128),          -- token de aplicacao (validado no cmd/config)
    active        BOOLEAN      NOT NULL DEFAULT TRUE,
    created_at    TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT uq_devices_uuid UNIQUE (uuid),
    CONSTRAINT fk_devices_user FOREIGN KEY (user_id) REFERENCES users (username)
        ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE INDEX idx_devices_user ON devices (user_id);
