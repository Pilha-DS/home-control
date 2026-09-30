import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/models.dart';
import '../services/api_client.dart';
import '../services/auth_models.dart';
import '../services/auth_service.dart';
import '../services/ws_service.dart';

class AppState extends ChangeNotifier {
  AppState() {
    _init();
  }

  final ApiClient api = ApiClient(const ServerConfig());
  final WsService ws = WsService();
  late final AuthService auth = AuthService(api);

  ServerConfig server = const ServerConfig();

  List<HomeDevice> devices = [];

  String? selectedDeviceId;

  /// Usuário logado (user_id vem do JWT).
  AuthUser? currentUser;

  /// Secret unico do usuario para autenticacao do ESP (X-User-Secret).
  String? espSecret;

  /// Conexao WebSocket ativa com o backend.
  bool wsConnected = false;

  String? lastError;

  /// Última resposta de config (config/ack) recebida via WS.
  String? lastConfigAck;

  bool loading = true;

  /// Completa quando prefs/server foram carregados (ou falharam).
  final Completer<void> _ready = Completer<void>();
  Future<void> get ready => _ready.future;

  /// Libera loading/ready à força (evita spinner eterno no web).
  void forceReady() {
    loading = false;
    if (!_ready.isCompleted) _ready.complete();
    notifyListeners();
  }

  int controlPin = 4;

  int pulseMs = 0;

  /// Atualiza UI para refletir "fora do ar" sem nova mensagem.
  Timer? _presenceTimer;

  /// Polling GET /api/devices a cada 30 s (usuario comum).
  Timer? _devicesPollTimer;

  /// True enquanto um refresh manual/automatico esta em andamento.
  bool devicesRefreshing = false;

  /// Ultima sincronizacao bem-sucedida da lista.
  DateTime? lastDevicesRefresh;

  /// Usuarios cadastrados (sessao admin).
  List<AdminUser> adminUsers = [];

  bool adminUsersLoading = false;

  /// Ultimo comando de rele enviado (evita telemetria atrasada sobrescrever a UI).
  /// UI so muda o botao quando chega ack/state do Arduino confirmando.
  final Map<String, ({bool on, DateTime at})> _pendingRelay = {};
  final Map<String, Timer> _pendingRelayTimers = {};

  /// Evita toque duplo mandar liga+desliga em sequencia.
  final Set<String> _pokeInFlight = {};
  final Map<String, DateTime> _pokeCooldownUntil = {};

  static const Duration _relayConfirmTimeout = Duration(seconds: 8);

  HomeDevice? get selected {
    if (devices.isEmpty) return null;
    final id = selectedDeviceId;
    if (id == null) return devices.first;
    for (final d in devices) {
      if (d.id == id) return d;
    }
    return devices.first;
  }

  bool get loggedIn => currentUser != null;

  bool get isAdminSession => currentUser?.isAdmin == true;

  Future<void> _init() async {
    try {
      final prefs = await SharedPreferences.getInstance();

      final serverRaw = prefs.getString('server');
      if (serverRaw != null) {
        server = ServerConfig.fromJson(jsonDecode(serverRaw) as Map<String, dynamic>);
      }
      // Migração: API saiu de automacao → mqtt.omny.app.br
      if (server.normalizedBaseUrl.contains('automacao.omny.app.br')) {
        server = const ServerConfig(baseUrl: ServerConfig.productionBaseUrl);
        await prefs.setString('server', jsonEncode(server.toJson()));
      }
      if (server.normalizedBaseUrl.isEmpty) {
        server = const ServerConfig();
      }
      // Web em produção: IP privado / localhost na prefs trava o spinner
      // (fetch sem rota). Força a API pública.
      if (kIsWeb && _isUnreachableFromBrowser(server.normalizedBaseUrl)) {
        server = const ServerConfig(baseUrl: ServerConfig.productionBaseUrl);
        await prefs.setString('server', jsonEncode(server.toJson()));
      }
      api.server = server;

      selectedDeviceId = prefs.getString('selectedDeviceId');
      controlPin = prefs.getInt('controlPin') ?? 4;
      pulseMs = prefs.getInt('pulseMs') ?? 0;

      ws.statusStream.listen((connected) {
        wsConnected = connected;
        notifyListeners();
      });

      ws.events.listen(
        (event) {
          try {
            _onWsEvent(event);
          } catch (e, st) {
            debugPrint('WS event error: $e\n$st');
          }
        },
      );

      _presenceTimer?.cancel();
      _presenceTimer = Timer.periodic(const Duration(seconds: 5), (_) {
        if (devices.any((d) => d.lastSeen != null)) {
          notifyListeners();
        }
      });
    } catch (e, st) {
      debugPrint('AppState init error: $e\n$st');
      lastError = _friendlyError(e);
    } finally {
      loading = false;
      if (!_ready.isCompleted) _ready.complete();
      notifyListeners();
    }
  }

