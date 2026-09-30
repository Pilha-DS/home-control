import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';

/// Bottom sheet com histórico de quedas.
///
/// - Gerais: backend devolve só a última.
/// - Por dispositivo (⋮): backend pagina de 10 em 10 (`limit`/`offset`).
Future<void> showOutagesSheet(
  BuildContext context, {
  String? uuid,
  String? deviceName,
}) {
  return showModalBottomSheet<void>(
    context: context,
    isScrollControlled: true,
    backgroundColor: AppColors.surfaceRaised,
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
    ),
    builder: (ctx) => _OutagesSheet(uuid: uuid, deviceName: deviceName),
  );
}

class _OutagesSheet extends StatefulWidget {
  const _OutagesSheet({this.uuid, this.deviceName});

  final String? uuid;
  final String? deviceName;

  bool get isDeviceScope => uuid != null && uuid!.isNotEmpty;

  @override
  State<_OutagesSheet> createState() => _OutagesSheetState();
}

class _OutagesSheetState extends State<_OutagesSheet> {
  static const int _pageSize = 10;

  bool _loading = true;
  bool _loadingMore = false;
  String? _error;

  /// Gerais: no máximo 1 evento (já limitado no backend).
  _FlatOutage? _latest;

  /// Por dispositivo: página acumulada do backend.
  DeviceOutageSummary? _deviceSummary;
  final List<DeviceOutageEvent> _events = [];
  int _offset = 0;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _error = null;
      _events.clear();
      _offset = 0;
    });
    try {
      final state = context.read<AppState>();
      if (widget.isDeviceScope) {
        final summary = await state.fetchDeviceOutages(
          widget.uuid!,
          limit: _pageSize,
          offset: 0,
        );
        if (!mounted) return;
        setState(() {
          _deviceSummary = summary;
          _events
            ..clear()
            ..addAll(summary.outages);
          _offset = summary.outages.length;
          _latest = null;
          _loading = false;
        });
      } else {
        final list = await state.fetchOutages();
        if (!mounted) return;
        setState(() {
          _latest = _fromGeneralResponse(list);
          _deviceSummary = null;
          _loading = false;
        });
      }
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = '$e';
        _loading = false;
      });
    }
  }

  Future<void> _loadMore() async {
    if (_loadingMore || !widget.isDeviceScope) return;
    final total = _deviceSummary?.outageCount ?? 0;
    if (_events.length >= total) return;

    setState(() => _loadingMore = true);
    try {
      final page = await context.read<AppState>().fetchDeviceOutages(
            widget.uuid!,
            limit: _pageSize,
            offset: _offset,
          );
      if (!mounted) return;
      setState(() {
        _deviceSummary = page;
        _events.addAll(page.outages);
        _offset += page.outages.length;
        _loadingMore = false;
      });
    } catch (e) {
      if (!mounted) return;
      setState(() {
        _error = '$e';
        _loadingMore = false;
      });
    }
  }

  /// Backend já manda 0 ou 1 resumo com 0 ou 1 evento.
  static _FlatOutage? _fromGeneralResponse(List<DeviceOutageSummary> list) {
    if (list.isEmpty) return null;
    final s = list.first;
    if (s.outages.isEmpty) return null;
    final e = s.outages.first;
    return _FlatOutage(
      name: s.name.isNotEmpty
          ? s.name
          : (e.name.isNotEmpty ? e.name : s.uuid),
      uuid: s.uuid.isNotEmpty ? s.uuid : e.uuid,
      event: e,
    );
  }

  @override
  Widget build(BuildContext context) {
    final title = widget.isDeviceScope
        ? 'Quedas — ${widget.deviceName ?? widget.uuid}'
        : 'Última queda';

    return DraggableScrollableSheet(
      expand: false,
      initialChildSize: 0.55,
      minChildSize: 0.35,
      maxChildSize: 0.95,
      builder: (ctx, scroll) {
        return Padding(
          padding: const EdgeInsets.fromLTRB(20, 12, 20, 20),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Center(
                child: Container(
                  width: 40,
                  height: 4,
                  decoration: BoxDecoration(
                    color: AppColors.textSecondary.withValues(alpha: 0.35),
                    borderRadius: BorderRadius.circular(999),
                  ),
                ),
              ),
              const SizedBox(height: 14),
              Row(
                children: [
                  Expanded(
                    child: Text(
                      title,
                      style: const TextStyle(
                        fontWeight: FontWeight.w800,
                        fontSize: 18,
                      ),
                    ),
                  ),
                  IconButton(
                    tooltip: 'Atualizar',
                    onPressed: _loading ? null : _load,
                    icon: const Icon(Icons.refresh_rounded),
                  ),
                ],
              ),
              Text(
                widget.isDeviceScope
                    ? 'Histórico deste Arduino — 10 por página no servidor. Offline = +50 s sem sinal.'
                    : 'Só a última queda da conta (limitado no servidor). Histórico completo: ⋮ no dispositivo.',
                style: const TextStyle(
                  color: AppColors.textSecondary,
                  fontSize: 13,
                  height: 1.35,
                ),
              ),
              const SizedBox(height: 14),
              Expanded(child: _body(scroll)),
            ],
          ),
        );
      },
    );
  }

  Widget _body(ScrollController scroll) {
    if (_loading) {
      return const Center(
        child: CircularProgressIndicator(color: AppColors.accent),
      );
    }
    if (_error != null) {
      return Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Text(_error!, textAlign: TextAlign.center),
            const SizedBox(height: 12),
            SoftCard(
              color: AppColors.accent.withValues(alpha: 0.16),
              onTap: _load,
              padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 20),
              child: const Text(
                'Tentar de novo',
                style: TextStyle(
                  color: AppColors.accent,
                  fontWeight: FontWeight.w800,
                ),
              ),
            ),
          ],
        ),
      );
    }

    if (widget.isDeviceScope) {
      return _deviceBody(scroll);
    }
    return _generalBody(scroll);
  }

  Widget _generalBody(ScrollController scroll) {
    final latest = _latest;
    if (latest == null) {
      return ListView(
        controller: scroll,
        children: const [
          EmptyState(
            icon: Icons.check_circle_outline_rounded,
            title: 'Sem quedas registradas',
            message: 'Quando um Arduino cair, a última queda aparece aqui.',
          ),
        ],
      );
    }

    return ListView(
      controller: scroll,
      children: [
        SoftCard(
          color: AppColors.surfaceSoft,
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: Text(
                      latest.name,
                      style: const TextStyle(
                        fontWeight: FontWeight.w800,
                        fontSize: 16,
                      ),
                    ),
                  ),
                  StatusChip(
                    label: latest.event.open ? 'fora do ar' : 'recuperou',
                    color: latest.event.open
                        ? AppColors.danger
                        : AppColors.success,
                  ),
                ],
              ),
              const SizedBox(height: 4),
              Text(
                latest.uuid,
                style: const TextStyle(
                  color: AppColors.textSecondary,
                  fontSize: 12,
                ),
              ),
              const SizedBox(height: 12),
              _EventRow(event: latest.event),
            ],
          ),
        ),
      ],
    );
  }

  Widget _deviceBody(ScrollController scroll) {
    final summary = _deviceSummary;
    final total = summary?.outageCount ?? 0;
    if (summary == null || total == 0 || _events.isEmpty) {
      return ListView(
        controller: scroll,
        children: const [
          EmptyState(
            icon: Icons.check_circle_outline_rounded,
            title: 'Sem quedas registradas',
            message: 'Quando este Arduino cair, o histórico aparece aqui.',
          ),
        ],
      );
    }

    final hasMore = _events.length < total;

    return ListView(
      controller: scroll,
      children: [
        SoftCard(
          color: AppColors.surfaceSoft,
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: Text(
                      summary.name,
                      style: const TextStyle(
                        fontWeight: FontWeight.w800,
                        fontSize: 16,
                      ),
                    ),
                  ),
                  StatusChip(
                    label: summary.currentlyDown ? 'fora do ar' : 'ok',
                    color: summary.currentlyDown
                        ? AppColors.danger
                        : AppColors.success,
                  ),
                ],
              ),
              const SizedBox(height: 4),
              Text(
                summary.uuid,
                style: const TextStyle(
                  color: AppColors.textSecondary,
                  fontSize: 12,
                ),
              ),
              const SizedBox(height: 10),
              Wrap(
                spacing: 10,
                runSpacing: 8,
                children: [
                  _StatPill(label: 'Quedas', value: '$total'),
                  _StatPill(
                    label: 'Total offline',
                    value: summary.totalDownLabel,
                  ),
                  if (summary.currentlyDown)
                    _StatPill(
                      label: 'Atual',
                      value: summary.currentDownLabel,
                      accent: AppColors.danger,
                    ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        Text(
          'Histórico (${_events.length} de $total)',
          style: const TextStyle(
            fontWeight: FontWeight.w700,
            fontSize: 13,
            color: AppColors.textSecondary,
          ),
        ),
        const SizedBox(height: 8),
        for (final e in _events) ...[
          SoftCard(
            color: AppColors.surfaceSoft,
            padding: const EdgeInsets.fromLTRB(14, 12, 14, 12),
            child: _EventRow(event: e),
          ),
          const SizedBox(height: 8),
        ],
        if (hasMore)
          SoftCard(
            color: AppColors.accent.withValues(alpha: 0.14),
            onTap: _loadingMore ? null : _loadMore,
            padding: const EdgeInsets.symmetric(vertical: 14),
            child: Center(
              child: _loadingMore
                  ? const SizedBox(
                      width: 22,
                      height: 22,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        color: AppColors.accent,
                      ),
                    )
                  : Text(
                      'Mostrar mais 10 (${total - _events.length} restantes)',
                      style: const TextStyle(
                        color: AppColors.accent,
                        fontWeight: FontWeight.w800,
                        fontSize: 13,
                      ),
                    ),
            ),
          ),
      ],
    );
  }
}

