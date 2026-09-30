import 'package:mqtt_client/mqtt_browser_client.dart';
import 'package:mqtt_client/mqtt_client.dart';

MqttClient createMqttClient(String serverUri, String clientId, int port) {
  // No browser só existe WebSocket — URI deve ser ws:// ou wss://
  return MqttBrowserClient.withPort(serverUri, clientId, port);
}

void configureMqttTransport(MqttClient client) {
  // Browser já usa WebSocket; nada a configurar além dos protocols no service.
}