  /// Hosts que o browser na internet não alcança (dev local salvo nas prefs).
  static bool _isUnreachableFromBrowser(String url) {
    final u = url.toLowerCase();
    if (u.isEmpty) return true;
    return u.contains('localhost') ||
        u.contains('127.0.0.1') ||
        u.contains('0.0.0.0') ||
        RegExp(r'https?://192\.168\.').hasMatch(u) ||
        RegExp(r'https?://10\.').hasMatch(u) ||
        RegExp(r'https?://172\.(1[6-9]|2\d|3[0-1])\.').hasMatch(u);
  }

  String _friendlyError(Object e) {
    final s = e.toString();
    if (s.contains('Failed to fetch') ||
        s.contains('XMLHttpRequest') ||
        s.contains('CORS') ||
        s.contains('NetworkError') ||
        s.contains('ClientException') ||
        s.contains('SocketException')) {
      return 'Sem conexao com o servidor. Verifique a URL (${ServerConfig.productionBaseUrl}). $s';
    }
    return s;
  }

  /// Resolve o tipo de placa a partir do nome `modelo`.
  /// Sem modelo conhecido, assume ESP-01 (padrao ate o logs/basic chegar).
  static BoardType boardTypeFromModelo(String? modelo) =>
      BoardType.fromModelo(modelo) ?? BoardType.esp01;

  // ---------------- Auth ----------------

  Future<void> login(String username, String password) async {
    lastError = null;
    notifyListeners();
    try {
      final user = await auth.login(username, password);
      currentUser = user;
      espSecret = user.espSecret;
      api.token = user.token;
      await afterAuth();
    } catch (e) {
      lastError = _friendlyError(e);
      notifyListeners();
      rethrow;
    }
  }

  Future<void> logout() async {
    _stopDevicesPolling();
    ws.stopWatch();
    await auth.logout();
    currentUser = null;
    espSecret = null;
    api.token = null;
    devices = [];
    selectedDeviceId = null;
    devicesRefreshing = false;
    lastDevicesRefresh = null;
    adminUsers = [];
    adminUsersLoading = false;
    _pendingRelay.clear();
    for (final t in _pendingRelayTimers.values) {
      t.cancel();
    }
    _pendingRelayTimers.clear();
    _pokeInFlight.clear();
    _pokeCooldownUntil.clear();
    await ws.disconnect();
    notifyListeners();
  }

  Future<void> afterAuth() async {
    if (currentUser!.isAdmin) {
      _stopDevicesPolling();
      ws.stopWatch();
      await ws.disconnect();
      devices = [];
      selectedDeviceId = null;
      wsConnected = false;
      notifyListeners();
      try {
        await refreshAdminUsers();
      } catch (e) {
        // Não derruba o restore: admin vê shell mesmo se a lista falhar.
        debugPrint('refreshAdminUsers apos auth: $e');
      }
      return;
    }
    try {
      await ws.connect(server, currentUser!.token);
    } catch (e) {
      debugPrint('ws.connect apos auth: $e');
    }
    ws.startWatch(interval: const Duration(seconds: 10));
    if (espSecret == null || espSecret!.isEmpty) {
      try {
        espSecret = await api.getEspSecret();
        await auth.saveEspSecret(espSecret!);
      } catch (_) {}
    }
    await refreshDevices();
    _startDevicesPolling();
    notifyListeners();
  }

