import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';

/// Troca de senha do usuario logado (user ou admin).
class ChangePasswordScreen extends StatefulWidget {
  const ChangePasswordScreen({super.key});

  @override
  State<ChangePasswordScreen> createState() => _ChangePasswordScreenState();
}

class _ChangePasswordScreenState extends State<ChangePasswordScreen> {
  final _current = TextEditingController();
  final _newPass = TextEditingController();
  final _confirm = TextEditingController();
  bool _sending = false;

  @override
  void dispose() {
    _current.dispose();
    _newPass.dispose();
    _confirm.dispose();
    super.dispose();
  }

  Future<void> _submit() async {
    if (_sending) return;
    final current = _current.text;
    final next = _newPass.text;
    final confirm = _confirm.text;

    if (current.isEmpty || next.isEmpty) {
      _snack('Preencha a senha atual e a nova');
      return;
    }
    if (next != confirm) {
      _snack('As senhas não coincidem');
      return;
    }
    if (next.length < 4) {
      _snack('Senha muito curta (mínimo 4 caracteres)');
      return;
    }

    setState(() => _sending = true);
    try {
      await context.read<AppState>().changePassword(
            currentPassword: current,
            newPassword: next,
          );
      if (!mounted) return;
      Navigator.of(context).pop();
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Senha alterada com sucesso')),
      );
    } catch (e) {
      if (mounted) _snack(_friendly(e.toString()));
    } finally {
      if (mounted) setState(() => _sending = false);
    }
  }

  String _friendly(String raw) {
    if (raw.contains('senha_atual_incorreta')) {
      return 'Senha atual incorreta. Verifique e tente novamente.';
    }
    if (raw.contains('senha_muito_curta')) {
      return 'Senha muito curta (mínimo 4 caracteres).';
    }
    if (raw.contains('usuario_nao_encontrado')) {
      return 'Usuário não encontrado no servidor.';
    }
    return raw;
  }

  void _snack(String msg) {
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.canvas,
      appBar: AppBar(
        backgroundColor: AppColors.surface,
        foregroundColor: AppColors.textPrimary,
        title: const Text(
          'Alterar senha',
          style: TextStyle(fontWeight: FontWeight.w800),
        ),
      ),
      body: ListView(
        padding: const EdgeInsets.all(20),
        children: [
          SoftCard(
            color: AppColors.surface,
            child: Column(
              children: [
                TextField(
                  controller: _current,
                  obscureText: true,
                  textInputAction: TextInputAction.next,
                  decoration: const InputDecoration(
                    labelText: 'Senha atual',
                    prefixIcon: Icon(Icons.lock_outline_rounded),
                  ),
                ),
                const SizedBox(height: 14),
                TextField(
                  controller: _newPass,
                  obscureText: true,
                  textInputAction: TextInputAction.next,
                  decoration: const InputDecoration(
                    labelText: 'Nova senha',
                    prefixIcon: Icon(Icons.lock_reset_rounded),
                  ),
                ),
                const SizedBox(height: 14),
                TextField(
                  controller: _confirm,
                  obscureText: true,
                  textInputAction: TextInputAction.done,
                  onSubmitted: (_) => _submit(),
                  decoration: const InputDecoration(
                    labelText: 'Confirmar nova senha',
                    prefixIcon: Icon(Icons.lock_outline_rounded),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 16),
          SoftCard(
            color: _sending
                ? AppColors.surfaceSoft
                : AppColors.accent.withValues(alpha: 0.18),
            onTap: _sending ? null : _submit,
            padding: const EdgeInsets.symmetric(vertical: 16),
            child: Center(
              child: _sending
                  ? const SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: AppColors.accent,
                      ),
                    )
                  : const Text(
                      'Salvar nova senha',
                      style: TextStyle(
                        color: AppColors.accent,
                        fontWeight: FontWeight.w800,
                      ),
                    ),
            ),
          ),
        ],
      ),
    );
  }
}
