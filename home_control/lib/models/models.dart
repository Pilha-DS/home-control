import 'dart:convert';

/// Tipo de hardware do dispositivo HOME (firmware multi-modelo).
///
/// Cada modelo tem seu próprio mapa de pinos lógicos (1..N) → GPIO,
/// polaridade, botão e LED onboard. O campo `pin` do comando MQTT é o
/// número lógico (1..N) do perfil — não o GPIO.
enum BoardType {
  /// NodeMCU / Generic ESP8266 — saídas D1–D8 (relé padrão D4, botão D5).
  nodeMcu(
    storage: 'nodeMcu',
    modelo: 'NodeMCU',
    label: 'NodeMCU / Generic ESP8266',
    hint: 'Saídas D1–D8 · relé padrão D1 (GPIO5) · botão D5',
    usesPin: true,
    pinMin: 1,
    pinMax: 8,
    defaultPin: 1,
    gpioMap: [-1, 5, 4, 0, 2, 14, 12, 13, 15],
    disabledPins: {5},
    fixedOnMs: 0,
  ),
  /// NodeMCU com firmware V1_FIXO (liga e desliga sozinho em 5 s).
  nodeMcuFixo(
    storage: 'nodeMcuFixo',
    modelo: 'NodeMCU-FIXO',
    label: 'NodeMCU · Fixo 5s',
    hint: 'Firmware V1_FIXO · liga e desliga automaticamente em 5 s',
    usesPin: true,
    pinMin: 1,
    pinMax: 8,
    defaultPin: 1,
    gpioMap: [-1, 5, 4, 0, 2, 14, 12, 13, 15],
    disabledPins: {5},
    fixedOnMs: 5000,
  ),
  /// ESP-12S / 12E / 12F — saídas GPIO 12,13,14,16,5,4 (sem bootstraps).
  esp12s(
    storage: 'esp12s',
    modelo: 'ESP-12S',
    label: 'ESP-12S / ESP-12E / ESP-12F',
    hint: 'Saídas GPIO 4,5,12,13,14,16 · relé padrão GPIO12',
    usesPin: true,
    pinMin: 1,
    pinMax: 6,
    defaultPin: 1,
    gpioMap: [-1, 12, 13, 14, 16, 5, 4],
    disabledPins: {},
    fixedOnMs: 0,
  ),
  /// ESP-12S com firmware V1_FIXO.
  esp12sFixo(
    storage: 'esp12sFixo',
    modelo: 'ESP-12S-FIXO',
    label: 'ESP-12S · Fixo 5s',
    hint: 'Firmware V1_FIXO · liga e desliga automaticamente em 5 s',
    usesPin: true,
    pinMin: 1,
    pinMax: 6,
    defaultPin: 1,
    gpioMap: [-1, 12, 13, 14, 16, 5, 4],
    disabledPins: {},
    fixedOnMs: 5000,
  ),
  /// ESP-01/01S + relé v4.0 — só GPIO0 (ativo-baixo), sem escolha de pino.
  esp01(
    storage: 'esp01',
    modelo: 'ESP-01',
    label: 'ESP-01 Relé',
    hint: 'Relé no GPIO0 (ativo-baixo) · sem escolha de pino',
    usesPin: false,
    pinMin: 1,
    pinMax: 1,
    defaultPin: 0,
    gpioMap: [-1, 0],
    disabledPins: {},
    fixedOnMs: 0,
  ),
  /// ESP-01 com firmware V1_FIXO (5 s fixos).
  esp01Fixo(
    storage: 'esp01Fixo',
    modelo: 'ESP-01-FIXO',
    label: 'ESP-01 · Fixo 5s',
    hint: 'Firmware V1_FIXO · GPIO0 · auto-desliga em 5 s',
    usesPin: false,
    pinMin: 1,
    pinMax: 1,
    defaultPin: 0,
    gpioMap: [-1, 0],
    disabledPins: {},
    fixedOnMs: 5000,
  );

  const BoardType({
    required this.storage,
    required this.modelo,
    required this.label,
    required this.hint,
    required this.usesPin,
    required this.pinMin,
    required this.pinMax,
    required this.defaultPin,
    required this.gpioMap,
    required this.disabledPins,
    required this.fixedOnMs,
  });

  final String storage;

  /// Valor canônico enviado no campo `MODELO` do firmware
  /// (`"ESP-01"`, `"NodeMCU"`, `"ESP-12S"`, `"*-FIXO"`).
  final String modelo;

  final String label;
  final String hint;