  void _startDevicesPolling() {
    _devicesPollTimer?.cancel();
    _devicesPollTimer = Timer.periodic(const Duration(seconds: 30), (_) {
      if (loggedIn && !isAdminSession && !devicesRefreshing) {
        refreshDevices(silent: true);
      }
    });
  }

  void _stopDevicesPolling() {
    _devicesPollTimer?.cancel();
    _devicesPollTimer = null;
  }

  /// Cadastra usuário (admin-only). Username = user_id MQTT.
  Future<Map<String, dynamic>> createUser({
    required String username,
    required String password,
    bool admin = false,
  }) async {
    if (currentUser == null || !currentUser!.isAdmin) {
      throw Exception('Apenas administradores podem cadastrar usuários');
    }
    final result = await api.createUser(
      username: username,
      password: password,
      admin: admin,
    );
    await refreshAdminUsers();
    return result;
  }

  /// Lista usuários cadastrados (admin-only).
  Future<void> refreshAdminUsers() async {
    if (!loggedIn || !isAdminSession) return;
    if (adminUsersLoading) return;

    adminUsersLoading = true;
    notifyListeners();

    try {
      adminUsers = await api.listUsers();
      lastError = null;
    } catch (e) {
      lastError = _friendlyError(e);
      rethrow;
    } finally {
      adminUsersLoading = false;
      notifyListeners();
    }
  }

  /// Ativa ou desativa um usuário (admin-only).
  Future<void> setUserEnabled(String username, bool enabled) async {
    if (currentUser == null || !currentUser!.isAdmin) {
      throw Exception('Apenas administradores podem alterar usuários');
    }
    final updated = await api.setUserEnabled(username, enabled);
    adminUsers = adminUsers
        .map((u) => u.username == updated.username ? updated : u)
        .toList();
    notifyListeners();
  }

  /// Exclui um usuário (admin-only).
  Future<void> deleteUser(String username) async {
    if (currentUser == null || !currentUser!.isAdmin) {
      throw Exception('Apenas administradores podem excluir usuários');
    }
    await api.deleteUser(username);
    adminUsers = adminUsers.where((u) => u.username != username).toList();
    notifyListeners();
  }

  /// Admin redefine a senha de outro usuário (admin-only).
  Future<void> resetUserPassword(String username, String newPassword) async {
    if (currentUser == null || !currentUser!.isAdmin) {
      throw Exception('Apenas administradores podem redefinir senhas');
    }
    await api.resetUserPassword(username, newPassword);
    lastError = null;
    notifyListeners();
  }

  /// Recarrega a lista via GET /api/devices (JWT).
  /// [silent] = true no polling automatico (nao mostra spinner se ja houver lista).
  Future<void> refreshDevices({bool silent = false}) async {
    if (!loggedIn || isAdminSession) return;
    if (devicesRefreshing) return;

    devicesRefreshing = true;
    if (!silent) notifyListeners();

    try {
      final list = await api.listDevices();
      final previous = {for (final d in devices) d.uuid.toLowerCase(): d};
      devices = list.map((json) {
        final next = HomeDevice.fromApi(json);
        final key = next.uuid.toLowerCase();
        final prev = previous[key];
        var merged = next;
        // Polling nao pode apagar placa ja conhecida se a API veio sem modelo
        // (cache MQTT vazio logo apos restart do backend).
        if (prev != null &&
            (next.modelo == null || next.modelo!.trim().isEmpty) &&
            prev.modelo != null &&
            prev.modelo!.trim().isNotEmpty) {
          merged = next.copyWith(
            modelo: prev.modelo,
            boardType: prev.boardType,
            defaultPin: prev.defaultPin,
          );
        }
        // Nao sobrescreve rele enquanto espera ack do poke (cache pode
        // ainda ter state antigo — ex.: desligar poke=0/time=0).
        if (prev != null) {
          final confirmed = next.relayOn != null
              ? _confirmedRelay(next.uuid, next.relayOn!)
              : null;
          if (confirmed != null) {
            merged = merged.copyWith(relayOn: confirmed);
          } else if (_pendingRelay.containsKey(key)) {
            merged = merged.copyWith(relayOn: prev.relayOn);
          } else if (next.relayOn == null && prev.relayOn != null) {
            merged = merged.copyWith(relayOn: prev.relayOn);
          }
        }
        return merged;
      }).toList();
      if (devices.isNotEmpty &&
          (selectedDeviceId == null ||
              !devices.any((d) => d.id == selectedDeviceId))) {
        selectedDeviceId = devices.first.id;
      }
      lastDevicesRefresh = DateTime.now();
      lastError = null;
    } catch (e) {
      lastError = _friendlyError(e);
    } finally {
      devicesRefreshing = false;
      notifyListeners();
    }
  }

