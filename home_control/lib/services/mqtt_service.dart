import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:mqtt_client/mqtt_client.dart';
import 'package:uuid/uuid.dart';

import '../models/models.dart';
import 'mqtt_client_setup.dart'
    if (dart.library.io) 'mqtt_client_setup_io.dart'
    if (dart.library.js_interop) 'mqtt_client_setup_web.dart';

/// Cliente MQTT sobre WebSocket (WSS) — IO (Windows/Android) ou Browser.
class MqttService {
  MqttClient? _client;
  StreamSubscription<List<MqttReceivedMessage<MqttMessage>>>? _sub;
  BrokerConfig _config = const BrokerConfig();
  bool _connecting = false;

  final _statusController = StreamController<MqttConnectionState>.broadcast();
  final _messageController = StreamController<(String, String)>.broadcast();

  Stream<MqttConnectionState> get statusStream => _statusController.stream;
  Stream<(String, String)> get messageStream => _messageController.stream;

  MqttConnectionState get connectionState =>
      _client?.connectionStatus?.state ?? MqttConnectionState.disconnected;

  bool get isConnected => connectionState == MqttConnectionState.connected;

  BrokerConfig get config => _config;

  Future<void> connect(BrokerConfig config) async {
    if (_connecting) return;
    _connecting = true;
    _config = config;

    await disconnect();

    var path = config.path.trim().isEmpty ? '/' : config.path.trim();
    if (!path.startsWith('/')) path = '/$path';

    final scheme = config.useSsl ? 'wss' : 'ws';
    final serverUri = '$scheme://${config.host}$path';
    final clientId = 'home_app_${const Uuid().v4().substring(0, 8)}';

    debugPrint('MQTT connect → $serverUri port=${config.port} ($clientId)');

    final client = createMqttClient(serverUri, clientId, config.port)
      ..websocketProtocols = MqttClientConstants.protocolsSingleDefault
      ..keepAlivePeriod = 30
      ..autoReconnect = true
      ..resubscribeOnAutoReconnect = true
      ..connectTimeoutPeriod = 15000
      ..logging(on: kDebugMode)
      ..setProtocolV311();

    configureMqttTransport(client);

    client.connectionMessage = MqttConnectMessage()
        .withClientIdentifier(clientId)
        .authenticateAs(config.username, config.password)
        .startClean();

    client.onConnected = () {
      debugPrint('MQTT connected');
      _statusController.add(MqttConnectionState.connected);
      _subscribeDefaults();
    };
    client.onDisconnected = () {
      debugPrint('MQTT disconnected');
      _statusController.add(MqttConnectionState.disconnected);
    };
    client.onAutoReconnect = () {
      _statusController.add(MqttConnectionState.connecting);
    };
    client.onAutoReconnected = () {
      debugPrint('MQTT auto-reconnected');
      _statusController.add(MqttConnectionState.connected);
      _subscribeDefaults();
    };

    _client = client;
    _statusController.add(MqttConnectionState.connecting);

    try {
      await client.connect(config.username, config.password);
      final state = client.connectionStatus?.state;
      final code = client.connectionStatus?.returnCode;
      debugPrint('MQTT status=$state returnCode=$code');
      if (state != MqttConnectionState.connected) {
        throw Exception(
          'Falha MQTT ($code). URI: $serverUri:${config.port}',
        );
      }
      _sub = client.updates?.listen((events) {
        for (final event in events) {
          final rec = event.payload as MqttPublishMessage;
          final payload = MqttPublishPayload.bytesToStringAsString(
            rec.payload.message,
          );
          _messageController.add((event.topic, payload));
        }
      });
    } catch (e, st) {
      debugPrint('MQTT connect error: $e\n$st');
      _statusController.add(MqttConnectionState.faulted);
      try {
        client.disconnect();
      } catch (_) {}
      rethrow;
    } finally {
      _connecting = false;
    }
  }

