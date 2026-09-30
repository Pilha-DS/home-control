import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';

/// Início — só cards de dispositivo (layout ESP-01 vs demais placas).
class ControlScreen extends StatelessWidget {
  const ControlScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();

    return ResponsivePage(
      maxWidth: 1200,
      child: CustomScrollView(
        slivers: [
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 24),
            sliver: SliverToBoxAdapter(
              child: state.devices.isEmpty
                  ? const EmptyState(
                      icon: Icons.devices_other_rounded,
                      title: 'Nenhum dispositivo ativo',
                      message:
                          'Ligue o ESP com o secret da sua conta (aba Conta). '
                          'Ele aparece sozinho aqui.',
                    )
                  : ResponsiveCardGrid(
                      minCardWidth: 300,
                      children: [
                        for (final d in state.devices)
                          d.boardType.isEsp01Family
                              ? _Esp01DeviceCard(device: d)
                              : _GenericDeviceCard(device: d),
                      ],
                    ),
            ),
          ),
        ],
      ),
    );
  }
}

Future<void> _toggle(
  BuildContext context,
  AppState state,
  HomeDevice device, {
  required bool on,
  int? pin,
  int? timeMs,
}) async {
  try {
    await state.sendPoke(
      on,
      deviceId: device.id,
      pin: pin,
      timeMs: device.boardType.hasFixedOn
          ? (on ? device.boardType.fixedOnMs : 0)
          : (timeMs ?? 0),
    );
  } catch (e) {
    if (!context.mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text('$e')));
  }
}

/// Card ESP-01: liga/desliga + tempo (ms). O Arduino agenda o desligamento.
class _Esp01DeviceCard extends StatefulWidget {
  const _Esp01DeviceCard({required this.device});

  final HomeDevice device;

  @override
  State<_Esp01DeviceCard> createState() => _Esp01DeviceCardState();
}

class _Esp01DeviceCardState extends State<_Esp01DeviceCard> {
  late final TextEditingController _timeCtrl;

  @override
  void initState() {
    super.initState();
    _timeCtrl = TextEditingController(text: '0');
  }

  @override
  void dispose() {
    _timeCtrl.dispose();
    super.dispose();
  }

