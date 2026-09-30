import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

MqttClient createMqttClient(String serverUri, String clientId, int port) {
  return MqttServerClient.withPort(serverUri, clientId, port);
}

void configureMqttTransport(MqttClient client) {
  final c = client as MqttServerClient;
  c.useWebSocket = true;
  // secure=true é só TCP TLS — NÃO usar com WSS
  // Assinatura exige Object (cast interno da lib)
  c.onBadCertificate = (Object _) => true;
}
