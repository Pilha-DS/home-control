import 'package:mqtt_client/mqtt_client.dart';

/// Stub — substituído por setup_io / setup_web via conditional import.
MqttClient createMqttClient(String serverUri, String clientId, int port) {
  throw UnsupportedError('Plataforma MQTT não suportada');
}

void configureMqttTransport(MqttClient client) {
  // no-op
}
