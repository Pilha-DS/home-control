import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/auth_models.dart';
import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';
import 'admin_esp_monitor_screen.dart';
import 'admin_users_screen.dart';
import 'change_password_screen.dart';

/// Shell do app para administradores — sem controle de dispositivos.
class AdminShellScreen extends StatefulWidget {
  const AdminShellScreen({super.key});

  @override
  State<AdminShellScreen> createState() => _AdminShellScreenState();
}

class _AdminShellScreenState extends State<AdminShellScreen> {
  int _index = 0;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.canvas,
      body: SafeArea(
        child: IndexedStack(
          index: _index,
          children: const [
            _AdminHomeTab(),
            AdminEspMonitorScreen(),
            _AdminAccountTab(),
          ],
        ),
      ),
      bottomNavigationBar: ColoredBox(
        color: AppColors.surface,
        child: SafeArea(
          top: false,
          child: Padding(
            padding: const EdgeInsets.fromLTRB(8, 6, 8, 6),
            child: Row(
              children: [
                _NavItem(
                  icon: Icons.admin_panel_settings_rounded,
                  label: 'Admin',
                  selected: _index == 0,
                  onTap: () => setState(() => _index = 0),
                ),
                _NavItem(
                  icon: Icons.memory_rounded,
                  label: 'Arduinos',
                  selected: _index == 1,
                  onTap: () => setState(() => _index = 1),
                ),
                _NavItem(
                  icon: Icons.person_outline_rounded,
                  label: 'Conta',
                  selected: _index == 2,
                  onTap: () => setState(() => _index = 2),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _AdminHomeTab extends StatefulWidget {
  const _AdminHomeTab();

  @override
  State<_AdminHomeTab> createState() => _AdminHomeTabState();
}

class _AdminHomeTabState extends State<_AdminHomeTab> {
  String? _listError;

  static String _formatDate(DateTime dt) {
    final d = dt.toLocal();
    String two(int n) => n.toString().padLeft(2, '0');
    return '${two(d.day)}/${two(d.month)}/${d.year} ${two(d.hour)}:${two(d.minute)}';
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _reload());
  }

  Future<void> _reload() async {
    setState(() => _listError = null);
    try {
      await context.read<AppState>().refreshAdminUsers();
    } catch (e) {
      if (mounted) setState(() => _listError = '$e');
    }
  }

  Future<void> _openCreateUser() async {
    await Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => const AdminUsersScreen(standalone: true),
      ),
    );
    if (!mounted) return;
    await _reload();
  }

  Future<void> _toggleEnabled(AdminUser user) async {
    final app = context.read<AppState>();
    final action = user.enabled ? 'desativar' : 'ativar';
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('${user.enabled ? 'Desativar' : 'Ativar'} usuário'),
        content: Text(
          user.enabled
              ? 'O usuário "${user.username}" não poderá mais entrar no app.'
              : 'O usuário "${user.username}" voltará a poder entrar no app.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancelar'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: Text(action[0].toUpperCase() + action.substring(1)),
          ),
        ],
      ),
    );
    if (ok != true || !mounted) return;

    try {
      await app.setUserEnabled(user.username, !user.enabled);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            user.enabled
                ? 'Usuário "${user.username}" desativado'
                : 'Usuário "${user.username}" ativado',
          ),
        ),
      );
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('$e')),
        );
      }
    }
  }

  Future<void> _deleteUser(AdminUser user) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('Excluir usuário'),
        content: Text(
          'Excluir "${user.username}"? Todos os dispositivos deste usuário '
          'também serão removidos. Esta ação não pode ser desfeita.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('Cancelar'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx, true),
            style: TextButton.styleFrom(foregroundColor: AppColors.danger),
            child: const Text('Excluir'),
          ),
        ],
      ),
    );
    if (ok != true || !mounted) return;

    try {
      await context.read<AppState>().deleteUser(user.username);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Usuário "${user.username}" excluído')),
      );
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('$e')),
        );
      }
    }
  }

  Future<void> _resetPassword(AdminUser user) async {
    final controller = TextEditingController();
    final result = await showDialog<String>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('Redefinir senha de "${user.username}"'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Text(
              'A senha atual deixa de funcionar. Informe a nova senha:',
              style: TextStyle(height: 1.4),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: controller,
              obscureText: true,
              autofocus: true,
              textInputAction: TextInputAction.done,
              onSubmitted: (_) => Navigator.pop(ctx, controller.text),
              decoration: const InputDecoration(
                labelText: 'Nova senha',
                prefixIcon: Icon(Icons.lock_reset_rounded),
              ),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('Cancelar'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, controller.text),
            child: const Text('Salvar'),
          ),
        ],
      ),
    );
    final pass = result;
    controller.dispose();
    if (pass == null || pass.isEmpty || !mounted) return;
    if (pass.length < 4) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Senha muito curta (mínimo 4 caracteres)')),
      );
      return;
    }

    try {
      await context.read<AppState>().resetUserPassword(user.username, pass);
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Senha de "${user.username}" redefinida')),
      );
    } catch (e) {
      if (mounted) {
        final msg = e.toString();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              msg.contains('senha_muito_curta')
                  ? 'Senha muito curta (mínimo 4 caracteres).'
                  : msg.contains('usuario_nao_encontrado')
                      ? 'Usuário não encontrado no servidor.'
                      : msg,
            ),
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final app = context.watch<AppState>();
    final currentUsername = app.currentUser?.username ?? '';
    final users = app.adminUsers;

    return RefreshIndicator(
      color: AppColors.accent,
      onRefresh: _reload,
      child: ResponsivePage(
        maxWidth: 1200,
        child: ListView(
          physics: const AlwaysScrollableScrollPhysics(),
          children: [
            SectionBlock(
              color: AppColors.canvas,
              padding: const EdgeInsets.fromLTRB(20, 16, 20, 8),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    children: [
                      Expanded(
                        child: Text(
                          'Usuários',
                          style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                                fontWeight: FontWeight.w800,
                              ),
                        ),
                      ),
                      IconButton(
                        tooltip: 'Atualizar lista',
                        onPressed: app.adminUsersLoading ? null : _reload,
                        icon: app.adminUsersLoading
                            ? const SizedBox(
                                width: 20,
                                height: 20,
                                child: CircularProgressIndicator(strokeWidth: 2),
                              )
                            : const Icon(Icons.refresh_rounded),
                      ),
                    ],
                  ),
                  const SizedBox(height: 6),
                  const Text(
                    'Todos os logins cadastrados. Desative para bloquear o acesso '
                    'ou exclua para remover permanentemente.',
                    style: TextStyle(color: AppColors.textSecondary, height: 1.4),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    'build ${const String.fromEnvironment('BUILD_ID', defaultValue: 'local')}',
                    style: const TextStyle(
                      color: AppColors.textSecondary,
                      fontSize: 11,
                    ),
                  ),
                ],
              ),
            ),
            SectionBlock(
              color: AppColors.surface,
              child: SoftCard(
                color: AppColors.accent.withValues(alpha: 0.14),
                onTap: _openCreateUser,
                padding: const EdgeInsets.symmetric(vertical: 16, horizontal: 16),
                child: const Row(
                  children: [
                    Icon(Icons.person_add_rounded, color: AppColors.accent, size: 26),
                    SizedBox(width: 12),
                    Expanded(
                      child: Text(
                        'Cadastrar usuário',
                        style: TextStyle(
                          color: AppColors.accent,
                          fontWeight: FontWeight.w800,
                          fontSize: 16,
                        ),
                      ),
                    ),
                    Icon(Icons.chevron_right_rounded, color: AppColors.accent),
                  ],
                ),
              ),
            ),
            if (_listError != null)
              SectionBlock(
                color: AppColors.canvas,
                child: SoftCard(
                  color: AppColors.danger.withValues(alpha: 0.14),
                  child: Text(
                    'Erro ao carregar usuários:\n$_listError',
                    style: const TextStyle(color: AppColors.danger, height: 1.35),
                  ),
                ),
              ),
            if (app.adminUsersLoading && users.isEmpty)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 48),
                child: Center(
                  child: CircularProgressIndicator(color: AppColors.accent),
                ),
              )
            else if (users.isEmpty && _listError == null)
              const SectionBlock(
                color: AppColors.canvas,
                child: SoftCard(
                  color: AppColors.surface,
                  child: Text(
                    'Nenhum usuário cadastrado ainda.',
                    style: TextStyle(color: AppColors.textSecondary),
                  ),
                ),
              )
            else if (users.isNotEmpty)
              SectionBlock(
                color: AppColors.canvas,
                child: ResponsiveCardGrid(
                  minCardWidth: 300,
                  children: [
                    for (final u in users)
                      _UserCard(
                        user: u,
                        dateLabel: _formatDate(u.createdAt),
                        isSelf: u.username == currentUsername,
                        onToggleEnabled: () => _toggleEnabled(u),
                        onResetPassword: () => _resetPassword(u),
                        onDelete: () => _deleteUser(u),
                      ),
                  ],
                ),
              ),
            const SizedBox(height: 32),
          ],
        ),
      ),
    );
  }
}

