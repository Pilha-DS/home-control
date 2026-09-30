import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../services/api_client.dart';
import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';

/// Monitor global de Arduinos — exige senha admin para desbloquear.
class AdminEspMonitorScreen extends StatefulWidget {
  const AdminEspMonitorScreen({super.key});

  @override
  State<AdminEspMonitorScreen> createState() => _AdminEspMonitorScreenState();
}

class _AdminEspMonitorScreenState extends State<AdminEspMonitorScreen> {
  bool _unlocked = false;
  bool _unlocking = false;
  bool _loading = false;
  String? _error;
  final _passCtrl = TextEditingController();

  Map<String, dynamic>? _snapshot;
  Timer? _poll;

  @override
  void dispose() {
    _poll?.cancel();
    _passCtrl.dispose();
    super.dispose();
  }

  void _startPoll() {
    _poll?.cancel();
    _poll = Timer.periodic(const Duration(seconds: 5), (_) {
      if (_unlocked && mounted) _refresh(silent: true);
    });
  }

  Future<void> _tryUnlock() async {
    if (_unlocking) return;
    final pass = _passCtrl.text;
    if (pass.isEmpty) {
      setState(() => _error = 'Informe a senha de administrador');
      return;
    }

    final app = context.read<AppState>();
    final user = app.currentUser;
    if (user == null || !user.isAdmin) {
      setState(() => _error = 'Sessão admin necessária');
      return;
    }

    setState(() {
      _unlocking = true;
      _error = null;
    });

    try {
      // Revalida senha admin (não troca o JWT da sessão).
      await app.api.login(user.username, pass);
      if (!mounted) return;
      setState(() {
        _unlocked = true;
        _passCtrl.clear();
      });
      _startPoll();
      await _refresh();
    } catch (e) {
      if (mounted) setState(() => _error = 'Senha incorreta');
    } finally {
      if (mounted) setState(() => _unlocking = false);
    }
  }

  Future<void> _refresh({bool silent = false}) async {
    if (!silent) setState(() => _loading = true);
    try {
      final data = await context.read<AppState>().api.getEspMonitor();
      if (!mounted) return;
      setState(() {
        _snapshot = data;
        _error = null;
      });
    } catch (e) {
      if (mounted && !silent) setState(() => _error = '$e');
    } finally {
      if (mounted && !silent) setState(() => _loading = false);
    }
  }

  void _lock() {
    _poll?.cancel();
    setState(() {
      _unlocked = false;
      _snapshot = null;
      _error = null;
    });
  }

  static String _fmtAgo(int? ms, int now) {
    if (ms == null || ms <= 0) return 'nunca';
    final sec = ((now - ms) / 1000).floor();
    if (sec < 5) return 'agora';
    if (sec < 60) return '${sec}s';
    if (sec < 3600) return '${sec ~/ 60}m';
    if (sec < 86400) return '${sec ~/ 3600}h';
    return '${sec ~/ 86400}d';
  }

  static String _fmtClock(int ms) {
    final d = DateTime.fromMillisecondsSinceEpoch(ms).toLocal();
    String two(int n) => n.toString().padLeft(2, '0');
    return '${two(d.hour)}:${two(d.minute)}:${two(d.second)}';
  }

  @override
  Widget build(BuildContext context) {
    if (!_unlocked) return _buildGate();
    return _buildMonitor();
  }

