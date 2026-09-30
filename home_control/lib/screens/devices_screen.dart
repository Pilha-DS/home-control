import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/models.dart';
import '../state/app_state.dart';
import '../theme/app_theme.dart';
import '../widgets/ui_kit.dart';
import 'outages_sheet.dart';

class DevicesScreen extends StatelessWidget {
  const DevicesScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final state = context.watch<AppState>();
    final online = state.devices.where((d) => d.isAlive).length;

    return ResponsivePage(
      child: CustomScrollView(
        slivers: [
          SliverToBoxAdapter(
            child: SectionBlock(
              color: AppColors.canvas,
              padding: const EdgeInsets.fromLTRB(20, 20, 20, 12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  PageHeader(
                    title: 'Dispositivos',
                    subtitle: state.lastDevicesRefresh == null
                        ? 'Salvos na sua conta · sync a cada 30 s'
                        : 'Conta sincronizada · ${_fmtTime(state.lastDevicesRefresh!)}',
                    trailing: state.devices.isNotEmpty
                        ? StatusChip(
                            label: '$online online',
                            color: online > 0
                                ? AppColors.success
                                : AppColors.textSecondary,
                          )
                        : null,
                  ),
                  const SizedBox(height: 8),
                  SoftCard(
                    color: AppColors.surfaceRaised,
                    padding: const EdgeInsets.all(10),
                    child: const Row(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Icon(Icons.auto_awesome_rounded,
                            color: AppColors.accent, size: 18),
                        SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            'Com o secret da conta no ESP, o Arduino entra '
                            'sozinho na lista na primeira conexão.',
                            style: TextStyle(
                              color: AppColors.textSecondary,
                              fontSize: 12,
                              height: 1.3,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 10),
                  Row(
                    children: [
                      Expanded(
                        child: SoftCard(
                          color: AppColors.accent.withValues(alpha: 0.14),
                          onTap: () => showOutagesSheet(context),
                          padding: const EdgeInsets.symmetric(
                            vertical: 11,
                            horizontal: 10,
                          ),
                          child: const Row(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Icon(Icons.cloud_off_rounded,
                                  color: AppColors.accent, size: 18),
                              SizedBox(width: 6),
                              Text(
                                'Quedas',
                                style: TextStyle(
                                  color: AppColors.accent,
                                  fontWeight: FontWeight.w800,
                                  fontSize: 13,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                      const SizedBox(width: 8),
                      SoftCard(
                        color: AppColors.surfaceRaised,
                        onTap: () => _showAddSheet(context),
                        padding: const EdgeInsets.symmetric(
                          vertical: 11,
                          horizontal: 12,
                        ),
                        child: const Icon(
                          Icons.add_rounded,
                          color: AppColors.accent,
                          size: 20,
                        ),
                      ),
                      const SizedBox(width: 8),
                      SoftCard(
                        color: AppColors.surfaceRaised,
                        onTap: state.devicesRefreshing
                            ? null
                            : () async {
                                await state.refreshDevices();
                                if (context.mounted) {
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    const SnackBar(
                                      content: Text('Lista atualizada'),
                                      duration: Duration(seconds: 1),
                                    ),
                                  );
                                }
                              },
                        padding: const EdgeInsets.symmetric(
                          vertical: 11,
                          horizontal: 12,
                        ),
                        child: state.devicesRefreshing
                            ? const SizedBox(
                                width: 18,
                                height: 18,
                                child: CircularProgressIndicator(
                                  strokeWidth: 2,
                                  color: AppColors.accent,
                                ),
                              )
                            : const Icon(
                                Icons.refresh_rounded,
                                color: AppColors.accent,
                                size: 20,
                              ),
                      ),
                    ],
                  ),
                ],
              ),
            ),
          ),
          SliverToBoxAdapter(
            child: SectionBlock(
              color: AppColors.surface,
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SectionTitle(
                    'Ativos na sua conta',
                    subtitle: state.devices.isEmpty
                        ? 'Aguardando o primeiro ESP com o secret'
                        : '${state.devices.length} ativo(s) · $online online',
                    trailing: IconButton(
                      tooltip: 'Atualizar agora',
                      onPressed: state.devicesRefreshing
                          ? null
                          : () => state.refreshDevices(),
                      icon: state.devicesRefreshing
                          ? const SizedBox(
                              width: 20,
                              height: 20,
                              child: CircularProgressIndicator(
                                strokeWidth: 2,
                                color: AppColors.accent,
                              ),
                            )
                          : const Icon(Icons.refresh_rounded),
                    ),
                  ),
                  if (state.devices.isEmpty)
                    const EmptyState(
                      icon: Icons.sensors_off_rounded,
                      title: 'Nenhum Arduino ativo',
                      message:
                          'Copie o secret em Conta, cole no portal do ESP e '
                          'ligue-o. Em poucos segundos ele aparece aqui '
                          'automaticamente. O + é só para cadastro manual.',
                    )
                  else
                    ...state.devices.map(
                      (d) => Padding(
                        padding: const EdgeInsets.only(bottom: 10),
                        child: _DeviceListTile(
                          device: d,
                          onTap: () => state.selectDevice(d.id),
                          onConfigure: () => _editDeviceConfig(context, d),
                          onBoardType: () => _editBoardType(context, d),
                          onOutages: () => showOutagesSheet(
                            context,
                            uuid: d.uuid,
                            deviceName: d.name,
                          ),
                          onRemove: () => _confirmRemove(context, d),
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ),
          const SliverToBoxAdapter(child: SizedBox(height: 32)),
        ],
      ),
    );
  }

  static String _fmtTime(DateTime dt) {
    final h = dt.hour.toString().padLeft(2, '0');
    final m = dt.minute.toString().padLeft(2, '0');
    final s = dt.second.toString().padLeft(2, '0');
    return '$h:$m:$s';
  }

  /// Host do backend Java para o ESP (sem esquema/porta).
  static String _javaHostDefault(AppState state) {
    final base = state.server.normalizedBaseUrl;
    if (base.isEmpty) return '192.168.15.15';
    try {
      return Uri.parse(base).host;
    } catch (_) {
      return '192.168.15.15';
    }
  }

  /// Porta HTTPS/HTTP do backend — alinhada com o firmware (443 em produção).
  static int _javaPortDefault(AppState state) {
    final base = state.server.normalizedBaseUrl;
    if (base.isEmpty) return 443;
    try {
      final uri = Uri.parse(base);
      if (uri.hasPort) return uri.port;
      return uri.scheme == 'https' ? 443 : 8080;
    } catch (_) {
      return 443;
    }
  }

  Future<void> _confirmRemove(BuildContext context, HomeDevice d) async {
    final ok = await showModalBottomSheet<bool>(
      context: context,
      backgroundColor: AppColors.surfaceRaised,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
      ),
      builder: (ctx) => Padding(
        padding: const EdgeInsets.fromLTRB(24, 24, 24, 32),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'Remover ${d.name}?',
              style: const TextStyle(fontWeight: FontWeight.w800, fontSize: 18),
            ),
            const SizedBox(height: 8),
            const Text(
              'Ele some da lista ativa. Se o ESP voltar a conectar com o '
              'secret da conta, pode reaparecer sozinho.',
              style: TextStyle(color: AppColors.textSecondary),
            ),
            const SizedBox(height: 20),
            Row(
              children: [
                Expanded(
                  child: SoftCard(
                    color: AppColors.surfaceSoft,
                    onTap: () => Navigator.pop(ctx, false),
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    child: const Center(
                      child: Text(
                        'Cancelar',
                        style: TextStyle(fontWeight: FontWeight.w700),
                      ),
                    ),
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: SoftCard(
                    color: AppColors.danger.withValues(alpha: 0.18),
                    onTap: () => Navigator.pop(ctx, true),
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    child: const Center(
                      child: Text(
                        'Remover',
                        style: TextStyle(
                          color: AppColors.danger,
                          fontWeight: FontWeight.w800,
                        ),
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
    if (ok == true && context.mounted) {
      await context.read<AppState>().removeDevice(d.id);
    }
  }

  Future<void> _editDeviceConfig(BuildContext context, HomeDevice d) async {
    final state = context.read<AppState>();
    final nameCtrl = TextEditingController(text: d.name);
    final uuidCtrl = TextEditingController(text: d.uuid);
    final userCtrl = TextEditingController(text: d.userId ?? '');
    final wifiSsidCtrl = TextEditingController(text: d.internetSsid ?? '');
    final wifiPassCtrl = TextEditingController();
    final javaHostCtrl = TextEditingController(
      text: d.serverHost?.isNotEmpty == true ? d.serverHost! : '',
    );
    final javaPortCtrl = TextEditingController(
      text: '${_javaPortDefault(state)}',
    );
    final adminCtrl = TextEditingController();
    final newAdminCtrl = TextEditingController();
    // Só envia o host/porta se o ESP já reportou um host — senão o save
    // (ex.: renomear) gravaria um host derivado do app no ESP por engano.
    var showJava = d.serverHost?.isNotEmpty == true;
    var sending = false;
    // Valor do dropdown de modelo: '' = não alterar; senão, nome canônico MODELO.
    var modeloSel = BoardType.fromModelo(d.modelo)?.modelo ?? '';
    // AP {uuid}-setup: '' = não alterar; '1' = sempre aberto; '0' = desligado.
    var apOpenSel = '';

    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: AppColors.surfaceRaised,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
      ),
      builder: (ctx) {
        return StatefulBuilder(
          builder: (ctx, setModalState) {
            return Padding(
              padding: EdgeInsets.fromLTRB(
                24,
                20,
                24,
                24 + MediaQuery.viewInsetsOf(ctx).bottom,
              ),
              child: SingleChildScrollView(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      'Configurar ${d.name}',
                      style: const TextStyle(
                        fontWeight: FontWeight.w800,
                        fontSize: 18,
                      ),
                    ),
                    const SizedBox(height: 6),
                    const Text(
                      'Envia config via Java (backend enfileira; ESP busca no poll). '
                      'Campos vazios não são alterados (exceto nome no app).',
                      style: TextStyle(
                        color: AppColors.textSecondary,
                        height: 1.35,
                        fontSize: 13,
                      ),
                    ),
                    if (d.lastIp != null || d.internetSsid != null) ...[
                      const SizedBox(height: 10),
                      Text(
                        [
                          if (d.lastIp != null) 'IP ${d.lastIp}',
                          if (d.internetSsid != null) d.internetSsid!,
                          if (d.userId != null) 'user ${d.userId}',
                        ].join(' · '),
                        style: const TextStyle(
                          color: AppColors.accent,
                          fontSize: 12,
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ],
                    const SizedBox(height: 16),
                    TextField(
                      controller: nameCtrl,
                      decoration: const InputDecoration(
                        labelText: 'Nome no app',
                      ),
                    ),
                    const SizedBox(height: 10),
                    TextField(
                      controller: uuidCtrl,
                      decoration: const InputDecoration(
                        labelText: 'UUID no Arduino',
                        hintText: 'muda o identificador do ESP',
                      ),
                    ),
                    const SizedBox(height: 10),
                    TextField(
                      controller: userCtrl,
                      decoration: const InputDecoration(
                        labelText: 'user_id (dono / pai)',
                      ),
                    ),
                    const SizedBox(height: 10),
                    DropdownButtonFormField<String>(
                      initialValue: modeloSel,
                      dropdownColor: AppColors.surfaceSoft,
                      decoration: const InputDecoration(
                        labelText: 'Modelo da placa',
                      ),
                      items: [
                        const DropdownMenuItem(
                          value: '',
                          child: Text('Não alterar'),
                        ),
                        for (final t in BoardType.values)
                          DropdownMenuItem(
                            value: t.modelo,
                            child: Text(t.label),
                          ),
                      ],
                      onChanged: (v) =>
                          setModalState(() => modeloSel = v ?? ''),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      BoardType.fromModelo(modeloSel)?.hint ??
                          'Vazio = mantém o modelo atual no ESP.',
                      style: const TextStyle(
                        color: AppColors.textSecondary,
                        fontSize: 12,
                        height: 1.3,
                      ),
                    ),
                    const SizedBox(height: 10),
                    TextField(
                      controller: wifiSsidCtrl,
                      decoration: const InputDecoration(
                        labelText: 'Wi-Fi SSID',
                        hintText: 'vazio = manter',
                      ),
                    ),
                    const SizedBox(height: 10),
                    TextField(
                      controller: wifiPassCtrl,
                      obscureText: true,
                      decoration: const InputDecoration(
                        labelText: 'Senha do Wi-Fi',
                        hintText: 'só se for trocar a rede',
                      ),
                    ),
                    const SizedBox(height: 8),
                    SoftCard(
                      color: AppColors.surfaceSoft,
                      padding: const EdgeInsets.symmetric(
                        horizontal: 4,
                        vertical: 2,
                      ),
                      child: SwitchListTile(
                        contentPadding:
                            const EdgeInsets.symmetric(horizontal: 12),
                        title: const Text(
                          'Servidor Java do ESP',
                          style: TextStyle(fontSize: 14),
                        ),
                        value: showJava,
                        activeThumbColor: AppColors.accent,
                        onChanged: (v) => setModalState(() => showJava = v),
                      ),
                    ),
                    if (showJava) ...[
                      const SizedBox(height: 10),
                      TextField(
                        controller: javaHostCtrl,
                        decoration: InputDecoration(
                          labelText: 'Host do backend',
                          hintText: _javaHostDefault(state),
                        ),
                      ),
                      const SizedBox(height: 10),
                      TextField(
                        controller: javaPortCtrl,
                        keyboardType: TextInputType.number,
                        decoration: InputDecoration(
                          labelText: 'Porta (443 = HTTPS produção)',
                          hintText: '${_javaPortDefault(state)}',
                        ),
                      ),
                    ],
                    const SizedBox(height: 10),
                    DropdownButtonFormField<String>(
                      initialValue: apOpenSel,
                      dropdownColor: AppColors.surfaceSoft,
                      decoration: const InputDecoration(
                        labelText: 'Wi-Fi da placa (AP {uuid}-setup)',
                      ),
                      items: const [
                        DropdownMenuItem(
                          value: '',
                          child: Text('Não alterar'),
                        ),
                        DropdownMenuItem(
                          value: '1',
                          child: Text('Sempre aberta'),
                        ),
                        DropdownMenuItem(
                          value: '0',
                          child: Text('Desligada'),
                        ),
                      ],
                      onChanged: (v) =>
                          setModalState(() => apOpenSel = v ?? ''),
                    ),
                    const SizedBox(height: 6),
                    Text(
                      'Rede ${d.uuid.toLowerCase().replaceAll('_', '-')}-setup para configurar via http://192.168.4.1. '
                      'Enviado pelo /config (sem reinício se for só isso).',
                      style: const TextStyle(
                        color: AppColors.textSecondary,
                        fontSize: 12,
                        height: 1.3,
                      ),
                    ),
                    const SizedBox(height: 10),
                    TextField(
                      controller: adminCtrl,
                      obscureText: true,
                      decoration: const InputDecoration(
                        labelText: 'Senha admin do ESP *',
                        hintText: 'obrigatória para gravar no Arduino',
                      ),
                    ),
                    const SizedBox(height: 10),
                    TextField(
                      controller: newAdminCtrl,
                      obscureText: true,
                      decoration: const InputDecoration(
                        labelText: 'Nova senha admin',
                        hintText: 'opcional',
                      ),
                    ),
                    const SizedBox(height: 16),
                    SoftCard(
                      color: sending
                          ? AppColors.surfaceSoft
                          : AppColors.accent.withValues(alpha: 0.18),
                      onTap: sending
                          ? null
                          : () async {
                              setModalState(() => sending = true);
                              try {
                                await state.configureDevice(
                                  deviceId: d.id,
                                  adminPass: adminCtrl.text,
                                  localName: nameCtrl.text,
                                  newUuid: uuidCtrl.text,
                                  userId: userCtrl.text,
                                  modelo:
                                      modeloSel.isEmpty ? null : modeloSel,
                                  wifiSsid: wifiSsidCtrl.text,
                                  wifiPass: wifiPassCtrl.text,
                                  javaHost: showJava ? javaHostCtrl.text : null,
                                  javaPort: showJava
                                      ? int.tryParse(javaPortCtrl.text.trim())
                                      : null,
                                  newAdminPass: newAdminCtrl.text,
                                  apOpen: apOpenSel.isEmpty
                                      ? null
                                      : int.tryParse(apOpenSel),
                                );
                                if (ctx.mounted) {
                                  Navigator.pop(ctx);
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    const SnackBar(
                                      content: Text(
                                        'Config enviada. O ESP pode reiniciar; '
                                        'aguarde o config/ack.',
                                      ),
                                    ),
                                  );
                                }
                              } catch (e) {
                                setModalState(() => sending = false);
                                if (ctx.mounted) {
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    SnackBar(content: Text('$e')),
                                  );
                                }
                              }
                            },
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      child: Center(
                        child: Text(
                          sending ? 'Enviando…' : 'Salvar no Arduino',
                          style: TextStyle(
                            color: sending
                                ? AppColors.textSecondary
                                : AppColors.accent,
                            fontWeight: FontWeight.w800,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            );
          },
        );
      },
    );

    nameCtrl.dispose();
    uuidCtrl.dispose();
    userCtrl.dispose();
    wifiSsidCtrl.dispose();
    wifiPassCtrl.dispose();
    javaHostCtrl.dispose();
    javaPortCtrl.dispose();
    adminCtrl.dispose();
    newAdminCtrl.dispose();
  }

  Future<void> _editBoardType(BuildContext context, HomeDevice d) async {
    final state = context.read<AppState>();
    final chosen = await showModalBottomSheet<BoardType>(
      context: context,
      isScrollControlled: true,
      backgroundColor: AppColors.surfaceRaised,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
      ),
      builder: (ctx) {
        final maxH = MediaQuery.sizeOf(ctx).height * 0.85;
        return SafeArea(
          child: ConstrainedBox(
            constraints: BoxConstraints(maxHeight: maxH),
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(24, 24, 24, 32),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    'Placa — ${d.name}',
                    style: const TextStyle(
                      fontWeight: FontWeight.w800,
                      fontSize: 18,
                    ),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Define a pinagem/lógica que o app usa para controlar o '
                    'relé: NodeMCU, ESP-12S ou ESP-01 — com ou sem Fixo 5s '
                    '(V1_FIXO).',
                    style: TextStyle(
                      color: AppColors.textSecondary,
                      height: 1.4,
                    ),
                  ),
                  const SizedBox(height: 16),
                  for (final t in BoardType.values)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 10),
                      child: SoftCard(
                        color: d.boardType == t
                            ? AppColors.accent.withValues(alpha: 0.18)
                            : AppColors.surfaceSoft,
                        onTap: () => Navigator.pop(ctx, t),
                        padding: const EdgeInsets.all(16),
                        child: Row(
                          children: [
                            Icon(
                              t.isEsp01Family
                                  ? Icons.electrical_services_rounded
                                  : Icons.developer_board_rounded,
                              color: AppColors.accent,
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(
                                    t.label,
                                    style: const TextStyle(
                                      fontWeight: FontWeight.w800,
                                    ),
                                  ),
                                  Text(
                                    t.hint,
                                    style: const TextStyle(
                                      color: AppColors.textSecondary,
                                      fontSize: 12,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                            if (d.boardType == t)
                              const Icon(
                                Icons.check_rounded,
                                color: AppColors.accent,
                              ),
                          ],
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ),
        );
      },
    );
    if (chosen != null && context.mounted) {
      try {
        await state.setDeviceBoardType(d.id, chosen);
      } catch (_) {
        if (context.mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            SnackBar(content: Text(state.lastError ?? 'Falha ao salvar placa')),
          );
        }
      }
    }
  }

  Future<void> _showAddSheet(BuildContext context) async {
    final nameCtrl = TextEditingController();
    final uuidCtrl = TextEditingController();
    final state = context.read<AppState>();
    var boardType = BoardType.nodeMcu;

    await showModalBottomSheet<void>(
      context: context,
      isScrollControlled: true,
      backgroundColor: AppColors.surfaceRaised,
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(28)),
      ),
      builder: (ctx) {
        return StatefulBuilder(
          builder: (ctx, setModalState) {
            final maxH = MediaQuery.sizeOf(ctx).height * 0.9;
            return SafeArea(
              child: ConstrainedBox(
                constraints: BoxConstraints(maxHeight: maxH),
                child: SingleChildScrollView(
                  padding: EdgeInsets.fromLTRB(
                    24,
                    24,
                    24,
                    24 + MediaQuery.viewInsetsOf(ctx).bottom,
                  ),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                  const Text(
                    'Cadastro manual (opcional)',
                    style: TextStyle(fontWeight: FontWeight.w800, fontSize: 18),
                  ),
                  const SizedBox(height: 6),
                  const Text(
                    'Normalmente o ESP entra sozinho com o secret. Use isto '
                    'só se quiser reservar o UUID antes de ligar a placa.',
                    style: TextStyle(
                      color: AppColors.textSecondary,
                      fontSize: 13,
                      height: 1.35,
                    ),
                  ),
                  const SizedBox(height: 16),
                  TextField(
                    controller: nameCtrl,
                    decoration: const InputDecoration(
                      hintText: 'Nome (ex.: Portão)',
                    ),
                  ),
                  const SizedBox(height: 10),
                  TextField(
                    controller: uuidCtrl,
                    decoration: const InputDecoration(
                      hintText: 'UUID (igual ao do ESP)',
                    ),
                    textCapitalization: TextCapitalization.characters,
                  ),
                  const SizedBox(height: 14),
                  const Text(
                    'Tipo de placa',
                    style: TextStyle(
                      color: AppColors.textSecondary,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const SizedBox(height: 8),
                  for (final t in BoardType.values)
                    Padding(
                      padding: const EdgeInsets.only(bottom: 8),
                      child: SoftCard(
                        color: boardType == t
                            ? AppColors.accent.withValues(alpha: 0.2)
                            : AppColors.surfaceSoft,
                        onTap: () => setModalState(() => boardType = t),
                        padding: const EdgeInsets.all(14),
                        child: Row(
                          children: [
                            Icon(
                              t.isEsp01Family
                                  ? Icons.electrical_services_rounded
                                  : Icons.developer_board_rounded,
                              color: boardType == t
                                  ? AppColors.accent
                                  : AppColors.textSecondary,
                              size: 22,
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Text(
                                    t.label,
                                    style: TextStyle(
                                      fontWeight: FontWeight.w800,
                                      fontSize: 13,
                                      color: boardType == t
                                          ? AppColors.accent
                                          : AppColors.textPrimary,
                                    ),
                                  ),
                                  Text(
                                    t.hint,
                                    style: const TextStyle(
                                      color: AppColors.textSecondary,
                                      fontSize: 11,
                                      height: 1.25,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                            if (boardType == t)
                              const Icon(
                                Icons.check_rounded,
                                color: AppColors.accent,
                                size: 20,
                              ),
                          ],
                        ),
                      ),
                    ),
                  const SizedBox(height: 16),
                  SoftCard(
                    color: AppColors.accent.withValues(alpha: 0.18),
                    onTap: () async {
                      await state.addDevice(
                        name: nameCtrl.text,
                        uuid: uuidCtrl.text,
                        boardType: boardType,
                      );
                      if (ctx.mounted) Navigator.pop(ctx);
                    },
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    child: const Center(
                      child: Text(
                        'Salvar',
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
            );
          },
        );
      },
    );
  }
}

class _DeviceListTile extends StatelessWidget {
  const _DeviceListTile({
    required this.device,
    required this.onTap,
    required this.onConfigure,
    required this.onBoardType,
    required this.onOutages,
    required this.onRemove,
  });

  final HomeDevice device;
  final VoidCallback onTap;
  final VoidCallback onConfigure;
  final VoidCallback onBoardType;
  final VoidCallback onOutages;
  final VoidCallback onRemove;

  @override
  Widget build(BuildContext context) {
    final alive = device.isAlive;
    final statusColor = alive ? AppColors.success : AppColors.danger;

    return SoftCard(
      color: AppColors.surfaceRaised,
      onTap: onTap,
      padding: const EdgeInsets.fromLTRB(12, 10, 4, 10),
      child: Row(
        children: [
          Container(
            width: 40,
            height: 40,
            decoration: BoxDecoration(
              color: statusColor.withValues(alpha: 0.16),
              borderRadius: BorderRadius.circular(12),
            ),
            child: Icon(Icons.memory_rounded, color: statusColor, size: 20),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Expanded(
                      child: Text(
                        device.name,
                        style: const TextStyle(
                          fontWeight: FontWeight.w800,
                          fontSize: 14,
                        ),
                      ),
                    ),
                    StatusChip(label: device.presenceLabel, color: statusColor),
                  ],
                ),
                const SizedBox(height: 2),
                Text(
                  device.uuid,
                  style: const TextStyle(
                    color: AppColors.textSecondary,
                    fontSize: 11,
                  ),
                ),
                const SizedBox(height: 2),
                if (device.lastIp != null)
                  Text(
                    'http://${device.lastIp}/',
                    style: const TextStyle(
                      color: AppColors.accent,
                      fontSize: 11,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                Text(
                  'http://${device.uuid.toLowerCase().replaceAll('_', '-')}.local/ '
                  '(mesma rede Wi-Fi)',
                  style: const TextStyle(
                    color: AppColors.textSecondary,
                    fontSize: 11,
                  ),
                ),
                const SizedBox(height: 6),
                Wrap(
                  spacing: 12,
                  runSpacing: 4,
                  children: [
                    _MetaTag(
                      icon: Icons.developer_board_rounded,
                      text: device.boardType.label,
                    ),
                    if (device.lastIp != null)
                      _MetaTag(icon: Icons.wifi_rounded, text: device.lastIp!),
                    _MetaTag(
                      icon: Icons.schedule_rounded,
                      text: device.lastSeenLabel,
                    ),
                  ],
                ),
              ],
            ),
          ),
          PopupMenuButton<String>(
            icon: const Icon(
              Icons.more_vert_rounded,
              color: AppColors.textSecondary,
            ),
            color: AppColors.surfaceSoft,
            onSelected: (v) {
              switch (v) {
                case 'config':
                  onConfigure();
                case 'board':
                  onBoardType();
                case 'outages':
                  onOutages();
                case 'remove':
                  onRemove();
              }
            },
            itemBuilder: (_) => const [
              PopupMenuItem(
                value: 'config',
                child: Row(
                  children: [
                    Icon(Icons.settings_rounded, size: 20, color: AppColors.accent),
                    SizedBox(width: 10),
                    Text('Configurar ESP'),
                  ],
                ),
              ),
              PopupMenuItem(
                value: 'board',
                child: Row(
                  children: [
                    Icon(Icons.developer_board_rounded, size: 20),
                    SizedBox(width: 10),
                    Text('Tipo de placa'),
                  ],
                ),
              ),
              PopupMenuItem(
                value: 'outages',
                child: Row(
                  children: [
                    Icon(Icons.cloud_off_rounded, size: 20),
                    SizedBox(width: 10),
                    Text('Histórico de quedas'),
                  ],
                ),
              ),
              PopupMenuItem(
                value: 'remove',
                child: Row(
                  children: [
                    Icon(
                      Icons.delete_outline_rounded,
                      size: 20,
                      color: AppColors.danger,
                    ),
                    SizedBox(width: 10),
                    Text('Remover', style: TextStyle(color: AppColors.danger)),
                  ],
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _MetaTag extends StatelessWidget {
  const _MetaTag({required this.icon, required this.text});

  final IconData icon;
  final String text;

  @override
  Widget build(BuildContext context) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: 13, color: AppColors.textSecondary),
        const SizedBox(width: 4),
        Text(
          text,
          style: const TextStyle(
            color: AppColors.textSecondary,
            fontSize: 11,
          ),
        ),
      ],
    );
  }
}