  /// Se o app mostra seleção de pino neste modelo.
  final bool usesPin;

  /// Faixa de pinos lógicos (1..N) do perfil.
  final int pinMin;
  final int pinMax;

  /// Pino lógico padrão (0 = sem pino, ESP-01).
  final int defaultPin;

  /// Mapa pin lógico (1..N) → GPIO (-1 = inválido).
  final List<int> gpioMap;

  /// Pinos lógicos que não podem ser usados como saída (ex.: botão).
  final Set<int> disabledPins;

  /// >0 = firmware V1_FIXO: tempo de liga fixo (ms); UI esconde campo tempo.
  final int fixedOnMs;

  bool get hasFixedOn => fixedOnMs > 0;

  bool get isEsp01Family =>
      this == BoardType.esp01 || this == BoardType.esp01Fixo;

  /// GPIO correspondente a um pin lógico (ou -1 se fora da faixa).
  int gpioFor(int pin) =>
      (pin >= pinMin && pin <= pinMax) ? gpioMap[pin] : -1;

  /// Rótulo do pino exibido na UI.
  String pinLabel(int pin) => switch (this) {
        BoardType.nodeMcu || BoardType.nodeMcuFixo => 'D$pin',
        BoardType.esp12s || BoardType.esp12sFixo => 'GPIO${gpioFor(pin)}',
        BoardType.esp01 || BoardType.esp01Fixo => 'GPIO0',
      };

  /// Pinos lógicos disponíveis como saída (exclui botão/LED reservados).
  List<int> get availablePins =>
      [for (var p = pinMin; p <= pinMax; p++) if (!disabledPins.contains(p)) p];

  static BoardType fromStorage(String? raw) {
    switch (raw) {
      case 'esp01':
        return BoardType.esp01;
      case 'esp01Fixo':
        return BoardType.esp01Fixo;
      case 'esp12s':
        return BoardType.esp12s;
      case 'esp12sFixo':
        return BoardType.esp12sFixo;
      case 'nodeMcuFixo':
        return BoardType.nodeMcuFixo;
      case 'nodeMcu':
      default:
        return BoardType.nodeMcu;
    }
  }

  /// Resolve a partir do nome canônico `MODELO` enviado pelo firmware.
  static BoardType? fromModelo(String? raw) {
    final m = (raw ?? '').trim().toLowerCase();
    if (m.isEmpty) return null;
    final fixo = m.contains('fixo');
    if (m.contains('esp-01') || m.contains('esp01')) {
      return fixo ? BoardType.esp01Fixo : BoardType.esp01;
    }
    if (m.contains('esp-12') || m.contains('esp12')) {
      return fixo ? BoardType.esp12sFixo : BoardType.esp12s;
    }
    if (m.contains('nodemcu') ||
        m.contains('node mcu') ||
        m.contains('generic')) {
      return fixo ? BoardType.nodeMcuFixo : BoardType.nodeMcu;
    }
    return null;
  }
}

class HomeDevice {
  /// Intervalo de telemetria do firmware (basic/advance).
  static const Duration telemetryInterval = Duration(seconds: 20);

  /// Sem mensagem nesse prazo → fora do ar (alinhado ao backend: 50 s).
  static const Duration offlineAfter = Duration(seconds: 50);

  final String id;
  final String name;
  final String uuid;
  final BoardType boardType;
  final int defaultPin;
  final String? userId;
  final String? modelo;
  final bool? online;
  final bool? relayOn;
  final String? lastIp;
  final String? lastAck;
  final DateTime? lastSeen;
  // Telemetria (home/logs/.../basic + advance) — não persistida
  final String? internetSsid;
  final String? serverHost;
  final double? temperature;
  final int? freeHeap;
  final int? rssi;
  final int? connectedTime;

  const HomeDevice({
    required this.id,
    required this.name,
    required this.uuid,
    this.boardType = BoardType.nodeMcu,
    this.defaultPin = 1,
    this.userId,
    this.modelo,
    this.online,
    this.relayOn,
    this.lastIp,
    this.lastAck,
    this.lastSeen,
    this.internetSsid,
    this.serverHost,
    this.temperature,
    this.freeHeap,
    this.rssi,
    this.connectedTime,
  });

  /// true se recebeu telemetria/cmd recente (baseado em [lastSeen]).
  bool get isAlive {
    final t = lastSeen;
    if (t == null) return false;
    return DateTime.now().difference(t) < offlineAfter;
  }

  /// Rótulo curto para chip de status.
  String get presenceLabel {
    if (lastSeen == null) return 'sem sinal';
    if (isAlive) return 'online';
    return 'fora do ar';
  }

