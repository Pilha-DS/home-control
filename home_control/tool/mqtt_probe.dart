import 'package:mqtt_client/mqtt_client.dart';
import 'package:mqtt_client/mqtt_server_client.dart';

Future<void> main() async {
  const host = 'mosquito.omny.tec.br';
  const port = 443;
  const path = '/';
  const user = 'cardoso';
  const pass = 'C6m8n4d2d3';

  final uri = 'wss://$host$path';
  print('Connecting to $uri:$port ...');

  final client = MqttServerClient.withPort(uri, 'home_test_cli', port)
    ..useWebSocket = true
    ..websocketProtocols = MqttClientConstants.protocolsSingleDefault
    ..keepAlivePeriod = 20
    ..connectTimeoutPeriod = 15000
    ..logging(on: true)
    ..setProtocolV311()
    ..onBadCertificate = (Object _) => true;

  client.connectionMessage = MqttConnectMessage()
      .withClientIdentifier('home_test_cli')
      .authenticateAs(user, pass)
      .startClean();

  try {
    await client.connect(user, pass);
    print('STATE=${client.connectionStatus?.state}');
    print('CODE=${client.connectionStatus?.returnCode}');
    if (client.connectionStatus?.state == MqttConnectionState.connected) {
      print('OK connected');
      client.disconnect();
    } else {
      print('FAIL not connected');
    }
  } catch (e, st) {
    print('ERROR $e');
    print(st);
  }
}
