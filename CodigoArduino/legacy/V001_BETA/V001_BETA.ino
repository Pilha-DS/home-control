// ============================================================================
//  HOME Sistema — MQTT sobre WebSocket Seguro (WSS) — Mosquitto
//  Versão: V001_BETA
//
//  Conversão do projeto original (Blynk) para MQTT.
//
//  • Broker:    wss://mosquito.omny.tec.br  (porta 443, path "/")
//  • Usuário:   cardoso
//  • Senha:     C6m8n4d2d3
//  • Reconexão: tenta se conectar a cada 10 segundos
//  • Config do broker MQTT SEPARADA da config do Wi-Fi (EEPROM independente,
//    uma alteração não afeta a outra)
//  • Página web de configuração protegida por SENHA DE ADMINISTRADOR
//    (padrão: C6m8n4d2d3) — necessária para mudar Wi-Fi, MQTT, UUID e a
//    própria senha de admin
//  • Pelo navegador é possível mudar a qualquer momento:
//       - UUID do dispositivo
//       - senha do Wi-Fi
//       - host/porta/usuário/senha/path do broker MQTT
//       - senha de administrador
//
//  COMO USAR:
//    1ª vez: o ESP8266 abre o AP "HOME_Lock" (senha C6m8n4d2d3).
//            Conecte-se ao AP e acesse http://192.168.4.1 — a tela inicial
//            explica o projeto e leva às telas de Wi-Fi, MQTT e Dispositivo.
//    Depois: acesse http://<ip-do-esp>/ no navegador.
//
//  TÓPICOS:
//    home/cmd                     recebe JSON com poke, time, device e pin
//    home/config                  recebe JSON de configuração (PASS obrigatório)
//    home/<uuid>/state            estado da saída (retido)
//    home/<uuid>/ack              confirmação do comando
//    home/<uuid>/config/ack       resposta da configuração (JSON)
//    home/<uuid>/status           online / offline (retido)
//    home/<uuid>/logs             logs
//    home/<uuid>/ip               IP local
//    home/<uuid>/stats/...        telemetria
//
//  COMANDO (publicar em home/cmd):
//    {"poke":1,"time":0,"device":"HOME1","pin":4}
//    poke 1=liga 0=desliga | time=ms até inverter (0=permanente)
//    device = UUID do dispositivo | pin = Dx do NodeMCU (1=D1 … 8=D8; padrão 4=D4)
//
//  CONFIG (publicar em home/config):
//    {"PASS":"C6m8n4d2d3","UUID":"NOVO","LOGICA_DO_RELE":1,"NEW_PASS":"nova"}
//    PASS = senha de administrador (obrigatória) | UUID = novo UUID
//    LOGICA_DO_RELE = 1 (HIGH liga) ou 2 (LOW liga) | NEW_PASS = nova senha admin
//
//  BIBLIOTECAS (Gerenciador de Bibliotecas do Arduino IDE):
//    1. WebSockets_Generic       (khoih-prog)
//    2. MQTTPubSubClient_Generic (khoih-prog)
//    3. WiFiManager              (tzapu)
// ============================================================================

// Bibliotecas
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <EEPROM.h>

// MQTT sobre WebSocket Seguro (WSS) — DEVE vir antes do MQTT
#include <WebSocketsClient_Generic.h>
#ifndef MQTTPUBSUBCLIENT_USE_WEBSOCKETS
#define MQTTPUBSUBCLIENT_USE_WEBSOCKETS true
#endif
#include <MQTTPubSubClient_Generic.h>

// ------------------------- Valores padrão -----------------------------------
// Usados na primeira gravação; depois podem ser alterados pela interface web
// (mantidos na EEPROM).
#define MQTT_DEFAULT_HOST   "mosquito.omny.tec.br"   // host do broker
#define MQTT_DEFAULT_PORT   443                      // WSS (WebSocket Seguro)
#define MQTT_DEFAULT_USER   "cardoso"                // usuário do broker
#define MQTT_DEFAULT_PASS   "C6m8n4d2d3"             // senha do broker
#define MQTT_DEFAULT_PATH   "/"                  // path do WebSocket (este broker usa "/")

#define ADMIN_DEFAULT_PASS  "C6m8n4d2d3"   // senha para mudar as configurações
#define AP_SSID             "HOME_Lock"   // nome do AP de configuração
#define AP_PASSWORD         "C6m8n4d2d3"   // senha do AP de configuração

#define MQTT_RETRY_MS       10000UL        // tenta reconectar a cada 10 s
#define MQTT_TOPIC_RELAY_CMD "home/cmd"   // comando centralizado (device no JSON)
#define MQTT_TOPIC_CONFIG    "home/config" // configuração via MQTT (PASS obrigatório)

// Saídas: "pin" no JSON = rótulo Dx do NodeMCU (1=D1 … 8=D8)
// Mapa GPIO: D1=5, D2=4, D3=0, D4=2, D5=14, D6=12, D7=13, D8=15
#define OUT_PIN_MIN 1
#define OUT_PIN_MAX 8
#define OUT_PIN_DEFAULT 4   // D4 (GPIO2) — LED padrão
const int OUT_GPIO[OUT_PIN_MAX + 1] = {
  -1, 5, 4, 0, 2, 14, 12, 13, 15
};

int relayPin = 2;       // GPIO ativo no momento (resolvido a partir de "pin")
int relayLogicalPin = OUT_PIN_DEFAULT;  // Dx lógico atual (1..8)
int buttonPin = 14;     // GPIO14 (D5) — botão 4 s (D1–D4 livres para saída)

// Polaridade do relé/LED: true = ativo-alto (HIGH liga), false = ativo-baixo (LOW liga, JQC3F)
bool relayActiveHigh = true;
bool relayLogicalOn = false;   // estado lógico atual (true = ligado)

// Tempos de conexão/desconexão
unsigned long connectedTime = 0;
unsigned long dailyConnectedTime[7] = {0};
unsigned long dailyDisconnectedTime[7] = {0};
unsigned long previousMillis = 0;
unsigned long previousSensorMillis = 0;
unsigned long previousPingMillis = 0;
unsigned long previousAvailabilityMillis = 0;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;

// Intervalos
const unsigned long intervalConnection = 120000;  // 2 min (tempos de conexão)
const unsigned long intervalSensors = 15000;      // 15 s (telemetria)
const unsigned long pingInterval = 20000;         // 20 s (teste de internet)
const unsigned long intervalAvailability = 60000; // 60 s (disponibilidade)

// Logs das últimas 24h
String logs[24];

// Monitoramento
unsigned long lastReconnectAttempt = 0;
unsigned long lastMqttAttempt = 0;
bool isConnected = false;
bool lastMqttState = false;
bool lastPingOk = true;   // estado anterior do teste de internet (para não repetir log)

// Agendamento do relé (comando JSON com "time" > 0)
bool relayScheduled = false;
unsigned long relayScheduleStart = 0;
unsigned long relayScheduleDuration = 0;
bool relayScheduleNextOn = false;   // estado a aplicar quando o tempo expirar

// ------------------------------ MQTT (WSS) ----------------------------------
WebSocketsClient wsClient;
MQTTPubSub::PubSubClient<512> mqtt;   // buffer 512 para os logs

// Configurações carregadas da EEPROM
char mqttHost[40] = MQTT_DEFAULT_HOST;
int  mqttPort     = MQTT_DEFAULT_PORT;
char mqttUser[30] = MQTT_DEFAULT_USER;
char mqttPass[30] = MQTT_DEFAULT_PASS;
char mqttPath[30] = MQTT_DEFAULT_PATH;

// Configurações do Wi-Fi (guardadas na EEPROM, usadas no boot)
char wifiSSID[40] = "";
char wifiPass[40] = "";

// Senha de administrador (protege as alterações de configuração)
char adminPass[30] = ADMIN_DEFAULT_PASS;

// UUID do dispositivo (identidade; usado no client ID e nos tópicos)
char uuid[40] = "";

// Buffer para montar os tópicos
char topicBuf[80];

// MQTT só pode ser consultado depois do mqtt.begin()
bool mqttReady = false;

// WiFiManager global (portal de configuração + servidor web)
WiFiManager wifiManager;