  Widget _buildGate() {
    return SectionBlock(
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SoftCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Container(
                      width: 48,
                      height: 48,
                      decoration: BoxDecoration(
                        color: AppColors.surfaceSoft,
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: const Icon(Icons.lock_rounded, color: AppColors.accent),
                    ),
                    const SizedBox(width: 14),
                    const Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'Monitor de Arduinos',
                            style: TextStyle(
                              fontSize: 18,
                              fontWeight: FontWeight.w700,
                              color: AppColors.textPrimary,
                            ),
                          ),
                          SizedBox(height: 4),
                          Text(
                            'Digite a senha de administrador para ver '
                            'todos os ESPs conectando ou enviando dados.',
                            style: TextStyle(
                              fontSize: 13,
                              color: AppColors.textSecondary,
                              height: 1.35,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 20),
                TextField(
                  controller: _passCtrl,
                  obscureText: true,
                  onSubmitted: (_) => _tryUnlock(),
                  decoration: const InputDecoration(
                    labelText: 'Senha admin',
                    prefixIcon: Icon(Icons.password_rounded),
                  ),
                ),
                if (_error != null) ...[
                  const SizedBox(height: 12),
                  Text(
                    _error!,
                    style: const TextStyle(color: AppColors.danger, fontSize: 13),
                  ),
                ],
                const SizedBox(height: 16),
                FilledButton(
                  onPressed: _unlocking ? null : _tryUnlock,
                  child: _unlocking
                      ? const SizedBox(
                          width: 20,
                          height: 20,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Text('Desbloquear'),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildMonitor() {
    final snap = _snapshot;
    final now = (snap?['now'] as num?)?.toInt() ?? DateTime.now().millisecondsSinceEpoch;
    final devices = (snap?['devices'] as List<dynamic>? ?? const [])
        .cast<Map<String, dynamic>>();
    final activity = (snap?['activity'] as List<dynamic>? ?? const [])
        .cast<Map<String, dynamic>>();
    final online = (snap?['onlineCount'] as num?)?.toInt() ?? 0;
    final total = (snap?['deviceCount'] as num?)?.toInt() ?? devices.length;

    return SectionBlock(
      child: RefreshIndicator(
        color: AppColors.accent,
        onRefresh: _refresh,
        child: ListView(
          children: [
            SoftCard(
              child: Row(
                children: [
                  Expanded(
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        const Text(
                          'Arduinos no servidor',
                          style: TextStyle(
                            fontSize: 18,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                        const SizedBox(height: 6),
                        Text(
                          '$online online · $total no total · atualiza a cada 5 s',
                          style: const TextStyle(
                            color: AppColors.textSecondary,
                            fontSize: 13,
                          ),
                        ),
                      ],
                    ),
                  ),
                  IconButton(
                    tooltip: 'Atualizar',
                    onPressed: _loading ? null : () => _refresh(),
                    icon: _loading
                        ? const SizedBox(
                            width: 20,
                            height: 20,
                            child: CircularProgressIndicator(strokeWidth: 2),
                          )
                        : const Icon(Icons.refresh_rounded),
                  ),
                  IconButton(
                    tooltip: 'Bloquear',
                    onPressed: _lock,
                    icon: const Icon(Icons.lock_outline_rounded),
                  ),
                ],
              ),
            ),
            if (_error != null) ...[
              const SizedBox(height: 12),
              SoftCard(
                color: AppColors.danger.withValues(alpha: 0.12),
                child: Text(_error!, style: const TextStyle(color: AppColors.danger)),
              ),
            ],
            const SizedBox(height: 16),
            const Text(
              'Dispositivos',
              style: TextStyle(
                fontWeight: FontWeight.w700,
                fontSize: 15,
                color: AppColors.textSecondary,
              ),
            ),
            const SizedBox(height: 10),
            if (devices.isEmpty)
              const SoftCard(
                child: EmptyState(
                  icon: Icons.sensors_off_rounded,
                  title: 'Nenhum sinal',
                  message:
                      'Nenhum ESP cadastrou nem enviou telemetria ainda.',
                ),
              )
            else
              ...devices.map((d) => Padding(
                    padding: const EdgeInsets.only(bottom: 10),
                    child: _DeviceRow(
                      data: d,
                      ago: _fmtAgo((d['lastSeenMs'] as num?)?.toInt(), now),
                    ),
                  )),
            const SizedBox(height: 20),
            const Text(
              'Atividade recente',
              style: TextStyle(
                fontWeight: FontWeight.w700,
                fontSize: 15,
                color: AppColors.textSecondary,
              ),
            ),
            const SizedBox(height: 10),
            if (activity.isEmpty)
              const SoftCard(
                child: Text(
                  'Sem eventos ainda. Quando um ESP chamar /events ou falhar autenticação, aparece aqui.',
                  style: TextStyle(color: AppColors.textSecondary, height: 1.4),
                ),
              )
            else
              SoftCard(
                padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 16),
                child: Column(
                  children: activity.take(40).map((h) {
                    final ok = h['ok'] == true;
                    final at = (h['at'] as num?)?.toInt() ?? 0;
                    return Padding(
                      padding: const EdgeInsets.symmetric(vertical: 8),
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Icon(
                            ok ? Icons.check_circle_rounded : Icons.error_rounded,
                            size: 18,
                            color: ok ? AppColors.success : AppColors.danger,
                          ),
                          const SizedBox(width: 10),
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  '${h['uuid']} · ${h['action']}'
                                  '${(h['type'] as String?)?.isNotEmpty == true ? ' / ${h['type']}' : ''}',
                                  style: const TextStyle(
                                    fontWeight: FontWeight.w600,
                                    fontSize: 13,
                                  ),
                                ),
                                const SizedBox(height: 2),
                                Text(
                                  [
                                    if ((h['userId'] as String?)?.isNotEmpty == true)
                                      'user ${h['userId']}',
                                    if ((h['detail'] as String?)?.isNotEmpty == true)
                                      h['detail'],
                                    _fmtClock(at),
                                  ].join(' · '),
                                  style: const TextStyle(
                                    color: AppColors.textSecondary,
                                    fontSize: 12,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                    );
                  }).toList(),
                ),
              ),
            const SizedBox(height: 24),
          ],
        ),
      ),
    );
  }
}

class _DeviceRow extends StatelessWidget {
  const _DeviceRow({required this.data, required this.ago});

  final Map<String, dynamic> data;
  final String ago;

  @override
  Widget build(BuildContext context) {
    final online = data['online'] == true;
    final registered = data['registered'] == true;
    final uuid = '${data['uuid'] ?? ''}';
    final userId = '${data['userId'] ?? ''}';
    final modelo = '${data['modelo'] ?? ''}';
    final ip = '${data['ip'] ?? ''}';
    final status = '${data['status'] ?? ''}';
    final lastType = '${data['lastType'] ?? ''}';

    return SoftCard(
      padding: const EdgeInsets.all(16),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 10,
            height: 10,
            margin: const EdgeInsets.only(top: 6),
            decoration: BoxDecoration(
              color: online ? AppColors.success : AppColors.textSecondary,
              shape: BoxShape.circle,
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  uuid,
                  style: const TextStyle(
                    fontWeight: FontWeight.w700,
                    fontSize: 15,
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  [
                    if (userId.isNotEmpty) 'conta $userId',
                    if (modelo.isNotEmpty) modelo,
                    if (!registered) 'não cadastrado',
                    online ? 'online' : 'offline',
                    'visto $ago',
                  ].join(' · '),
                  style: const TextStyle(
                    color: AppColors.textSecondary,
                    fontSize: 12,
                    height: 1.35,
                  ),
                ),
                if (ip.isNotEmpty || status.isNotEmpty || lastType.isNotEmpty) ...[
                  const SizedBox(height: 4),
                  Text(
                    [
                      if (ip.isNotEmpty) 'IP $ip',
                      if (status.isNotEmpty) status,
                      if (lastType.isNotEmpty) lastType,
                    ].join(' · '),
                    style: const TextStyle(
                      color: AppColors.accentBlue,
                      fontSize: 12,
                    ),
                  ),
                ],
              ],
            ),
          ),
        ],
      ),
    );
  }
}
