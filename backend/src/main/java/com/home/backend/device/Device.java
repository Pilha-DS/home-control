package com.home.backend.device;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import jakarta.persistence.UniqueConstraint;

import java.time.Instant;

/**
 * Dispositivo ESP8266 pertencente a um usuario ({@code user_id}).
 * Guarda a credencial MQTT exclusiva do dispositivo (token) usada no
 * firmware para se conectar ao broker de forma isolada.
 */
@Entity
@Table(name = "devices", uniqueConstraints = @UniqueConstraint(
        name = "uq_devices_user_uuid", columnNames = {"user_id", "uuid"}))
public class Device {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, length = 64)
    private String uuid;

    @Column(name = "user_id", nullable = false, length = 40)
    private String userId;

    @Column(length = 64)
    private String name;

    @Column(length = 30)
    private String modelo;

    @Column(name = "mqtt_username", length = 64)
    private String mqttUsername;

    @Column(name = "mqtt_password", length = 128)
    private String mqttPassword;

    @Column(name = "device_token", length = 128)
    private String deviceToken;

    @Column(nullable = false)
    private boolean active = true;

    @Column(name = "created_at", nullable = false)
    private Instant createdAt = Instant.now();

    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }

    public String getUuid() { return uuid; }
    public void setUuid(String uuid) { this.uuid = uuid; }

    public String getUserId() { return userId; }
    public void setUserId(String userId) { this.userId = userId; }

    public String getName() { return name; }
    public void setName(String name) { this.name = name; }

    public String getModelo() { return modelo; }
    public void setModelo(String modelo) { this.modelo = modelo; }

    public String getMqttUsername() { return mqttUsername; }
    public void setMqttUsername(String mqttUsername) { this.mqttUsername = mqttUsername; }

    public String getMqttPassword() { return mqttPassword; }
    public void setMqttPassword(String mqttPassword) { this.mqttPassword = mqttPassword; }

    public String getDeviceToken() { return deviceToken; }
    public void setDeviceToken(String deviceToken) { this.deviceToken = deviceToken; }

    public boolean isActive() { return active; }
    public void setActive(boolean active) { this.active = active; }

    public Instant getCreatedAt() { return createdAt; }
    public void setCreatedAt(Instant createdAt) { this.createdAt = createdAt; }
}