// ------------------------------- EEPROM -------------------------------------
// Endereços usados:
//   0      magic MQTT (0x56)
//   1..2   porta MQTT (uint16)
//   4..17  tempos diários (7 conectados + 7 desconectados)
//   100..  host do broker (40 bytes)
//   140..  usuário MQTT (30 bytes)
//   170..  senha MQTT (30 bytes)
//   200    magic Wi-Fi (0x57)
//   201..  SSID do Wi-Fi (40 bytes)
//   241..  senha do Wi-Fi (40 bytes)
//   281    magic senha admin (0x58)
//   282..  senha admin (30 bytes)
//   312    magic UUID (0x59)
//   313..  UUID (40 bytes)
//   353..  path do WebSocket (30 bytes)
//   390    magic polaridade relé (0x5A)
//   391    polaridade (1 = ativo-alto, 0 = ativo-baixo)

void writeStringEEPROM(int addr, const String& s, int maxLen) {
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, i < (int)s.length() ? s.charAt(i) : 0);
  }
}

// Copia uma String para um buffer char garantindo terminação nula
void copyStr(char* dst, size_t dstSize, const String& src) {
  strncpy(dst, src.c_str(), dstSize - 1);
  dst[dstSize - 1] = 0;
}

String readStringEEPROM(int addr, int maxLen) {
  String s;
  for (int i = 0; i < maxLen; i++) {
    char c = EEPROM.read(addr + i);
    if (c == 0) break;
    s += c;
  }
  return s;
}

// --- Configuração do broker MQTT (separada do Wi-Fi) ---
void saveMQTTConfig() {
  EEPROM.write(0, 0x56);
  EEPROM.write(1, mqttPort & 0xFF);
  EEPROM.write(2, (mqttPort >> 8) & 0xFF);
  writeStringEEPROM(100, String(mqttHost), 40);
  writeStringEEPROM(140, String(mqttUser), 30);
  writeStringEEPROM(170, String(mqttPass), 30);
  writeStringEEPROM(353, String(mqttPath), 30);
  EEPROM.commit();
  Serial.println("Configuração do broker salva na EEPROM.");
}

void loadMQTTConfig() {
  if (EEPROM.read(0) == 0x56) {
    int port = EEPROM.read(1) | (EEPROM.read(2) << 8);
    if (port > 0) mqttPort = port;
    String v = readStringEEPROM(100, 40);
    if (v.length() > 0) copyStr(mqttHost, sizeof(mqttHost), v);
    v = readStringEEPROM(140, 30);
    if (v.length() > 0) copyStr(mqttUser, sizeof(mqttUser), v);
    v = readStringEEPROM(170, 30);
    if (v.length() > 0) copyStr(mqttPass, sizeof(mqttPass), v);
    v = readStringEEPROM(353, 30);
    if (v.length() > 0) copyStr(mqttPath, sizeof(mqttPath), v);
    // Este broker (mosquito.omny.tec.br) usa path "/" — corrige /mqtt antigo
    if (strcmp(mqttPath, "/mqtt") == 0) {
      copyStr(mqttPath, sizeof(mqttPath), String("/"));
      saveMQTTConfig();
      Serial.println("Path MQTT corrigido: /mqtt -> /");
    }
  }
}

// --- Configuração do Wi-Fi (separada da do broker) ---
void saveWiFiConfig(const String& ssid, const String& pass) {
  EEPROM.write(200, 0x57);
  writeStringEEPROM(201, ssid, 40);
  writeStringEEPROM(241, pass, 40);
  EEPROM.commit();
  Serial.println("Configuração do Wi-Fi salva na EEPROM.");
}

void clearWiFiConfig() {
  EEPROM.write(200, 0);
  EEPROM.commit();
  Serial.println("Configuração do Wi-Fi apagada da EEPROM.");
}

void loadWiFiConfig() {
  if (EEPROM.read(200) == 0x57) {
    String s = readStringEEPROM(201, 40);
    String p = readStringEEPROM(241, 40);
    if (s.length() > 0) {
      copyStr(wifiSSID, sizeof(wifiSSID), s);
      copyStr(wifiPass, sizeof(wifiPass), p);
    }
  }
}

// --- Senha de administrador ---
void saveAdminConfig() {
  EEPROM.write(281, 0x58);
  writeStringEEPROM(282, String(adminPass), 30);
  EEPROM.commit();
  Serial.println("Senha de administrador salva na EEPROM.");
}

void loadAdminConfig() {
  if (EEPROM.read(281) == 0x58) {
    String v = readStringEEPROM(282, 30);
    if (v.length() > 0) copyStr(adminPass, sizeof(adminPass), v);
  }
}

// --- UUID do dispositivo ---
String sanitizeUUID(const String& in) {
  String out;
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (ok) out += c;
  }
  if (out.length() > 32) out = out.substring(0, 32);
  return out;
}

// Compara UUID sem diferenciar maiúsculas/minúsculas
bool uuidEqualsCI(const String& a, const char* b) {
  if ((int)a.length() != (int)strlen(b)) return false;
  for (unsigned int i = 0; i < a.length(); i++) {
    char ca = a.charAt(i);
    char cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = ca - 'a' + 'A';
    if (cb >= 'a' && cb <= 'z') cb = cb - 'a' + 'A';
    if (ca != cb) return false;
  }
  return true;
}

void saveUUID(const String& u) {
  EEPROM.write(312, 0x59);
  writeStringEEPROM(313, u, 40);
  EEPROM.commit();
  Serial.println("UUID salvo na EEPROM.");
}

void loadUUID() {
  if (EEPROM.read(312) == 0x59) {
    String v = readStringEEPROM(313, 40);
    if (v.length() > 0) copyStr(uuid, sizeof(uuid), v);
  }
  if (strlen(uuid) == 0) {
    snprintf(uuid, sizeof(uuid), "HOME_%08X", (uint32_t)ESP.getChipId());
    saveUUID(String(uuid));
    Serial.print("UUID gerado: ");
    Serial.println(uuid);
  }
}

// --- Polaridade do relé/LED (ativo-alto ou ativo-baixo) ---
void saveRelayPolarity() {
  EEPROM.write(390, 0x5A);
  EEPROM.write(391, relayActiveHigh ? 1 : 0);
  EEPROM.commit();
  Serial.print("Polaridade do relé salva na EEPROM: ");
  Serial.println(relayActiveHigh ? "ativo-alto" : "ativo-baixo");
}

void loadRelayPolarity() {
  if (EEPROM.read(390) == 0x5A) {
    relayActiveHigh = (EEPROM.read(391) == 1);
  } else {
    relayActiveHigh = true;   // padrão: LED ativo-alto
    saveRelayPolarity();
  }
  Serial.print("Polaridade: ");
  Serial.print(relayActiveHigh ? "ativo-alto (HIGH liga)" : "ativo-baixo (LOW liga)");
  Serial.print(" | Pino GPIO");
  Serial.print(relayPin);
  Serial.println(" (D4)");
  if (!relayActiveHigh) {
    Serial.println("AVISO: LED comum precisa 'Ativo-alto' na tela Dispositivo!");
  }
}

// ------------------------------- Helpers MQTT -------------------------------
void buildTopic(const char* suffix) {
  snprintf(topicBuf, sizeof(topicBuf), "home/%s/%s", uuid, suffix);
}

void publishMQTT(const char* suffix, const char* msg, bool retained = false) {
  if (mqttReady && mqtt.isConnected()) {
    buildTopic(suffix);
    mqtt.publish(topicBuf, msg, retained);
  }
}

bool mqttConnected() {
  return mqttReady && mqtt.isConnected();
}

void sendLogsMQTT(String logMessage) {
  // Publica apenas a mensagem nova (em vez do histórico acumulado inteiro)
  publishMQTT("logs", logMessage.c_str());
  // Mantém o buffer interno das últimas 24h
  for (int i = 0; i < 23; i++) logs[i] = logs[i + 1];
  logs[23] = logMessage;
}

// ------------------------- Controle do relé (com JSON) -----------------------
// Publicar em home/cmd:
//   {"poke":1,"time":0,"device":"HOME1","pin":4}
// pin = Dx do NodeMCU (1..8). Sem pin → D4.

// Converte número lógico Dx (1..8) → GPIO; -1 se inválido
int logicalPinToGpio(int logicalPin) {
  if (logicalPin < OUT_PIN_MIN || logicalPin > OUT_PIN_MAX) return -1;
  return OUT_GPIO[logicalPin];
}

void selectOutputPin(int logicalPin) {
  int gpio = logicalPinToGpio(logicalPin);
  if (gpio < 0) return;
  relayLogicalPin = logicalPin;
  relayPin = gpio;
  pinMode(relayPin, OUTPUT);
}

void initAllOutputPins() {
  for (int p = OUT_PIN_MIN; p <= OUT_PIN_MAX; p++) {
    int gpio = OUT_GPIO[p];
    if (gpio < 0 || gpio == buttonPin) continue;
    pinMode(gpio, OUTPUT);
    if (relayActiveHigh) digitalWrite(gpio, LOW);
    else digitalWrite(gpio, HIGH);
  }
  selectOutputPin(OUT_PIN_DEFAULT);
}