  /// Sincroniza um dispositivo com GET /api/devices/{uuid}.
  Future<void> refreshDevice(String uuid) async {
    if (!loggedIn || isAdminSession) return;
    try {
      final json = await api.getDevice(uuid);
      final updated = HomeDevice.fromApi(json);
      final idx = devices.indexWhere(
        (d) => d.uuid.toLowerCase() == uuid.toLowerCase(),
      );
      if (idx >= 0) {
        devices = [...devices]..[idx] = updated;
      } else {
        devices = [...devices, updated];
      }
      lastError = null;
    } catch (e) {
      lastError = _friendlyError(e);
    }
    notifyListeners();
  }

  void _onWsEvent(WsEvent event) {
    if (event.type == 'device/discovered') {
      refreshDevices();
      return;
    }

    final idx = devices.indexWhere(
      (d) => d.uuid.toLowerCase() == event.uuid.toLowerCase(),
    );
    if (idx < 0) {
      // Dispositivo desconhecido: recarrega a lista para incluí-lo.
      refreshDevices();
      return;
    }

    var d = devices[idx];
    final now = DateTime.now();

    switch (event.type) {
      case 'status':
        d = d.copyWith(online: event.payload.trim() == 'online', lastSeen: now);
        break;
      case 'state':
        final serverOn = event.payload.trim().startsWith('0');
        d = d.copyWith(
          relayOn: _confirmedRelay(event.uuid, serverOn) ?? d.relayOn,
          lastSeen: now,
        );
        break;
      case 'ip':
        d = d.copyWith(lastIp: event.payload.trim(), lastSeen: now);
        break;
      case 'ack':
        final ack = event.payload.trim();
        // ok:poke=1,... → confirma estado do rele
        final pokeMatch = RegExp(r'ok:poke=([01])').firstMatch(ack);
        if (pokeMatch != null) {
          final serverOn = pokeMatch.group(1) == '1';
          d = d.copyWith(
            relayOn: _confirmedRelay(event.uuid, serverOn) ?? d.relayOn,
            lastAck: ack,
            lastSeen: now,
          );
        } else {
          d = d.copyWith(lastAck: ack, lastSeen: now);
        }
        break;
      case 'config/ack':
        lastConfigAck = event.payload.trim();
        final j = MqttServiceHelpers.tryParseJson(event.payload);
        if (j != null) {
          final modelo = j['modelo']?.toString();
          d = d.copyWith(
            uuid: (j['uuid']?.toString().isNotEmpty == true)
                ? j['uuid'].toString()
                : d.uuid,
            userId: j['user_id']?.toString() ?? d.userId,
            modelo: (modelo != null && modelo.trim().isNotEmpty) ? modelo : d.modelo,
            boardType: (modelo != null && modelo.trim().isNotEmpty)
                ? boardTypeFromModelo(modelo)
                : d.boardType,
            lastAck: lastConfigAck,
            lastSeen: now,
          );
          if (j['ok'] == false) {
            lastError = j['erro']?.toString() ?? 'config rejeitada';
          } else {
            lastError = null;
          }
        }
        break;
      case 'logs/basic':
      case 'basic': // compat: backend antigo enviava type sem o prefixo logs/
        final j = MqttServiceHelpers.tryParseJson(event.payload);
        if (j != null) {
          final modelo = j['modelo']?.toString();
          final hasModelo = modelo != null && modelo.trim().isNotEmpty;
          // Modelo ao vivo do ESP (servidor nao guarda — so repassa).
          d = d.copyWith(
            userId: j['user_id']?.toString() ?? d.userId,
            modelo: hasModelo ? modelo : d.modelo,
            internetSsid: j['internet']?.toString(),
            serverHost: j['server']?.toString(),
            lastIp: (j['ip']?.toString().isNotEmpty == true)
                ? j['ip'].toString()
                : d.lastIp,
            boardType: hasModelo ? boardTypeFromModelo(modelo) : d.boardType,
            lastSeen: now,
          );
        }
        break;
      case 'logs/advance':
      case 'advance':
        final j = MqttServiceHelpers.tryParseJson(event.payload);
        if (j != null) {
          bool? serverOn;
          final stateVal = j['state'];
          if (stateVal is num) {
            serverOn = stateVal.toInt() == 0;
          } else if (stateVal != null) {
            serverOn = stateVal.toString().startsWith('0');
          }
          d = d.copyWith(
            temperature: (j['temperature'] as num?)?.toDouble(),
            freeHeap: (j['free_heap'] as num?)?.toInt(),
            rssi: (j['rssi'] as num?)?.toInt(),
            connectedTime: (j['connected_time'] as num?)?.toInt(),
            relayOn: serverOn != null
                ? (_confirmedRelay(event.uuid, serverOn) ?? d.relayOn)
                : d.relayOn,
            lastSeen: now,
          );
        }
        break;
    }

    devices = [...devices]..[idx] = d;
    notifyListeners();
  }