class _UserCard extends StatelessWidget {
  const _UserCard({
    required this.user,
    required this.dateLabel,
    required this.isSelf,
    required this.onToggleEnabled,
    required this.onResetPassword,
    required this.onDelete,
  });

  final AdminUser user;
  final String dateLabel;
  final bool isSelf;
  final VoidCallback onToggleEnabled;
  final VoidCallback onResetPassword;
  final VoidCallback onDelete;

  @override
  Widget build(BuildContext context) {
    final roleLabel = user.isAdmin ? 'Administrador' : 'Usuário';
    final statusColor = user.enabled ? AppColors.accent : AppColors.textSecondary;
    final statusLabel = user.enabled ? 'Ativo' : 'Desativado';

    return SoftCard(
      color: user.enabled ? AppColors.surface : AppColors.surfaceSoft,
      padding: const EdgeInsets.fromLTRB(14, 12, 6, 12),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  user.username,
                  style: const TextStyle(
                    fontWeight: FontWeight.w800,
                    fontSize: 16,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  roleLabel,
                  style: const TextStyle(
                    color: AppColors.textSecondary,
                    fontSize: 13,
                  ),
                ),
                const SizedBox(height: 6),
                Text(
                  'Cadastrado em $dateLabel',
                  style: const TextStyle(
                    color: AppColors.textSecondary,
                    fontSize: 12,
                  ),
                ),
                const SizedBox(height: 6),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: statusColor.withValues(alpha: 0.15),
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(
                    statusLabel,
                    style: TextStyle(
                      color: statusColor,
                      fontSize: 11,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ),
                if (isSelf) ...[
                  const SizedBox(height: 6),
                  const Text(
                    'Sua conta (não pode ser alterada aqui)',
                    style: TextStyle(
                      color: AppColors.textSecondary,
                      fontSize: 11,
                      fontStyle: FontStyle.italic,
                    ),
                  ),
                ],
              ],
            ),
          ),
          Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                IconButton(
                  tooltip: 'Redefinir senha',
                  onPressed: onResetPassword,
                  icon: const Icon(
                    Icons.lock_reset_rounded,
                    color: AppColors.accent,
                  ),
                ),
                if (!isSelf) ...[
                  IconButton(
                    tooltip: user.enabled ? 'Desativar' : 'Ativar',
                    onPressed: onToggleEnabled,
                    icon: Icon(
                      user.enabled
                          ? Icons.block_rounded
                          : Icons.check_circle_outline_rounded,
                      color: user.enabled ? AppColors.warning : AppColors.accent,
                    ),
                  ),
                  IconButton(
                    tooltip: 'Excluir',
                    onPressed: onDelete,
                    icon: const Icon(Icons.delete_outline_rounded, color: AppColors.danger),
                  ),
                ],
              ],
            ),
        ],
      ),
    );
  }
}