void setAllOutputsOff() {
  for (int p = OUT_PIN_MIN; p <= OUT_PIN_MAX; p++) {
    int gpio = OUT_GPIO[p];
    if (gpio < 0 || gpio == buttonPin) continue;
    if (relayActiveHigh) digitalWrite(gpio, LOW);
    else digitalWrite(gpio, HIGH);
  }
  relayLogicalOn = false;
}

// Acha a posição logo após a chave (case-insensitive). Tolerante a "_" ou
// espaço na chave (ex.: "LOGICA_DO_RELE" acha "LOGICA DO RELE"). Retorna -1 se não achar.
int jsonFindKeyCI(const String& json, const char* key) {
  String jLower = json;
  jLower.toLowerCase();
  String kLower = key;
  kLower.toLowerCase();

  // Variação 1: exatamente como enviada; Variação 2: trocando _ por espaço
  for (int v = 0; v < 2; v++) {
    String k = kLower;
    if (v == 1) k.replace('_', ' ');
    // Preferir "chave" com aspas (evita achar "pin" no meio de outra palavra)
    String quoted = "\"" + k + "\"";
    int p = jLower.indexOf(quoted);
    if (p >= 0) return p + quoted.length();
    p = jLower.indexOf(k);
    if (p >= 0) return p + k.length();
  }
  return -1;
}

// Extrai inteiro de chave JSON (busca sem diferenciar maiúsculas/minúsculas)
long jsonGetIntCI(const String& json, const char* key) {
  int p = jsonFindKeyCI(json, key);
  if (p < 0) return -1;
  while (p < (int)json.length() &&
         (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n' ||
          json[p] == ':' || json[p] == '"')) p++;
  if (p >= (int)json.length()) return -1;
  bool neg = false;
  if (json[p] == '-') { neg = true; p++; }
  long v = 0;
  bool any = false;
  while (p < (int)json.length()) {
    char c = json.charAt(p);
    if (c >= '0' && c <= '9') { v = v * 10 + (c - '0'); any = true; p++; }
    else break;
  }
  if (!any) return -1;
  return neg ? -v : v;
}

// Extrai string entre aspas de uma chave JSON ("device":"HOME_...")
String jsonGetStringCI(const String& json, const char* key) {
  int p = jsonFindKeyCI(json, key);
  if (p < 0) return "";

  // Pula espaços / ':' até a aspas de abertura do valor
  while (p < (int)json.length()) {
    char c = json.charAt(p);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ':') { p++; continue; }
    break;
  }
  if (p >= (int)json.length() || json.charAt(p) != '"') return "";
  p++;
  String out;
  while (p < (int)json.length()) {
    char c = json.charAt(p);
    if (c == '"') break;
    if (c == '\\' && p + 1 < (int)json.length()) { p++; c = json.charAt(p); }
    out += c;
    p++;
  }
  return out;
}

// Aplica o estado físico do relé/LED respeitando a polaridade configurada
void setRelayOn(bool on) {
  if (relayActiveHigh) {
    digitalWrite(relayPin, on ? HIGH : LOW);   // ativo-alto: HIGH liga
  } else {
    digitalWrite(relayPin, on ? LOW : HIGH);   // ativo-baixo (JQC3F): LOW liga
  }
}

// Aplica o estado do relé e publica o resultado (0 = ligado, 1 = desligado)
void applyRelayState(bool on) {
  relayLogicalOn = on;
  setRelayOn(on);
  Serial.print("D");
  Serial.print(relayLogicalPin);
  Serial.print(" (GPIO");
  Serial.print(relayPin);
  Serial.print(") = ");
  Serial.println(digitalRead(relayPin) ? "HIGH" : "LOW");
  Serial.print("Relé/LED: ");
  Serial.println(on ? "LIGADO" : "DESLIGADO");
  char stateMsg[24];
  snprintf(stateMsg, sizeof(stateMsg), "%d:pin=%d", on ? 0 : 1, relayLogicalPin);
  publishMQTT("state", stateMsg, true);
  sendLogsMQTT(on ? "Relé LIGADO." : "Relé DESLIGADO.");
}

// Pisca o LED padrão (D4) 3 vezes no boot
void blinkRelaySelfTest() {
  selectOutputPin(OUT_PIN_DEFAULT);
  Serial.println("Teste D4 (GPIO2): 3 piscadas...");
  for (int i = 0; i < 3; i++) {
    setRelayOn(true);
    delay(250);
    setRelayOn(false);
    delay(250);
  }
  relayLogicalOn = false;
  Serial.println("Teste concluido.");
}

void handleRelayCommand(String msg) {
  msg.trim();

  int jsonStart = msg.indexOf('{');
  if (jsonStart > 0) msg = msg.substring(jsonStart);

  Serial.print("Comando home/cmd: ");
  Serial.println(msg);

  if (!msg.startsWith("{")) {
    Serial.println("Formato invalido — use JSON com poke, time, device e pin.");
    return;
  }

  // Filtra pelo UUID: só executa se "device" for este dispositivo
  String dev = sanitizeUUID(jsonGetStringCI(msg, "device"));
  if (dev.length() == 0) {
    Serial.println("JSON sem campo 'device' — ignorado.");
    return;
  }
  if (!uuidEqualsCI(dev, uuid)) {
    Serial.print("Comando para ");
    Serial.print(dev);
    Serial.print(" (meu UUID: ");
    Serial.print(uuid);
    Serial.println(") — ignorado. Use o UUID do Serial ou da pagina /device.");
    return;
  }

  long poke = jsonGetIntCI(msg, "poke");
  long tm = jsonGetIntCI(msg, "time");
  long pinArg = jsonGetIntCI(msg, "pin");
  if (tm < 0) tm = 0;
  if (poke != 0 && poke != 1) {
    Serial.println("JSON invalido: poke deve ser 0 ou 1.");
    publishMQTT("ack", "erro:poke_invalido");
    return;
  }

  int logicalPin = OUT_PIN_DEFAULT;
  if (pinArg >= 0) {
    if (logicalPinToGpio((int)pinArg) < 0) {
      Serial.println("JSON invalido: pin deve ser 1..8 (Dx do NodeMCU).");
      publishMQTT("ack", "erro:pin_invalido");
      return;
    }
    logicalPin = (int)pinArg;
  }
  // Não usar o pino do botão como saída
  if (logicalPinToGpio(logicalPin) == buttonPin) {
    Serial.println("pin conflita com o botao (D5) — escolha outro.");
    publishMQTT("ack", "erro:pin_botao");
    return;
  }
  selectOutputPin(logicalPin);

  bool on = (poke == 1);
  unsigned long t = (unsigned long)tm;
  Serial.print("Parse: device=");
  Serial.print(dev);
  Serial.print(" poke=");
  Serial.print(poke);
  Serial.print(" time=");
  Serial.print(tm);
  Serial.print(" pin=D");
  Serial.print(logicalPin);
  Serial.print(" (GPIO");
  Serial.print(relayPin);
  Serial.println(")");

  // Cancela agendamento anterior e aplica agora
  relayScheduled = false;
  applyRelayState(on);

  if (t > 0) {
    relayScheduled = true;
    relayScheduleStart = millis();
    relayScheduleDuration = t;
    relayScheduleNextOn = !on;
    Serial.print("Agendado: inverter D");
    Serial.print(logicalPin);
    Serial.print(" em ");
    Serial.print(t);
    Serial.println(" ms.");
  }

  // Confirma no broker que o comando foi processado
  char ack[64];
  snprintf(ack, sizeof(ack), "ok:poke=%d,time=%lu,pin=%d,gpio=%s",
           on ? 1 : 0, t, logicalPin, digitalRead(relayPin) ? "HIGH" : "LOW");
  publishMQTT("ack", ack);
}

void onRelayCommand(const char* payload, const size_t size) {
  // Deduplica: a lib chama callback global E o do tópico no mesmo pacote
  static unsigned long lastMs = 0;
  static uint32_t lastHash = 0;
  uint32_t h = (uint32_t)size * 2654435761u;
  for (size_t i = 0; i < size; i++) h = (h * 131) + (uint8_t)payload[i];
  unsigned long now = millis();
  if (h == lastHash && (now - lastMs) < 80) return;
  lastMs = now;
  lastHash = h;

  String msg;
  msg.reserve(size + 1);
  for (size_t i = 0; i < size; i++) msg += payload[i];
  handleRelayCommand(msg);
}