  String get lastSeenLabel {
    final t = lastSeen;
    if (t == null) return 'nunca';
    final d = DateTime.now().difference(t);
    if (d.inSeconds < 5) return 'agora';
    if (d.inSeconds < 60) return 'há ${d.inSeconds}s';
    if (d.inMinutes < 60) return 'há ${d.inMinutes} min';
    return 'há ${d.inHours} h';
  }

  HomeDevice copyWith({
    String? id,
    String? name,
    String? uuid,
    BoardType? boardType,
    int? defaultPin,
    String? userId,
    String? modelo,
    bool? online,
    bool? relayOn,
    String? lastIp,
    String? lastAck,
    DateTime? lastSeen,
    String? internetSsid,
    String? serverHost,
    double? temperature,
    int? freeHeap,
    int? rssi,
    int? connectedTime,
  }) {
    return HomeDevice(
      id: id ?? this.id,
      name: name ?? this.name,
      uuid: uuid ?? this.uuid,
      boardType: boardType ?? this.boardType,
      defaultPin: defaultPin ?? this.defaultPin,
      userId: userId ?? this.userId,
      modelo: modelo ?? this.modelo,
      online: online ?? this.online,
      relayOn: relayOn ?? this.relayOn,
      lastIp: lastIp ?? this.lastIp,
      lastAck: lastAck ?? this.lastAck,
      lastSeen: lastSeen ?? this.lastSeen,
      internetSsid: internetSsid ?? this.internetSsid,
      serverHost: serverHost ?? this.serverHost,
      temperature: temperature ?? this.temperature,
      freeHeap: freeHeap ?? this.freeHeap,
      rssi: rssi ?? this.rssi,
      connectedTime: connectedTime ?? this.connectedTime,
    );
  }

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'uuid': uuid,
        'boardType': boardType.storage,
        'defaultPin': defaultPin,
        if (userId != null) 'userId': userId,
        if (modelo != null) 'modelo': modelo,
      };

  factory HomeDevice.fromJson(Map<String, dynamic> json) => HomeDevice(
        id: json['id'] as String,
        name: json['name'] as String,
        uuid: json['uuid'] as String,
        boardType: BoardType.fromStorage(json['boardType'] as String?),
        defaultPin: (json['defaultPin'] as num?)?.toInt() ?? 1,
        userId: json['userId'] as String?,
        modelo: json['modelo'] as String?,
      );

  /// Constrói a partir da resposta do backend (GET /api/devices).
  /// O campo `state` traz o estado em tempo real (do cache MQTT).
  factory HomeDevice.fromApi(Map<String, dynamic> json) {
    final state = json['state'];
    Map<String, dynamic> st = const {};
    if (state is Map<String, dynamic>) st = state;

    // Prioridade: campo modelo da API (ja vem do MQTT ao vivo) > logs/basic.
    // Sem modelo: assume ESP-01 como padrao ate o logs/basic informar a placa.
    final modeloApi = (json['modelo'] as String?)?.trim();
    final basicPayload = st['logs/basic'] ?? st['basic'];
    final advancePayload = st['logs/advance'] ?? st['advance'];
    final modeloFromLogs = _jsonField(basicPayload, 'modelo');
    final modelo = (modeloApi != null && modeloApi.isNotEmpty)
        ? modeloApi
        : modeloFromLogs;
    final board = BoardType.fromModelo(modelo) ?? BoardType.esp01;

    return HomeDevice(
      id: json['uuid'] as String,
      name: (json['name'] as String?)?.isNotEmpty == true
          ? json['name'] as String
          : json['uuid'] as String,
      uuid: json['uuid'] as String,
      boardType: board,
      defaultPin: board.usesPin ? board.defaultPin : 0,
      userId: json['userId'] as String?,
      modelo: modelo,
      online: _boolOrNull(st['status'], onlineWhen: 'online'),
      relayOn: _relayOnFromState(st['state']),
      lastIp: st['ip'] as String? ?? _jsonField(basicPayload, 'ip'),
      lastAck: st['ack'] as String?,
      lastSeen: _lastSeenFromState(st),
      internetSsid: _jsonField(basicPayload, 'internet'),
      serverHost: _jsonField(basicPayload, 'server'),
      temperature: _numField(advancePayload, 'temperature'),
      freeHeap: _intField(advancePayload, 'free_heap'),
      rssi: _intField(advancePayload, 'rssi'),
      connectedTime: _intField(advancePayload, 'connected_time'),
    );
  }

  static bool? _boolOrNull(dynamic v, {required String onlineWhen}) {
    if (v == null) return null;
    return v.toString().trim() == onlineWhen;
  }

  static bool? _relayOnFromState(dynamic v) {
    if (v == null) return null;
    final s = v.toString().trim();
    return s.startsWith('0');
  }

  static DateTime? _lastSeenFromState(Map<String, dynamic> st) {
    final v = st['lastSeen'];
    if (v == null) return null;
    if (v is num) return DateTime.fromMillisecondsSinceEpoch(v.toInt());
    return null;
  }

  static String? _jsonField(dynamic v, String key) {
    if (v == null) return null;
    if (v is Map) return v[key]?.toString();
    try {
      final j = jsonDecode(v.toString());
      if (j is Map) return j[key]?.toString();
    } catch (_) {}
    return null;
  }

  static double? _numField(dynamic v, String key) {
    if (v == null) return null;
    final raw = v is Map ? v[key] : null;
    if (raw is num) return raw.toDouble();
    try {
      final j = jsonDecode(v.toString());
      if (j is Map) return (j[key] as num?)?.toDouble();
    } catch (_) {}
    return null;
  }

  static int? _intField(dynamic v, String key) {
    if (v == null) return null;
    if (v is Map) {
      final raw = v[key];
      if (raw is num) return raw.toInt();
    }
    try {
      final j = jsonDecode(v.toString());
      if (j is Map) return (j[key] as num?)?.toInt();
    } catch (_) {}
    return null;
  }
}