  // ---------------- Server config ----------------

  Future<void> saveServer(ServerConfig cfg) async {
    server = cfg;
    api.server = cfg;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('server', jsonEncode(cfg.toJson()));
    lastError = null;
    notifyListeners();
    if (loggedIn) {
      await afterAuth();
    }
  }

  Future<void> reconnect() async {
    lastError = null;
    notifyListeners();
    if (loggedIn) {
      await afterAuth();
    }
  }

  // ---------------- Devices ----------------

  Future<void> addDevice({
    required String name,
    required String uuid,
    BoardType boardType = BoardType.nodeMcu,
  }) async {
    if (isAdminSession) {
      throw Exception('Administradores não gerenciam dispositivos');
    }
    final clean = uuid.trim();
    if (clean.isEmpty) return;
    try {
      await api.createDevice(
        uuid: clean,
        name: name.trim().isEmpty ? clean : name.trim(),
        modelo: boardType.modelo,
      );
      await refreshDevices();
      lastError = null;
    } catch (e) {
      lastError = _friendlyError(e);
      notifyListeners();
      rethrow;
    }
  }

  Future<void> setDeviceBoardType(String id, BoardType boardType) async {
    final idx = devices.indexWhere((d) => d.id == id);
    if (idx < 0) return;
    final old = devices[idx];
    try {
      await api.updateDevice(old.uuid, modelo: boardType.modelo);
      final defaultPin = boardType.usesPin
          ? boardType.defaultPin.clamp(boardType.pinMin, boardType.pinMax)
          : 0;
      devices = [...devices]
        ..[idx] = old.copyWith(
          boardType: boardType,
          modelo: boardType.modelo,
          defaultPin: defaultPin,
        );
      if (selectedDeviceId == id) controlPin = defaultPin;
      lastError = null;
      // Confirma com o backend (ja com modelo persistido).
      await refreshDevice(old.uuid);
    } catch (e) {
      lastError = _friendlyError(e);
      notifyListeners();
      rethrow;
    }
    notifyListeners();
  }

  Future<void> removeDevice(String id) async {
    HomeDevice? d;
    for (final x in devices) {
      if (x.id == id) { d = x; break; }
    }
    if (d == null) return;
    try {
      await api.deleteDevice(d.uuid);
      devices = devices.where((x) => x.id != id).toList();
      if (selectedDeviceId == id) {
        selectedDeviceId = devices.isEmpty ? null : devices.first.id;
      }
      lastError = null;
    } catch (e) {
      lastError = _friendlyError(e);
    }
    notifyListeners();
  }

  /// Quedas de todos os Arduinos da conta (JWT).
  Future<List<DeviceOutageSummary>> fetchOutages() async {
    return api.listOutages();
  }

  /// Quedas de um UUID da conta logada (página no backend).
  Future<DeviceOutageSummary> fetchDeviceOutages(
    String uuid, {
    int limit = 10,
    int offset = 0,
  }) async {
    return api.getDeviceOutages(uuid, limit: limit, offset: offset);
  }