// ------------------- Configuração via MQTT (home/config) -------------------
// JSON aceito:
//   {"PASS":"senha_admin","UUID":"NOVO","LOGICA_DO_RELE":1,"NEW_PASS":"nova"}
// PASS é obrigatório (senha de administrador) — sem ela nada muda.
// LOGICA_DO_RELE: 1 = HIGH liga (padrão), 2 = LOW liga.
// Resposta em home/<uuid>/config/ack (JSON).

void publishConfigAck(const String& json) {
  publishMQTT("config/ack", json.c_str(), true);
  Serial.print("home/config -> ");
  Serial.println(json);
}

void handleConfigCommand(String msg) {
  msg.trim();
  int jsonStart = msg.indexOf('{');
  if (jsonStart > 0) msg = msg.substring(jsonStart);

  Serial.print("Comando home/config: ");
  Serial.println(msg);

  if (!msg.startsWith("{")) {
    publishConfigAck(F("{\"ok\":false,\"erro\":\"json_invalido\"}"));
    return;
  }

  // 1) Autenticação — senha de administrador
  String pass = jsonGetStringCI(msg, "PASS");
  if (pass.length() == 0 || pass != String(adminPass)) {
    Serial.println("home/config: senha admin incorreta — ignorado.");
    publishConfigAck(F("{\"ok\":false,\"erro\":\"senha_invalida\"}"));
    return;
  }

  String ack = "{\"ok\":true";
  bool changed = false;

  // 2) Novo UUID (opcional)
  String newUuid = sanitizeUUID(jsonGetStringCI(msg, "UUID"));
  if (newUuid.length() > 0 && !uuidEqualsCI(newUuid, uuid)) {
    copyStr(uuid, sizeof(uuid), newUuid);
    saveUUID(String(uuid));
    ack += ",\"uuid\":\"" + String(uuid) + "\"";
    changed = true;
  }

  // 3) Lógica do relé/LED (opcional) — 1 = HIGH liga, 2 = LOW liga
  long logica = jsonGetIntCI(msg, "LOGICA_DO_RELE");
  if (logica < 0) logica = jsonGetIntCI(msg, "LOGICA");
  if (logica == 1 || logica == 2) {
    bool novo = (logica == 1);
    if (novo != relayActiveHigh) {
      relayActiveHigh = novo;
      saveRelayPolarity();
      setAllOutputsOff();
      ack += ",\"logica\":" + String(logica);
      changed = true;
    }
  }

  // 4) Nova senha de administrador (opcional)
  String newPass = jsonGetStringCI(msg, "NEW_PASS");
  if (newPass.length() > 0) {
    copyStr(adminPass, sizeof(adminPass), newPass);
    saveAdminConfig();
    ack += ",\"new_pass\":true";
    changed = true;
  }

  if (changed) {
    ack += ",\"aplicado\":true}";
    publishConfigAck(ack);
    delay(600);      // dá tempo do broker entregar o ack
    ESP.restart();   // reaplica UUID/senha em todos os fluxos
  } else {
    publishConfigAck(F("{\"ok\":true,\"sem_alteracoes\":true}"));
  }
}

void onConfigCommand(const char* payload, const size_t size) {
  // Deduplica: a lib chama callback global E o do tópico no mesmo pacote
  static unsigned long lastMs = 0;
  static uint32_t lastHash = 0;
  uint32_t h = (uint32_t)size * 2654435761u;
  for (size_t i = 0; i < size; i++) h = (h * 131) + (uint8_t)payload[i];
  unsigned long now = millis();
  if (h == lastHash && (now - lastMs) < 80) return;
  lastMs = now;
  lastHash = h;

  String msg;
  msg.reserve(size + 1);
  for (size_t i = 0; i < size; i++) msg += payload[i];
  handleConfigCommand(msg);
}

// Callback global — backup (a lib também chama o callback do tópico)
void onGlobalMqtt(const char* topic, const char* payload, const size_t size) {
  String t = topic;
  t.trim();
  Serial.print("MQTT [");
  Serial.print(t);
  Serial.print("] (");
  Serial.print(size);
  Serial.println(" bytes)");
  if (t == MQTT_TOPIC_RELAY_CMD) {
    onRelayCommand(payload, size);
  } else if (t == MQTT_TOPIC_CONFIG) {
    onConfigCommand(payload, size);
  }
}

// Traduz erros MQTT para texto legível no Serial Monitor
String mqttErrorText(int err) {
  switch (err) {
    case 0:   return "OK";
    case -1:  return "BUFFER_TOO_SHORT";
    case -4:  return "NETWORK_TIMEOUT";
    case -9:  return "PACOTE_INVALIDO (path WS errado? tente / )";
    case -10: return "CONEXAO_NEGADA";
    default:  return String("erro ") + String(err);
  }
}

String mqttReturnText(int code) {
  switch (code) {
    case 0: return "aceito";
    case 1: return "protocolo invalido";
    case 2: return "client ID rejeitado";
    case 3: return "servidor indisponivel";
    case 4: return "usuario/senha errados";
    case 5: return "nao autorizado";
    case 6: return "resposta invalida (sem CONNACK — path WS?)";
    default: return String("codigo ") + String(code);
  }
}

// Conecta ao broker MQTT (handshake sobre o WebSocket já estabelecido)
bool connectMQTT() {
  if (!wsClient.isConnected()) return false;

  String clientId = String(uuid);
  if (clientId.length() == 0) {
    clientId = String("HOME_") + String((uint32_t)ESP.getChipId(), HEX);
  }

  buildTopic("status");
  mqtt.setWill(topicBuf, "offline", true, 0);   // avisa o broker se cair

  bool ok = mqtt.connect(clientId.c_str(), mqttUser, mqttPass);
  if (ok) {
    Serial.print("MQTT conectado via WSS: ");
    Serial.print(mqttHost);
    Serial.print(":");
    Serial.print(mqttPort);
    Serial.println(mqttPath);

    Serial.print("Assinando topicos: ");
    Serial.print(MQTT_TOPIC_RELAY_CMD);
    Serial.print(" e ");
    Serial.println(MQTT_TOPIC_CONFIG);
    // Callback do tópico (não deixar vazio — era a causa de comando “sumir”)
    bool subOk = mqtt.subscribe(String(MQTT_TOPIC_RELAY_CMD),
      [](const char* payload, const size_t size) {
        onRelayCommand(payload, size);
      });
    bool subOkCfg = mqtt.subscribe(String(MQTT_TOPIC_CONFIG),
      [](const char* payload, const size_t size) {
        onConfigCommand(payload, size);
      });
    Serial.print("Subscribe home/cmd: ");
    Serial.println(subOk ? "OK" : "FALHOU");
    Serial.print("Subscribe home/config: ");
    Serial.println(subOkCfg ? "OK" : "FALHOU");
    Serial.print("UUID deste ESP (use em device): ");
    Serial.println(uuid);

    buildTopic("status");
    mqtt.publish(topicBuf, "online", true);
    buildTopic("ip");
    mqtt.publish(topicBuf, WiFi.localIP().toString().c_str(), true);
    buildTopic("state");
    mqtt.publish(topicBuf, relayLogicalOn ? "0" : "1", true);
    publishMQTT("ack", "esp_pronto");
    Serial.println("Publicado home/<uuid>/ack=esp_pronto (ESP ouvindo comandos)");
  } else {
    Serial.println("--- FALHA MQTT ---");
    Serial.print("URL: wss://");
    Serial.print(mqttHost);
    Serial.print(":");
    Serial.print(mqttPort);
    Serial.println(mqttPath);
    Serial.print("lastError: ");
    Serial.println(mqttErrorText(mqtt.getLastError()));
    Serial.print("returnCode: ");
    Serial.println(mqttReturnText(mqtt.getReturnCode()));
    Serial.println("Dica: path WebSocket deste broker e / (nao /mqtt)");
  }
  return ok;
}

void updateConnectionStatus() {
  if (WiFi.status() == WL_CONNECTED && mqttConnected()) {
    publishMQTT("status", "online", true);
  } else {
    publishMQTT("status", "offline", true);
  }
}

// ------------------------- Página web de configuração -----------------------
// A configuração é feita pelo navegador e protegida pela senha de admin.
// O Wi-Fi e o broker MQTT ficam em áreas SEPARADAS da EEPROM.

String htmlEscape(const String& s) {
  String out;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

// Escapa apenas o que é necessário para um campo JSON (aspas e barra invertida)
String jsonEscape(const String& s) {
  String out;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      default:   out += c;
    }
  }
  return out;
}

