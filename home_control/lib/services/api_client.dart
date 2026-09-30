import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import 'auth_models.dart';
import '../models/models.dart';

/// Cliente REST do backend Spring Boot. Todas as chamadas autenticadas
/// enviam o JWT no header `Authorization: Bearer <token>`.
class ApiClient {
  static const Duration _timeout = Duration(seconds: 15);

  ServerConfig server;
  String? token;

  ApiClient(this.server, {this.token});

  Map<String, String> get _headers {
    final h = {'Content-Type': 'application/json'};
    if (token != null) h['Authorization'] = 'Bearer $token';
    return h;
  }

  Uri _uri(String path) => server.restUri(path);

  Future<http.Response> _get(Uri uri, {Map<String, String>? headers}) =>
      http.get(uri, headers: headers).timeout(_timeout);

  Future<http.Response> _post(
    Uri uri, {
    Map<String, String>? headers,
    Object? body,
  }) =>
      http.post(uri, headers: headers, body: body).timeout(_timeout);

  Future<http.Response> _put(
    Uri uri, {
    Map<String, String>? headers,
    Object? body,
  }) =>
      http.put(uri, headers: headers, body: body).timeout(_timeout);

  Future<http.Response> _patch(
    Uri uri, {
    Map<String, String>? headers,
    Object? body,
  }) =>
      http.patch(uri, headers: headers, body: body).timeout(_timeout);

  Future<http.Response> _delete(Uri uri, {Map<String, String>? headers}) =>
      http.delete(uri, headers: headers).timeout(_timeout);

  /// POST /api/auth/login -> AuthUser (token + username + role).
  Future<AuthUser> login(String username, String password) async {
    final resp = await _post(
      _uri('/api/auth/login'),
      headers: {'Content-Type': 'application/json'},
      body: jsonEncode({'username': username, 'password': password}),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Login falhou'));
    }
    return AuthUser.fromJson(jsonDecode(resp.body) as Map<String, dynamic>);
  }

  /// POST /api/users/me/password — usuario logado troca a propria senha.
  Future<void> changeOwnPassword({
    required String currentPassword,
    required String newPassword,
  }) async {
    final resp = await _post(
      _uri('/api/users/me/password'),
      headers: _headers,
      body: jsonEncode({
        'currentPassword': currentPassword,
        'newPassword': newPassword,
      }),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao alterar senha'));
    }
  }

  /// POST /api/users/{username}/reset-password — admin redefine a senha.
  Future<void> resetUserPassword(String username, String newPassword) async {
    final resp = await _post(
      _uri('/api/users/$username/reset-password'),
      headers: _headers,
      body: jsonEncode({'newPassword': newPassword}),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao redefinir senha'));
    }
  }

  /// POST /api/users (admin-only) — cadastra novo usuário.
  Future<Map<String, dynamic>> createUser({
    required String username,
    required String password,
    bool admin = false,
  }) async {
    final resp = await _post(
      _uri('/api/users'),
      headers: _headers,
      body: jsonEncode({
        'username': username,
        'password': password,
        if (admin) 'role': 'ADMIN',
      }),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao cadastrar usuário'));
    }
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  /// GET /api/users (admin-only) — lista todos os usuários.
  Future<List<AdminUser>> listUsers() async {
    final resp = await _get(_uri('/api/users'), headers: _headers);
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao listar usuários'));
    }
    final list = jsonDecode(resp.body) as List<dynamic>;
    return list
        .map((e) => AdminUser.fromJson(e as Map<String, dynamic>))
        .toList();
  }

  /// PATCH /api/users/{username} — ativa ou desativa usuário.
  Future<AdminUser> setUserEnabled(String username, bool enabled) async {
    final resp = await _patch(
      _uri('/api/users/$username'),
      headers: _headers,
      body: jsonEncode({'enabled': enabled}),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao alterar usuário'));
    }
    return AdminUser.fromJson(jsonDecode(resp.body) as Map<String, dynamic>);
  }

  /// DELETE /api/users/{username} — exclui usuário e dispositivos.
  Future<void> deleteUser(String username) async {
    final resp = await _delete(
      _uri('/api/users/$username'),
      headers: _headers,
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao excluir usuário'));
    }
  }