  Future<void> selectDevice(String id) async {
    selectedDeviceId = id;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('selectedDeviceId', id);
    final d = selected;
    if (d != null) {
      controlPin = d.defaultPin;
      await refreshDevice(d.uuid);
    }
    notifyListeners();
  }

  Future<void> setControlPin(int pin) async {
    final b = selected?.boardType ?? BoardType.nodeMcu;
    controlPin = pin.clamp(b.pinMin, b.pinMax);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt('controlPin', controlPin);
    notifyListeners();
  }

  Future<void> setPulseMs(int ms) async {
    pulseMs = ms < 0 ? 0 : ms;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt('pulseMs', pulseMs);
    notifyListeners();
  }

  Future<void> sendPoke(
    bool on, {
    String? deviceId,
    int? pin,
    int? timeMs,
  }) async {
    if (isAdminSession) {
      throw Exception('Administradores não controlam dispositivos');
    }
    if (!loggedIn) throw Exception('Nao autenticado');

    HomeDevice? d;
    if (deviceId != null) {
      for (final x in devices) {
        if (x.id == deviceId) {
          d = x;
          break;
        }
      }
    } else {
      d = selected;
    }
    if (d == null) throw Exception('Nenhum dispositivo selecionado');
    final device = d;
    final key = device.uuid.toLowerCase();

    // Um comando por vez — evita liga e desliga no mesmo toque/duplo clique.
    if (_pokeInFlight.contains(key)) {
      throw Exception('Comando ainda em andamento');
    }
    final cool = _pokeCooldownUntil[key];
    if (cool != null && DateTime.now().isBefore(cool)) {
      throw Exception('Aguarde um instante e tente de novo');
    }
    _pokeInFlight.add(key);

    final usePin = device.boardType.usesPin;
    final pinOut = pin ?? device.defaultPin;
    // V1_FIXO: sempre 5s no liga; desliga nao usa timer.
    final int ms;
    if (device.boardType.hasFixedOn) {
      ms = on ? device.boardType.fixedOnMs : 0;
    } else {
      ms = timeMs ?? pulseMs;
    }

    // Nao muda o botao ainda — so apos ack/state do Arduino.
    _pendingRelay[key] = (on: on, at: DateTime.now());
    _pendingRelayTimers[key]?.cancel();
    _pendingRelayTimers[key] = Timer(_relayConfirmTimeout, () {
      if (_pendingRelay.remove(key) != null) {
        _pendingRelayTimers.remove(key);
        lastError = 'Arduino nao confirmou o comando a tempo';
        notifyListeners();
      }
    });
    notifyListeners();

    try {
      await api.command(device.uuid, {
        'poke': on ? 1 : 0,
        'timeMs': ms,
        if (usePin) 'pin': pinOut,
      });
      final coolMs = (ms > 0) ? ms : 400;
      _pokeCooldownUntil[key] =
          DateTime.now().add(Duration(milliseconds: coolMs));
    } catch (e) {
      _clearPendingRelay(key);
      notifyListeners();
      rethrow;
    } finally {
      _pokeInFlight.remove(key);
      notifyListeners();
    }
  }

  void _clearPendingRelay(String key) {
    _pendingRelay.remove(key);
    _pendingRelayTimers.remove(key)?.cancel();
  }

  bool isRelayPending(String uuid) {
    final key = uuid.toLowerCase();
    if (_pokeInFlight.contains(key)) return true;
    final cool = _pokeCooldownUntil[key];
    if (cool != null && DateTime.now().isBefore(cool)) return true;
    final p = _pendingRelay[key];
    if (p == null) return false;
    if (DateTime.now().difference(p.at) > _relayConfirmTimeout) {
      _clearPendingRelay(key);
      return false;
    }
    return true;
  }

  /// Só aceita o estado se o Arduino confirmar o poke (ignora telemetria velha).
  bool? _confirmedRelay(String uuid, bool serverOn) {
    final key = uuid.toLowerCase();
    final p = _pendingRelay[key];
    if (p == null) return serverOn;

    if (DateTime.now().difference(p.at) > _relayConfirmTimeout) {
      _clearPendingRelay(key);
      return serverOn;
    }

    if (serverOn == p.on) {
      _clearPendingRelay(key);
      return serverOn;
    }
    // Ainda aguardando o ack do comando pedido — ignora state antigo.
    return null;
  }