// CSS comum das páginas — fica em flash (PROGMEM) para economizar RAM
const char PAGE_CSS[] PROGMEM =
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;"
  "background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);color:#e8e8f0;"
  "min-height:100vh;padding:20px}"
  ".wrap{max-width:520px;margin:0 auto}"
  ".topbar{display:flex;align-items:center;justify-content:space-between;margin-bottom:16px}"
  ".logo{font-size:20px;font-weight:800;letter-spacing:1px;"
  "background:linear-gradient(90deg,#6ee7b7,#60a5fa);-webkit-background-clip:text;"
  "-webkit-text-fill-color:transparent}"
  ".badge{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.12);"
  "border-radius:20px;padding:4px 12px;font-size:12px;color:#c4c8d4}"
  ".nav{display:flex;gap:8px;margin-bottom:18px;flex-wrap:wrap}"
  ".nav a{flex:1;text-align:center;padding:10px 6px;border-radius:10px;"
  "background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.1);"
  "color:#c4c8d4;text-decoration:none;font-size:13px;min-width:70px;transition:.2s}"
  ".nav a.on{background:rgba(59,130,246,.25);border-color:#60a5fa;color:#93c5fd}"
  ".card{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.1);"
  "border-radius:16px;padding:18px;margin-bottom:14px;transition:.2s}"
  "a.card{display:block;text-decoration:none}"
  "a.card:hover{border-color:#60a5fa;background:rgba(59,130,246,.08)}"
  "h1{font-size:20px;margin-bottom:6px;font-weight:700}"
  "a.card h1{font-size:16px}"
  ".sub{color:#9ca3af;font-size:13px;line-height:1.55;margin-bottom:8px}"
  "label{display:block;font-size:13px;color:#c4c8d4;margin:12px 0 4px}"
  "input{width:100%;padding:11px 12px;border-radius:10px;"
  "border:1px solid rgba(255,255,255,.15);background:rgba(0,0,0,.25);color:#fff;"
  "font-size:15px;outline:none}"
  "input:focus{border-color:#60a5fa}"
  "select{width:100%;padding:11px 12px;border-radius:10px;"
  "border:1px solid rgba(255,255,255,.15);background:rgba(0,0,0,.25);color:#fff;"
  "font-size:15px;outline:none;appearance:none}"
  "select:focus{border-color:#60a5fa}"
  ".btn{display:block;width:100%;padding:13px;border:none;border-radius:12px;"
  "background:linear-gradient(90deg,#10b981,#3b82f6);color:#fff;font-size:15px;"
  "font-weight:600;cursor:pointer;margin-top:16px}"
  ".btn:hover{filter:brightness(1.12)}"
  ".btn.secondary{background:rgba(255,255,255,.1);color:#e8e8f0;margin-top:8px}"
  ".status{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:10px}"
  ".pill{background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.1);"
  "border-radius:20px;padding:6px 12px;font-size:12px;color:#c4c8d4}"
  ".pill b{color:#6ee7b7}"
  ".hint{font-size:12px;color:#9ca3af;margin-top:6px;line-height:1.5}"
  ".ok{color:#6ee7b7}.err{color:#f87171}"
  ".wifi-item{display:flex;align-items:center;gap:10px;padding:12px;"
  "border-radius:10px;background:rgba(0,0,0,.2);border:1px solid rgba(255,255,255,.08);"
  "margin-bottom:8px;cursor:pointer;transition:.15s}"
  ".wifi-item:hover{border-color:#60a5fa;background:rgba(59,130,246,.12)}"
  ".wifi-item .ssid{flex:1;font-size:14px;font-weight:500}"
  ".wifi-item .rssi{font-size:12px;color:#9ca3af}"
  ".wifi-item .lock{font-size:11px;color:#93c5fd;font-style:normal}"
  "#net{margin-top:12px}"
  ".titlebar{display:flex;align-items:center;justify-content:space-between;margin-bottom:4px}"
  ".back{color:#93c5fd;text-decoration:none;font-size:13px}"
  ".hero{text-align:center;padding:28px 0 20px}"
  ".hero .logo-lg{font-size:34px;font-weight:800;letter-spacing:2px;"
  "background:linear-gradient(90deg,#6ee7b7,#60a5fa);-webkit-background-clip:text;"
  "-webkit-text-fill-color:transparent}"
  ".hero p{color:#9ca3af;font-size:13px;margin-top:6px}"
  ".menu{display:flex;flex-direction:column;gap:10px;margin-top:14px}"
  ".menu a{display:flex;align-items:center;gap:12px;padding:14px 16px;"
  "border-radius:14px;background:rgba(255,255,255,.05);"
  "border:1px solid rgba(255,255,255,.08);color:#e8e8f0;text-decoration:none;"
  "font-size:15px;transition:.15s}"
  ".menu a:hover{border-color:#60a5fa;background:rgba(59,130,246,.1)}"
  ".menu .ico{width:34px;height:34px;border-radius:10px;display:flex;align-items:center;"
  "justify-content:center;font-size:11px;font-weight:700;"
  "background:rgba(59,130,246,.18);color:#93c5fd}";

// Cache do scan Wi-Fi (evita bloquear o servidor web a cada /scan)
String wifiScanJson;

// Envia HTML — o ESP8266WebServer (core 3.1.2) já faz o streaming internamente,
// com yield() e timeout corretos. Enviar em um único send() é mais confiável
// do que o envio manual em pedaços, que truncava a página quando a RAM estava
// apertada (header Content-Length prometia mais bytes do que eram enviados).
void sendHtml(const String& html) {
  wifiManager.server->send(200, "text/html", html);
}

// JS da pagina Wi-Fi — em flash para nao estourar RAM ao montar a pagina
const char WIFI_SCAN_JS[] PROGMEM =
  "<script>"
  "function setSsid(el){document.getElementById('wifi_ssid').value="
  "el.querySelector('.ssid').textContent;}"
  "function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;')"
  ".replace(/>/g,'&gt;');}"
  "async function scan(){var el=document.getElementById('net');"
  "el.innerHTML='<p class=\"hint\">Buscando redes...</p>';"
  "try{var r=await fetch('/scan');var d=await r.json();"
  "if(d.scanning){setTimeout(scan,800);return;}"
  "if(!d.networks||!d.networks.length){"
  "el.innerHTML='<p class=\"hint\">Nenhuma rede encontrada.</p>';return;}"
  "var h='';for(var i=0;i<d.networks.length;i++){var n=d.networks[i];"
  "h+='<div class=\"wifi-item\" onclick=\"setSsid(this)\">"
  "<span class=\"ssid\">'+esc(n.s)+'</span>"
  "<span class=\"rssi\">'+n.r+' dBm</span>"
  "<span class=\"lock\">'+(n.l?'protegida':'aberta')+'</span></div>';}"
  "el.innerHTML=h;}catch(e){"
  "el.innerHTML='<p class=\"hint\">Erro ao buscar redes.</p>';}}"
  "scan();"
  "</script>";

