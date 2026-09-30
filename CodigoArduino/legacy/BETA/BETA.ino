// ============================================================================
//  HOME Sistema — MQTT sobre WebSocket Seguro (WSS) — Mosquitto
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
//    1ª vez: o ESP8266 abre o AP "home-setup" (senha C6m8n4d2d3). Conecte-se
//            e acesse http://192.168.4.1 — setup obrigatório (todos os campos).
//    Depois: acesse http://<uuid>.local/ (mDNS) ou http://<ip-do-esp>/.
//
//  TÓPICOS (por usuário + dispositivo):
//    home/<user_id>/<uuid>/cmd
//    home/<user_id>/<uuid>/config
//    home/<user_id>/<uuid>/state|ack|status|ip|config/ack
//    home/logs/<user_id>/<uuid>/basic|advance
//
//  FÁBRICA (seco): só broker MQTT pré-preenchido. Setup obrigatório no AP
//  "home-setup" (http://192.168.4.1) — todos os campos.
//
//  COMANDO (home/<user>/<uuid>/cmd):
//    {"poke":1,"time":0,"pin":4}
//    poke 1=liga 0=desliga | time=ms até inverter (0=permanente)
//    pin = Dx do NodeMCU (1=D1 … 8=D8; padrão 4=D4)
//
//  CONFIG (home/<user>/<uuid>/config) — PASS obrigatório; demais campos opcionais:
//    {
//      "PASS":"C6m8n4d2d3",
//      "UUID":"novo_nome",
//      "USER_ID":"pai",
//      "MODELO":"NodeMCU",
//      "WIFI_SSID":"rede","WIFI_PASS":"senha",
//      "MQTT_HOST":"mosquito.omny.tec.br","MQTT_PORT":443,
//      "MQTT_USER":"cardoso","MQTT_PASS":"xxx","MQTT_PATH":"/",
//      "LOGICA_DO_RELE":1,
//      "NEW_PASS":"nova_admin"
//    }
//    USER_ID = dono/pai (só via MQTT config) | MODELO = nome da placa
//    LOGICA_DO_RELE = 1 (HIGH liga) ou 2 (LOW liga) | NEW_PASS = nova senha admin
//    Resposta em home/<user>/<uuid>/config/ack
//
//  LOGS basic (home/logs/<user>/<uuid>/basic):
//    {"internet":"SSID","server":"host","modelo":"NodeMCU","uuid":"...","user_id":"...","ip":"192.168.x.x"}
//  LOGS advance (home/logs/<user>/<uuid>/advance):
//    {"temperature":..,"free_heap":..,"rssi":..,"state":0|1,"connected_time":..}
//    state: 0=ligado, 1=desligado
//
//  BIBLIOTECAS (Gerenciador de Bibliotecas do Arduino IDE):
//    1. WebSockets_Generic       (khoih-prog)
//    2. MQTTPubSubClient_Generic (khoih-prog)
//    3. WiFiManager              (tzapu)
// ============================================================================

// Bibliotecas
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
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
// Nome do AP de configuração é DINÂMICO (usado no connectWiFi):
// "home-setup" até provisionar; depois igual ao UUID.
#define AP_PASSWORD         "C6m8n4d2d3"   // senha do AP de configuração

#define MQTT_RETRY_MS       10000UL        // tenta reconectar a cada 10 s
// Comando/config por UUID (não usa mais home/cmd nem home/config globais)

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
const unsigned long intervalSensors = 20000;      // 20 s (telemetria basic+advance)
const unsigned long pingInterval = 20000;         // 20 s (teste de internet)
const unsigned long intervalAvailability = 60000; // 60 s (disponibilidade)

// Logs das últimas 24h (legado local; MQTT usa só basic/advance)
String logs[24];

// Identidade: vazia de fábrica — preenchida no setup inicial (portal)
char userId[40] = "";
char modelo[30] = "";
bool deviceProvisioned = false;

// Monitoramento
unsigned long lastReconnectAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastWsRestart = 0;
uint8_t wifiReconnectFails = 0;
uint8_t internetFailCount = 0;
bool isConnected = false;
bool lastMqttState = false;
bool lastPingOk = true;   // estado anterior do teste de internet (para não repetir log)

// Agendamento do relé (comando JSON com "time" > 0)
bool relayScheduled = false;
unsigned long relayScheduleStart = 0;
unsigned long relayScheduleDuration = 0;
bool relayScheduleNextOn = false;   // estado a aplicar quando o tempo expirar

void checkRelaySchedule();

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

// Buffer para montar os tópicos home/<user>/<uuid>/…
char topicBuf[96];

// MQTT só pode ser consultado depois do mqtt.begin()
bool mqttReady = false;

// WiFiManager global (portal de configuração + servidor web)
WiFiManager wifiManager;

// ------------------------------- EEPROM -------------------------------------
// Endereços usados (CONFIGURAÇÃO ESTÁVEL — só sobrescrita após commit):
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
//
// Contingência (CANDIDATO — nunca apaga a estável até confirmar):
//   400    magic (0x5B)
//   401    flags: bit0=wifi_pendente, bit1=mqtt_pendente
//   402    tentativas já feitas (0..6)
//   404..  Wi-Fi SSID candidato (40)
//   444..  Wi-Fi senha candidata (40)
//   484..  MQTT porta candidata (2)
//   486..  MQTT host (40)
//   526..  MQTT user (30)
//   556..  MQTT pass (30)
//   586..  MQTT path (30)
//   620    magic user_id (0x5C) — só alterável via home/config
//   621..  user_id (40)
//   661    magic modelo (0x5D)
//   662..  modelo (30)
//   700    magic provisionado (0x5E)

#define EEPROM_SIZE            1024
#define CFG_MAX_ATTEMPTS       6
#define CFG_ATTEMPT_WINDOW_MS  20000UL

#define EE_CFG_MAGIC           400
#define EE_CFG_FLAGS           401
#define EE_CFG_ATTEMPTS        402
#define EE_CAND_WIFI_SSID      404
#define EE_CAND_WIFI_PASS      444
#define EE_CAND_MQTT_PORT      484
#define EE_CAND_MQTT_HOST      486
#define EE_CAND_MQTT_USER      526
#define EE_CAND_MQTT_PASS      556
#define EE_CAND_MQTT_PATH      586
#define EE_USER_MAGIC          620
#define EE_USER_ID             621
#define EE_MODELO_MAGIC        661
#define EE_MODELO              662
#define EE_PROV_MAGIC          700   // 0x5E = provisionado (setup inicial ok)

bool cfgWifiPending = false;
bool cfgMqttPending = false;
uint8_t cfgAttempts = 0;

// Protótipos — usadas pela contingência antes das definições completas
void publishMQTT(const char* suffix, const char* msg, bool retained = false);
void sendLogsMQTT(String logMessage);
void restartMqttTransport();
String deviceHostname();

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

bool hasStableWiFi() {
  if (EEPROM.read(200) != 0x57) return false;
  return readStringEEPROM(201, 40).length() > 0;
}

bool hasStableMQTT() {
  if (EEPROM.read(0) != 0x56) return false;
  return readStringEEPROM(100, 40).length() > 0;
}

bool configTestPending() {
  return cfgWifiPending || cfgMqttPending;
}

// --- Configuração ESTÁVEL do broker MQTT (só após commit / 1ª gravação) ---
void saveMQTTConfig() {
  EEPROM.write(0, 0x56);
  EEPROM.write(1, mqttPort & 0xFF);
  EEPROM.write(2, (mqttPort >> 8) & 0xFF);
  writeStringEEPROM(100, String(mqttHost), 40);
  writeStringEEPROM(140, String(mqttUser), 30);
  writeStringEEPROM(170, String(mqttPass), 30);
  writeStringEEPROM(353, String(mqttPath), 30);
  EEPROM.commit();
  Serial.println("MQTT ESTAVEL salvo na EEPROM.");
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
    if (strcmp(mqttPath, "/mqtt") == 0) {
      copyStr(mqttPath, sizeof(mqttPath), String("/"));
      saveMQTTConfig();
      Serial.println("Path MQTT corrigido: /mqtt -> /");
    }
  }
}

