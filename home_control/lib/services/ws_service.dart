import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:web_socket_channel/web_socket_channel.dart';

import 'auth_models.dart';

/// Evento recebido do backend via WebSocket. Envelope:
/// `{type, uuid, payload}` (type = sufixo do tópico MQTT original).
class WsEvent {
  final String type;
  final String uuid;
  final String payload;

  const WsEvent({required this.type, required this.uuid, required this.payload});

  factory WsEvent.fromJson(Map<String, dynamic> json) => WsEvent(
        type: json['type'] as String? ?? '',
        uuid: json['uuid'] as String? ?? '',
        payload: json['payload']?.toString() ?? '',
      );
}

/// Conexao WebSocket com o backend (/ws/events?token=...).
class WsService {
  WebSocketChannel? _channel;
  StreamSubscription? _sub;
  bool _connecting = false;
  bool _connected = false;

  ServerConfig? _lastServer;
  String? _lastToken;
  Timer? _watchTimer;
  bool _autoReconnect = false;

  final _eventController = StreamController<WsEvent>.broadcast();
  final _statusController = StreamController<bool>.broadcast();

  Stream<WsEvent> get events => _eventController.stream;
  Stream<bool> get statusStream => _statusController.stream;

  bool get isConnected => _connected && _channel != null;

  /// Mantém WS vivo: a cada [interval] verifica e reconecta se caiu.
  void startWatch({Duration interval = const Duration(seconds: 10)}) {
    _autoReconnect = true;
    _watchTimer?.cancel();
    _watchTimer = Timer.periodic(interval, (_) => _ensureConnected());
  }

  void stopWatch() {
    _autoReconnect = false;
    _watchTimer?.cancel();
    _watchTimer = null;
  }

  Future<void> _ensureConnected() async {
    if (!_autoReconnect) return;
    if (_connecting || isConnected) return;
    final server = _lastServer;
    final token = _lastToken;
    if (server == null || token == null || token.isEmpty) return;
    debugPrint('WS watch: reconectando…');
    await connect(server, token);
  }

  Future<void> connect(ServerConfig server, String token) async {
    if (_connecting) return;
    _connecting = true;
    _lastServer = server;
    _lastToken = token;
    await disconnect(clearCreds: false);

    final uri = server.wsUri('/ws/events?token=${Uri.encodeComponent(token)}');
    debugPrint('WS connect -> $uri');

    try {
      final channel = WebSocketChannel.connect(uri);
      try {
        await channel.ready.timeout(const Duration(seconds: 12));
      } catch (e) {
        debugPrint('WS ready timeout/error: $e');
        try {
          await channel.sink.close();
        } catch (_) {}
        _connected = false;
        _statusController.add(false);
        return;
      }

      _channel = channel;
      _connected = true;
      _statusController.add(true);

      _sub = channel.stream.listen(
        (data) {
          try {
            final j = jsonDecode(data as String);
            if (j is Map<String, dynamic>) {
              _eventController.add(WsEvent.fromJson(j));
            }
          } catch (e) {
            debugPrint('WS parse error: $e');
          }
        },
        onDone: () {
          debugPrint('WS closed');
          _connected = false;
          _channel = null;
          _statusController.add(false);
        },
        onError: (e) {
          debugPrint('WS error: $e');
          _connected = false;
          _channel = null;
          _statusController.add(false);
        },
        cancelOnError: true,
      );
    } catch (e) {
      debugPrint('WS connect error: $e');
      _connected = false;
      _channel = null;
      _statusController.add(false);
    } finally {
      _connecting = false;
    }
  }

  Future<void> disconnect({bool clearCreds = true}) async {
    await _sub?.cancel();
    _sub = null;
    try {
      await _channel?.sink.close();
    } catch (_) {}
    _channel = null;
    _connected = false;
    if (clearCreds) {
      _lastServer = null;
      _lastToken = null;
    }
    _statusController.add(false);
  }

  void dispose() {
    stopWatch();
    _sub?.cancel();
    _channel?.sink.close();
    _eventController.close();
    _statusController.close();
  }
}