// Abre a página com o visual padrão e a navegação entre as telas.
// Com minimal = true, omite a barra superior e as abas (usado na tela inicial).
String pageTop(const String& title, const char* active, bool minimal = false) {
  String h = String(F("<!DOCTYPE html><html><head><meta charset='utf-8'>"));
  h += F("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  h += F("<title>"); h += title; h += F("</title><style>");
  h += FPSTR(PAGE_CSS);
  h += F("</style></head><body><div class='wrap'>");
  if (!minimal) {
    h += F("<div class='topbar'><div class='logo'>HOME</div><div class='badge'>");
    h += htmlEscape(String(uuid));
    h += F("</div></div><div class='nav'>");
    h += F("<a href='/'");  if (strcmp(active, "/") == 0) h += F(" class='on'");
    h += F(">Início</a><a href='/wifi'"); if (strcmp(active, "/wifi") == 0) h += F(" class='on'");
    h += F(">Wi-Fi</a><a href='/mqtt'"); if (strcmp(active, "/mqtt") == 0) h += F(" class='on'");
    h += F(">MQTT</a><a href='/device'"); if (strcmp(active, "/device") == 0) h += F(" class='on'");
    h += F(">Dispositivo</a></div>");
  }
  return h;
}

String pageBottom() {
  return String(F("</div></body></html>"));
}

String errorPage(const String& msg) {
  String h = pageTop("Erro", "/");
  h += F("<h1 class='err'>Algo deu errado</h1>");
  h += F("<div class='card'><p style='font-size:14px'>");
  h += msg;
  h += F("</p></div><a href='/' class='btn secondary'>Voltar ao início</a>");
  h += pageBottom();
  return h;
}

String okPage(const String& msg) {
  String h = pageTop("Salvo", "/");
  h += F("<h1 class='ok'>Configurações salvas</h1>");
  h += F("<div class='card'><p style='font-size:14px'>");
  h += msg;
  h += F("</p><p class='hint'>O dispositivo será reiniciado agora. Se o Wi-Fi "
         "mudou, o IP também pode mudar — verifique o roteador ou conecte no "
         "ponto de acesso HOME_Lock (192.168.4.1).</p></div>");
  h += pageBottom();
  return h;
}

// ------------------------------- Tela inicial --------------------------------
// Minimalista: título, uma linha de descrição e os atalhos de navegação.
void handleHome() {
  String html = pageTop("HOME", "/", true);

  html += F("<div class='hero'>");
  html += F("<div class='logo-lg'>HOME</div>");
  html += F("<p>Controle do relé via MQTT</p>");
  html += F("</div>");

  html += F("<div class='menu'>");
  html += F("<a href='/wifi'><span class='ico'>Wi</span>Wi-Fi</a>");
  html += F("<a href='/mqtt'><span class='ico'>MQ</span>Broker MQTT</a>");
  html += F("<a href='/device'><span class='ico'>ID</span>Dispositivo</a>");
  html += F("</div>");

  html += pageBottom();
  sendHtml(html);
}

// ------------------------------ Tela de Wi-Fi --------------------------------
// Lista as redes próximas (via /scan) e deixa clicar para preencher o SSID.
void handleWifiPage() {
  String html = pageTop("Wi-Fi", "/wifi");
  html.reserve(2200);
  html += F("<h1>Configurar Wi-Fi</h1>");
  html += F("<p class='sub'>A lista abaixo é atualizada automaticamente. Toque "
            "na rede desejada para preencher o campo.</p>");

  if (strlen(wifiPass) == 0) {
    html += F("<div class='card' style='border-color:#f87171'>"
              "<p style='color:#f87171;font-size:13px'>Não há senha salva — "
              "informe a senha da rede para o dispositivo conectar.</p></div>");
  }

  html += F("<div class='card'>");
  html += F("<form method='POST' action='/save'>");
  html += F("<input type='hidden' name='section' value='wifi'>");
  html += F("<label>Rede (SSID)</label>");
  html += F("<input name='wifi_ssid' id='wifi_ssid' value='");
  html += htmlEscape(String(wifiSSID));
  html += F("' placeholder='Escolha na lista ou digite'>");
  html += F("<div id='net'><p class='hint'>Buscando redes próximas...</p></div>");
  html += F("<label>Senha da rede</label>");
  html += F("<input type='password' name='wifi_pass' placeholder='Deixe vazio para manter a atual'>");
  html += F("<label>Senha de administrador</label>");
  html += F("<input type='password' name='admin' required placeholder='Necessária para salvar'>");
  html += F("<button type='button' class='btn secondary' onclick='scan()'>"
            "Buscar redes de novo</button>");
  html += F("<input type='submit' class='btn' value='Salvar e reiniciar'>");
  html += F("</form></div>");
  html += FPSTR(WIFI_SCAN_JS);
  html += pageBottom();
  sendHtml(html);
}

// ---------------------------- Telas MQTT e Dispositivo ----------------------
void handleMqttPage() {
  String html = pageTop("MQTT — HOME", "/mqtt");
  html += F("<h1>Broker MQTT</h1>");
  html += F("<p class='sub'>Configuração SEPARADA do Wi-Fi — um erro aqui não "
            "afeta a conexão com a internet.</p>");
  html += F("<div class='card'>");
  html += F("<form method='POST' action='/save'>");
  html += F("<input type='hidden' name='section' value='mqtt'>");
  html += F("<label>Host do broker</label>");
  html += F("<input name='mhost' value='");
  html += htmlEscape(String(mqttHost));
  html += F("' placeholder='ex.: wss://broker.exemplo.com'>");
  html += F("<label>Porta</label>");
  html += F("<input name='mport' value='");
  html += String(mqttPort);
  html += F("'>");
  html += F("<label>Usuário</label>");
  html += F("<input name='muser' value='");
  html += htmlEscape(String(mqttUser));
  html += F("'>");
  html += F("<label>Senha do broker</label>");
  html += F("<input type='password' name='mpass' placeholder='Deixe vazio para manter a atual'>");
  html += F("<label>Path WebSocket</label>");
  html += F("<input name='mpath' value='");
  html += htmlEscape(String(mqttPath));
  html += F("' placeholder='ex.: /  (padrao deste broker)'>");
  html += F("<label>Senha de administrador</label>");
  html += F("<input type='password' name='admin' required placeholder='Necessária para salvar'>");
  html += F("<input type='submit' class='btn' value='Salvar e reiniciar'>");
  html += F("</form></div>");
  html += pageBottom();
  sendHtml(html);
}

void handleDevicePage() {
  String html = pageTop("Dispositivo — HOME", "/device");
  html += F("<h1>Identificação do dispositivo</h1>");
  html += F("<p class='sub'>O UUID identifica este dispositivo nos tópicos MQTT "
            "(home/&lt;uuid&gt;/...). Mudá-lo altera os tópicos publicados.</p>");
  html += F("<div class='card'>");
  html += F("<form method='POST' action='/save'>");
  html += F("<input type='hidden' name='section' value='device'>");
  html += F("<label>UUID do dispositivo</label>");
  html += F("<input name='uuid' value='");
  html += htmlEscape(String(uuid));
  html += F("' placeholder='Identificador único'>");
  html += F("<label>Lógica do relé/LED</label>");
  html += F("<select name='relaymode'>");
  html += F("<option value='1'");
  if (relayActiveHigh) html += F(" selected");
  html += F(">Ativo-alto (HIGH liga)</option>");
  html += F("<option value='0'");
  if (!relayActiveHigh) html += F(" selected");
  html += F(">Ativo-baixo (LOW liga — JQC3F)</option>");
  html += F("</select>");
  html += F("<p class='hint'>LED no D4 (externo para GND): ativo-alto. LED da placa (D4 onboard) ou JQC3F: ativo-baixo.</p>");
  html += F("<label>Senha de administrador (atual)</label>");
  html += F("<input type='password' name='admin' required placeholder='Necessária para salvar'>");
  html += F("<label>Nova senha de administrador</label>");
  html += F("<input type='password' name='newadmin' placeholder='Deixe vazio para manter a atual'>");
  html += F("<input type='submit' class='btn' value='Salvar e reiniciar'>");
  html += F("</form></div>");
  html += pageBottom();
  sendHtml(html);
}

// --------------------------- Scan de redes Wi-Fi -----------------------------
// Scan assíncrono — não trava o servidor web
void handleScan() {
  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    wifiManager.server->send(200, "application/json", "{\"networks\":[],\"scanning\":1}");
    return;
  }

  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    wifiScanJson = String(F("{\"networks\":[]}"));
    wifiManager.server->send(200, "application/json", wifiScanJson);
    return;
  }

  if (n >= 0) {
    String json = String(F("{\"networks\":["));
    for (int i = 0; i < n; i++) {
      if (i > 0) json += F(",");
      json += F("{\"s\":\"");
      json += jsonEscape(WiFi.SSID(i));
      json += F("\",\"r\":");
      json += String(WiFi.RSSI(i));
      json += F(",\"l\":");
      json += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? F("0") : F("1");
      json += F("}");
    }
    json += F("]}");
    WiFi.scanDelete();
    wifiScanJson = json;
    wifiManager.server->send(200, "application/json", wifiScanJson);
    return;
  }

  // Inicia scan assíncrono (não bloqueia)
  WiFi.scanNetworks(true, true);
  wifiManager.server->send(200, "application/json", "{\"networks\":[],\"scanning\":1}");
}