// --- Configuração ESTÁVEL do Wi-Fi (só após commit / 1ª gravação) ---
void saveWiFiConfig(const String& ssid, const String& pass) {
  EEPROM.write(200, 0x57);
  writeStringEEPROM(201, ssid, 40);
  writeStringEEPROM(241, pass, 40);
  EEPROM.commit();
  Serial.println("Wi-Fi ESTAVEL salvo na EEPROM.");
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

// -------------------- Contingência: candidato / commit / rollback --------------------

void persistContingencyMeta() {
  EEPROM.write(EE_CFG_MAGIC, 0x5B);
  uint8_t f = 0;
  if (cfgWifiPending) f |= 0x01;
  if (cfgMqttPending) f |= 0x02;
  EEPROM.write(EE_CFG_FLAGS, f);
  EEPROM.write(EE_CFG_ATTEMPTS, cfgAttempts);
  EEPROM.commit();
}

void clearContingency() {
  cfgWifiPending = false;
  cfgMqttPending = false;
  cfgAttempts = 0;
  EEPROM.write(EE_CFG_MAGIC, 0);
  EEPROM.write(EE_CFG_FLAGS, 0);
  EEPROM.write(EE_CFG_ATTEMPTS, 0);
  EEPROM.commit();
}

void proposeWiFiCandidate(const String& ssid, const String& pass) {
  writeStringEEPROM(EE_CAND_WIFI_SSID, ssid, 40);
  writeStringEEPROM(EE_CAND_WIFI_PASS, pass, 40);
  cfgWifiPending = true;
  cfgAttempts = 0;
  persistContingencyMeta();
  Serial.print("Wi-Fi CANDIDATO proposto (estavel intacta): ");
  Serial.println(ssid);
}

void proposeMqttCandidate() {
  EEPROM.write(EE_CAND_MQTT_PORT, mqttPort & 0xFF);
  EEPROM.write(EE_CAND_MQTT_PORT + 1, (mqttPort >> 8) & 0xFF);
  writeStringEEPROM(EE_CAND_MQTT_HOST, String(mqttHost), 40);
  writeStringEEPROM(EE_CAND_MQTT_USER, String(mqttUser), 30);
  writeStringEEPROM(EE_CAND_MQTT_PASS, String(mqttPass), 30);
  writeStringEEPROM(EE_CAND_MQTT_PATH, String(mqttPath), 30);
  cfgMqttPending = true;
  cfgAttempts = 0;
  persistContingencyMeta();
  Serial.print("MQTT CANDIDATO proposto (estavel intacta): ");
  Serial.println(mqttHost);
}

void applyCandidateMqttToRam() {
  int port = EEPROM.read(EE_CAND_MQTT_PORT) | (EEPROM.read(EE_CAND_MQTT_PORT + 1) << 8);
  if (port > 0) mqttPort = port;
  String v = readStringEEPROM(EE_CAND_MQTT_HOST, 40);
  if (v.length() > 0) copyStr(mqttHost, sizeof(mqttHost), v);
  v = readStringEEPROM(EE_CAND_MQTT_USER, 30);
  if (v.length() > 0) copyStr(mqttUser, sizeof(mqttUser), v);
  v = readStringEEPROM(EE_CAND_MQTT_PASS, 30);
  if (v.length() > 0) copyStr(mqttPass, sizeof(mqttPass), v);
  v = readStringEEPROM(EE_CAND_MQTT_PATH, 30);
  if (v.length() > 0) copyStr(mqttPath, sizeof(mqttPath), v);
}

void applyCandidateWifiToRam() {
  String s = readStringEEPROM(EE_CAND_WIFI_SSID, 40);
  String p = readStringEEPROM(EE_CAND_WIFI_PASS, 40);
  if (s.length() > 0) {
    copyStr(wifiSSID, sizeof(wifiSSID), s);
    copyStr(wifiPass, sizeof(wifiPass), p);
  }
}

void commitConfig() {
  Serial.println("COMMIT: promovendo candidato -> estavel");
  if (cfgWifiPending) {
    String s = readStringEEPROM(EE_CAND_WIFI_SSID, 40);
    String p = readStringEEPROM(EE_CAND_WIFI_PASS, 40);
    saveWiFiConfig(s, p);
    copyStr(wifiSSID, sizeof(wifiSSID), s);
    copyStr(wifiPass, sizeof(wifiPass), p);
  }
  if (cfgMqttPending) {
    applyCandidateMqttToRam();
    saveMQTTConfig();
  }
  clearContingency();
  sendLogsMQTT("Commit: nova configuracao Wi-Fi/servidor estabilizada.");
  publishMQTT("config/ack", "{\"ok\":true,\"commit\":true}", true);
}

void rollbackConfig() {
  Serial.println("ROLLBACK: descartando candidato, restaurando estavel");
  clearContingency();
  loadWiFiConfig();
  loadMQTTConfig();
  sendLogsMQTT("Rollback: configuracao anterior restaurada.");
  publishMQTT("config/ack", "{\"ok\":true,\"rollback\":true}", true);
}

// Carrega meta de contingência; se pendente, aplica candidato na RAM (estável
// já deve ter sido carregada). Se tentativas esgotadas, faz rollback imediato.
void loadContingencyAndApply() {
  cfgWifiPending = false;
  cfgMqttPending = false;
  cfgAttempts = 0;
  if (EEPROM.read(EE_CFG_MAGIC) != 0x5B) return;

  uint8_t f = EEPROM.read(EE_CFG_FLAGS);
  cfgWifiPending = (f & 0x01) != 0;
  cfgMqttPending = (f & 0x02) != 0;
  cfgAttempts = EEPROM.read(EE_CFG_ATTEMPTS);

  if (!configTestPending()) return;

  if (cfgAttempts >= CFG_MAX_ATTEMPTS) {
    Serial.println("Contingencia: tentativas esgotadas (boot) — ROLLBACK");
    rollbackConfig();
    return;
  }

  Serial.print("Contingencia ativa (tentativa ");
  Serial.print(cfgAttempts);
  Serial.print("/");
  Serial.print(CFG_MAX_ATTEMPTS);
  Serial.println("). Aplicando candidato na RAM.");

  if (cfgWifiPending) {
    applyCandidateWifiToRam();
    Serial.print("  Wi-Fi candidato: ");
    Serial.println(wifiSSID);
  }
  if (cfgMqttPending) {
    applyCandidateMqttToRam();
    Serial.print("  MQTT candidato: ");
    Serial.print(mqttHost);
    Serial.print(":");
    Serial.println(mqttPort);
  }
}

void doRollbackAndReconnect() {
  rollbackConfig();
  Serial.println("Reconectando com configuracao ESTAVEL...");
  if (strlen(wifiSSID) > 0) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.disconnect(true);
    delay(200);
    WiFi.begin(wifiSSID, wifiPass);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
      delay(200);
      Serial.print(".");
    }
    Serial.println();
  }
  if (WiFi.status() == WL_CONNECTED) {
    String apName = deviceHostname();
    WiFi.softAP(apName.c_str(), AP_PASSWORD);
    wifiManager.startWebPortal();
    restartMqttTransport();
  } else {
    Serial.println("Estavel tambem falhou — abrindo portal.");
    String apName = deviceHostname();
    wifiManager.startConfigPortal(apName.c_str(), AP_PASSWORD);
  }
}

// Máquina de estados: testa candidato até 6 vezes; commit ou rollback.
void processConfigTest() {
  if (!configTestPending()) return;
  if (!mqttReady && cfgMqttPending) return;  // ainda no setup

  static unsigned long attemptStarted = 0;
  unsigned long now = millis();
  if (attemptStarted == 0) attemptStarted = now;

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  bool mqttOk = mqttReady && mqtt.isConnected();

  bool success = false;
  if (cfgWifiPending && cfgMqttPending) {
    success = wifiOk && mqttOk;
  } else if (cfgWifiPending) {
    success = wifiOk;
  } else {
    success = wifiOk && mqttOk;
  }

  if (success) {
    commitConfig();
    attemptStarted = 0;
    return;
  }

  // Janela de espera desta tentativa (Wi-Fi e/ou MQTT subirem)
  if ((now - attemptStarted) < CFG_ATTEMPT_WINDOW_MS) return;

  cfgAttempts++;
  persistContingencyMeta();
  Serial.print("Contingencia: falha na tentativa ");
  Serial.print(cfgAttempts);
  Serial.print("/");
  Serial.println(CFG_MAX_ATTEMPTS);

  if (cfgAttempts >= CFG_MAX_ATTEMPTS) {
    attemptStarted = 0;
    doRollbackAndReconnect();
    return;
  }

  // Próxima tentativa
  attemptStarted = now;
  Serial.println("Contingencia: nova tentativa de conexao...");
  if (cfgWifiPending || !wifiOk) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(200);
    WiFi.begin(wifiSSID, wifiPass);
  }
  if (cfgMqttPending) {
    restartMqttTransport();
  }
}

// Propõe Wi-Fi com contingência (ou grava estável se for a 1ª vez)
bool adoptWiFiChange(const String& ssid, const String& pass) {
  copyStr(wifiSSID, sizeof(wifiSSID), ssid);
  copyStr(wifiPass, sizeof(wifiPass), pass);
  if (hasStableWiFi()) {
    proposeWiFiCandidate(ssid, pass);
    return true;  // precisa testar
  }
  saveWiFiConfig(ssid, pass);
  return false;
}

// Propõe MQTT com contingência (valores já estão na RAM)
bool adoptMqttChange() {
  if (hasStableMQTT()) {
    proposeMqttCandidate();
    return true;
  }
  saveMQTTConfig();
  return false;
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

// Na 1ª vez: escolhe "home". Se já existir uma rede com esse SSID no ar,
// usa "home1", depois "home2", e assim por diante.
String allocateHomeName() {
  const int MAX_N = 64;
  bool taken[64];
  for (int i = 0; i < MAX_N; i++) taken[i] = false;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Procurando redes 'home' para escolher nome unico...");
  int n = WiFi.scanNetworks(false, true);
  if (n < 0) n = 0;

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    ssid.trim();
    ssid.toLowerCase();
    if (ssid == "home") {
      taken[0] = true;
      continue;
    }
    if (!ssid.startsWith("home")) continue;
    String suf = ssid.substring(4);
    if (suf.length() == 0) continue;
    bool digits = true;
    for (unsigned int k = 0; k < suf.length(); k++) {
      char c = suf.charAt(k);
      if (c < '0' || c > '9') { digits = false; break; }
    }
    if (!digits) continue;
    int num = suf.toInt();
    if (num >= 1 && num < MAX_N) taken[num] = true;
  }
  WiFi.scanDelete();

  if (!taken[0]) return String(F("home"));
  for (int i = 1; i < MAX_N; i++) {
    if (!taken[i]) return String(F("home")) + String(i);
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "home%02x", (unsigned)(ESP.getChipId() & 0xFF));
  return String(buf);
}

void loadUUID() {
  if (EEPROM.read(312) == 0x59) {
    String v = readStringEEPROM(313, 40);
    if (v.length() > 0) copyStr(uuid, sizeof(uuid), v);
  }
  // De fábrica fica vazio — preenchido no setup inicial
}

bool isProvisioned() {
  return deviceProvisioned &&
         strlen(uuid) > 0 &&
         strlen(userId) > 0 &&
         strlen(modelo) > 0 &&
         strlen(wifiSSID) > 0;
}

void debugProvisionState(const char* where) {
  Serial.print("Provision [");
  Serial.print(where);
  Serial.print("] flag=");
  Serial.print(deviceProvisioned ? 1 : 0);
  Serial.print(" uuid=");
  Serial.print(uuid[0] ? uuid : "-");
  Serial.print(" user=");
  Serial.print(userId[0] ? userId : "-");
  Serial.print(" modelo=");
  Serial.print(modelo[0] ? modelo : "-");
  Serial.print(" wifi=");
  Serial.println(wifiSSID[0] ? wifiSSID : "-");
}

