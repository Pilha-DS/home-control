package com.home.backend.device;

/**
 * Extrai user_id, uuid e sufixo de topicos MQTT do firmware:
 * <ul>
 *   <li>{@code home/<user_id>/<uuid>/<suffix>}</li>
 *   <li>{@code home/logs/<user_id>/<uuid>/<suffix>}</li>
 * </ul>
 * O backend assina {@code home/+/+/...} e mapeia cada UUID ao user_id do topico.
 */
public final class TopicParser {

    private TopicParser() {}

    public record ParsedTopic(String userId, String uuid, String suffix) {}

    public static ParsedTopic parse(String topic) {
        String[] parts = topic.split("/");
        if (parts.length == 0 || !"home".equals(parts[0])) return null;

        // Mantém o prefixo "logs/" no sufixo (ex.: "logs/basic"), igual ao
        // app Flutter e ao DeviceResponse — senão o modelo da placa some do
        // cache e a UI cai no default NodeMCU.
        if (parts.length >= 5 && "logs".equals(parts[1])) {
            String rest = String.join("/", java.util.Arrays.copyOfRange(parts, 4, parts.length));
            return new ParsedTopic(parts[2], parts[3], "logs/" + rest);
        }
        if (parts.length >= 4) {
            return new ParsedTopic(parts[1], parts[2], String.join("/", java.util.Arrays.copyOfRange(parts, 3, parts.length)));
        }
        return null;
    }
}
