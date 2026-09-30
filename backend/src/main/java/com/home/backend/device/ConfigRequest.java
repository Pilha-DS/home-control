package com.home.backend.device;

/**
 * Requisicao de configuracao remota (equivale aos campos aceitos pelo
 * firmware em home/&lt;user&gt;/&lt;uuid&gt;/config). PASS e obrigatorio.
 */
public record ConfigRequest(
        String adminPass,
        String newUuid,
        String userId,
        String modelo,
        String wifiSsid,
        String wifiPass,
        String javaHost,
        Integer javaPort,
        String mqttHost,
        Integer mqttPort,
        String mqttUser,
        String mqttPass,
        String mqttPath,
        Integer logicaDoRele,
        String newAdminPass,
        /** 1 = AP home-setup sempre aberto, 0 = desligado. null = nao alterar. */
        Integer apOpen) {
}