void saveProvisioned(bool ok) {
  deviceProvisioned = ok;
  EEPROM.write(EE_PROV_MAGIC, ok ? 0x5E : 0);
  EEPROM.commit();
}

void loadProvisioned() {
  deviceProvisioned = (EEPROM.read(EE_PROV_MAGIC) == 0x5E);
}

// Volta ao estado de fábrica “seco”: só broker MQTT nos defaults.
void factoryResetToDry() {
  Serial.println("=== FACTORY RESET → seco ===");

  uuid[0] = 0;
  EEPROM.write(312, 0);
  writeStringEEPROM(313, "", 40);

  userId[0] = 0;
  EEPROM.write(EE_USER_MAGIC, 0);
  writeStringEEPROM(EE_USER_ID, "", 40);

  modelo[0] = 0;
  EEPROM.write(EE_MODELO_MAGIC, 0);
  writeStringEEPROM(EE_MODELO, "", 30);

  wifiSSID[0] = 0;
  wifiPass[0] = 0;
  clearWiFiConfig();

  copyStr(mqttHost, sizeof(mqttHost), MQTT_DEFAULT_HOST);
  mqttPort = MQTT_DEFAULT_PORT;
  copyStr(mqttUser, sizeof(mqttUser), MQTT_DEFAULT_USER);
  copyStr(mqttPass, sizeof(mqttPass), MQTT_DEFAULT_PASS);
  copyStr(mqttPath, sizeof(mqttPath), MQTT_DEFAULT_PATH);
  saveMQTTConfig();

  copyStr(adminPass, sizeof(adminPass), ADMIN_DEFAULT_PASS);
  saveAdminConfig();

  EEPROM.write(EE_CFG_MAGIC, 0);
  cfgWifiPending = false;
  cfgMqttPending = false;
  cfgAttempts = 0;

  saveProvisioned(false);
  EEPROM.commit();

  WiFi.persistent(true);
  WiFi.disconnect(true);
  delay(100);
  ESP.eraseConfig();
  delay(100);
  WiFi.persistent(false);

  Serial.println("Reset seco concluido. Broker MQTT nos defaults.");
}

// Nome do dispositivo usado no mDNS e no AP de configuração
String deviceHostname() {
  if (!isProvisioned()) return String(F("home-setup"));
  String n = sanitizeUUID(String(uuid));
  n.toLowerCase();
  if (n.length() == 0) n = "home-setup";
  return n;
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

// --- user_id (somente via home/config) e modelo ---
String sanitizeUserId(const String& in) {
  String out;
  for (unsigned int i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ';
    if (ok) out += c;
  }
  out.trim();
  if (out.length() > 32) out = out.substring(0, 32);
  return out;
}

String sanitizeModelo(const String& in) {
  return sanitizeUserId(in);  // mesmas regras
}

void saveUserId() {
  EEPROM.write(EE_USER_MAGIC, 0x5C);
  writeStringEEPROM(EE_USER_ID, String(userId), 40);
  EEPROM.commit();
  Serial.print("user_id salvo: ");
  Serial.println(userId);
}

void loadUserId() {
  if (EEPROM.read(EE_USER_MAGIC) == 0x5C) {
    String v = readStringEEPROM(EE_USER_ID, 40);
    copyStr(userId, sizeof(userId), v);
  }
}

void saveModelo() {
  EEPROM.write(EE_MODELO_MAGIC, 0x5D);
  writeStringEEPROM(EE_MODELO, String(modelo), 30);
  EEPROM.commit();
  Serial.print("modelo salvo: ");
  Serial.println(modelo);
}

void loadModelo() {
  if (EEPROM.read(EE_MODELO_MAGIC) == 0x5D) {
    String v = readStringEEPROM(EE_MODELO, 30);
    if (v.length() > 0) copyStr(modelo, sizeof(modelo), v);
  }
  // De fábrica fica vazio até o setup
}

// ------------------------------- Helpers MQTT -------------------------------
void buildTopic(const char* suffix) {
  snprintf(topicBuf, sizeof(topicBuf), "home/%s/%s/%s", userId, uuid, suffix);
}

void publishMQTT(const char* suffix, const char* msg, bool retained) {
  if (mqttReady && mqtt.isConnected()) {
    buildTopic(suffix);
    mqtt.publish(topicBuf, msg, retained);
  }
}

void publishAbsolute(const char* topic, const char* msg, bool retained = false) {
  if (mqttReady && mqtt.isConnected()) {
    mqtt.publish(topic, msg, retained);
  }
}

// Escapa string para valor JSON (aspas/barra)
void jsonEscapeTo(char* dst, size_t dstSize, const char* src) {
  size_t j = 0;
  for (size_t i = 0; src[i] != 0 && j + 1 < dstSize; i++) {
    char c = src[i];
    if (c == '"' || c == '\\') {
      if (j + 2 >= dstSize) break;
      dst[j++] = '\\';
      dst[j++] = c;
    } else if ((unsigned char)c >= 0x20) {
      dst[j++] = c;
    }
  }
  dst[j] = 0;
}

// Telemetria oficial: home/logs/<uuid>/basic + advance (a cada 20 s)
void publishLogsBasic() {
  if (!mqttConnected()) return;

  char internetEsc[48], serverEsc[48], modeloEsc[40], uuidEsc[48], userEsc[48], ipEsc[24];
  jsonEscapeTo(internetEsc, sizeof(internetEsc),
               (WiFi.status() == WL_CONNECTED) ? wifiSSID : "");
  jsonEscapeTo(serverEsc, sizeof(serverEsc), mqttHost);
  jsonEscapeTo(modeloEsc, sizeof(modeloEsc), modelo);
  jsonEscapeTo(uuidEsc, sizeof(uuidEsc), uuid);
  jsonEscapeTo(userEsc, sizeof(userEsc), userId);
  String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  jsonEscapeTo(ipEsc, sizeof(ipEsc), ipStr.c_str());

  char basic[400];
  snprintf(basic, sizeof(basic),
           "{\"internet\":\"%s\",\"server\":\"%s\",\"modelo\":\"%s\","
           "\"uuid\":\"%s\",\"user_id\":\"%s\",\"ip\":\"%s\"}",
           internetEsc, serverEsc, modeloEsc, uuidEsc, userEsc, ipEsc);

  char topicBasic[96];
  snprintf(topicBasic, sizeof(topicBasic), "home/logs/%s/%s/basic", userId, uuid);
  publishAbsolute(topicBasic, basic, false);
}

void publishLogsAdvance() {
  if (!mqttConnected()) return;

  char tempBuf[16];
  float chipTemp = (analogRead(A0) / 1024.0f) * 100.0f;
  dtostrf(chipTemp, 1, 2, tempBuf);

  char advance[200];
  snprintf(advance, sizeof(advance),
           "{\"temperature\":%s,\"free_heap\":%u,\"rssi\":%d,"
           "\"state\":%d,\"connected_time\":%lu}",
           tempBuf,
           (unsigned)ESP.getFreeHeap(),
           (int)WiFi.RSSI(),
           relayLogicalOn ? 0 : 1,
           connectedTime);

  char topicAdv[96];
  snprintf(topicAdv, sizeof(topicAdv), "home/logs/%s/%s/advance", userId, uuid);
  publishAbsolute(topicAdv, advance, false);
}

// Após ação dirigida a este UUID: state retido + basic imediato
void publishStateAndBasic() {
  char stateMsg[24];
  snprintf(stateMsg, sizeof(stateMsg), "%d:pin=%d",
           relayLogicalOn ? 0 : 1, relayLogicalPin);
  publishMQTT("state", stateMsg, true);
  publishLogsBasic();
}

void publishLogsTelemetry() {
  if (!mqttConnected()) return;
  publishLogsBasic();
  publishLogsAdvance();

  Serial.print("Telemetria logs: RSSI ");
  Serial.print(WiFi.RSSI());
  Serial.print(" dBm, mem ");
  Serial.println(ESP.getFreeHeap());
}

bool mqttConnected() {
  return mqttReady && mqtt.isConnected();
}

void sendLogsMQTT(String logMessage) {
  // Evento pontual (legado) — telemetria periódica é basic/advance
  publishMQTT("logs", logMessage.c_str());
}

// ------------------------- Controle do relé (com JSON) -----------------------
// Publicar em home/<user>/<uuid>/cmd:
//   {"poke":1,"time":0,"pin":4}
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
  setRelayOn(on);   // hardware primeiro
  Serial.print("D");
  Serial.print(relayLogicalPin);
  Serial.print(" = ");
  Serial.println(on ? "LIGADO" : "DESLIGADO");
  char stateMsg[24];
  snprintf(stateMsg, sizeof(stateMsg), "%d:pin=%d", on ? 0 : 1, relayLogicalPin);
  publishMQTT("state", stateMsg, true);
  publishMQTT("logs", on ? "Rele LIGADO." : "Rele DESLIGADO.");
}