// Salva a configuração de uma seção (wifi, mqtt ou device) após validar a
// senha de administrador. O formulário indica a seção em 'section'.
void handleSave() {
  String admin = wifiManager.server->arg("admin");
  admin.trim();
  if (admin != String(adminPass)) {
    wifiManager.server->send(200, "text/html", errorPage("Senha incorreta!"));
    return;
  }

  String section = wifiManager.server->arg("section");
  section.trim();

  // --- Wi-Fi (só altera se informado o SSID) ---
  if (section == "wifi" || section == "all") {
    String ssid = wifiManager.server->arg("wifi_ssid");
    String wpass = wifiManager.server->arg("wifi_pass");
    ssid.trim();
    wpass.trim();
    if (ssid.length() > 0) {
      // Se não há senha salva e o usuário não digitou senha, a conexão vai falhar
      if (strlen(wifiPass) == 0 && wpass.length() == 0) {
        wifiManager.server->send(200, "text/html",
          errorPage("Digite a senha do Wi-Fi! Na primeira configuração a senha é obrigatória."));
        return;
      }
      copyStr(wifiSSID, sizeof(wifiSSID), ssid);
      if (wpass.length() > 0) copyStr(wifiPass, sizeof(wifiPass), wpass);
      saveWiFiConfig(String(wifiSSID), String(wifiPass));
    }
  }

  // --- Broker MQTT (configuração separada do Wi-Fi) ---
  if (section == "mqtt" || section == "all") {
    String host = wifiManager.server->arg("mhost");
    host.trim();
    if (host.length() > 0) copyStr(mqttHost, sizeof(mqttHost), host);

    String port = wifiManager.server->arg("mport");
    port.trim();
    if (port.length() > 0) {
      int p = port.toInt();
      if (p > 0 && p <= 65535) mqttPort = p;
    }

    String muser = wifiManager.server->arg("muser");
    muser.trim();
    copyStr(mqttUser, sizeof(mqttUser), muser);

    String mpass = wifiManager.server->arg("mpass");
    mpass.trim();
    if (mpass.length() > 0) copyStr(mqttPass, sizeof(mqttPass), mpass);

    String mpath = wifiManager.server->arg("mpath");
    mpath.trim();
    if (mpath.length() > 0) {
      if (mpath.charAt(0) != '/') mpath = "/" + mpath;
      copyStr(mqttPath, sizeof(mqttPath), mpath);
    }
    // Se o campo vier vazio, mantém o path atual (não força "/")

    saveMQTTConfig();
  }

  // --- Identificador (UUID), polaridade e senha de administrador ---
  if (section == "device" || section == "all") {
    String u = sanitizeUUID(wifiManager.server->arg("uuid"));
    if (u.length() > 0 && u != String(uuid)) {
      copyStr(uuid, sizeof(uuid), u);
      saveUUID(String(uuid));
    }

    String rm = wifiManager.server->arg("relaymode");
    rm.trim();
    if (rm == "0" || rm == "1") {
      bool novo = (rm == "1");
      if (novo != relayActiveHigh) {
        relayActiveHigh = novo;
        saveRelayPolarity();
      }
    }

    String newAdmin = wifiManager.server->arg("newadmin");
    newAdmin.trim();
    if (newAdmin.length() > 0) {
      copyStr(adminPass, sizeof(adminPass), newAdmin);
      saveAdminConfig();
    }
  }

  Serial.println("=== Resumo do que foi salvo ===");
  Serial.print("WiFi SSID: ["); Serial.print(wifiSSID); Serial.println("]");
  Serial.print("WiFi senha: ");
  if (strlen(wifiPass) > 0) Serial.println("(salva)");
  else Serial.println("(VAZIA!)");
  Serial.print("UUID: ["); Serial.print(uuid); Serial.println("]");
  Serial.print("Relé: "); Serial.println(relayActiveHigh ? "ativo-alto" : "ativo-baixo");
  Serial.print("MQTT host: ["); Serial.print(mqttHost); Serial.println("]");
  Serial.print("MQTT porta: ["); Serial.print(mqttPort); Serial.println("]");
  Serial.print("MQTT user: ["); Serial.print(mqttUser); Serial.println("]");
  Serial.print("MQTT path: ["); Serial.print(mqttPath); Serial.println("]");
  Serial.println("=================================");
  Serial.println("Reiniciando...");
  wifiManager.server->send(200, "text/html", okPage("Configurações salvas!"));
  delay(800);
  ESP.restart();
}

// Salva o Wi-Fi informado pelo portal nativo do WiFiManager (fallback).
// Usa o que o WiFiManager acabou de aplicar, já que o callback roda depois
// da requisição (server->arg() já não é confiável neste momento).
void onWifiManagerSave() {
  String ssid = wifiManager.getWiFiSSID(false);
  String pass = wifiManager.getWiFiPass(false);
  if (ssid.length() > 0) {
    saveWiFiConfig(ssid, pass);
  }
}

// Teste local do relé/LED sem MQTT — /relay-test?poke=1&time=0&pin=4
void handleRelayTest() {
  String pokeArg = wifiManager.server->arg("poke");
  pokeArg.trim();
  if (pokeArg != "0" && pokeArg != "1") {
    wifiManager.server->send(400, "text/plain",
      "Use /relay-test?poke=1 ou /relay-test?poke=0 [&time=ms][&pin=1..8]");
    return;
  }
  long tm = wifiManager.server->arg("time").toInt();
  if (tm < 0) tm = 0;
  String pinArg = wifiManager.server->arg("pin");
  pinArg.trim();
  String json = String("{\"poke\":") + pokeArg + ",\"time\":" + String(tm) +
                ",\"device\":\"" + String(uuid) + "\"";
  if (pinArg.length() > 0) json += ",\"pin\":" + pinArg;
  json += "}";
  handleRelayCommand(json);
  wifiManager.server->send(200, "text/plain",
    String("OK poke=") + pokeArg + " D" + String(relayLogicalPin) +
    " GPIO" + String(relayPin) +
    " = " + (digitalRead(relayPin) ? "HIGH" : "LOW"));
}

// Registra as rotas do servidor web
void bindServerCallback() {
  // O portal nativo do WiFiManager é substituído pela nossa interface
  wifiManager.server->on("/", HTTP_GET, handleHome);
  wifiManager.server->on("/wifi", HTTP_GET, handleWifiPage);
  wifiManager.server->on("/mqtt", HTTP_GET, handleMqttPage);
  wifiManager.server->on("/device", HTTP_GET, handleDevicePage);
  wifiManager.server->on("/scan", HTTP_GET, handleScan);
  wifiManager.server->on("/relay-test", HTTP_GET, handleRelayTest);
  wifiManager.server->on("/save", HTTP_POST, handleSave);
}

// ------------------------------- Wi-Fi --------------------------------------
// Traduz o WiFi.status() para texto legível (ajuda a achar o problema)
String wifiStatusText(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:      return "WL_IDLE_STATUS (iniciando)";
    case WL_NO_SSID_AVAIL:    return "WL_NO_SSID_AVAIL (rede nao encontrada)";
    case WL_SCAN_COMPLETED:   return "WL_SCAN_COMPLETED";
    case WL_CONNECTED:        return "WL_CONNECTED";
    case WL_CONNECT_FAILED:   return "WL_CONNECT_FAILED (falha generica)";
    case WL_CONNECTION_LOST:  return "WL_CONNECTION_LOST (sinal perdido)";
    case WL_WRONG_PASSWORD:   return "WL_WRONG_PASSWORD (senha errada!)";
    case WL_DISCONNECTED:     return "WL_DISCONNECTED (desconectado)";
    default:                  return String("status ") + String((int)s);
  }
}

// Usa a configuração salva na EEPROM; sem configuração (ou em caso de falha)
// abre o portal de configuração protegido por senha.
void connectWiFi() {
  wifiManager.setWebServerCallback(bindServerCallback);
  wifiManager.setSaveConfigCallback(onWifiManagerSave);
  wifiManager.setConfigPortalTimeout(240);

  if (strlen(wifiSSID) > 0) {
    Serial.print("Conectando ao Wi-Fi salvo: ");
    Serial.println(wifiSSID);
    if (strlen(wifiPass) == 0) {
      Serial.println("ATENCAO: a senha do Wi-Fi salva esta VAZIA.");
      Serial.println("Se a rede tem senha, a conexao vai falhar.");
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPass);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(200);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("Falha ao conectar no Wi-Fi: ");
      Serial.println(wifiStatusText(WiFi.status()));
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Abrindo o portal de configuração (AP HOME_Lock)...");
    wifiManager.startConfigPortal(AP_SSID, AP_PASSWORD);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado ao Wi-Fi! IP: ");
    Serial.println(WiFi.localIP());
    // Mantém o AP de configuração sempre disponível, com senha
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    wifiManager.startWebPortal();
  } else {
    Serial.println("Sem conexão Wi-Fi após o portal. Reiniciando...");
    ESP.restart();
  }

  Serial.println("Interface de configuração: http://192.168.4.1");
  Serial.print("ou http://");
  Serial.print(WiFi.localIP().toString());
  Serial.println("/");
}

// ------------------------------- Utilidades ---------------------------------
// Verifica a conectividade com a internet (TCP com o Google)
bool pingGoogle() {
  WiFiClient client;
  return client.connect("www.google.com", 80);
}

