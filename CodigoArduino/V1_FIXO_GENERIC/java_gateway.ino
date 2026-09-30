//============================================================================
// Gateway Java (HTTPS) — V1_FIXO
// ESP → POST /api/esp/{uuid}/events  |  GET /api/esp/{uuid}/poll
// Includes: ESP8266HTTPClient / WiFiClientSecure em V1_FIXO.ino
//
// Regra: POLL manda (presenca + cmd). POST so com link saudavel.
// Keep-alive no ESP8266 + BearSSL costuma virar socket zumbi — reuse=false.
//============================================================================

bool javaGatewayReady = false;
unsigned long lastJavaPoll = 0;
const unsigned long JAVA_POLL_MS = 2000UL;
const unsigned long JAVA_POLL_RETRY_MS = 3000UL;
const unsigned long JAVA_POLL_RECOVERY_MS = 10000UL;
const unsigned long JAVA_SOFT_RESET_MS = 10000UL;
const unsigned long JAVA_WIFI_RECOVER_MS = 35000UL;
const unsigned long JAVA_HARD_RESTART_MS = 120000UL;
const uint32_t JAVA_HEAP_MIN = 7000;
const int JAVA_HTTP_TIMEOUT_MS = 1500;

#define JAVA_QMAX 4
#define JAVA_QPAY 360
static char javaQType[JAVA_QMAX][24];
static char javaQPay[JAVA_QMAX][JAVA_QPAY];
static uint8_t javaQHead = 0;
static uint8_t javaQLen = 0;

static WiFiClientSecure javaTlsClient;
static WiFiClient javaPlainClient;
static BearSSL::Session javaTlsSession;
static bool javaLastPollOk = true;
static uint8_t javaFailStreak = 0;
static uint8_t javaSoftResetCount = 0;
static unsigned long javaLastSuccessMs = 0;
static unsigned long javaLastSoftResetMs = 0;
static unsigned long javaLastWifiRecoverMs = 0;
static unsigned long javaHeapLowSince = 0;

// (No Generic a pausa de POST apos ligar GPIO0 nao se aplica: a saida padrao
// e D1/GPIO5; so haveria risco se o usuario escolhesse D3/GPIO0 via comando.)

// Servidor Java inacessível: falhas seguidas de comunicação com Wi-Fi ok.
// O loop() usa esta flag para abrir o AP <uuid>-setup (reconfigurar host).
bool javaServerUnreachable = false;
static uint8_t javaServerFailCount = 0;
#define JAVA_SERVER_AP_THRESHOLD 5

// Servidor respondeu (poll/POST HTTP 200) — prova real de comunicação.
// O teste de contingência só comita o candidato quando esta flag é true.
bool javaServerOk = false;

static bool javaUseTls() {
  return mqttPort == 443 || mqttPort == 8443;
}

void javaResetHttpClient() {
  if (javaUseTls()) {
    javaTlsClient.stop();
  } else {
    javaPlainClient.stop();
  }
}

static void javaClearQueue() {
  javaQHead = 0;
  javaQLen = 0;
}

static void javaMarkSuccess() {
  javaLastPollOk = true;
  javaFailStreak = 0;
  javaSoftResetCount = 0;
  javaServerFailCount = 0;
  javaServerUnreachable = false;
  javaServerOk = true;   // poll HTTP 200 real — servidor respondeu
  javaLastSuccessMs = millis();
}

static void javaMarkFail() {
  javaLastPollOk = false;
  if (javaFailStreak < 250) javaFailStreak++;
  javaServerOk = false;
  javaResetHttpClient();
  // Wi-Fi conectado mas servidor inacessível (host vazio/errado, TLS, etc.)
  if (WiFi.status() == WL_CONNECTED) {
    if (javaServerFailCount < 250) javaServerFailCount++;
    if (javaServerFailCount >= JAVA_SERVER_AP_THRESHOLD) {
      javaServerUnreachable = true;
    }
  }
}

static bool javaHeapOkForHttp() {
  uint32_t heap = ESP.getFreeHeap();
  if (heap >= JAVA_HEAP_MIN) {
    javaHeapLowSince = 0;
    return true;
  }
  if (javaHeapLowSince == 0) javaHeapLowSince = millis();
  Serial.print("Java: heap baixo ");
  Serial.println(heap);
  if (millis() - javaHeapLowSince > 5000UL) {
    javaResetHttpClient();
    javaClearQueue();
    javaHeapLowSince = millis();
  }
  return false;
}