/// Resumo de quedas de um Arduino (GET /api/devices/outages).
class DeviceOutageSummary {
  final String name;
  final String uuid;
  final String userId;
  final int outageCount;
  final bool currentlyDown;
  final int currentDownMs;
  final int totalDownMs;
  final List<DeviceOutageEvent> outages;

  const DeviceOutageSummary({
    required this.name,
    required this.uuid,
    required this.userId,
    required this.outageCount,
    required this.currentlyDown,
    required this.currentDownMs,
    required this.totalDownMs,
    required this.outages,
  });

  factory DeviceOutageSummary.fromJson(Map<String, dynamic> json) {
    final list = json['outages'];
    return DeviceOutageSummary(
      name: (json['name'] as String?) ?? (json['uuid'] as String? ?? ''),
      uuid: json['uuid'] as String? ?? '',
      userId: json['userId'] as String? ?? '',
      outageCount: (json['outageCount'] as num?)?.toInt() ?? 0,
      currentlyDown: json['currentlyDown'] as bool? ?? false,
      currentDownMs: (json['currentDownMs'] as num?)?.toInt() ?? 0,
      totalDownMs: (json['totalDownMs'] as num?)?.toInt() ?? 0,
      outages: list is List
          ? list
              .whereType<Map<String, dynamic>>()
              .map(DeviceOutageEvent.fromJson)
              .toList()
          : const [],
    );
  }

  String get totalDownLabel => _fmtDuration(totalDownMs);
  String get currentDownLabel => _fmtDuration(currentDownMs);
}

class DeviceOutageEvent {
  final int id;
  final String name;
  final String uuid;
  final String userId;
  final DateTime? startedAt;
  final DateTime? endedAt;
  final int durationMs;
  final bool open;

  const DeviceOutageEvent({
    required this.id,
    required this.name,
    required this.uuid,
    required this.userId,
    required this.startedAt,
    required this.endedAt,
    required this.durationMs,
    required this.open,
  });

  factory DeviceOutageEvent.fromJson(Map<String, dynamic> json) {
    return DeviceOutageEvent(
      id: (json['id'] as num?)?.toInt() ?? 0,
      name: (json['name'] as String?) ?? '',
      uuid: json['uuid'] as String? ?? '',
      userId: json['userId'] as String? ?? '',
      startedAt: _parseInstant(json['startedAt']),
      endedAt: _parseInstant(json['endedAt']),
      durationMs: (json['durationMs'] as num?)?.toInt() ?? 0,
      open: json['open'] as bool? ?? false,
    );
  }

  String get durationLabel => _fmtDuration(durationMs);
}

DateTime? _parseInstant(dynamic v) {
  if (v == null) return null;
  if (v is String) return DateTime.tryParse(v)?.toLocal();
  return null;
}

String _fmtDuration(int ms) {
  if (ms <= 0) return '0 s';
  final s = (ms / 1000).round();
  if (s < 60) return '$s s';
  final m = s ~/ 60;
  if (m < 60) {
    final rem = s % 60;
    return rem == 0 ? '${m} min' : '${m} min ${rem}s';
  }
  final h = m ~/ 60;
  final remM = m % 60;
  return remM == 0 ? '${h} h' : '${h} h ${remM} min';
}

