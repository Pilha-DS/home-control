import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import 'screens/admin_shell_screen.dart';
import 'screens/control_screen.dart';
import 'screens/devices_screen.dart';
import 'screens/login_screen.dart';
import 'screens/settings_screen.dart';
import 'state/app_state.dart';
import 'theme/app_theme.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  SystemChrome.setSystemUIOverlayStyle(
    const SystemUiOverlayStyle(
      statusBarColor: Colors.transparent,
      statusBarIconBrightness: Brightness.light,
      systemNavigationBarColor: AppColors.surface,
      systemNavigationBarIconBrightness: Brightness.light,
    ),
  );
  runApp(const HomeControlApp());
}

class HomeControlApp extends StatelessWidget {
  const HomeControlApp({super.key});

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => AppState(),
      child: MaterialApp(
        title: 'HOME Control',
        debugShowCheckedModeBanner: false,
        theme: AppTheme.dark,
        home: const RootScreen(),
      ),
    );
  }
}

/// Decide entre login e o shell do app com base no estado de autenticação.
class RootScreen extends StatefulWidget {
  const RootScreen({super.key});

  @override
  State<RootScreen> createState() => _RootScreenState();
}

class _RootScreenState extends State<RootScreen> {
  bool _restored = false;

  @override
  void initState() {
    super.initState();
    _restoreSession();
  }

  Future<void> _restoreSession() async {
    final state = context.read<AppState>();
    try {
      // Espera prefs/server prontos — evita afterAuth com URL antiga/errada.
      await state.ready.timeout(const Duration(seconds: 8));
      final user = await state.auth.restore();
      if (!mounted) return;
      if (user != null) {
        state.currentUser = user;
        state.espSecret = user.espSecret;
        state.api.token = user.token;
        try {
          await state.afterAuth().timeout(const Duration(seconds: 15));
        } catch (e) {
          // Token inválido / API fora: limpa sessão e mostra login
          // em vez de ficar no spinner para sempre.
          debugPrint('afterAuth restore falhou: $e');
          await state.logout();
        }
      }
    } catch (e) {
      debugPrint('restoreSession falhou: $e');
    }
    // Nunca deixar loading=true segurar a UI (prefs/IndexedDB podem travar).
    state.forceReady();
    if (!mounted) return;
    setState(() => _restored = true);
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    // Só _restored: state.loading sozinho deixava spinner eterno se
    // SharedPreferences/IndexedDB não resolvesse.
    if (!_restored) {
      return const Scaffold(
        backgroundColor: AppColors.canvas,
        body: Center(child: CircularProgressIndicator(color: AppColors.accent)),
      );
    }
    return state.loggedIn
        ? (state.currentUser!.isAdmin
            ? const AdminShellScreen()
            : const ShellScreen())
        : const LoginScreen();
  }
}

class ShellScreen extends StatefulWidget {
  const ShellScreen({super.key});

  @override
  State<ShellScreen> createState() => _ShellScreenState();
}

class _ShellScreenState extends State<ShellScreen> {
  int _index = 0;

  static const _pages = [
    ControlScreen(),
    DevicesScreen(),
    SettingsScreen(),
  ];

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: AppColors.canvas,
      body: SafeArea(
        child: IndexedStack(index: _index, children: _pages),
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
                  icon: Icons.home_rounded,
                  label: 'Início',
                  selected: _index == 0,
                  onTap: () => setState(() => _index = 0),
                ),
                _NavItem(
                  icon: Icons.devices_rounded,
                  label: 'Dispositivos',
                  selected: _index == 1,
                  onTap: () => setState(() => _index = 1),
                ),
                _NavItem(
                  icon: Icons.person_rounded,
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