class _AdminAccountTab extends StatelessWidget {
  const _AdminAccountTab();

  Future<void> _logout(BuildContext context) async {
    await context.read<AppState>().logout();
    if (!context.mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(content: Text('Sessão encerrada')),
    );
  }

  void _openChangePassword(BuildContext context) {
    Navigator.of(context).push(
      MaterialPageRoute(builder: (_) => const ChangePasswordScreen()),
    );
  }

  @override
  Widget build(BuildContext context) {
    final user = context.watch<AppState>().currentUser;

    return ListView(
      children: [
        SectionBlock(
          color: AppColors.canvas,
          padding: const EdgeInsets.fromLTRB(20, 16, 20, 8),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Conta admin',
                style: Theme.of(context).textTheme.headlineSmall?.copyWith(
                      fontWeight: FontWeight.w800,
                    ),
              ),
            ],
          ),
        ),
        SectionBlock(
          color: AppColors.surface,
          child: SoftCard(
            color: AppColors.surfaceRaised,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text(
                  'Logado como',
                  style: TextStyle(fontWeight: FontWeight.w800),
                ),
                const SizedBox(height: 8),
                Text(
                  user?.username ?? '',
                  style: const TextStyle(
                    color: AppColors.accent,
                    fontSize: 18,
                    fontWeight: FontWeight.w700,
                  ),
                ),
                const SizedBox(height: 4),
                const Text(
                  'Administrador do sistema',
                  style: TextStyle(color: AppColors.textSecondary, fontSize: 13),
                ),
                const SizedBox(height: 12),
                SoftCard(
                  color: AppColors.surfaceSoft,
                  onTap: () => _openChangePassword(context),
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
        SectionBlock(
          color: AppColors.canvas,
          child: SoftCard(
            color: AppColors.danger.withValues(alpha: 0.14),
            onTap: () => _logout(context),
            padding: const EdgeInsets.symmetric(vertical: 18),
            child: const Center(
              child: Text(
                'Sair (logout)',
                style: TextStyle(
                  color: AppColors.danger,
                  fontWeight: FontWeight.w800,
                  fontSize: 16,
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _NavItem extends StatelessWidget {
  const _NavItem({
    required this.icon,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final color = selected ? AppColors.accent : AppColors.textSecondary;
    return Expanded(
      child: Material(
        color: selected
            ? AppColors.accent.withValues(alpha: 0.12)
            : Colors.transparent,
        borderRadius: BorderRadius.circular(12),
        child: InkWell(
          onTap: onTap,
          borderRadius: BorderRadius.circular(12),
          child: Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                Icon(icon, color: color, size: 20),
                const SizedBox(height: 2),
                Text(
                  label,
                  style: TextStyle(
                    color: color,
                    fontWeight: FontWeight.w700,
                    fontSize: 11,
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