static String jsonEscapeSimple(const char* in) {
  String out = "\"";
  if (!in) return "\"\"";
  for (const char* p = in; *p; p++) {
    char c = *p;
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

static String javaBaseUrl() {
  String scheme = javaUseTls() ? "https://" : "http://";
  if (javaUseTls() && mqttPort == 443) {
    return scheme + String(mqttHost);
  }
  return scheme + String(mqttHost) + ":" + String(mqttPort);
}

static bool javaBeginRequest(HTTPClient& http, const String& url, int timeoutMs) {
  ESP.wdtFeed();
  yield();
  if (!javaHeapOkForHttp()) return false;

  // Sempre fecha o socket anterior: keep-alive no ESP8266 trava o TLS.
  javaResetHttpClient();

  http.setTimeout(timeoutMs);
  http.setReuse(false);

  if (javaUseTls()) {
    javaTlsClient.setInsecure();
    javaTlsClient.setBufferSizes(1024, 512);
    javaTlsClient.setSession(&javaTlsSession);
    javaTlsClient.setTimeout(timeoutMs);
    return http.begin(javaTlsClient, url);
  }
  javaPlainClient.setTimeout(timeoutMs);
  return http.begin(javaPlainClient, url);
}

static String javaJsonUnescape(const String& s) {
  String out;
  out.reserve(s.length());
  for (unsigned i = 0; i < s.length(); i++) {
    if (s[i] == '\\' && i + 1 < s.length()) {
      char c = s[++i];
      if (c == 'n') out += '\n';
      else if (c == 'r') out += '\r';
      else if (c == 't') out += '\t';
      else out += c;
    } else {
      out += s[i];
    }
  }
  return out;
}

static bool javaExtractPayload(const String& resp, int payKey, String& payloadOut) {
  int payStart = resp.indexOf(':', payKey) + 1;
  while (payStart < (int)resp.length() && resp[payStart] == ' ') payStart++;
  if (payStart >= (int)resp.length()) return false;

  String payload;
  if (resp[payStart] == '"') {
    payStart++;
    int payEnd = payStart;
    while (payEnd < (int)resp.length()) {
      if (resp[payEnd] == '"' && resp[payEnd - 1] != '\\') break;
      payEnd++;
    }
    payload = javaJsonUnescape(resp.substring(payStart, payEnd));
  } else if (resp[payStart] == '{') {
    int depth = 0;
    int payEnd = payStart;
    for (; payEnd < (int)resp.length(); payEnd++) {
      if (resp[payEnd] == '{') depth++;
      else if (resp[payEnd] == '}') {
        depth--;
        if (depth == 0) { payEnd++; break; }
      }
    }
    payload = resp.substring(payStart, payEnd);
  } else {
    return false;
  }

  payload.trim();
  if (payload.length() == 0) return false;
  payloadOut = payload;
  return true;
}

static void javaDispatchPollItem(const String& typeRaw, const String& payload) {
  String type = typeRaw;
  type.replace("\"", "");
  type.trim();
  Serial.print("Java poll: type=");
  Serial.print(type);
  Serial.print(" payload=");
  Serial.println(payload);
  if (type == "cmd") handleRelayCommand(payload);
  else if (type == "config") handleConfigCommand(payload);
  else {
    Serial.println("Java poll: type desconhecido — ignorado.");
  }
}

bool javaGatewayConnected() {
  const char* tok = deviceToken[0] ? deviceToken : mqttPass;
  return javaGatewayReady && WiFi.status() == WL_CONNECTED && tok[0];
}

void javaQueueEvent(const char* type, const char* payload) {
  if (!type || !type[0]) return;

  // Com link ruim, telemetria so piora — poll precisa respirar.
  if (javaFailStreak > 0 || !javaLastPollOk) {
    if (strncmp(type, "logs/", 5) == 0 || strncmp(type, "stats/", 6) == 0) {
      return;
    }
  }

  const char* pay = payload ? payload : "";

  for (uint8_t n = 0; n < javaQLen; n++) {
    uint8_t i = (javaQHead + n) % JAVA_QMAX;
    if (strcmp(javaQType[i], type) == 0) {
      strncpy(javaQPay[i], pay, JAVA_QPAY - 1);
      javaQPay[i][JAVA_QPAY - 1] = 0;
      return;
    }
  }

  if (javaQLen >= JAVA_QMAX) {
    javaQHead = (javaQHead + 1) % JAVA_QMAX;
    javaQLen--;
  }
  uint8_t i = (javaQHead + javaQLen) % JAVA_QMAX;
  strncpy(javaQType[i], type, sizeof(javaQType[i]) - 1);
  javaQType[i][sizeof(javaQType[i]) - 1] = 0;
  strncpy(javaQPay[i], pay, JAVA_QPAY - 1);
  javaQPay[i][JAVA_QPAY - 1] = 0;
  javaQLen++;
}

bool javaPostEvent(const char* type, const char* payload) {
  if (!isProvisioned() || strlen(uuid) == 0) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const char* tok = deviceToken[0] ? deviceToken : mqttPass;
  if (!tok[0]) return false;

  ESP.wdtFeed();
  yield();

  HTTPClient http;
  String url = javaBaseUrl() + "/api/esp/" + String(uuid) + "/events";
  if (!javaBeginRequest(http, url, JAVA_HTTP_TIMEOUT_MS)) {
    javaMarkFail();
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-User-Secret", tok);
  http.addHeader("X-Device-Token", tok);
  http.addHeader("Connection", "close");

  String body = String("{\"type\":") + jsonEscapeSimple(type) + ",\"payload\":" +
                jsonEscapeSimple(payload ? payload : "") + "}";
  int code = http.POST(body);
  http.end();
  javaResetHttpClient();
  ESP.wdtFeed();
  yield();
  if (code < 200 || code >= 300) {
    Serial.print("Java POST ");
    Serial.print(type);
    Serial.print(" HTTP ");
    Serial.println(code);
    javaMarkFail();
    return false;
  }
  javaMarkSuccess();
  return true;
}

static void javaFlushOneEvent() {
  if (javaQLen == 0) return;
  // Link ruim: nao descarta state/ack — espera o poll voltar.
  if (!javaLastPollOk || javaFailStreak > 0) return;

  uint8_t i = javaQHead;
  char typeBuf[24];
  char payBuf[JAVA_QPAY];
  strncpy(typeBuf, javaQType[i], sizeof(typeBuf) - 1);
  typeBuf[sizeof(typeBuf) - 1] = 0;
  strncpy(payBuf, javaQPay[i], JAVA_QPAY - 1);
  payBuf[JAVA_QPAY - 1] = 0;
  javaQHead = (javaQHead + 1) % JAVA_QMAX;
  javaQLen--;
  javaPostEvent(typeBuf, payBuf);
  ESP.wdtFeed();
  yield();
}

void javaPollCommands() {
  const char* tok = deviceToken[0] ? deviceToken : mqttPass;
  if (!tok[0] || WiFi.status() != WL_CONNECTED) {
    javaLastPollOk = false;
    return;
  }

  HTTPClient http;
  String url = javaBaseUrl() + "/api/esp/" + String(uuid) + "/poll";
  if (!javaBeginRequest(http, url, JAVA_HTTP_TIMEOUT_MS)) {
    Serial.println("Java poll: begin falhou");
    javaMarkFail();
    return;
  }
  http.addHeader("X-User-Secret", tok);
  http.addHeader("X-Device-Token", tok);
  http.addHeader("Connection", "close");

  int code = http.GET();
  if (code != 200) {
    Serial.print("Java poll: HTTP ");
    Serial.println(code);
    http.end();
    javaResetHttpClient();
    javaMarkFail();
    return;
  }

  String resp = http.getString();
  http.end();
  javaResetHttpClient();

  int cmdArray = resp.indexOf("\"commands\"");
  if (cmdArray < 0) {
    if (resp.length() > 0 && resp.indexOf('{') < 0) {
      Serial.println("Java poll: resposta invalida");
      javaMarkFail();
    } else {
      javaMarkSuccess();
    }
    return;
  }
  javaMarkSuccess();

  int idx = cmdArray;
  String lastCmd;
  String lastCfg;
  while ((idx = resp.indexOf("\"type\"", idx)) >= 0) {
    int typeStart = resp.indexOf(':', idx) + 1;
    int typeEnd = resp.indexOf(',', typeStart);
    if (typeEnd < 0) typeEnd = resp.indexOf('}', typeStart);
    String type = resp.substring(typeStart, typeEnd);
    type.replace("\"", "");
    type.trim();
    if (type != "cmd" && type != "config") {
      idx = typeStart + 1;
      continue;
    }

    int payKey = resp.indexOf("\"payload\"", typeEnd);
    if (payKey < 0) break;

    String payload;
    if (javaExtractPayload(resp, payKey, payload)) {
      if (type == "cmd") lastCmd = payload;
      else lastCfg = payload;
    } else {
      Serial.println("Java poll: payload nao parseado");
    }

    idx = payKey + 8;
  }

  if (lastCmd.length()) javaDispatchPollItem("cmd", lastCmd);
  if (lastCfg.length()) javaDispatchPollItem("config", lastCfg);
  ESP.wdtFeed();
  yield();
  // Nao faz rajada de POSTs aqui: poll ja usou TLS. O loop() manda
  // 1 evento por ciclo (ack/state/status).
}

static void javaSoftRecover() {
  Serial.println("Java HTTP soft-reset");
  javaResetHttpClient();
  // Nao limpa state/ack — so telemetria envelhecida.
  javaFailStreak = 0;
  lastJavaPoll = 0;
  javaLastSoftResetMs = millis();
  if (javaSoftResetCount < 250) javaSoftResetCount++;
}

static void javaWifiRecover() {
  Serial.println("Java HTTP — reconectando Wi-Fi");
  javaResetHttpClient();
  javaClearQueue();
  javaLastWifiRecoverMs = millis();
  // Wi-Fi está OK mas o servidor não responde: não derruba a STA.
  // O loop() abre o AP <uuid>-setup para reconfigurar o host.
  if (javaServerUnreachable) {
    Serial.println("Servidor inacessível — mantendo Wi-Fi, abrindo AP <uuid>-setup.");
    return;
  }
  // Protecao se a energia estiver em D3/GPIO0: reconectar Wi-Fi com GPIO0
  // em LOW (rele ligado) arrisca reset em modo flash. Desliga antes.
  if (relayPin == 0 && relayLogicalOn) {
    Serial.println("Rele em GPIO0 (D3) ligado — desligando antes de recuperar Wi-Fi.");
    setAllOutputsOff();
    delay(300);
  }
  // forceWifiReconnect esta em V1_FIXO.ino
  forceWifiReconnect(true);
}

bool connectJavaGateway() {
  if (!isProvisioned()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const char* tok = deviceToken[0] ? deviceToken : mqttPass;
  Serial.print("espSecret len=");
  Serial.print(tok ? strlen(tok) : 0);
  Serial.print(" (deviceToken=");
  Serial.print(deviceToken[0] ? "sim" : "nao");
  Serial.println(")");

  javaResetHttpClient();
  javaClearQueue();
  javaQueueEvent("status", "online");
  javaQueueEvent("state", relayLogicalOn ? "0" : "1");
  javaGatewayReady = true;
  lastJavaPoll = 0;
  javaLastSuccessMs = millis();
  javaFailStreak = 0;
  javaSoftResetCount = 0;
  javaServerFailCount = 0;
  // NÃO limpa javaServerUnreachable aqui: só um poll HTTP 200 real
  // (javaMarkSuccess) pode derrubar o AP <uuid>-setup. Wi-Fi conectado
  // não significa servidor OK — senão o AP fecha com "falso positivo".
  javaServerOk = false;   // exige um poll HTTP 200 real antes de dar OK
  Serial.print("Java gateway OK em ");
  Serial.println(javaBaseUrl());
  return true;
}

void javaGatewayLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    javaGatewayReady = false;
    return;
  }

  unsigned long now = millis();
  if (javaLastSuccessMs == 0) javaLastSuccessMs = now;

  // AP de recuperação aberto (servidor rejeitando/inacessível): o usuário
  // precisa reconfigurar. Não fica batendo recovers (soft/wifi) — só um poll
  // esporádico para detectar quando o servidor voltar a responder (HTTP 200)
  // e o loop() fechar o AP.
  if (javaServerUnreachable) {
    if (now - lastJavaPoll >= JAVA_POLL_RECOVERY_MS) {
      javaPollCommands();
      lastJavaPoll = millis();
      ESP.wdtFeed();
      yield();
    }
    return;
  }

  if (now - javaLastSuccessMs > JAVA_SOFT_RESET_MS &&
      now - javaLastSoftResetMs > 5000UL) {
    javaSoftRecover();
  }

  if (now - javaLastSuccessMs > JAVA_WIFI_RECOVER_MS &&
      now - javaLastWifiRecoverMs > 20000UL) {
    javaWifiRecover();
    return;
  }

  if (now - javaLastSuccessMs > JAVA_HARD_RESTART_MS) {
    Serial.println("Java HTTP morto 120s — restart");
    delay(80);
    // safeRestart deixa GPIO0/2/15 em estado seguro antes do reset —
    // necessario se a energia (rele) estiver em D3/GPIO0, senao o boot
    // cai em modo flash e a telemetria morre.
    safeRestart();
    return;
  }

  unsigned long gap = javaLastPollOk ? JAVA_POLL_MS : JAVA_POLL_RETRY_MS;
  bool pollDue = (now - lastJavaPoll >= gap);

  if (pollDue) {
    javaPollCommands();
    lastJavaPoll = millis();
    ESP.wdtFeed();
    yield();
    // Se o poll nao trouxe cmd, ainda pode haver telemetria pendente.
    if (javaQLen > 0) javaFlushOneEvent();
    return;
  }

  if (javaQLen > 0) {
    javaFlushOneEvent();
  }
}
