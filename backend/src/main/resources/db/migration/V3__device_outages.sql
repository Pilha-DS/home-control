-- UUID deixa de ser unico globalmente: cada conta pode ter o mesmo UUID.
ALTER TABLE devices DROP INDEX uq_devices_uuid;
ALTER TABLE devices ADD CONSTRAINT uq_devices_user_uuid UNIQUE (user_id, uuid);

-- Quedas (offline) por usuario + UUID.
CREATE TABLE device_outages (
    id           BIGINT AUTO_INCREMENT PRIMARY KEY,
    user_id      VARCHAR(40)  NOT NULL,
    device_uuid  VARCHAR(64)  NOT NULL,
    device_name  VARCHAR(64),
    started_at   TIMESTAMP    NOT NULL,
    ended_at     TIMESTAMP    NULL,
    duration_ms  BIGINT       NULL,
    CONSTRAINT fk_outages_user FOREIGN KEY (user_id) REFERENCES users (username)
        ON DELETE CASCADE ON UPDATE CASCADE
);

CREATE INDEX idx_outages_user ON device_outages (user_id);
CREATE INDEX idx_outages_user_uuid ON device_outages (user_id, device_uuid);
CREATE INDEX idx_outages_open ON device_outages (user_id, device_uuid, ended_at);