  void _subscribeDefaults() {
    final c = _client;
    if (c == null || !isConnected) return;
    // home/{user}/{uuid}/… e home/logs/{user}/{uuid}/…
    c.subscribe('home/+/+/status', MqttQos.atLeastOnce);
    c.subscribe('home/+/+/state', MqttQos.atLeastOnce);
    c.subscribe('home/+/+/ack', MqttQos.atLeastOnce);
    c.subscribe('home/+/+/ip', MqttQos.atLeastOnce);
    c.subscribe('home/+/+/config/ack', MqttQos.atLeastOnce);
    c.subscribe('home/logs/+/+/basic', MqttQos.atLeastOnce);
    c.subscribe('home/logs/+/+/advance', MqttQos.atLeastOnce);
  }

  Future<void> publishCommand(RelayCommand cmd) async {
    final c = _client;
    if (c == null || !isConnected) {
      throw Exception('MQTT desconectado');
    }
    if (cmd.userId.trim().isEmpty) {
      throw Exception('user_id do dispositivo vazio');
    }
    final topic = 'home/${cmd.userId}/${cmd.deviceUuid}/cmd';
    final builder = MqttClientPayloadBuilder()..addString(cmd.toJson());
    c.publishMessage(topic, MqttQos.atLeastOnce, builder.payload!);
  }

  Future<void> publishDeviceConfig(DeviceRemoteConfig cfg) async {
    final c = _client;
    if (c == null || !isConnected) {
      throw Exception('MQTT desconectado');
    }
    final pathUser = cfg.topicUserId.trim().isNotEmpty
        ? cfg.topicUserId.trim()
        : (cfg.userId?.trim() ?? '');
    if (pathUser.isEmpty) {
      throw Exception('user_id necessário no tópico de config');
    }
    final topic = 'home/$pathUser/${cfg.deviceUuid}/config';
    final builder = MqttClientPayloadBuilder()..addString(cfg.toJson());
    c.publishMessage(topic, MqttQos.atLeastOnce, builder.payload!);
  }

  Future<void> disconnect() async {
    await _sub?.cancel();
    _sub = null;
    try {
      _client?.disconnect();
    } catch (_) {}
    _client = null;
  }

  void dispose() {
    unawaited(disconnect());
    _statusController.close();
    _messageController.close();
  }

  /// Extrai user_id de `home/{user}/{uuid}/…` ou `home/logs/{user}/{uuid}/…`.
  static String? userIdFromTopic(String topic) {
    final parts = topic.split('/');
    if (parts.isEmpty || parts[0] != 'home') return null;
    if (parts.length >= 5 && parts[1] == 'logs') return parts[2];
    if (parts.length >= 4 && parts[1] != 'logs') return parts[1];
    return null;
  }

  /// Extrai UUID de `home/{user}/{uuid}/…` ou `home/logs/{user}/{uuid}/…`.
  static String? uuidFromTopic(String topic) {
    final parts = topic.split('/');
    if (parts.isEmpty || parts[0] != 'home') return null;
    if (parts.length >= 5 && parts[1] == 'logs') return parts[3];
    if (parts.length >= 4 && parts[1] != 'logs') return parts[2];
    return null;
  }

  /// Sufixo útil: `status`, `state`, `logs/basic`, `logs/advance`, …
  static String? suffixFromTopic(String topic) {
    final parts = topic.split('/');
    if (parts.isEmpty || parts[0] != 'home') return null;
    if (parts.length >= 5 && parts[1] == 'logs') {
      return 'logs/${parts[4]}';
    }
    if (parts.length >= 4 && parts[1] != 'logs') {
      return parts.skip(3).join('/');
    }
    return null;
  }

  static Map<String, dynamic>? tryParseJson(String payload) {
    try {
      final v = jsonDecode(payload);
      if (v is Map<String, dynamic>) return v;
    } catch (_) {}
    return null;
  }
}