// -------------------------------- SETUP -------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(buttonPin, INPUT_PULLUP);
  EEPROM.begin(512);

  // Carrega as configurações da EEPROM (uma não depende da outra)
  loadMQTTConfig();
  loadWiFiConfig();
  loadAdminConfig();
  loadUUID();
  loadRelayPolarity();

  initAllOutputPins();  // D1–D8 como saída, desligados
  setAllOutputsOff();
  blinkRelaySelfTest(); // 3 piscadas no D4

  Serial.println("=== HOME Sistema — MQTT (Mosquitto) ===");
  Serial.print("UUID: ");
  Serial.println(uuid);
  Serial.print("Broker: wss://");
  Serial.print(mqttHost);
  Serial.print(":");
  Serial.print(mqttPort);
  Serial.println(mqttPath);

  connectWiFi();   // config salva ou portal de configuração

  // Configura o MQTT sobre WebSocket Seguro (WSS)
  wsClient.beginSSL(mqttHost, mqttPort, mqttPath, (uint8_t*)NULL, "mqtt");
  wsClient.setReconnectInterval(MQTT_RETRY_MS);   // tenta a cada 10 s
  mqtt.begin(wsClient);
  mqtt.subscribe(onGlobalMqtt);   // recebe comandos home/cmd via callback global
  mqttReady = true;

  // Lê os tempos diários salvos na EEPROM
  for (int i = 0; i < 7; i++) {
    dailyConnectedTime[i] = EEPROM.read(4 + i);
    dailyDisconnectedTime[i] = EEPROM.read(11 + i);
  }

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }

  sendLogsMQTT("Sistema iniciado com sucesso.");
  updateConnectionStatus();
}

// --------------------------------- LOOP -------------------------------------
void loop() {
  unsigned long now = millis();
  int currentDay = (now / 86400000) % 7;

  // Mantém o WebSocket vivo e reconecta a cada 10 s
  static bool wsWasUp = false;
  if (WiFi.status() == WL_CONNECTED) {
    wsClient.loop();
    bool wsUp = wsClient.isConnected();
    if (wsUp && !wsWasUp) {
      Serial.println("WebSocket conectado — conectando MQTT...");
      connectMQTT();
    }
    wsWasUp = wsUp;
    if (wsUp) {
      if (!mqtt.isConnected()) {
        if (now - lastMqttAttempt >= MQTT_RETRY_MS) {
          lastMqttAttempt = now;
          connectMQTT();
        }
      } else {
        mqtt.update();   // processa mensagens e mantém o keep-alive
      }
    }
  } else {
    wsWasUp = false;
  }
  wifiManager.process();   // mantém o servidor web ativo

  // Executa o agendamento do relé quando o tempo expira
  if (relayScheduled &&
      (unsigned long)(millis() - relayScheduleStart) >= relayScheduleDuration) {
    relayScheduled = false;
    applyRelayState(relayScheduleNextOn);
    Serial.println("Agendamento do relé executado.");
  }

  // Monitora o botão (4 s = reconfigurar Wi-Fi)
  if (digitalRead(buttonPin) == LOW) {
    if (!buttonPressed) {
      buttonPressTime = now;
      buttonPressed = true;
      Serial.println("Botão pressionado.");
    }
    if (now - buttonPressTime > 4000) {
      Serial.println("Botão pressionado por mais de 4 s. Apagando Wi-Fi e abrindo o portal.");
      sendLogsMQTT("Configuração de Wi-Fi redefinida pelo botão.");
      relayScheduled = false;
      setAllOutputsOff();   // saídas desligadas ao entrar no modo config
      clearWiFiConfig();
      wifiManager.resetSettings();
      delay(1000);
      ESP.restart();
    }
  } else {
    buttonPressed = false;
  }

  // ------------------- Sem Wi-Fi -------------------
  if (WiFi.status() != WL_CONNECTED) {
    if (isConnected) {
      isConnected = false;
      Serial.println("Desconectado da Internet.");
      dailyDisconnectedTime[currentDay] += 2;
      relayScheduled = false;
      setAllOutputsOff();   // saídas desligadas (estado seguro)

      for (int i = 0; i < 7; i++) {
        EEPROM.write(4 + i, dailyConnectedTime[i]);
        EEPROM.write(11 + i, dailyDisconnectedTime[i]);
      }
      EEPROM.commit();

      sendLogsMQTT("Falha na conexão com a internet.");
    }

    // Tenta reconectar a cada 10 s
    if (now - lastReconnectAttempt >= MQTT_RETRY_MS) {
      lastReconnectAttempt = now;
      Serial.println("Tentando reconectar ao Wi-Fi...");
      WiFi.reconnect();
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
        delay(100);
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Conectado ao Wi-Fi.");
        sendLogsMQTT("Conectado ao Wi-Fi.");
      }
    }
  } else {
    // ------------------- Com Wi-Fi -------------------
    if (!isConnected) {
      isConnected = true;
      Serial.println("Conexão com a Internet estabelecida.");
      sendLogsMQTT("Conexão com a Internet estabelecida.");
      connectedTime = 0;
      connectMQTT();
    }

    // Teste de internet (Google) — loga apenas quando MUDOU.
    // NÃO mexe no relé: o estado do relé é controlado SOMENTE pelo MQTT.
    if (now - previousPingMillis >= pingInterval) {
      previousPingMillis = now;
      bool ok = pingGoogle();
      if (ok != lastPingOk) {
        lastPingOk = ok;
        if (ok) {
          Serial.println("Conexão com a Internet OK...");
          sendLogsMQTT("Conexão com a Internet OK.");
        } else {
          Serial.println("Falha ao acessar o Google.");
          sendLogsMQTT("Falha ao acessar o Google.");
          relayScheduled = false;
          setAllOutputsOff();   // desliga saídas (falha de internet)
        }
      }
    }

    // Log da conexão com o broker apenas na mudança de estado
    if (mqtt.isConnected() != lastMqttState) {
      lastMqttState = mqtt.isConnected();
      if (lastMqttState) {
        Serial.println("Conectado ao broker MQTT!");
      } else {
        Serial.println("Desconectado do broker MQTT!");
      }
    }

    // Atualiza os tempos de conexão a cada 2 minutos
    if (now - previousMillis >= intervalConnection) {
      previousMillis = now;
      connectedTime += 2;
      dailyConnectedTime[currentDay] += 2;

      for (int i = 0; i < 7; i++) {
        EEPROM.write(4 + i, dailyConnectedTime[i]);
        EEPROM.write(11 + i, dailyDisconnectedTime[i]);
      }
      EEPROM.commit();
    }

    // Telemetria a cada 15 segundos
    if (now - previousSensorMillis >= intervalSensors) {
      previousSensorMillis = now;

      publishMQTT("stats/connected_time", String(connectedTime).c_str());
      publishMQTT("stats/rssi", String(WiFi.RSSI()).c_str());

      char buf[16];
      float voltage = analogRead(A0) * (3.3 / 1024.0);        // voltagem A0
      dtostrf(voltage, 1, 2, buf);
      publishMQTT("stats/voltage", buf);

      float chipTemp = (analogRead(A0) / 1024.0) * 100;       // estimativa
      dtostrf(chipTemp, 1, 2, buf);
      publishMQTT("stats/temperature", buf);

      publishMQTT("stats/free_heap", String(ESP.getFreeHeap()).c_str());
      publishMQTT("stats/sdk_version", ESP.getSdkVersion());

      // Apenas 1 linha no Serial (e só a cada 15s)
      Serial.print("Telemetria: RSSI ");
      Serial.print(WiFi.RSSI());
      Serial.print(" dBm, volt ");
      Serial.print(voltage, 2);
      Serial.print(" V, mem ");
      Serial.println(ESP.getFreeHeap());
    }

    // Disponibilidade (últimos 7 dias) — só a cada 60 s, fora do loop apertado
    if (now - previousAvailabilityMillis >= intervalAvailability) {
      previousAvailabilityMillis = now;

      unsigned long totalConnectedTime = 0;
      unsigned long totalDisconnectedTime = 0;
      for (int i = 0; i < 7; i++) {
        totalConnectedTime += dailyConnectedTime[i];
        totalDisconnectedTime += dailyDisconnectedTime[i];
      }
      unsigned long totalTime = totalConnectedTime + totalDisconnectedTime;
      if (totalTime > 0) {
        float availability = (float)totalConnectedTime / totalTime * 100;
        float unavailability = (float)totalDisconnectedTime / totalTime * 100;
        char buf[16];
        dtostrf(availability, 1, 2, buf);
        publishMQTT("stats/availability", buf);
        dtostrf(unavailability, 1, 2, buf);
        publishMQTT("stats/unavailability", buf);
      }
    }
  }
}
