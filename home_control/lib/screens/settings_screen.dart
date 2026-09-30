import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../services/auth_models.dart';
import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';
import 'change_password_screen.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  late final TextEditingController _baseUrl;
  bool _showAdvanced = false;
  int _connectionTapCount = 0;

  @override
  void initState() {
    super.initState();
    final state = context.read<AppState>();
    _baseUrl = TextEditingController(
      text: state.server.normalizedBaseUrl.isNotEmpty
          ? state.server.normalizedBaseUrl
          : ServerConfig.productionBaseUrl,
    );
  }

  @override
  void dispose() {
    _baseUrl.dispose();
    super.dispose();
  }

  void _onConnectionHeaderTap() {
    _connectionTapCount++;
    if (_connectionTapCount >= 7) {
      setState(() => _showAdvanced = true);
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Modo avançado: URL do servidor'),
          duration: Duration(seconds: 2),
        ),
      );
    }
  }

  String _connectionDetail(AppState state) {
    if (state.wsConnected) return 'Conectado';
    return 'Desconectado — toque em reconectar';
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    final user = state.currentUser;

    return ResponsivePage(
      child: ListView(
        children: [
          SectionBlock(
            color: AppColors.canvas,
            padding: const EdgeInsets.fromLTRB(20, 20, 20, 12),
            child: const PageHeader(
              title: 'Conta',
              subtitle: 'Perfil e conexão com o servidor',
            ),
          ),
          if (user != null)
            SectionBlock(
              color: AppColors.surface,
              child: TopicBlock(
                icon: Icons.person_rounded,
                title: 'Seu perfil',
                child: Column(
                  children: [
                    InfoRow(label: 'Usuário', value: user.username),
                    InfoRow(
                      label: 'Tipo',
                      value: user.isAdmin ? 'Administrador' : 'Usuário comum',
                    ),
                    const SizedBox(height: 12),
                    SoftCard(
                      color: AppColors.surfaceRaised,
                      onTap: () => _openChangePassword(),
                      padding: const EdgeInsets.symmetric(vertical: 12),
                      child: const Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(
                            Icons.lock_reset_rounded,
                            size: 18,
                            color: AppColors.accent,
                          ),
                          SizedBox(width: 8),
                          Text(
                            'Alterar minha senha',
                            style: TextStyle(
                              color: AppColors.accent,
                              fontWeight: FontWeight.w800,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ),
          if (user != null && !user.isAdmin)
            SectionBlock(
              color: AppColors.surface,
              child: TopicBlock(
                icon: Icons.key_rounded,
                title: 'Secret do ESP',
                subtitle:
                    'Cole no Arduino (X-User-Secret). Com ele, o ESP se '
                    'cadastra sozinho na sua conta.',
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    InfoRow(
                      label: 'Secret atual',
                      value: state.maskedEspSecret,
                    ),
                    const SizedBox(height: 12),
                    SoftCard(
                      color: AppColors.surfaceRaised,
                      onTap: () => _copySecret(state),
                      padding: const EdgeInsets.symmetric(vertical: 10),
                      child: const Center(
                        child: Text(
                          'Copiar',
                          style: TextStyle(fontWeight: FontWeight.w700),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          SectionBlock(
            color: AppColors.canvas,
            child: GestureDetector(
              onTap: _onConnectionHeaderTap,
              behavior: HitTestBehavior.opaque,
              child: TopicBlock(
                icon: Icons.cloud_rounded,
                title: 'Conexão',
                background: AppColors.canvas,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    ConnectionBanner(
                      connected: state.wsConnected,
                      onReconnect: state.reconnect,
                      detail: _connectionDetail(state),
                    ),
                  ],
                ),
              ),
            ),
          ),
          if (_showAdvanced)
            SectionBlock(
              color: AppColors.surface,
              child: TopicBlock(
                icon: Icons.link_rounded,
                title: 'Servidor (avançado)',
                subtitle: 'Somente para ambiente de desenvolvimento ou LAN',
                child: Column(
                  children: [
                    SoftCard(
                      color: AppColors.surfaceRaised,
                      child: TextField(
                        controller: _baseUrl,
                        keyboardType: TextInputType.url,
                        decoration: InputDecoration(
                          labelText: 'URL do backend',
                          hintText: ServerConfig.productionBaseUrl,
                        ),
                      ),
                    ),
                    const SizedBox(height: 12),
                    SoftCard(
                      color: AppColors.accent.withValues(alpha: 0.16),
                      onTap: _save,
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      child: const Center(
                        child: Text(
                          'Salvar e reconectar',
                          style: TextStyle(
                            color: AppColors.accent,
                            fontWeight: FontWeight.w800,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          SectionBlock(
            color: AppColors.canvas,
            child: SoftCard(
              color: AppColors.danger.withValues(alpha: 0.14),
              onTap: _logout,
              padding: const EdgeInsets.symmetric(vertical: 12),
              child: const Center(
                child: Text(
                  'Sair da conta',
                  style: TextStyle(
                    color: AppColors.danger,
                    fontWeight: FontWeight.w800,
                    fontSize: 14,
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(height: 20),
        ],
      ),
    );
  }

  Future<void> _save() async {
    final state = context.read<AppState>();
    final cfg = ServerConfig(baseUrl: _baseUrl.text.trim());
    await state.saveServer(cfg);
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Servidor salvo — reconectando…')),
    );
  }

  void _openChangePassword() {
    Navigator.of(context).push(
      MaterialPageRoute(builder: (_) => const ChangePasswordScreen()),
    );
  }

  Future<void> _copySecret(AppState state) async {
    final s = state.espSecret;
    if (s == null || s.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Secret ainda nao carregado')),
      );
      return;
    }
    await Clipboard.setData(ClipboardData(text: s));
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(
          'Secret completo copiado (${s.length} caracteres). '
          'Cole no portal do ESP (campo espSecret).',
        ),
      ),
    );
  }

  Future<void> _logout() async {
    final state = context.read<AppState>();
    await state.logout();
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Sessão encerrada')),
    );
  }
}