class _FlatOutage {
  const _FlatOutage({
    required this.name,
    required this.uuid,
    required this.event,
  });

  final String name;
  final String uuid;
  final DeviceOutageEvent event;
}

class _StatPill extends StatelessWidget {
  const _StatPill({
    required this.label,
    required this.value,
    this.accent,
  });

  final String label;
  final String value;
  final Color? accent;

  @override
  Widget build(BuildContext context) {
    final c = accent ?? AppColors.accent;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      decoration: BoxDecoration(
        color: c.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(14),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            style: TextStyle(
              color: c.withValues(alpha: 0.9),
              fontSize: 11,
              fontWeight: FontWeight.w600,
            ),
          ),
          Text(
            value,
            style: TextStyle(
              color: c,
              fontWeight: FontWeight.w800,
              fontSize: 14,
            ),
          ),
        ],
      ),
    );
  }
}

class _EventRow extends StatelessWidget {
  const _EventRow({required this.event});

  final DeviceOutageEvent event;

  @override
  Widget build(BuildContext context) {
    final start = event.startedAt;
    final end = event.endedAt;
    String when = '—';
    if (start != null) {
      when = _fmt(start);
      if (end != null) when = '$when → ${_fmt(end)}';
      if (event.open) when = '$when → agora';
    }

    return Row(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Icon(
          event.open ? Icons.cloud_off_rounded : Icons.history_rounded,
          size: 18,
          color: event.open ? AppColors.danger : AppColors.textSecondary,
        ),
        const SizedBox(width: 8),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                when,
                style: const TextStyle(fontSize: 12, height: 1.3),
              ),
              Text(
                event.open
                    ? 'Em andamento · ${event.durationLabel}'
                    : 'Durou ${event.durationLabel}',
                style: TextStyle(
                  fontSize: 12,
                  color: event.open
                      ? AppColors.danger
                      : AppColors.textSecondary,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }

  static String _fmt(DateTime dt) {
    final d = dt.day.toString().padLeft(2, '0');
    final m = dt.month.toString().padLeft(2, '0');
    final h = dt.hour.toString().padLeft(2, '0');
    final min = dt.minute.toString().padLeft(2, '0');
    return '$d/$m $h:$min';
  }
}