class RelayCommand {
  final String userId;
  final String deviceUuid;
  final int poke; // 1 = liga, 0 = desliga
  final int timeMs;
  /// Null = não envia `pin` (ESP-01). NodeMCU usa 1–8.
  final int? pin;

  const RelayCommand({
    required this.userId,
    required this.deviceUuid,
    required this.poke,
    this.timeMs = 0,
    this.pin = 4,
  });

  String toJson() {
    final buf = StringBuffer('{"poke":$poke,"time":$timeMs');
    if (pin != null) {
      buf.write(',"pin":$pin');
    }
    buf.write('}');
    return buf.toString();
  }
}

/// Payload para `home/<user>/<uuid>/config` (PASS obrigatório).
class DeviceRemoteConfig {
  final String adminPass;
  /// user_id já no path do tópico (dono atual).
  final String topicUserId;
  final String deviceUuid;
  final String? newUuid;
  final String? userId;
  final String? modelo;
  final String? wifiSsid;
  final String? wifiPass;
  final String? mqttHost;
  final int? mqttPort;
  final String? mqttUser;
  final String? mqttPass;
  final String? mqttPath;
  /// 1 = ativo-alto, 2 = ativo-baixo (ESP-01 só aceita 2).
  final int? logicaDoRele;
  final String? newAdminPass;
  /// 1 = AP home-setup sempre aberto, 0 = desligado.
  final int? apOpen;

  const DeviceRemoteConfig({
    required this.adminPass,
    required this.topicUserId,
    required this.deviceUuid,
    this.newUuid,
    this.userId,
    this.modelo,
    this.wifiSsid,
    this.wifiPass,
    this.mqttHost,
    this.mqttPort,
    this.mqttUser,
    this.mqttPass,
    this.mqttPath,
    this.logicaDoRele,
    this.newAdminPass,
    this.apOpen,
  });

  String toJson() {
    final m = <String, dynamic>{
      'PASS': adminPass,
    };
    void put(String key, String? v) {
      final t = v?.trim();
      if (t != null && t.isNotEmpty) m[key] = t;
    }

    put('UUID', newUuid);
    put('USER_ID', userId);
    put('MODELO', modelo);
    put('WIFI_SSID', wifiSsid);
    put('WIFI_PASS', wifiPass);
    put('MQTT_HOST', mqttHost);
    if (mqttPort != null && mqttPort! > 0) m['MQTT_PORT'] = mqttPort;
    put('MQTT_USER', mqttUser);
    put('MQTT_PASS', mqttPass);
    put('MQTT_PATH', mqttPath);
    if (logicaDoRele == 1 || logicaDoRele == 2) {
      m['LOGICA_DO_RELE'] = logicaDoRele;
    }
    put('NEW_PASS', newAdminPass);
    if (apOpen == 0 || apOpen == 1) m['AP_OPEN'] = apOpen;
    return jsonEncode(m);
  }
}

class BrokerConfig {
  final String host;
  final int port;
  final String username;
  final String password;
  final String path;
  final bool useSsl;

  const BrokerConfig({
    this.host = 'mosquito.omny.tec.br',
    this.port = 443,
    this.username = 'cardoso',
    this.password = 'C6m8n4d2d3',
    this.path = '/',
    this.useSsl = true,
  });

  BrokerConfig copyWith({
    String? host,
    int? port,
    String? username,
    String? password,
    String? path,
    bool? useSsl,
  }) {
    return BrokerConfig(
      host: host ?? this.host,
      port: port ?? this.port,
      username: username ?? this.username,
      password: password ?? this.password,
      path: path ?? this.path,
      useSsl: useSsl ?? this.useSsl,
    );
  }

  Map<String, dynamic> toJson() => {
        'host': host,
        'port': port,
        'username': username,
        'password': password,
        'path': path,
        'useSsl': useSsl,
      };

  factory BrokerConfig.fromJson(Map<String, dynamic> json) => BrokerConfig(
        host: json['host'] as String? ?? 'mosquito.omny.tec.br',
        port: (json['port'] as num?)?.toInt() ?? 443,
        username: json['username'] as String? ?? 'cardoso',
        password: json['password'] as String? ?? 'C6m8n4d2d3',
        path: json['path'] as String? ?? '/',
        useSsl: json['useSsl'] as bool? ?? true,
      );
}
