import 'package:shared_preferences/shared_preferences.dart';

import 'api_client.dart';
import 'auth_models.dart';

/// Gerencia login/logout e a persistência do JWT.
///
/// Usa shared_preferences por simplicidade (o app roda em Windows/desktop).
/// Em produção mobile, troque por flutter_secure_storage para guardar o
/// token de forma criptografada.
class AuthService {
  static const _kToken = 'jwt_token';
  static const _kUsername = 'username';
  static const _kRole = 'role';
  static const _kEspSecret = 'esp_secret';

  final ApiClient api;

  AuthService(this.api);

  Future<AuthUser> login(String username, String password) async {
    final user = await api.login(username, password);
    await _save(user);
    return user;
  }

  Future<void> logout() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove(_kToken);
    await prefs.remove(_kUsername);
    await prefs.remove(_kRole);
    await prefs.remove(_kEspSecret);
  }

  Future<AuthUser?> restore() async {
    final prefs = await SharedPreferences.getInstance();
    final token = prefs.getString(_kToken);
    if (token == null || token.isEmpty) return null;
    final username = prefs.getString(_kUsername) ?? '';
    final role = prefs.getString(_kRole) ?? 'ROLE_USER';
    final espSecret = prefs.getString(_kEspSecret);
    return AuthUser(
      username: username,
      role: role,
      token: token,
      espSecret: espSecret,
    );
  }

  Future<void> _save(AuthUser user) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_kToken, user.token);
    await prefs.setString(_kUsername, user.username);
    await prefs.setString(_kRole, user.role);
    if (user.espSecret != null && user.espSecret!.isNotEmpty) {
      await prefs.setString(_kEspSecret, user.espSecret!);
    }
  }

  Future<void> saveEspSecret(String secret) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_kEspSecret, secret);
  }
}