  int get _timeMs {
    final v = int.tryParse(_timeCtrl.text.trim());
    if (v == null || v < 0) return 0;
    return v;
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    final device = widget.device;
    final on = device.relayOn == true;

    return SoftCard(
      color: AppColors.surfaceRaised,
      padding: const EdgeInsets.all(12),
      child: Column(
        children: [
          Row(
            children: [
              const _CircuitIcon(),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  device.name,
                  textAlign: TextAlign.center,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontWeight: FontWeight.w800,
                    fontSize: 14,
                  ),
                ),
              ),
              const SizedBox(width: 10),
              _PowerToggle(
                on: on,
                busy: state.isRelayPending(device.uuid),
                onPressed: () => _toggle(
                  context,
                  state,
                  device,
                  on: !on,
                  timeMs: _timeMs,
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          _MetaGrid(device: device),
          if (device.boardType.hasFixedOn) ...[
            const SizedBox(height: 10),
            Text(
              'Fixo ${device.boardType.fixedOnMs ~/ 1000}s — auto-desliga',
              style: const TextStyle(
                color: AppColors.textSecondary,
                fontSize: 12,
                fontWeight: FontWeight.w600,
              ),
            ),
          ] else ...[
            const SizedBox(height: 10),
            TextField(
              controller: _timeCtrl,
              keyboardType: TextInputType.number,
              inputFormatters: [FilteringTextInputFormatter.digitsOnly],
              decoration: const InputDecoration(
                labelText: 'Tempo',
                hintText: 'ms — 0 = permanente',
                isDense: true,
                contentPadding:
                    EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

/// Card genérico: ícone | nome — meta — liga/desliga | tempo | pin.
class _GenericDeviceCard extends StatefulWidget {
  const _GenericDeviceCard({required this.device});

  final HomeDevice device;

  @override
  State<_GenericDeviceCard> createState() => _GenericDeviceCardState();
}

class _GenericDeviceCardState extends State<_GenericDeviceCard> {
  late int _pin;
  late final TextEditingController _timeCtrl;

  @override
  void initState() {
    super.initState();
    final b = widget.device.boardType;
    final pins = b.availablePins;
    _pin = pins.contains(widget.device.defaultPin)
        ? widget.device.defaultPin
        : (pins.isNotEmpty ? pins.first : b.defaultPin);
    _timeCtrl = TextEditingController(text: '0');
  }

  @override
  void dispose() {
    _timeCtrl.dispose();
    super.dispose();
  }

  int get _timeMs {
    final v = int.tryParse(_timeCtrl.text.trim());
    if (v == null || v < 0) return 0;
    return v;
  }

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    final device = widget.device;
    final on = device.relayOn == true;
    final pins = device.boardType.availablePins;

    return SoftCard(
      color: AppColors.surfaceRaised,
      padding: const EdgeInsets.all(12),
      child: Column(
        children: [
          Row(
            children: [
              const _CircuitIcon(),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  device.name,
                  textAlign: TextAlign.center,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontWeight: FontWeight.w800,
                    fontSize: 14,
                  ),
                ),
              ),
              const SizedBox(width: 36),
            ],
          ),
          const SizedBox(height: 10),
          _MetaGrid(device: device),
          const SizedBox(height: 10),
          Row(
            children: [
              _PowerToggle(
                on: on,
                busy: state.isRelayPending(device.uuid),
                onPressed: () => _toggle(
                  context,
                  state,
                  device,
                  on: !on,
                  pin: _pin,
                  timeMs: _timeMs,
                ),
              ),
              const SizedBox(width: 10),
              if (device.boardType.hasFixedOn)
                Expanded(
                  child: Text(
                    'Fixo ${device.boardType.fixedOnMs ~/ 1000}s',
                    style: const TextStyle(
                      color: AppColors.textSecondary,
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                )
              else
                Expanded(
                  child: TextField(
                    controller: _timeCtrl,
                    keyboardType: TextInputType.number,
                    inputFormatters: [FilteringTextInputFormatter.digitsOnly],
                    decoration: const InputDecoration(
                      labelText: 'Tempo',
                      hintText: 'ms',
                      isDense: true,
                      contentPadding:
                          EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                    ),
                  ),
                ),
              const SizedBox(width: 10),
              Expanded(
                child: DropdownButtonFormField<int>(
                  initialValue: pins.contains(_pin) ? _pin : pins.first,
                  decoration: const InputDecoration(
                    labelText: 'Pin',
                    isDense: true,
                    contentPadding:
                        EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                  ),
                  dropdownColor: AppColors.surfaceSoft,
                  items: [
                    for (final p in pins)
                      DropdownMenuItem(
                        value: p,
                        child: Text(device.boardType.pinLabel(p)),
                      ),
                  ],
                  onChanged: (v) {
                    if (v != null) setState(() => _pin = v);
                  },
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _CircuitIcon extends StatelessWidget {
  const _CircuitIcon();

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 36,
      height: 36,
      decoration: BoxDecoration(
        color: AppColors.accent.withValues(alpha: 0.14),
        borderRadius: BorderRadius.circular(10),
      ),
      child: const Icon(
        Icons.developer_board_rounded,
        color: AppColors.accent,
        size: 20,
      ),
    );
  }
}

class _PowerToggle extends StatelessWidget {
  const _PowerToggle({
    required this.on,
    required this.onPressed,
    this.busy = false,
  });

  final bool on;
  final bool busy;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final color = on ? AppColors.success : AppColors.textSecondary;
    return Material(
      color: color.withValues(alpha: 0.16),
      borderRadius: BorderRadius.circular(10),
      child: InkWell(
        onTap: busy ? null : onPressed,
        borderRadius: BorderRadius.circular(10),
        child: SizedBox(
          width: 40,
          height: 40,
          child: busy
              ? Padding(
                  padding: const EdgeInsets.all(10),
                  child: CircularProgressIndicator(
                    strokeWidth: 2,
                    color: color,
                  ),
                )
              : Icon(
                  on
                      ? Icons.power_settings_new_rounded
                      : Icons.power_off_rounded,
                  color: color,
                  size: 22,
                ),
        ),
      ),
    );
  }
}

class _MetaGrid extends StatelessWidget {
  const _MetaGrid({required this.device});

  final HomeDevice device;

  static String _placaLabel(HomeDevice d) {
    final m = d.modelo?.trim();
    if (m == null || m.isEmpty) return 'Desconhecida';
    return BoardType.fromModelo(m)?.label ?? m;
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Row(
          children: [
            Expanded(
              child: _MetaCell(
                label: 'IP',
                value: device.lastIp ?? '—',
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: _MetaCell(
                label: 'Placa',
                value: _placaLabel(device),
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Expanded(
              child: _MetaCell(
                label: 'Último sinal',
                value: device.lastSeenLabel,
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: _MetaCell(
                label: 'Status',
                value: device.isAlive ? 'Online' : 'Offline',
                valueColor:
                    device.isAlive ? AppColors.success : AppColors.danger,
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _MetaCell extends StatelessWidget {
  const _MetaCell({
    required this.label,
    required this.value,
    this.valueColor,
  });

  final String label;
  final String value;
  final Color? valueColor;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
      decoration: BoxDecoration(
        color: AppColors.surfaceSoft,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            style: const TextStyle(
              color: AppColors.textSecondary,
              fontSize: 10,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 2),
          Text(
            value,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: TextStyle(
              fontWeight: FontWeight.w700,
              fontSize: 12,
              color: valueColor ?? AppColors.textPrimary,
            ),
          ),
        ],
      ),
    );
  }
}