  /// GET /api/devices
  Future<List<Map<String, dynamic>>> listDevices() async {
    final resp = await _get(_uri('/api/devices'), headers: _headers);
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao listar dispositivos'));
    }
    final list = jsonDecode(resp.body) as List<dynamic>;
    return list.cast<Map<String, dynamic>>();
  }

  /// GET /api/devices/{uuid} — detalhe + estado em tempo real (JWT).
  Future<Map<String, dynamic>> getDevice(String uuid) async {
    final resp = await _get(_uri('/api/devices/$uuid'), headers: _headers);
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao carregar dispositivo'));
    }
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  /// PUT /api/devices/{uuid} — atualiza nome/modelo (JWT, ownership).
  Future<Map<String, dynamic>> updateDevice(
    String uuid, {
    String? name,
    String? modelo,
  }) async {
    final body = <String, dynamic>{};
    if (name != null && name.isNotEmpty) body['name'] = name;
    if (modelo != null && modelo.isNotEmpty) body['modelo'] = modelo;

    final resp = await _put(
      _uri('/api/devices/$uuid'),
      headers: _headers,
      body: jsonEncode(body),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao atualizar dispositivo'));
    }
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  /// POST /api/users/me/esp-secret
  Future<String> getEspSecret() async {
    final resp =
        await _get(_uri('/api/users/me/esp-secret'), headers: _headers);
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao carregar secret'));
    }
    final j = jsonDecode(resp.body) as Map<String, dynamic>;
    return j['espSecret'] as String;
  }

  /// POST /api/users/me/esp-secret/refresh — invalida o anterior.
  Future<String> refreshEspSecret() async {
    final resp = await _post(
      _uri('/api/users/me/esp-secret/refresh'),
      headers: _headers,
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao renovar secret'));
    }
    final j = jsonDecode(resp.body) as Map<String, dynamic>;
    return j['espSecret'] as String;
  }

  /// POST /api/devices -> retorna uuid + esp_secret do usuario.
  Future<Map<String, dynamic>> createDevice({
    required String uuid,
    String? name,
    String? modelo,
  }) async {
    final resp = await _post(
      _uri('/api/devices'),
      headers: _headers,
      body: jsonEncode({
        'uuid': uuid,
        if (name != null && name.isNotEmpty) 'name': name,
        if (modelo != null && modelo.isNotEmpty) 'modelo': modelo,
      }),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao criar dispositivo'));
    }
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  /// DELETE /api/devices/{uuid}
  Future<void> deleteDevice(String uuid) async {
    final resp =
        await _delete(_uri('/api/devices/$uuid'), headers: _headers);
    if (resp.statusCode != 200 && resp.statusCode != 204) {
      throw ApiException(_errorMessage(resp, 'Falha ao remover dispositivo'));
    }
  }

  /// POST /api/devices/{uuid}/command
  Future<void> command(String uuid, Map<String, dynamic> body) async {
    final resp = await _post(
      _uri('/api/devices/$uuid/command'),
      headers: _headers,
      body: jsonEncode(body),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao enviar comando'));
    }
  }

  /// POST /api/devices/{uuid}/config
  Future<void> config(String uuid, Map<String, dynamic> body) async {
    final resp = await _post(
      _uri('/api/devices/$uuid/config'),
      headers: _headers,
      body: jsonEncode(body),
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao enviar config'));
    }
  }

  /// GET /api/devices/outages — gerais: só a última (backend limita).
  Future<List<DeviceOutageSummary>> listOutages({
    String? userId,
    String? uuid,
  }) async {
    final params = <String, String>{};
    if (userId != null && userId.isNotEmpty) params['userId'] = userId;
    if (uuid != null && uuid.isNotEmpty) params['uuid'] = uuid;
    final base = server.restUri('/api/devices/outages');
    final uri = params.isEmpty
        ? base
        : base.replace(queryParameters: params);
    final resp = await _get(uri, headers: _headers);
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao carregar quedas'));
    }
    final list = jsonDecode(resp.body) as List<dynamic>;
    return list
        .whereType<Map<String, dynamic>>()
        .map(DeviceOutageSummary.fromJson)
        .toList();
  }

  /// GET /api/devices/{uuid}/outages?limit=&offset= — página no backend.
  Future<DeviceOutageSummary> getDeviceOutages(
    String uuid, {
    int limit = 10,
    int offset = 0,
  }) async {
    final uri = server.restUri('/api/devices/$uuid/outages').replace(
      queryParameters: {
        'limit': '$limit',
        'offset': '$offset',
      },
    );
    final resp = await _get(uri, headers: _headers);
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao carregar quedas'));
    }
    return DeviceOutageSummary.fromJson(
      jsonDecode(resp.body) as Map<String, dynamic>,
    );
  }

  /// GET /api/admin/esp-monitor — visão global de ESPs (admin).
  Future<Map<String, dynamic>> getEspMonitor({int activityLimit = 120}) async {
    final resp = await _get(
      _uri('/api/admin/esp-monitor?activityLimit=$activityLimit'),
      headers: _headers,
    );
    if (resp.statusCode != 200) {
      throw ApiException(_errorMessage(resp, 'Falha ao carregar monitor ESP'));
    }
    return jsonDecode(resp.body) as Map<String, dynamic>;
  }

  String _errorMessage(http.Response resp, String fallback) {
    try {
      final j = jsonDecode(resp.body);
      if (j is Map<String, dynamic> && j['error'] != null) {
        final err = '${j['error']}';
        if (err == 'comando_em_andamento') {
          final rem = (j['remainingMs'] as num?)?.toInt() ?? 0;
          final sec = (rem / 1000).ceil();
          return 'Aguarde o tempo do comando anterior (${sec}s)';
        }
        return err;
      }
    } catch (_) {}
    return '$fallback (${resp.statusCode})';
  }
}

class ApiException implements Exception {
  final String message;
  ApiException(this.message);

  @override
  String toString() => message;
}
