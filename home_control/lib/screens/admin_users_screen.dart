import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';

/// Cadastro de usuários (somente admin). O username vira o user_id MQTT.
class AdminUsersScreen extends StatefulWidget {
  const AdminUsersScreen({super.key, this.standalone = false});

  /// Se true, exibe AppBar com botão voltar (aberto via Navigator).
  final bool standalone;

  @override
  State<AdminUsersScreen> createState() => _AdminUsersScreenState();
}

class _AdminUsersScreenState extends State<AdminUsersScreen> {
  final _username = TextEditingController();
  final _password = TextEditingController();
  final _confirm = TextEditingController();
  bool _asAdmin = false;
  bool _sending = false;

  @override
  void dispose() {
    _username.dispose();
    _password.dispose();
    _confirm.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    if (_sending) return;

    final user = _username.text.trim();
    final pass = _password.text;
    final confirm = _confirm.text;

    if (user.isEmpty || pass.isEmpty) {
      _snack('Informe usuário e senha');
      return;
    }
    if (pass != confirm) {
      _snack('As senhas não coincidem');
      return;
    }
    if (pass.length < 4) {
      _snack('Senha muito curta (mínimo 4 caracteres)');
      return;
    }

    setState(() => _sending = true);
    try {
      final result = await context.read<AppState>().createUser(
            username: user,
            password: pass,
            admin: _asAdmin,
          );
      if (!mounted) return;
      _username.clear();
      _password.clear();
      _confirm.clear();
      setState(() => _asAdmin = false);
      _snack(
        'Usuário "${result['username']}" cadastrado '
        '(${result['role'] == 'ROLE_ADMIN' ? 'admin' : 'usuário'})',
      );
    } catch (e) {
      if (mounted) _snack('$e');
    } finally {
      if (mounted) setState(() => _sending = false);
    }
  }

  void _snack(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  @override
  Widget build(BuildContext context) {
    final body = ListView(
      children: [
        if (!widget.standalone)
          SectionBlock(
            color: AppColors.canvas,
            padding: const EdgeInsets.fromLTRB(20, 16, 20, 8),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Novo usuário',
                  style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                        fontWeight: FontWeight.w800,
                      ),
                ),
                const SizedBox(height: 6),
                const Text(
                  'O username é o user_id dos tópicos MQTT '
                  '(home/{user_id}/{uuid}/…).',
                  style: TextStyle(color: AppColors.textSecondary, height: 1.4),
                ),
              ],
            ),
          ),
        if (widget.standalone)
          SectionBlock(
            color: AppColors.canvas,
            padding: const EdgeInsets.fromLTRB(20, 16, 20, 8),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Novo usuário',
                  style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                        fontWeight: FontWeight.w800,
                      ),
                ),
                const SizedBox(height: 6),
                const Text(
                  'O username é o user_id dos tópicos MQTT '
                  '(home/{user_id}/{uuid}/…). Cada usuário vê só os '
                  'próprios dispositivos.',
                  style: TextStyle(color: AppColors.textSecondary, height: 1.4),
                ),
              ],
            ),
          ),
          SectionBlock(
            color: AppColors.surface,
            child: SoftCard(
              color: AppColors.surfaceRaised,
              child: Column(
                children: [
                  TextField(
                    controller: _username,
                    textInputAction: TextInputAction.next,
                    decoration: const InputDecoration(
                      labelText: 'Usuário (user_id)',
                      hintText: 'ex.: pai, joao, casa1',
                      prefixIcon: Icon(Icons.person_outline_rounded),
                    ),
                  ),
                  const SizedBox(height: 14),
                  TextField(
                    controller: _password,
                    obscureText: true,
                    textInputAction: TextInputAction.next,
                    decoration: const InputDecoration(
                      labelText: 'Senha',
                      prefixIcon: Icon(Icons.lock_outline_rounded),
                    ),
                  ),
                  const SizedBox(height: 14),
                  TextField(
                    controller: _confirm,
                    obscureText: true,
                    textInputAction: TextInputAction.done,
                    onSubmitted: (_) => _submit(),
                    decoration: const InputDecoration(
                      labelText: 'Confirmar senha',
                      prefixIcon: Icon(Icons.lock_outline_rounded),
                    ),
                  ),
                  const SizedBox(height: 8),
                  SoftCard(
                    color: AppColors.surfaceSoft,
                    padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
                    child: SwitchListTile(
                      contentPadding: const EdgeInsets.symmetric(horizontal: 12),
                      title: const Text(
                        'Conceder perfil admin',
                        style: TextStyle(fontSize: 14),
                      ),
                      subtitle: const Text(
                        'Pode cadastrar outros usuários',
                        style: TextStyle(fontSize: 12, color: AppColors.textSecondary),
                      ),
                      value: _asAdmin,
                      activeThumbColor: AppColors.accent,
                      onChanged: _sending ? null : (v) => setState(() => _asAdmin = v),
                    ),
                  ),
                ],
              ),
            ),
          ),
          SectionBlock(
            color: AppColors.canvas,
            child: SoftCard(
              color: _sending
                  ? AppColors.surfaceSoft
                  : AppColors.accent.withValues(alpha: 0.18),
              onTap: _sending ? null : _submit,
              padding: const EdgeInsets.symmetric(vertical: 18),
              child: Center(
                child: _sending
                    ? const SizedBox(
                        width: 22,
                        height: 22,
                        child: CircularProgressIndicator(
                          strokeWidth: 2,
                          color: AppColors.accent,
                        ),
                      )
                    : const Text(
                        'Cadastrar',
                        style: TextStyle(
                          color: AppColors.accent,
                          fontWeight: FontWeight.w800,
                          fontSize: 16,
                        ),
                      ),
              ),
            ),
          ),
          const SizedBox(height: 40),
        ],
    );

    if (!widget.standalone) {
      return Scaffold(
        backgroundColor: AppColors.canvas,
        body: body,
      );
    }

    return Scaffold(
      backgroundColor: AppColors.canvas,
      appBar: AppBar(
        backgroundColor: AppColors.surface,
        foregroundColor: AppColors.textPrimary,
        title: const Text(
          'Cadastrar usuário',
          style: TextStyle(fontWeight: FontWeight.w800),
        ),
      ),
      body: body,
    );
  }
}