void checkRelaySchedule() {
  if (!relayScheduled) return;
  if ((millis() - relayScheduleStart) < relayScheduleDuration) return;
  relayScheduled = false;
  bool next = relayScheduleNextOn;
  Serial.print("Timer expirado (");
  Serial.print(relayScheduleDuration);
  Serial.print(" ms) -> ");
  Serial.println(next ? "LIGAR" : "DESLIGAR");
  applyRelayState(next);
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

  if (!msg.startsWith("{")) {
    Serial.println("cmd: formato invalido (JSON).");
    return;
  }

  // Tópico já é home/<user>/<uuid>/cmd — não precisa de "device" no JSON
  String dev = sanitizeUUID(jsonGetStringCI(msg, "device"));
  if (dev.length() > 0 && !uuidEqualsCI(dev, uuid)) {
    Serial.print("cmd com device=");
    Serial.print(dev);
    Serial.println(" — ignorado.");
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
  if (logicalPinToGpio(logicalPin) == buttonPin) {
    Serial.println("pin conflita com o botao (D5) — escolha outro.");
    publishMQTT("ack", "erro:pin_botao");
    return;
  }
  selectOutputPin(logicalPin);

  bool on = (poke == 1);
  unsigned long t = (unsigned long)tm;

  // Hardware + timer antes de MQTT (mais confiável)
  relayScheduled = false;
  relayLogicalOn = on;
  setRelayOn(on);

  if (t > 0) {
    relayScheduled = true;
    relayScheduleStart = millis();
    relayScheduleDuration = t;
    relayScheduleNextOn = !on;
    Serial.print("CMD D");
    Serial.print(logicalPin);
    Serial.print(" -> ");
    Serial.print(on ? "LIGAR" : "DESLIGAR");
    Serial.print(" por ");
    Serial.print(t);
    Serial.print(" ms, depois ");
    Serial.println(relayScheduleNextOn ? "LIGAR" : "DESLIGAR");
  } else {
    Serial.print("CMD D");
    Serial.print(logicalPin);
    Serial.print(" -> ");
    Serial.println(on ? "LIGAR (permanente)" : "DESLIGAR (permanente)");
  }

  char stateMsg[24];
  snprintf(stateMsg, sizeof(stateMsg), "%d:pin=%d", on ? 0 : 1, relayLogicalPin);
  publishMQTT("state", stateMsg, true);
  publishLogsBasic();  // basic imediato após cmd dirigido
  char ack[80];
  snprintf(ack, sizeof(ack), "ok:poke=%d,time=%lu,pin=%d,next=%d,gpio=%s",
           on ? 1 : 0, t, logicalPin, relayScheduleNextOn ? 1 : 0,
           digitalRead(relayPin) ? "HIGH" : "LOW");
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

// ------------------- Configuração via MQTT (home/<user>/<uuid>/config) -------------------
// PASS é obrigatório. Resposta em home/<user>/<uuid>/config/ack (JSON).

void publishConfigAck(const String& json) {
  publishMQTT("config/ack", json.c_str(), true);
  Serial.print("home/<user>/<uuid>/config -> ");
  Serial.println(json);
}

// Primeira chave string não vazia dentre as alternativas
String jsonGetStringAny(const String& json, const char* k1,
                        const char* k2 = nullptr, const char* k3 = nullptr) {
  String v = jsonGetStringCI(json, k1);
  if (v.length() > 0) return v;
  if (k2) { v = jsonGetStringCI(json, k2); if (v.length() > 0) return v; }
  if (k3) { v = jsonGetStringCI(json, k3); if (v.length() > 0) return v; }
  return "";
}

void handleConfigCommand(String msg) {
  msg.trim();
  int jsonStart = msg.indexOf('{');
  if (jsonStart > 0) msg = msg.substring(jsonStart);

  if (!msg.startsWith("{")) {
    publishConfigAck(F("{\"ok\":false,\"erro\":\"json_invalido\"}"));
    return;
  }

  // 1) Autenticação — senha de administrador
  String pass = jsonGetStringCI(msg, "PASS");
  if (pass.length() == 0) pass = jsonGetStringCI(msg, "ADMIN");
  if (pass.length() == 0 || pass != String(adminPass)) {
    Serial.println("config: senha admin incorreta.");
    publishConfigAck(F("{\"ok\":false,\"erro\":\"senha_invalida\"}"));
    return;
  }

  // DEVICE opcional (legado). Tópico já é por UUID — normalmente omitir.
  String target = sanitizeUUID(jsonGetStringAny(msg, "DEVICE", "TARGET", "DEV"));
  if (target.length() > 0 && !uuidEqualsCI(target, uuid)) {
    Serial.print("config com DEVICE=");
    Serial.print(target);
    Serial.println(" — ignorado.");
    return;
  }

  // Ação dirigida a este ESP → state + basic na hora
  publishStateAndBasic();

  String ack = "{\"ok\":true";
  bool changed = false;
  bool wifiChanged = false;
  bool mqttChanged = false;

  // 3) Novo UUID / nome do dispositivo
  String newUuid = sanitizeUUID(jsonGetStringCI(msg, "UUID"));
  if (newUuid.length() == 0) newUuid = sanitizeUUID(jsonGetStringCI(msg, "NAME"));
  if (newUuid.length() > 0 && !uuidEqualsCI(newUuid, uuid)) {
    copyStr(uuid, sizeof(uuid), newUuid);
    saveUUID(String(uuid));
    ack += ",\"uuid\":\"" + String(uuid) + "\"";
    changed = true;
  }

  // 3b) user_id (somente via home/config)
  String newUser = sanitizeUserId(jsonGetStringAny(msg, "USER_ID", "USERID", "user_id"));
  if (newUser.length() > 0 && newUser != String(userId)) {
    copyStr(userId, sizeof(userId), newUser);
    saveUserId();
    ack += ",\"user_id\":\"" + String(userId) + "\"";
    changed = true;
  }

  // 3c) modelo da placa (via home/config)
  String newModelo = sanitizeModelo(jsonGetStringAny(msg, "MODELO", "MODEL", "BOARD"));
  if (newModelo.length() > 0 && newModelo != String(modelo)) {
    copyStr(modelo, sizeof(modelo), newModelo);
    saveModelo();
    ack += ",\"modelo\":\"" + String(modelo) + "\"";
    changed = true;
  }

  // 4) Wi-Fi (com contingência se já houver estável)
  String wssid = jsonGetStringAny(msg, "WIFI_SSID", "SSID", "WIFI");
  String wpass = jsonGetStringAny(msg, "WIFI_PASS", "WIFI_PASSWORD", "WPASS");
  if (wssid.length() > 0) {
    if (wpass.length() == 0) wpass = String(wifiPass);
    if (adoptWiFiChange(wssid, wpass)) {
      ack += ",\"wifi\":\"" + String(wifiSSID) + "\",\"wifi_teste\":true";
    } else {
      ack += ",\"wifi\":\"" + String(wifiSSID) + "\"";
    }
    wifiChanged = true;
    changed = true;
  } else if (wpass.length() > 0 && strlen(wifiSSID) > 0) {
    if (adoptWiFiChange(String(wifiSSID), wpass)) {
      ack += ",\"wifi_pass\":true,\"wifi_teste\":true";
    } else {
      ack += ",\"wifi_pass\":true";
    }
    wifiChanged = true;
    changed = true;
  }

  // 5) Broker MQTT (com contingência se já houver estável)
  bool mqttDirty = false;
  String mhost = jsonGetStringAny(msg, "MQTT_HOST", "MHOST", "HOST");
  if (mhost.length() > 0) {
    if (mhost.startsWith("wss://")) mhost = mhost.substring(6);
    else if (mhost.startsWith("ws://")) mhost = mhost.substring(5);
    int slash = mhost.indexOf('/');
    if (slash > 0) {
      String pathPart = mhost.substring(slash);
      mhost = mhost.substring(0, slash);
      if (jsonGetStringAny(msg, "MQTT_PATH", "MPATH", "PATH").length() == 0) {
        copyStr(mqttPath, sizeof(mqttPath), pathPart);
      }
    }
    int colon = mhost.indexOf(':');
    if (colon > 0) {
      long p = mhost.substring(colon + 1).toInt();
      mhost = mhost.substring(0, colon);
      if (p > 0 && p <= 65535 && jsonGetIntCI(msg, "MQTT_PORT") < 0 &&
          jsonGetIntCI(msg, "MPORT") < 0) {
        mqttPort = (int)p;
      }
    }
    copyStr(mqttHost, sizeof(mqttHost), mhost);
    mqttDirty = true;
  }

  long mport = jsonGetIntCI(msg, "MQTT_PORT");
  if (mport < 0) mport = jsonGetIntCI(msg, "MPORT");
  if (mport < 0) mport = jsonGetIntCI(msg, "PORT");
  if (mport > 0 && mport <= 65535 && (int)mport != mqttPort) {
    mqttPort = (int)mport;
    mqttDirty = true;
  }

  String muser = jsonGetStringCI(msg, "MQTT_USER");
  if (muser.length() == 0) muser = jsonGetStringCI(msg, "MUSER");
  if (muser.length() > 0) {
    copyStr(mqttUser, sizeof(mqttUser), muser);
    mqttDirty = true;
  }

  String mpass = jsonGetStringCI(msg, "MQTT_PASS");
  if (mpass.length() == 0) mpass = jsonGetStringCI(msg, "MPASS");
  if (mpass.length() == 0) mpass = jsonGetStringCI(msg, "BROKER_PASS");
  if (mpass.length() > 0) {
    copyStr(mqttPass, sizeof(mqttPass), mpass);
    mqttDirty = true;
  }

  String mpath = jsonGetStringAny(msg, "MQTT_PATH", "MPATH", "PATH");
  if (mpath.length() > 0) {
    if (mpath.charAt(0) != '/') mpath = "/" + mpath;
    copyStr(mqttPath, sizeof(mqttPath), mpath);
    mqttDirty = true;
  }

  if (mqttDirty) {
    if (adoptMqttChange()) {
      ack += ",\"mqtt\":\"";
      ack += mqttHost;
      ack += "\",\"mqtt_teste\":true";
    } else {
      ack += ",\"mqtt\":\"";
      ack += mqttHost;
      ack += "\"";
    }
    ack += ",\"mqtt_port\":";
    ack += String(mqttPort);
    mqttChanged = true;
    changed = true;
  }

  // 6) Lógica do relé/LED — 1 = HIGH liga, 2 = LOW liga
  long logica = jsonGetIntCI(msg, "LOGICA_DO_RELE");
  if (logica < 0) logica = jsonGetIntCI(msg, "LOGICA");
  if (logica < 0) logica = jsonGetIntCI(msg, "RELAYMODE");
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

  // 7) Nova senha de administrador
  String newPass = jsonGetStringAny(msg, "NEW_PASS", "NEW_ADMIN", "ADMIN_PASS");
  if (newPass.length() > 0) {
    copyStr(adminPass, sizeof(adminPass), newPass);
    saveAdminConfig();
    ack += ",\"new_pass\":true";
    changed = true;
  }

  if (wifiChanged) ack += ",\"wifi_ok\":true";

  if (changed) {
    if (configTestPending()) {
      ack += ",\"teste\":true,\"tentativas_max\":";
      ack += String(CFG_MAX_ATTEMPTS);
      ack += ",\"aplicado\":true}";
    } else {
      ack += ",\"aplicado\":true}";
    }
    publishConfigAck(ack);
    delay(600);
    ESP.restart();
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

  char myCmd[96], myCfg[96];
  snprintf(myCmd, sizeof(myCmd), "home/%s/%s/cmd", userId, uuid);
  snprintf(myCfg, sizeof(myCfg), "home/%s/%s/config", userId, uuid);

  if (t.equalsIgnoreCase(myCmd)) {
    onRelayCommand(payload, size);
  } else if (t.equalsIgnoreCase(myCfg)) {
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
  if (!isProvisioned()) {
    static unsigned long lastProvLog = 0;
    if (millis() - lastProvLog > 30000UL) {
      lastProvLog = millis();
      debugProvisionState("mqtt-skip");
      Serial.println("MQTT: nao provisionado — complete o setup no portal.");
    }
    return false;
  }

  String clientId = String(userId) + "_" + String(uuid);
  if (clientId.length() > 40) clientId = String(uuid);
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

    char topicCmd[96], topicCfg[96];
    snprintf(topicCmd, sizeof(topicCmd), "home/%s/%s/cmd", userId, uuid);
    snprintf(topicCfg, sizeof(topicCfg), "home/%s/%s/config", userId, uuid);

    Serial.print("Assinando topicos: ");
    Serial.print(topicCmd);
    Serial.print(" e ");
    Serial.println(topicCfg);
    // Callback do tópico (não deixar vazio — era a causa de comando “sumir”)
    bool subOk = mqtt.subscribe(String(topicCmd),
      [](const char* payload, const size_t size) {
        onRelayCommand(payload, size);
      });
    bool subOkCfg = mqtt.subscribe(String(topicCfg),
      [](const char* payload, const size_t size) {
        onConfigCommand(payload, size);
      });
    Serial.print("Subscribe cmd: ");
    Serial.println(subOk ? "OK" : "FALHOU");
    Serial.print("Subscribe config: ");
    Serial.println(subOkCfg ? "OK" : "FALHOU");
    Serial.print("user/uuid: ");
    Serial.print(userId);
    Serial.print("/");
    Serial.println(uuid);

    buildTopic("status");
    mqtt.publish(topicBuf, "online", true);
    buildTopic("ip");
    mqtt.publish(topicBuf, WiFi.localIP().toString().c_str(), true);
    buildTopic("state");
    char stateMsg[24];
    snprintf(stateMsg, sizeof(stateMsg), "%d:pin=%d",
             relayLogicalOn ? 0 : 1, relayLogicalPin);
    mqtt.publish(topicBuf, stateMsg, true);
    publishMQTT("ack", "esp_pronto");
    Serial.println("Publicado ack=esp_pronto");
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

// CSS comum das páginas — fica em flash (PROGMEM) e é servido em /style.css
// (separado do HTML evita truncar o visual no Safari/Mac/iOS quando a RAM aperta).
const char PAGE_CSS[] PROGMEM =
  "*{box-sizing:border-box}"
  "html{-webkit-text-size-adjust:100%}"
  "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;"
  "background:#0f0c29;background:linear-gradient(135deg,#0f0c29,#302b63,#24243e);"
  "background-attachment:fixed;color:#e8e8f0;min-height:100vh;min-height:-webkit-fill-available;"
  "padding:20px;margin:0;-webkit-font-smoothing:antialiased}"
  ".wrap{max-width:520px;margin:0 auto}"
  ".topbar{display:-webkit-flex;display:flex;-webkit-align-items:center;align-items:center;"
  "-webkit-justify-content:space-between;justify-content:space-between;margin-bottom:16px}"
  ".logo{font-size:20px;font-weight:800;letter-spacing:1px;color:#6ee7b7;"
  "background:linear-gradient(90deg,#6ee7b7,#60a5fa);"
  "-webkit-background-clip:text;background-clip:text;"
  "-webkit-text-fill-color:transparent}"
  ".badge{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.12);"
  "border-radius:20px;padding:4px 12px;font-size:12px;color:#c4c8d4}"
  ".nav{display:-webkit-flex;display:flex;gap:8px;margin-bottom:18px;-webkit-flex-wrap:wrap;flex-wrap:wrap}"
  ".nav a{display:block;-webkit-flex:1;flex:1;text-align:center;padding:10px 6px;border-radius:10px;"
  "background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.1);"
  "color:#c4c8d4;text-decoration:none;font-size:13px;min-width:70px;"
  "-webkit-tap-highlight-color:transparent}"
  ".nav a.on{background:rgba(59,130,246,.25);border-color:#60a5fa;color:#93c5fd}"
  ".card{background:rgba(255,255,255,.06);border:1px solid rgba(255,255,255,.1);"
  "border-radius:16px;padding:18px;margin-bottom:14px}"
  "a.card{display:block;text-decoration:none;color:inherit}"
  "h1{font-size:20px;margin:0 0 6px;font-weight:700;color:#e8e8f0}"
  "a.card h1{font-size:16px}"
  ".sub{color:#9ca3af;font-size:13px;line-height:1.55;margin:0 0 8px}"
  "label{display:block;font-size:13px;color:#c4c8d4;margin:12px 0 4px}"
  "input,select{width:100%;padding:11px 12px;border-radius:10px;"
  "border:1px solid rgba(255,255,255,.15);background:rgba(0,0,0,.25);color:#fff;"
  "font-size:16px;outline:none;-webkit-appearance:none;-moz-appearance:none;appearance:none;"
  "box-sizing:border-box}"
  "input:focus,select:focus{border-color:#60a5fa}"
  "select{background-image:linear-gradient(45deg,transparent 50%,#93c5fd 50%),"
  "linear-gradient(135deg,#93c5fd 50%,transparent 50%);"
  "background-position:calc(100% - 18px) calc(50% - 3px),calc(100% - 12px) calc(50% - 3px);"
  "background-size:6px 6px,6px 6px;background-repeat:no-repeat}"
  ".btn{display:block;width:100%;padding:13px;border:none;border-radius:12px;"
  "background:#3b82f6;background:linear-gradient(90deg,#10b981,#3b82f6);color:#fff;"
  "font-size:15px;font-weight:600;cursor:pointer;margin-top:16px;"
  "-webkit-appearance:none;appearance:none;-webkit-tap-highlight-color:transparent}"
  ".btn.secondary{background:rgba(255,255,255,.1);color:#e8e8f0;margin-top:8px}"
  ".status{display:-webkit-flex;display:flex;-webkit-flex-wrap:wrap;flex-wrap:wrap;gap:8px;margin-bottom:10px}"
  ".pill{background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.1);"
  "border-radius:20px;padding:6px 12px;font-size:12px;color:#c4c8d4}"
  ".pill b{color:#6ee7b7}"
  ".hint{font-size:12px;color:#9ca3af;margin-top:6px;line-height:1.5}"
  ".ok{color:#6ee7b7}.err{color:#f87171}"
  ".wifi-item{display:-webkit-flex;display:flex;-webkit-align-items:center;align-items:center;"
  "gap:10px;padding:12px;border-radius:10px;background:rgba(0,0,0,.2);"
  "border:1px solid rgba(255,255,255,.08);margin-bottom:8px;cursor:pointer;"
  "-webkit-tap-highlight-color:transparent}"
  ".wifi-item .ssid{-webkit-flex:1;flex:1;font-size:14px;font-weight:500;color:#e8e8f0}"
  ".wifi-item .rssi{font-size:12px;color:#9ca3af}"
  ".wifi-item .lock{font-size:11px;color:#93c5fd;font-style:normal}"
  "#net{margin-top:12px}"
  ".titlebar{display:-webkit-flex;display:flex;-webkit-align-items:center;align-items:center;"
  "-webkit-justify-content:space-between;justify-content:space-between;margin-bottom:4px}"
  ".back{color:#93c5fd;text-decoration:none;font-size:13px}"
  ".hero{text-align:center;padding:28px 0 20px}"
  ".hero .logo-lg{font-size:34px;font-weight:800;letter-spacing:2px;color:#6ee7b7;"
  "background:linear-gradient(90deg,#6ee7b7,#60a5fa);"
  "-webkit-background-clip:text;background-clip:text;"
  "-webkit-text-fill-color:transparent}"
  ".hero p{color:#9ca3af;font-size:13px;margin-top:6px}"
  ".menu{display:-webkit-flex;display:flex;-webkit-flex-direction:column;flex-direction:column;"
  "gap:10px;margin-top:14px}"
  ".menu a{display:-webkit-flex;display:flex;-webkit-align-items:center;align-items:center;"
  "gap:12px;padding:14px 16px;border-radius:14px;background:rgba(255,255,255,.05);"
  "border:1px solid rgba(255,255,255,.08);color:#e8e8f0;text-decoration:none;"
  "font-size:15px;-webkit-tap-highlight-color:transparent}"
  ".menu .ico{width:34px;height:34px;border-radius:10px;display:-webkit-flex;display:flex;"
  "-webkit-align-items:center;align-items:center;-webkit-justify-content:center;"
  "justify-content:center;font-size:11px;font-weight:700;"
  "background:rgba(59,130,246,.18);color:#93c5fd;-webkit-flex-shrink:0;flex-shrink:0}";

// Cache do scan Wi-Fi (evita bloquear o servidor web a cada /scan)
String wifiScanJson;

// Content-Type com charset — Safari/Mac rejeitam ou “quebram” o visual sem isso
void sendHtml(const String& html) {
  wifiManager.server->send(200, "text/html; charset=utf-8", html);
}

void handleStyle() {
  wifiManager.server->sendHeader(F("Cache-Control"), F("public, max-age=86400"));
  wifiManager.server->send_P(200, PSTR("text/css; charset=utf-8"), PAGE_CSS);
}

// JS da pagina Wi-Fi — em flash para nao estourar RAM ao montar a pagina
const char WIFI_SCAN_JS[] PROGMEM =
  "<script>"
  "function setSsid(el){document.getElementById('wifi_ssid').value="
  "el.querySelector('.ssid').textContent;}"
  "function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;')"
  ".replace(/>/g,'&gt;');}"
  "function scan(){var el=document.getElementById('net');"
  "el.innerHTML='<p class=\"hint\">Buscando redes...</p>';"
  "fetch('/scan').then(function(r){return r.json();}).then(function(d){"
  "if(d.scanning){setTimeout(scan,800);return;}"
  "if(!d.networks||!d.networks.length){"
  "el.innerHTML='<p class=\"hint\">Nenhuma rede encontrada.</p>';return;}"
  "var h='',i,n;for(i=0;i<d.networks.length;i++){n=d.networks[i];"
  "h+='<div class=\"wifi-item\" onclick=\"setSsid(this)\">"
  "<span class=\"ssid\">'+esc(n.s)+'</span>"
  "<span class=\"rssi\">'+n.r+' dBm</span>"
  "<span class=\"lock\">'+(n.l?'protegida':'aberta')+'</span></div>';}"
  "el.innerHTML=h;}).catch(function(){"
  "el.innerHTML='<p class=\"hint\">Erro ao buscar redes.</p>';});}"
  "scan();"
  "</script>";

// Abre a página com o visual padrão e a navegação entre as telas.
// Com minimal = true, omite a barra superior e as abas (usado na tela inicial).
String pageTop(const String& title, const char* active, bool minimal = false) {
  String h = String(F("<!DOCTYPE html><html lang='pt-BR'><head>"));
  h += F("<meta charset='utf-8'>");
  h += F("<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>");
  h += F("<meta name='color-scheme' content='dark'>");
  h += F("<meta name='format-detection' content='telephone=no'>");
  h += F("<title>"); h += title; h += F("</title>");
  h += F("<link rel='stylesheet' href='/style.css'>");
  h += F("</head><body><div class='wrap'>");
  if (!minimal) {
    h += F("<div class='topbar'><div class='logo'>HOME</div><div class='badge'>");
    if (isProvisioned()) {
      h += htmlEscape(String(userId));
      h += F("/");
      h += htmlEscape(String(uuid));
    } else {
      h += F("setup");
    }
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
         "mudou, o IP também pode mudar — tente http://");
  h += deviceHostname();
  h += F(".local/ (mDNS), verifique o roteador ou conecte no ponto de acesso ");
  h += deviceHostname();
  h += F(" (senha C6m8n4d2d3).</p></div>");
  h += F("<script>"
         "function voltarInicio(){location.href='/';}"
         "setTimeout(voltarInicio,1000);"       // volta ao início após 1 s
         "setTimeout(voltarInicio,2500);"        // retries p/ caso o ESP ainda reinicie
         "setTimeout(voltarInicio,4000);"
         "setTimeout(voltarInicio,6000);"
         "setTimeout(voltarInicio,8000);"
         "</script>");
  h += pageBottom();
  return h;
}

// ------------------------------- Tela inicial --------------------------------
// Não provisionado: formulário único obrigatório (todas as opções).
// Provisionado: menu com atalhos.
void handleHome() {
  if (!isProvisioned()) {
    String html = pageTop("Setup inicial — HOME", "/", true);
    html.reserve(6500);
    html += F("<div class='hero'>");
    html += F("<div class='logo-lg'>HOME</div>");
    html += F("<p>Setup inicial — preencha <b>todos</b> os campos</p>");
    html += F("</div>");
    html += F("<div class='card' style='border:1px solid #5eead4'>");
    html += F("<p class='hint'>Dispositivo seco: só o servidor MQTT vem "
              "pré-preenchido. Tópicos serão "
              "<code>home/&lt;user_id&gt;/&lt;uuid&gt;/…</code>. "
              "Se a lista Wi-Fi estiver vazia, digite o SSID manualmente "
              "(redes 5 GHz não aparecem no ESP8266).</p>");
    html += F("<form method='POST' action='/save'>");
    html += F("<input type='hidden' name='section' value='setup'>");

    html += F("<h1 style='font-size:16px;margin:12px 0 8px'>1. Wi-Fi</h1>");
    html += F("<div id='net'></div>");
    html += F("<label>SSID da rede *</label>");
    html += F("<input id='wifi_ssid' name='wifi_ssid' required "
              "placeholder='Toque numa rede abaixo ou digite'>");
    html += F("<label>Senha do Wi-Fi *</label>");
    html += F("<input type='password' name='wifi_pass' required "
              "placeholder='Senha da rede'>");

    html += F("<h1 style='font-size:16px;margin:18px 0 8px'>2. Identidade</h1>");
    html += F("<label>user_id (dono / casa) *</label>");
    html += F("<input name='user_id' required placeholder='ex.: pai, joao'>");
    html += F("<label>UUID do dispositivo *</label>");
    html += F("<input name='uuid' required placeholder='ex.: portao, lampada'>");
    html += F("<label>Modelo da placa *</label>");
    html += F("<input name='modelo' required value='NodeMCU' "
              "placeholder='NodeMCU'>");

    html += F("<h1 style='font-size:16px;margin:18px 0 8px'>3. Broker MQTT</h1>");
    html += F("<p class='hint'>Pré-preenchido — confira ou altere.</p>");
    html += F("<label>Host *</label>");
    html += F("<input name='mhost' required value='");
    html += htmlEscape(String(mqttHost));
    html += F("'>");
    html += F("<label>Porta *</label>");
    html += F("<input name='mport' required type='number' value='");
    html += String(mqttPort);
    html += F("'>");
    html += F("<label>Path WebSocket *</label>");
    html += F("<input name='mpath' required value='");
    html += htmlEscape(String(mqttPath));
    html += F("'>");
    html += F("<label>Usuário MQTT *</label>");
    html += F("<input name='muser' required value='");
    html += htmlEscape(String(mqttUser));
    html += F("'>");
    html += F("<label>Senha MQTT *</label>");
    html += F("<input type='password' name='mpass' required value='");
    html += htmlEscape(String(mqttPass));
    html += F("'>");

    html += F("<h1 style='font-size:16px;margin:18px 0 8px'>4. Senha admin</h1>");
    html += F("<p class='hint'>Defina uma senha nova (a de fábrica deixa de valer).</p>");
    html += F("<label>Nova senha de administrador *</label>");
    html += F("<input type='password' name='newadmin' required minlength='6' "
              "placeholder='Mínimo 6 caracteres'>");
    html += F("<label>Confirmar nova senha *</label>");
    html += F("<input type='password' name='newadmin2' required minlength='6' "
              "placeholder='Repita a senha'>");
    html += F("<input type='submit' class='btn' value='Salvar setup e reiniciar'>");

    html += F("</form></div>");
    html += F(
      "<script>"
      "function setSsid(el){document.getElementById('wifi_ssid').value="
      "el.querySelector('.ssid').textContent;}"
      "function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;')"
      ".replace(/>/g,'&gt;');}"
      "function scan(){var el=document.getElementById('net');"
      "if(!el)return;"
      "el.innerHTML='<p class=\"hint\">Buscando redes...</p>';"
      "fetch('/scan').then(function(r){return r.json();}).then(function(d){"
      "if(d.scanning){setTimeout(scan,800);return;}"
      "if(!d.networks||!d.networks.length){"
      "el.innerHTML='<p class=\"hint\">Nenhuma rede encontrada.</p>';return;}"
      "var h='',i,n;for(i=0;i<d.networks.length;i++){n=d.networks[i];"
      "h+='<div class=\"wifi-item\" onclick=\"setSsid(this)\">"
      "<span class=\"ssid\">'+esc(n.s)+'</span>"
      "<span class=\"rssi\">'+n.r+' dBm</span>"
      "<span class=\"lock\">'+(n.l?'protegida':'aberta')+'</span></div>';}"
      "el.innerHTML=h;}).catch(function(){"
      "el.innerHTML='<p class=\"hint\">Erro ao buscar redes.</p>';});}"
      "scan();"
      "</script>");
    html += pageBottom();
    sendHtml(html);
    return;
  }

  String html = pageTop("HOME", "/", true);
  html.reserve(900);
  html += F("<div class='hero'>");
  html += F("<div class='logo-lg'>HOME</div>");
  html += F("<p>");
  html += htmlEscape(String(userId));
  html += F(" / ");
  html += htmlEscape(String(uuid));
  html += F("</p></div>");
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
  if (!isProvisioned()) { handleHome(); return; }
  String html = pageTop("Wi-Fi", "/wifi");
  html.reserve(2800);   // CSS agora é /style.css — página menor e mais estável
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
  if (!isProvisioned()) { handleHome(); return; }
  String html = pageTop("MQTT — HOME", "/mqtt");
  html.reserve(1800);
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
  if (!isProvisioned()) { handleHome(); return; }
  String html = pageTop("Dispositivo — HOME", "/device");
  html.reserve(1800);
  html += F("<h1>Identificação do dispositivo</h1>");
  html += F("<p class='sub'>O UUID identifica este dispositivo nos tópicos MQTT "
            "(home/&lt;user&gt;/&lt;uuid&gt;/...). Mudá-lo altera os tópicos publicados.</p>");
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

  html += F("<div class='card' style='margin-top:16px;border:1px solid #f87171'>");
  html += F("<h1 style='font-size:16px;color:#f87171'>Reset de fábrica (seco)</h1>");
  html += F("<p class='hint'>Apaga Wi-Fi, user_id, uuid, modelo e senha admin. "
            "Mantém só o servidor MQTT nos valores padrão. Depois abre o AP "
            "<code>home-setup</code> para novo setup completo.</p>");
  html += F("<form method='POST' action='/save' "
            "onsubmit=\"return confirm('Resetar para seco? Todo o setup será apagado.');\">");
  html += F("<input type='hidden' name='section' value='factory_reset'>");
  html += F("<label>Senha de administrador *</label>");
  html += F("<input type='password' name='admin' required "
            "placeholder='Senha admin atual'>");
  html += F("<input type='submit' class='btn' style='background:#f87171;color:#111' "
            "value='Resetar para seco'>");
  html += F("</form></div>");

  html += pageBottom();
  sendHtml(html);
}

// --------------------------- Scan de redes Wi-Fi -----------------------------
// Em portal AP precisa de WIFI_AP_STA para enxergar redes.
void handleScan() {
  // Garante rádio STA ativo para scan (senão a lista fica vazia)
  WiFiMode_t mode = WiFi.getMode();
  if (mode == WIFI_AP || mode == WIFI_OFF) {
    WiFi.mode(WIFI_AP_STA);
    delay(200);
  }

  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    wifiManager.server->send(200, "application/json", "{\"networks\":[],\"scanning\":1}");
    return;
  }

  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    wifiScanJson = String(F("{\"networks\":[]}"));
    wifiManager.server->send(200, "application/json", wifiScanJson);
    // tenta de novo
    WiFi.scanNetworks(true, true);
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

// Salva a configuração de uma seção (wifi, mqtt, device ou setup inicial).
void handleSave() {
  String section = wifiManager.server->arg("section");
  section.trim();
  bool testing = false;

  // ---------- Setup inicial (todas as opções obrigatórias) ----------
  if (section == "setup") {
    if (isProvisioned()) {
      sendHtml(errorPage("Dispositivo já provisionado. Use as telas Wi-Fi/MQTT/Dispositivo."));
      return;
    }

    String ssid = wifiManager.server->arg("wifi_ssid");
    String wpass = wifiManager.server->arg("wifi_pass");
    String uid = sanitizeUserId(wifiManager.server->arg("user_id"));
    String u = sanitizeUUID(wifiManager.server->arg("uuid"));
    String mod = sanitizeModelo(wifiManager.server->arg("modelo"));
    String host = wifiManager.server->arg("mhost");
    String port = wifiManager.server->arg("mport");
    String muser = wifiManager.server->arg("muser");
    String mpass = wifiManager.server->arg("mpass");
    String mpath = wifiManager.server->arg("mpath");
    String na = wifiManager.server->arg("newadmin");
    String na2 = wifiManager.server->arg("newadmin2");
    ssid.trim(); wpass.trim(); host.trim(); port.trim();
    muser.trim(); mpass.trim(); mpath.trim(); na.trim(); na2.trim();

    if (ssid.length() == 0 || wpass.length() == 0 ||
        uid.length() == 0 || u.length() == 0 || mod.length() == 0 ||
        host.length() == 0 || port.length() == 0 ||
        muser.length() == 0 || mpass.length() == 0 || mpath.length() == 0 ||
        na.length() < 6) {
      sendHtml(errorPage("Preencha todos os campos (senha admin mín. 6 caracteres)."));
      return;
    }
    if (na != na2) {
      sendHtml(errorPage("A confirmação da senha admin não confere."));
      return;
    }

    copyStr(userId, sizeof(userId), uid);
    saveUserId();
    copyStr(uuid, sizeof(uuid), u);
    saveUUID(String(uuid));
    copyStr(modelo, sizeof(modelo), mod);
    saveModelo();

    if (mpath.charAt(0) != '/') mpath = "/" + mpath;
    copyStr(mqttHost, sizeof(mqttHost), host);
    int p = port.toInt();
    if (p > 0 && p <= 65535) mqttPort = p;
    copyStr(mqttUser, sizeof(mqttUser), muser);
    copyStr(mqttPass, sizeof(mqttPass), mpass);
    copyStr(mqttPath, sizeof(mqttPath), mpath);
    saveMQTTConfig();

    copyStr(wifiSSID, sizeof(wifiSSID), ssid);
    copyStr(wifiPass, sizeof(wifiPass), wpass);
    saveWiFiConfig(ssid, wpass);

    copyStr(adminPass, sizeof(adminPass), na);
    saveAdminConfig();

    saveProvisioned(true);

    // Confirma na EEPROM antes de reiniciar
    loadUUID();
    loadUserId();
    loadModelo();
    loadWiFiConfig();
    loadProvisioned();
    debugProvisionState("pos-setup");
    if (!isProvisioned()) {
      sendHtml(errorPage("Falha ao gravar o setup na EEPROM. Tente de novo."));
      return;
    }

    Serial.println("=== Setup inicial concluido ===");
    Serial.print("home/"); Serial.print(userId); Serial.print("/"); Serial.println(uuid);
    sendHtml(okPage("Setup completo! Reiniciando e conectando ao Wi-Fi…"));
    delay(800);
    ESP.restart();
    return;
  }

  String admin = wifiManager.server->arg("admin");
  admin.trim();
  if (admin != String(adminPass)) {
    sendHtml(errorPage("Senha incorreta!"));
    return;
  }

  // Reset de fábrica → estado seco (só broker MQTT default)
  if (section == "factory_reset") {
    factoryResetToDry();
    sendHtml(okPage("Reset seco concluído. Reiniciando — conecte no Wi-Fi "
                    "<b>home-setup</b> (senha C6m8n4d2d3) e faça o setup de novo."));
    delay(1000);
    ESP.restart();
    return;
  }

  // --- Wi-Fi (só altera se informado o SSID) ---
  if (section == "wifi" || section == "all") {
    String ssid = wifiManager.server->arg("wifi_ssid");
    String wpass = wifiManager.server->arg("wifi_pass");
    ssid.trim();
    wpass.trim();
    if (ssid.length() > 0) {
      if (strlen(wifiPass) == 0 && wpass.length() == 0) {
        sendHtml(errorPage("Digite a senha do Wi-Fi! Na primeira configuração a senha é obrigatória."));
        return;
      }
      if (wpass.length() == 0) wpass = String(wifiPass);
      if (adoptWiFiChange(ssid, wpass)) testing = true;
    }
  }

  // --- Broker MQTT ---
  if (section == "mqtt" || section == "all") {
    bool mqttDirty = false;
    String host = wifiManager.server->arg("mhost");
    host.trim();
    if (host.length() > 0) {
      copyStr(mqttHost, sizeof(mqttHost), host);
      mqttDirty = true;
    }

    String port = wifiManager.server->arg("mport");
    port.trim();
    if (port.length() > 0) {
      int p = port.toInt();
      if (p > 0 && p <= 65535) {
        mqttPort = p;
        mqttDirty = true;
      }
    }

    String muser = wifiManager.server->arg("muser");
    muser.trim();
    if (wifiManager.server->hasArg("muser")) {
      copyStr(mqttUser, sizeof(mqttUser), muser);
      mqttDirty = true;
    }

    String mpass = wifiManager.server->arg("mpass");
    mpass.trim();
    if (mpass.length() > 0) {
      copyStr(mqttPass, sizeof(mqttPass), mpass);
      mqttDirty = true;
    }

    String mpath = wifiManager.server->arg("mpath");
    mpath.trim();
    if (mpath.length() > 0) {
      if (mpath.charAt(0) != '/') mpath = "/" + mpath;
      copyStr(mqttPath, sizeof(mqttPath), mpath);
      mqttDirty = true;
    }

    if (mqttDirty) {
      if (adoptMqttChange()) testing = true;
    }
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
  if (testing) {
    Serial.println("Modo CONTINGENCIA: testando candidato (estavel preservada).");
  }
  Serial.println("=================================");
  Serial.println("Reiniciando...");
  if (testing) {
    sendHtml(okPage("Candidato salvo. O dispositivo vai testar a nova "
                    "configuração (até 6 tentativas). Se falhar, restaura a anterior."));
  } else {
    sendHtml(okPage("Configurações salvas!"));
  }
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
    adoptWiFiChange(ssid, pass);
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
  String json = String("{\"poke\":") + pokeArg + ",\"time\":" + String(tm);
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
  wifiManager.server->on("/style.css", HTTP_GET, handleStyle);
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

// Usa a configuração salva na EEPROM; sem provisionamento força AP home-setup
// (mesmo se o SDK ainda tiver Wi-Fi antigo na flash).
void connectWiFi() {
  wifiManager.setWebServerCallback(bindServerCallback);
  wifiManager.setSaveConfigCallback(onWifiManagerSave);

  // ---------- Setup inicial: SEMPRE sobe o AP ----------
  if (!isProvisioned()) {
    Serial.println("Nao provisionado — forcando portal AP home-setup...");
    wifiManager.setConfigPortalTimeout(0);

    WiFi.persistent(true);
    WiFi.disconnect(true);
    delay(200);
    WiFi.persistent(false);

    WiFi.mode(WIFI_AP_STA);
    delay(200);

    const char* apName = "home-setup";
    bool apOk = WiFi.softAP(apName, AP_PASSWORD);
    delay(300);
    Serial.print("softAP ");
    Serial.print(apName);
    Serial.print(apOk ? " OK" : " FALHOU");
    Serial.print(" | IP ");
    Serial.println(WiFi.softAPIP());
    Serial.print("Senha do AP: ");
    Serial.println(AP_PASSWORD);
    Serial.println("Conecte no Wi-Fi home-setup e abra http://192.168.4.1");

    wifiManager.startConfigPortal(apName, AP_PASSWORD);

    if (!isProvisioned() && WiFi.status() != WL_CONNECTED) {
      Serial.println("Setup nao concluido. Reiniciando para reabrir o AP...");
      delay(500);
      ESP.restart();
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Conectado ao Wi-Fi! IP: ");
      Serial.println(WiFi.localIP());
      WiFi.softAP(apName, AP_PASSWORD);
      wifiManager.startWebPortal();
    }
    return;
  }

  // ---------- Já provisionado ----------
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

  String apName = deviceHostname();

  if (WiFi.status() != WL_CONNECTED) {
    if (configTestPending()) {
      Serial.println("Contingencia: Wi-Fi candidato falhou nesta passagem — teste continua no loop.");
    } else {
      Serial.println("Abrindo o portal de configuração (AP " + apName + ")...");
      WiFi.mode(WIFI_AP_STA);
      delay(100);
      WiFi.softAP(apName.c_str(), AP_PASSWORD);
      delay(200);
      wifiManager.startConfigPortal(apName.c_str(), AP_PASSWORD);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado ao Wi-Fi! IP: ");
    Serial.println(WiFi.localIP());
    WiFi.softAP(apName.c_str(), AP_PASSWORD);
    wifiManager.startWebPortal();

    if (MDNS.begin(apName.c_str())) {
      MDNS.addService("http", "tcp", 80);
      Serial.println("mDNS ativo: http://" + apName + ".local/");
    } else {
      Serial.println("Falha ao iniciar o mDNS.");
    }
  } else if (!configTestPending()) {
    Serial.println("Sem conexão Wi-Fi após o portal. Reiniciando...");
    ESP.restart();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (strlen(wifiSSID) == 0 && WiFi.SSID().length() > 0) {
      copyStr(wifiSSID, sizeof(wifiSSID), WiFi.SSID());
      Serial.print("SSID recuperado do rádio: ");
      Serial.println(wifiSSID);
    }
    Serial.println("Interface de configuração: http://192.168.4.1");
    Serial.print("ou http://");
    Serial.print(WiFi.localIP().toString());
    Serial.println("/");
    Serial.println("ou ainda http://" + apName + ".local/ (mDNS local)");
  }
}

// ------------------------------- Utilidades ---------------------------------
// Verifica a conectividade com a internet (TCP com o Google)
bool pingGoogle() {
  WiFiClient client;
  client.setTimeout(3000);
  bool ok = client.connect("www.google.com", 80);
  if (ok) client.stop();
  return ok;
}

// Reinicia o transporte WSS (quando o socket fica “zumbi” após queda de internet)
void restartMqttTransport() {
  if (!isProvisioned()) return;  // sem setup, nao reinicia WSS em loop
  Serial.println("Reiniciando transporte MQTT (WSS)...");
  wsClient.disconnect();
  delay(50);
  wsClient.beginSSL(mqttHost, mqttPort, mqttPath, (uint8_t*)NULL, "mqtt");
  wsClient.setReconnectInterval(MQTT_RETRY_MS);
  lastWsRestart = millis();
  lastMqttAttempt = 0;
}

// Força nova associação Wi-Fi com as credenciais da EEPROM.
// WiFi.reconnect() sozinho falha com frequência no ESP8266 (STA travado).
void forceWifiReconnect(bool hard = false) {
  if (strlen(wifiSSID) == 0) {
    Serial.println("Sem SSID salvo — nao e possivel reconectar automaticamente.");
    return;
  }
  Serial.print(hard ? "Reconexao Wi-Fi FORCADA: " : "Reconectando Wi-Fi: ");
  Serial.println(wifiSSID);

  // Mantém o AP de configuração (AP+STA)
  WiFi.mode(WIFI_AP_STA);
  if (hard) {
    WiFi.disconnect(true);   // limpa estado do rádio
    delay(200);
  } else {
    WiFi.disconnect(false);
    delay(100);
  }
  WiFi.begin(wifiSSID, wifiPass);
}

// Chamado quando o Wi-Fi cai: limpa sockets MQTT/WS para não ficarem zumbis
void onWifiLostCleanup() {
  wsClient.disconnect();
}

// -------------------------------- SETUP -------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(buttonPin, INPUT_PULLUP);
  EEPROM.begin(EEPROM_SIZE);

  // Carrega ESTÁVEL primeiro; depois aplica CANDIDATO se houver teste pendente
  loadMQTTConfig();
  loadWiFiConfig();
  loadAdminConfig();
  loadUUID();
  loadRelayPolarity();
  loadUserId();
  loadModelo();
  loadProvisioned();
  // Migração: se já tem identidade completa (ex. config antiga), marca provisionado
  if (!deviceProvisioned &&
      strlen(uuid) > 0 && strlen(userId) > 0 &&
      strlen(wifiSSID) > 0 && strlen(modelo) > 0) {
    saveProvisioned(true);
  }
  // Só desfaz se faltar identidade (não só wifi — o SDK pode ter SSID)
  if (deviceProvisioned &&
      (strlen(uuid) == 0 || strlen(userId) == 0 || strlen(modelo) == 0)) {
    saveProvisioned(false);
    Serial.println("Provisionamento incompleto (sem identidade) — voltando ao setup.");
  }
  // Recupera SSID da EEPROM se flag ok mas wifiSSID vazio
  if (deviceProvisioned && strlen(wifiSSID) == 0) {
    loadWiFiConfig();
  }
  debugProvisionState("boot");
  loadContingencyAndApply();

  initAllOutputPins();  // D1–D8 como saída, desligados
  setAllOutputsOff();
  blinkRelaySelfTest(); // 3 piscadas no D4

  Serial.println("=== HOME Sistema — MQTT (Mosquitto) ===");
  Serial.print("Provisionado: ");
  Serial.println(isProvisioned() ? "sim" : "NAO (setup no AP home-setup)");
  Serial.print("UUID: ");
  Serial.println(uuid[0] ? uuid : "(vazio)");
  Serial.print("user_id: ");
  Serial.println(userId[0] ? userId : "(vazio)");
  Serial.print("modelo: ");
  Serial.println(modelo[0] ? modelo : "(vazio)");
  Serial.print("Broker: wss://");
  Serial.print(mqttHost);
  Serial.print(":");
  Serial.print(mqttPort);
  Serial.println(mqttPath);
  if (configTestPending()) {
    Serial.println("*** MODO CONTINGENCIA: testando configuracao candidata ***");
  }

  connectWiFi();   // config salva ou portal de configuração

  // Configura o MQTT sobre WebSocket Seguro (WSS)
  wsClient.beginSSL(mqttHost, mqttPort, mqttPath, (uint8_t*)NULL, "mqtt");
  wsClient.setReconnectInterval(MQTT_RETRY_MS);   // tenta a cada 10 s
  mqtt.begin(wsClient);
  mqtt.subscribe(onGlobalMqtt);   // backup: home/<user>/<uuid>/cmd|config
  mqttReady = true;

  // Lê os tempos diários salvos na EEPROM
  for (int i = 0; i < 7; i++) {
    dailyConnectedTime[i] = EEPROM.read(4 + i);
    dailyDisconnectedTime[i] = EEPROM.read(11 + i);
  }

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }

  sendLogsMQTT(configTestPending()
                 ? "Sistema iniciado — testando configuracao candidata."
                 : "Sistema iniciado com sucesso.");
  updateConnectionStatus();
}

// --------------------------------- LOOP -------------------------------------
void loop() {
  unsigned long now = millis();
  int currentDay = (now / 86400000) % 7;

  checkRelaySchedule();

  // Contingência Wi-Fi/MQTT: commit ou rollback (até 6 tentativas)
  processConfigTest();

  // Mantém o WebSocket vivo e reconecta a cada 10 s
  static bool wsWasUp = false;
  static bool wifiWasUp = true;   // setup já deixou o Wi-Fi conectado
  bool wifiUp = (WiFi.status() == WL_CONNECTED);

  if (wifiUp) {
    // Wi-Fi voltou depois de uma queda — limpa sockets zumbis
    if (!wifiWasUp) {
      Serial.println("Wi-Fi restabelecido — reiniciando WSS...");
      wifiReconnectFails = 0;
      restartMqttTransport();
      wsWasUp = false;
    }

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
        checkRelaySchedule();
      }
    } else {
      // WS caiu e a lib não recuperou sozinha — força reinício a cada 30 s
      if (now - lastWsRestart >= 30000UL) {
        restartMqttTransport();
      }
    }
  } else {
    if (wifiWasUp) {
      onWifiLostCleanup();
    }
    wsWasUp = false;
  }
  wifiWasUp = wifiUp;

  wifiManager.process();   // mantém o servidor web ativo
  if (wifiUp) MDNS.update();
  checkRelaySchedule();

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
  if (!wifiUp) {
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

    // Reconexão NÃO bloqueante — a cada 10 s (pausada durante teste de contingência)
    if (!configTestPending() && now - lastReconnectAttempt >= MQTT_RETRY_MS) {
      lastReconnectAttempt = now;
      wifiReconnectFails++;
      bool hard = (wifiReconnectFails % 3 == 0);
      Serial.print("Tentativa de reconexao Wi-Fi #");
      Serial.print(wifiReconnectFails);
      Serial.print(" status=");
      Serial.println(wifiStatusText(WiFi.status()));
      forceWifiReconnect(hard);
    }
  } else {
    // ------------------- Com Wi-Fi -------------------
    if (!isConnected) {
      isConnected = true;
      Serial.println("Conexão com a Internet estabelecida.");
      sendLogsMQTT("Conexão com a Internet estabelecida.");
      connectedTime = 0;
      // MQTT será reestabelecido quando o WebSocket subir (bloco acima)
    }

    // Teste de internet (Google) — detecta “Wi-Fi ok, internet morta”
    // e força o WSS a reconectar quando a internet volta / fica zumbi.
    if (now - previousPingMillis >= pingInterval) {
      previousPingMillis = now;
      bool ok = pingGoogle();
      if (ok != lastPingOk) {
        lastPingOk = ok;
        if (ok) {
          internetFailCount = 0;
          Serial.println("Conexão com a Internet OK...");
          sendLogsMQTT("Conexão com a Internet OK.");
          // Internet voltou — reinicia WSS caso MQTT esteja offline
          if (!mqttConnected()) restartMqttTransport();
        } else {
          Serial.println("Falha ao acessar o Google.");
          sendLogsMQTT("Falha ao acessar o Google.");
          relayScheduled = false;
          setAllOutputsOff();   // desliga saídas (falha de internet)
        }
      }
      if (!ok) {
        internetFailCount++;
        // Após 3 falhas seguidas (~60 s), reinicia o socket WSS (evita zumbi)
        if (internetFailCount >= 3 && (now - lastWsRestart >= 30000UL)) {
          Serial.println("Internet instavel — reiniciando WSS.");
          restartMqttTransport();
          internetFailCount = 0;
        }
      } else {
        internetFailCount = 0;
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

    // Telemetria a cada 20 s → home/logs/<uuid>/basic + advance
    if (now - previousSensorMillis >= intervalSensors) {
      previousSensorMillis = now;
      publishLogsTelemetry();
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
