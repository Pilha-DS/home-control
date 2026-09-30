import 'package:flutter/foundation.dart' show kIsWeb;

/// Dados do usuário logado (retornados no login).
class AuthUser {
  final String username; // == user_id dos tópicos MQTT
  final String role;
  final String token;
  final String? espSecret;

  const AuthUser({
    required this.username,
    required this.role,
    required this.token,
    this.espSecret,
  });

  factory AuthUser.fromJson(Map<String, dynamic> json) => AuthUser(
        username: json['username'] as String,
        role: json['role'] as String? ?? 'ROLE_USER',
        token: json['token'] as String,
        espSecret: json['espSecret'] as String?,
      );

  bool get isAdmin => role.contains('ADMIN');
}

/// Usuário listado na área admin.
class AdminUser {
  final String username;
  final String role;
  final bool enabled;
  final DateTime createdAt;

  const AdminUser({
    required this.username,
    required this.role,
    required this.enabled,
    required this.createdAt,
  });

  factory AdminUser.fromJson(Map<String, dynamic> json) => AdminUser(
        username: json['username'] as String,
        role: json['role'] as String? ?? 'ROLE_USER',
        enabled: json['enabled'] as bool? ?? true,
        createdAt: DateTime.parse(json['createdAt'] as String),
      );

  bool get isAdmin => role.contains('ADMIN');
}

/// Configuração do servidor backend (substitui o broker MQTT direto).
///
/// Padrão: `https://mqtt.omny.app.br` (API/backend em produção).
/// UI fica em `https://automacao.omny.app.br`.
/// Sobrescreva em build: `--dart-define=API_BASE_URL=http://host:8080`
class ServerConfig {
  static const String productionBaseUrl = 'https://mqtt.omny.app.br';

  static const String defaultBaseUrl = String.fromEnvironment(
    'API_BASE_URL',
    defaultValue: productionBaseUrl,
  );

  final String baseUrl;

  const ServerConfig({this.baseUrl = defaultBaseUrl});

  /// true quando REST/WS usam caminhos relativos na origem atual (somente web).
  bool get usesSameOrigin => normalizedBaseUrl.isEmpty && kIsWeb;

  /// true se falta host absoluto (APK sem URL configurada).
  bool get needsAbsoluteUrl => normalizedBaseUrl.isEmpty && !kIsWeb;

  String get normalizedBaseUrl {
    var u = baseUrl.trim();
    while (u.endsWith('/')) {
      u = u.substring(0, u.length - 1);
    }
    return u;
  }

  /// Monta URI REST (absoluta ou relativa à origem).
  Uri restUri(String path) {
    final p = path.startsWith('/') ? path : '/$path';
    if (needsAbsoluteUrl) {
      throw StateError(
        'Configure a URL do servidor (ex.: ${ServerConfig.productionBaseUrl})',
      );
    }
    if (usesSameOrigin) return Uri.parse(p);
    return Uri.parse('$normalizedBaseUrl$p');
  }

  /// Monta URI WebSocket (absoluta ou relativa à origem do app web).
  Uri wsUri(String pathAndQuery) {
    if (needsAbsoluteUrl) {
      throw StateError(
        'Configure a URL do servidor (ex.: ${ServerConfig.productionBaseUrl})',
      );
    }
    if (usesSameOrigin) {
      final base = Uri.base;
      final scheme = base.scheme == 'https' ? 'wss' : 'ws';
      final qIdx = pathAndQuery.indexOf('?');
      final path = qIdx >= 0 ? pathAndQuery.substring(0, qIdx) : pathAndQuery;
      final query = qIdx >= 0 ? pathAndQuery.substring(qIdx + 1) : null;
      return Uri(
        scheme: scheme,
        host: base.host,
        port: base.hasPort ? base.port : null,
        path: path.startsWith('/') ? path : '/$path',
        query: query,
      );
    }
    final u = Uri.parse(normalizedBaseUrl);
    final scheme = u.scheme == 'https' ? 'wss' : 'ws';
    final qIdx = pathAndQuery.indexOf('?');
    final path = qIdx >= 0 ? pathAndQuery.substring(0, qIdx) : pathAndQuery;
    final query = qIdx >= 0 ? pathAndQuery.substring(qIdx + 1) : null;
    return Uri(
      scheme: scheme,
      host: u.host,
      port: u.hasPort ? u.port : null,
      path: path.startsWith('/') ? path : '/$path',
      query: query,
    );
  }

  Map<String, dynamic> toJson() => {'baseUrl': baseUrl};

  factory ServerConfig.fromJson(Map<String, dynamic> json) =>
      ServerConfig(baseUrl: json['baseUrl'] as String? ?? defaultBaseUrl);
}
