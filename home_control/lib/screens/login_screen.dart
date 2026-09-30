import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/auth_models.dart';
import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';

/// Tela de login (usuário/senha) — layout compacto no celular e no PC.
class LoginScreen extends StatefulWidget {
  const LoginScreen({super.key});

  @override
  State<LoginScreen> createState() => _LoginScreenState();
}

class _LoginScreenState extends State<LoginScreen> {
  final _user = TextEditingController();
  final _pass = TextEditingController();
  final _serverUrl = TextEditingController();
  bool _sending = false;
  bool _showServerField = false;
  int _logoTapCount = 0;

  @override
  void initState() {
    super.initState();
    final state = context.read<AppState>();
    _serverUrl.text = state.server.normalizedBaseUrl.isNotEmpty
        ? state.server.normalizedBaseUrl
        : ServerConfig.productionBaseUrl;
  }

  @override
  void dispose() {
    _user.dispose();
    _pass.dispose();
    _serverUrl.dispose();
    super.dispose();
  }

  void _onLogoTap() {
    _logoTapCount++;
    if (_logoTapCount >= 7) {
      setState(() => _showServerField = true);
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Modo avançado: URL do servidor'),
          duration: Duration(seconds: 2),
        ),
      );
    }
  }

  Future<void> _login() async {
    if (_sending) return;
    final user = _user.text.trim();
    final pass = _pass.text;
    if (user.isEmpty || pass.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Informe usuário e senha')),
      );
      return;
    }

    final state = context.read<AppState>();
    if (_showServerField) {
      final url = _serverUrl.text.trim();
      if (url.isNotEmpty) {
        final uri = Uri.tryParse(url);
        if (uri == null || !uri.hasScheme || uri.host.isEmpty) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(
              content: Text(
                'URL inválida. Use ${ServerConfig.productionBaseUrl} ou http://IP:8080',
              ),
            ),
          );
          return;
        }
        if (url != state.server.normalizedBaseUrl) {
          await state.saveServer(ServerConfig(baseUrl: url));
        }
      }
    } else if (state.server.normalizedBaseUrl.isEmpty) {
      await state.saveServer(const ServerConfig());
    }

    setState(() => _sending = true);
    try {
      await state.login(user, pass);
    } catch (_) {
      // lastError já está no AppState.
    } finally {
      if (mounted) setState(() => _sending = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();

    return Scaffold(
      backgroundColor: AppColors.canvas,
      body: SafeArea(
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 360),
            child: SingleChildScrollView(
              padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 20),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  GestureDetector(
                    onTap: _onLogoTap,
                    behavior: HitTestBehavior.opaque,
                    child: Text(
                      'HOME',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.headlineMedium?.copyWith(
                            fontWeight: FontWeight.w800,
                            letterSpacing: 1.5,
                            color: AppColors.accent,
                            fontSize: 28,
                          ),
                    ),
                  ),
                  const SizedBox(height: 4),
                  const Text(
                    'Entre para controlar seus dispositivos',
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      color: AppColors.textSecondary,
                      height: 1.3,
                      fontSize: 13,
                    ),
                  ),
                  const SizedBox(height: 18),
                  SoftCard(
                    color: AppColors.surface,
                    padding: const EdgeInsets.all(12),
                    child: Column(
                      children: [
                        if (_showServerField) ...[
                          TextField(
                            controller: _serverUrl,
                            keyboardType: TextInputType.url,
                            textInputAction: TextInputAction.next,
                            decoration: InputDecoration(
                              labelText: 'URL do servidor',
                              hintText: ServerConfig.productionBaseUrl,
                              prefixIcon: const Icon(Icons.cloud_outlined, size: 20),
                            ),
                          ),
                          const SizedBox(height: 10),
                        ],
                        TextField(
                          controller: _user,
                          autofocus: true,
                          textInputAction: TextInputAction.next,
                          decoration: const InputDecoration(
                            labelText: 'Usuário (user_id)',
                            hintText: 'ex.: pai',
                            prefixIcon: Icon(Icons.person_outline_rounded, size: 20),
                          ),
                        ),
                        const SizedBox(height: 10),
                        TextField(
                          controller: _pass,
                          obscureText: true,
                          textInputAction: TextInputAction.done,
                          onSubmitted: (_) => _login(),
                          decoration: const InputDecoration(
                            labelText: 'Senha',
                            prefixIcon: Icon(Icons.lock_outline_rounded, size: 20),
                          ),
                        ),
                      ],
                    ),
                  ),
                  if (state.lastError != null) ...[
                    const SizedBox(height: 10),
                    SoftCard(
                      color: AppColors.danger.withValues(alpha: 0.12),
                      padding: const EdgeInsets.all(10),
                      child: Text(
                        state.lastError!,
                        style: const TextStyle(color: AppColors.danger, fontSize: 12),
                      ),
                    ),
                  ],
                  const SizedBox(height: 12),
                  SoftCard(
                    color: _sending
                        ? AppColors.surfaceSoft
                        : AppColors.accent.withValues(alpha: 0.18),
                    onTap: _sending ? null : _login,
                    padding: const EdgeInsets.symmetric(vertical: 12),
                    child: Center(
                      child: _sending
                          ? const SizedBox(
                              width: 18,
                              height: 18,
                              child: CircularProgressIndicator(
                                strokeWidth: 2,
                                color: AppColors.accent,
                              ),
                            )
                          : const Text(
                              'Entrar',
                              style: TextStyle(
                                color: AppColors.accent,
                                fontWeight: FontWeight.w800,
                                fontSize: 14,
                              ),
                            ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