  /// Publica config no dispositivo via backend (POST /api/devices/{uuid}/config).
  Future<void> configureDevice({
    required String deviceId,
    required String adminPass,
    String? localName,
    String? newUuid,
    String? userId,
    String? modelo,
    String? wifiSsid,
    String? wifiPass,
    String? javaHost,
    int? javaPort,
    int? logicaDoRele,
    String? newAdminPass,
    /// 1 = AP home-setup sempre aberto, 0 = desligado, null = nao alterar.
    int? apOpen,
  }) async {
    final idx = devices.indexWhere((d) => d.id == deviceId);
    if (idx < 0) throw Exception('Dispositivo nao encontrado');
    var d = devices[idx];

    final name = localName?.trim();
    if (name != null && name.isNotEmpty && name != d.name) {
      await api.updateDevice(d.uuid, name: name);
      await refreshDevice(d.uuid);
      d = devices.firstWhere((x) => x.id == deviceId);
    }

    final pass = adminPass.trim();
    if (pass.isEmpty) {
      throw Exception('Informe a senha de administrador do ESP');
    }

    final body = <String, dynamic>{'adminPass': pass};
    void put(String key, String? v) {
      final t = v?.trim();
      if (t != null && t.isNotEmpty) body[key] = t;
    }

    put('newUuid', newUuid);
    put('userId', userId);
    put('modelo', modelo);
    put('wifiSsid', wifiSsid);
    put('wifiPass', wifiPass);
    put('javaHost', javaHost);
    if (javaPort != null && javaPort > 0) body['javaPort'] = javaPort;
    if (logicaDoRele == 1 || logicaDoRele == 2) {
      body['logicaDoRele'] = logicaDoRele;
    }
    put('newAdminPass', newAdminPass);
    if (apOpen == 0 || apOpen == 1) body['apOpen'] = apOpen;

    lastConfigAck = null;
    lastError = null;
    notifyListeners();
    await api.config(d.uuid, body);
  }

  /// Gera novo secret (invalida o anterior) e atualiza cache local.
  Future<String> refreshEspSecret() async {
    if (!loggedIn || isAdminSession) {
      throw Exception('Apenas usuarios com dispositivos podem renovar o secret');
    }
    final fresh = await api.refreshEspSecret();
    espSecret = fresh;
    currentUser = AuthUser(
      username: currentUser!.username,
      role: currentUser!.role,
      token: currentUser!.token,
      espSecret: fresh,
    );
    await auth.saveEspSecret(fresh);
    notifyListeners();
    return fresh;
  }

  /// Usuario logado troca a propria senha (valida a senha atual).
  Future<void> changePassword({
    required String currentPassword,
    required String newPassword,
  }) async {
    if (!loggedIn) throw Exception('Nao autenticado');
    await api.changeOwnPassword(
      currentPassword: currentPassword,
      newPassword: newPassword,
    );
    lastError = null;
    notifyListeners();
  }

  /// Envia config minima a todos os ESPs para gravar o secret atual.
  Future<void> pushEspSecretToAllDevices(String adminPass) async {
    if (devices.isEmpty) throw Exception('Nenhum dispositivo cadastrado');
    for (final d in devices) {
      await configureDevice(deviceId: d.id, adminPass: adminPass);
    }
  }

  String get maskedEspSecret {
    final s = espSecret;
    if (s == null || s.length < 8) return s ?? '—';
    return '${s.substring(0, 4)}…${s.substring(s.length - 4)}';
  }

  @override
  void dispose() {
    _presenceTimer?.cancel();
    _stopDevicesPolling();
    ws.dispose();
    super.dispose();
  }
}

/// Utilitários de JSON compartilhados (substitui o antigo MqttService).
class MqttServiceHelpers {
  static Map<String, dynamic>? tryParseJson(String payload) {
    try {
      final v = jsonDecode(payload);
      if (v is Map<String, dynamic>) return v;
    } catch (_) {}
    return null;
  }
}
