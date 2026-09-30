// ============================================================================
//  HOME Sistema — MQTT sobre WebSocket Seguro (WSS) — Mosquitto
//
//  VERSÃO PARA ESP-01 / ESP-01S + MÓDULO RELÉ v4.0 (SRD-05VDC-SL-C)
//
//  Adaptação do projeto original (NodeMCU) para o módulo ESP-01/01S com
//  relé de 5V. O relé é controlado pelo GPIO0 (IO0) com lógica ATIVA-BAIXA:
//  LOW = RELÉ LIGADO, HIGH = RELÉ DESLIGADO.
//
//  • Broker:    wss://mosquito.omny.tec.br  (porta 443, path "/")
//  • Usuário:   cardoso
//  • Senha:     C6m8n4d2d3
//  • Reconexão: tenta se conectar a cada 10 segundos
//  • Config do broker MQTT SEPARADA da config do Wi-Fi (EEPROM independente,
//    uma alteração não afeta a outra)
//  • Setup / recuperação: portal SoftAP com senha de admin (padrão C6m8n4d2d3)
//  • Em operação normal (Wi-Fi OK): SEM portal HTTP — config via MQTT/app
//    (UUID, Wi-Fi, MQTT, lógica do relé, senha admin — mesmo JSON de /config)
//  • SoftAP+portal volta se o Wi-Fi falhar (recuperação, não brick)
//
//  PINOS DO ESP-01/01S:
//    GPIO0 (IO0) — controla o relé (LOW liga)  [pino de boot: HIGH = executa]  
//    GPIO2 (IO2) — LED onboard (LOW acende)     [pino de boot: HIGH = executa]
//    GPIO1 (TX) / GPIO3 (RX) — UART (não usar como saída)
//
//  AVISO (hardware): como o GPIO0 é pino de bootstrapping, o relé pode dar um
//  "clique" ao ligar/resetar. Isso é uma limitação física do módulo v4.0 —
//  não há solução 100% por software. Para eliminar, solde um capacitor
//  eletrolítico de ~470uF em paralelo com o LED do optoacoplador (PC817).
//
//  COMO USAR:
//    1ª vez: o ESP8266 escolhe um nome livre (home, home1, home2...) e abre o
//            AP com esse nome (senha C6m8n4d2d3). Conecte-se ao AP e acesse
//            http://192.168.4.1 — a tela inicial leva a Wi-Fi, MQTT e Dispositivo.
//    Depois: Wi-Fi conectado → sem portal HTTP (config via MQTT/app).
//            SoftAP+portal só se o Wi-Fi falhar (recuperação).
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
//    {"poke":1,"time":0}
//
//  CONFIG (home/<user>/<uuid>/config) — PASS obrigatório:
//    {
//      "PASS":"C6m8n4d2d3",
//      "UUID":"novo_nome",
//      "USER_ID":"pai",
//      "MODELO":"ESP-01",
//      "WIFI_SSID":"rede","WIFI_PASS":"senha",
//      "MQTT_HOST":"mosquito.omny.tec.br","MQTT_PORT":443,
//      "MQTT_USER":"cardoso","MQTT_PASS":"xxx","MQTT_PATH":"/",
//      "LOGICA_DO_RELE":2,
//      "NEW_PASS":"nova_admin"
//    }
//    USER_ID = dono/pai (só via home/<uuid>/config; na página /device só leitura)
//    MODELO = nome da placa (config MQTT ou página /device)
//    LOGICA_DO_RELE = neste ESP-01 só 2 (LOW liga) é aceito | NEW_PASS = nova senha admin
//    Resposta em home/<uuid>/config/ack
//
//  LOGS basic (home/logs/<uuid>/basic) — a cada 20 s:
//    {"internet":"SSID","server":"host","modelo":"ESP-01","uuid":"...","user_id":"...","ip":"192.168.x.x"}
//  LOGS advance (home/logs/<uuid>/advance) — a cada 20 s (um único JSON):
//    {"temperature":0,"free_heap":..,"rssi":..,"state":0|1,"connected_time":..}
//    state: 0=ligado, 1=desligado | temperature: 0 no ESP-01 (sem A0)
//
//  BIBLIOTECAS:
//    1. WebSockets_Generic
//    2. MQTTPubSubClient_Generic
//    (WiFiManager REMOVIDO — ESP8266WebServer nativo)
//
//  V010_ALPHA — ESP-01, no WiFiManager, SoftAP só no setup/recuperação, menos String.
// ============================================================================

// Bibliotecas
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <string.h>

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
// igual ao UUID (home, home1, home2...) escolhido na 1ª inicialização.
#define AP_PASSWORD         "C6m8n4d2d3"   // senha do AP de configuração

#define MQTT_RETRY_MS       10000UL        // tenta reconectar a cada 10 s
// Comando/config por UUID (não usa mais home/cmd nem home/config globais)

// ------------------- Pinos do ESP-01 / ESP-01S (relé v4.0) ------------------
// O relé v4.0 (SRD-05VDC-SL-C) é acionado pelo GPIO0, ativo-baixo (LOW liga).
// GPIO0 e GPIO2 são pinos de boot — ambos devem ficar HIGH no reset. Como o
// relé fica OFF em HIGH, o boot é seguro por padrão.
#define RELAY_PIN 0     // GPIO0 — controla o relé (LOW = LIGADO)
#define LED_PIN   2     // GPIO2 — LED onboard (LOW = ACESO)

int buttonPin = -1;     // ESP-01 não tem GPIO livre para botão (desabilitado)

// Polaridade do relé: SEMPRE ativo-baixo neste hardware (GPIO0 LOW = liga).
// NÃO use ativo-alto no ESP-01: "relé desligado" deixaria GPIO0 = LOW e o
// próximo reset entra em modo de gravação (parece "morto": sem Wi-Fi/Serial).
bool relayActiveHigh = false;
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

void checkRelaySchedule();  // protótipo — definida junto ao controle do relé

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

// Servidor HTTP só no SoftAP (setup / recuperação Wi-Fi)
ESP8266WebServer webServer(80);
bool webServerRunning = false;

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
//   620    magic user_id (0x5C) — só alterável via home/config
//   621..  user_id (40)
//   661    magic modelo (0x5D)
//   662..  modelo (30)
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
void sendLogsMQTT(const char* logMessage);
void bindServerRoutes();
void startWebServer();
void stopWebServer();
void runConfigPortal(const char* apName, unsigned long timeoutMs);
void restartMqttTransport();
String deviceHostname();

void writeEEPROMStr(int addr, const char* s, int maxLen) {
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, (s && i < (int)strlen(s)) ? s[i] : 0);
  }
}

void readEEPROMStr(int addr, char* dst, int maxLen) {
  int i = 0;
  for (; i < maxLen - 1; i++) {
    char c = EEPROM.read(addr + i);
    if (c == 0) break;
    dst[i] = c;
  }
  dst[i] = 0;
}

void copyStr(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = 0;
}

void copyStr(char* dst, size_t dstSize, const String& src) {
  copyStr(dst, dstSize, src.c_str());
}

bool hasStableWiFi() {
  if (EEPROM.read(200) != 0x57) return false;
  return EEPROM.read(201) != 0;
}

bool hasStableMQTT() {
  if (EEPROM.read(0) != 0x56) return false;
  return EEPROM.read(100) != 0;
}

bool configTestPending() {
  return cfgWifiPending || cfgMqttPending;
}

void saveMQTTConfig() {
  EEPROM.write(0, 0x56);
  EEPROM.write(1, mqttPort & 0xFF);
  EEPROM.write(2, (mqttPort >> 8) & 0xFF);
  writeEEPROMStr(100, mqttHost, 40);
  writeEEPROMStr(140, mqttUser, 30);
  writeEEPROMStr(170, mqttPass, 30);
  writeEEPROMStr(353, mqttPath, 30);
  EEPROM.commit();
  Serial.println("MQTT ESTAVEL salvo na EEPROM.");
}

void loadMQTTConfig() {
  if (EEPROM.read(0) == 0x56) {
    int port = EEPROM.read(1) | (EEPROM.read(2) << 8);
    if (port > 0) mqttPort = port;
    char v[40];
    readEEPROMStr(100, v, 40);
    if (v[0]) copyStr(mqttHost, sizeof(mqttHost), v);
    readEEPROMStr(140, v, 30);
    if (v[0]) copyStr(mqttUser, sizeof(mqttUser), v);
    readEEPROMStr(170, v, 30);
    if (v[0]) copyStr(mqttPass, sizeof(mqttPass), v);
    readEEPROMStr(353, v, 30);
    if (v[0]) copyStr(mqttPath, sizeof(mqttPath), v);
    if (strcmp(mqttPath, "/mqtt") == 0) {
      copyStr(mqttPath, sizeof(mqttPath), "/");
      saveMQTTConfig();
      Serial.println("Path MQTT corrigido: /mqtt -> /");
    }
  }
}

void saveWiFiConfig(const char* ssid, const char* pass) {
  EEPROM.write(200, 0x57);
  writeEEPROMStr(201, ssid, 40);
  writeEEPROMStr(241, pass, 40);
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
    char s[40], pw[40];
    readEEPROMStr(201, s, 40);
    readEEPROMStr(241, pw, 40);
    if (s[0]) {
      copyStr(wifiSSID, sizeof(wifiSSID), s);
      copyStr(wifiPass, sizeof(wifiPass), pw);
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

void proposeWiFiCandidate(const char* ssid, const char* pass) {
  writeEEPROMStr(EE_CAND_WIFI_SSID, ssid, 40);
  writeEEPROMStr(EE_CAND_WIFI_PASS, pass, 40);
  cfgWifiPending = true;
  cfgAttempts = 0;
  persistContingencyMeta();
  Serial.print("Wi-Fi CANDIDATO proposto (estavel intacta): ");
  Serial.println(ssid ? ssid : "");
}

void proposeMqttCandidate() {
  EEPROM.write(EE_CAND_MQTT_PORT, mqttPort & 0xFF);
  EEPROM.write(EE_CAND_MQTT_PORT + 1, (mqttPort >> 8) & 0xFF);
  writeEEPROMStr(EE_CAND_MQTT_HOST, mqttHost, 40);
  writeEEPROMStr(EE_CAND_MQTT_USER, mqttUser, 30);
  writeEEPROMStr(EE_CAND_MQTT_PASS, mqttPass, 30);
  writeEEPROMStr(EE_CAND_MQTT_PATH, mqttPath, 30);
  cfgMqttPending = true;
  cfgAttempts = 0;
  persistContingencyMeta();
  Serial.print("MQTT CANDIDATO proposto (estavel intacta): ");
  Serial.println(mqttHost);
}

void applyCandidateMqttToRam() {
  int port = EEPROM.read(EE_CAND_MQTT_PORT) | (EEPROM.read(EE_CAND_MQTT_PORT + 1) << 8);
  if (port > 0) mqttPort = port;
  char v[40];
  readEEPROMStr(EE_CAND_MQTT_HOST, v, 40);
  if (v[0]) copyStr(mqttHost, sizeof(mqttHost), v);
  readEEPROMStr(EE_CAND_MQTT_USER, v, 30);
  if (v[0]) copyStr(mqttUser, sizeof(mqttUser), v);
  readEEPROMStr(EE_CAND_MQTT_PASS, v, 30);
  if (v[0]) copyStr(mqttPass, sizeof(mqttPass), v);
  readEEPROMStr(EE_CAND_MQTT_PATH, v, 30);
  if (v[0]) copyStr(mqttPath, sizeof(mqttPath), v);
}

void applyCandidateWifiToRam() {
  char s[40], pw[40];
  readEEPROMStr(EE_CAND_WIFI_SSID, s, 40);
  readEEPROMStr(EE_CAND_WIFI_PASS, pw, 40);
  if (s[0]) {
    copyStr(wifiSSID, sizeof(wifiSSID), s);
    copyStr(wifiPass, sizeof(wifiPass), pw);
  }
}

void commitConfig() {
  Serial.println("COMMIT: promovendo candidato -> estavel");
  if (cfgWifiPending) {
    char s[40], pw[40];
    readEEPROMStr(EE_CAND_WIFI_SSID, s, 40);
    readEEPROMStr(EE_CAND_WIFI_PASS, pw, 40);
    saveWiFiConfig(s, pw);
    copyStr(wifiSSID, sizeof(wifiSSID), s);
    copyStr(wifiPass, sizeof(wifiPass), pw);
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
  stopWebServer();
  if (strlen(wifiSSID) > 0) {
    WiFi.mode(WIFI_STA);
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
    // Contingência OK com estável: só STA, sem portal HTTP
    WiFi.softAPdisconnect(true);
    stopWebServer();
    restartMqttTransport();
  } else {
    Serial.println("Estavel tambem falhou — abrindo portal SoftAP.");
    String apName = deviceHostname();
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    WiFi.softAP(apName.c_str(), AP_PASSWORD);
    delay(200);
    runConfigPortal(apName.c_str(), 240000UL);
  }
}

void processConfigTest() {
  if (!configTestPending()) return;
  if (!mqttReady && cfgMqttPending) return;

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

bool adoptWiFiChange(const char* ssid, const char* pass) {
  copyStr(wifiSSID, sizeof(wifiSSID), ssid);
  copyStr(wifiPass, sizeof(wifiPass), pass);
  if (hasStableWiFi()) {
    proposeWiFiCandidate(ssid, pass);
    return true;
  }
  saveWiFiConfig(ssid, pass);
  return false;
}

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
  writeEEPROMStr(282, adminPass, 30);
  EEPROM.commit();
  Serial.println("Senha de administrador salva na EEPROM.");
}

void loadAdminConfig() {
  if (EEPROM.read(281) == 0x58) {
    char v[30];
    readEEPROMStr(282, v, 30);
    if (v[0]) copyStr(adminPass, sizeof(adminPass), v);
  }
}

// --- UUID do dispositivo ---
void sanitizeUUIDTo(const char* in, char* out, size_t outSize) {
  size_t j = 0;
  if (!out || outSize == 0) return;
  if (!in) { out[0] = 0; return; }
  for (size_t i = 0; in[i] && j + 1 < outSize && j < 32; i++) {
    char c = in[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (ok) out[j++] = c;
  }
  out[j] = 0;
}

String sanitizeUUID(const String& in) {
  char buf[40];
  sanitizeUUIDTo(in.c_str(), buf, sizeof(buf));
  return String(buf);
}

static char toUpperAscii(char c) {
  if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
  return c;
}

bool uuidEqualsCI(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (toUpperAscii(*a) != toUpperAscii(*b)) return false;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

bool uuidEqualsCI(const String& a, const char* b) {
  return uuidEqualsCI(a.c_str(), b);
}

void saveUUID(const char* u) {
  EEPROM.write(312, 0x59);
  writeEEPROMStr(313, u, 40);
  EEPROM.commit();
  Serial.println("UUID salvo na EEPROM.");
}

void saveUUID(const String& u) {
  saveUUID(u.c_str());
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
    char v[40];
    readEEPROMStr(313, v, 40);
    if (v[0]) copyStr(uuid, sizeof(uuid), v);
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
  writeEEPROMStr(313, "", 40);

  userId[0] = 0;
  EEPROM.write(EE_USER_MAGIC, 0);
  writeEEPROMStr(EE_USER_ID, "", 40);

  modelo[0] = 0;
  EEPROM.write(EE_MODELO_MAGIC, 0);
  writeEEPROMStr(EE_MODELO, "", 30);

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

  // limpa contingência
  EEPROM.write(EE_CFG_MAGIC, 0);
  cfgWifiPending = false;
  cfgMqttPending = false;
  cfgAttempts = 0;

  saveProvisioned(false);
  EEPROM.commit();

  // Apaga Wi-Fi guardado pelo SDK (senão reconecta sozinho e o AP some)
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

// --- Polaridade do relé (ativo-alto ou ativo-baixo) ---
void saveRelayPolarity() {
  EEPROM.write(390, 0x5A);
  EEPROM.write(391, relayActiveHigh ? 1 : 0);
  EEPROM.commit();
  Serial.print("Polaridade do relé salva na EEPROM: ");
  Serial.println(relayActiveHigh ? "ativo-alto" : "ativo-baixo");
}

void loadRelayPolarity() {
  // Força ativo-baixo: ativo-alto neste pino (GPIO0) trava o boot após restart
  if (EEPROM.read(390) != 0x5A || EEPROM.read(391) != 0) {
    relayActiveHigh = false;
    saveRelayPolarity();
  } else {
    relayActiveHigh = false;
  }
  Serial.println("Polaridade: ativo-baixo (LOW liga) | Relé no GPIO0");
}

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
  return sanitizeUserId(in);
}

void saveUserId() {
  EEPROM.write(EE_USER_MAGIC, 0x5C);
  writeEEPROMStr(EE_USER_ID, userId, 40);
  EEPROM.commit();
  Serial.print("user_id salvo: ");
  Serial.println(userId);
}

void loadUserId() {
  if (EEPROM.read(EE_USER_MAGIC) == 0x5C) {
    char v[40];
    readEEPROMStr(EE_USER_ID, v, 40);
    copyStr(userId, sizeof(userId), v);
  }
}

void saveModelo() {
  EEPROM.write(EE_MODELO_MAGIC, 0x5D);
  writeEEPROMStr(EE_MODELO, modelo, 30);
  EEPROM.commit();
  Serial.print("modelo salvo: ");
  Serial.println(modelo);
}

void loadModelo() {
  if (EEPROM.read(EE_MODELO_MAGIC) == 0x5D) {
    char v[30];
    readEEPROMStr(EE_MODELO, v, 30);
    if (v[0]) copyStr(modelo, sizeof(modelo), v);
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

void publishLogsBasic() {
  if (!mqttConnected()) return;

  String ssidNow = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : String("");
  if (ssidNow.length() == 0 && WiFi.status() == WL_CONNECTED) ssidNow = String(wifiSSID);

  char internetEsc[48], serverEsc[48], modeloEsc[40], uuidEsc[48], userEsc[48], ipEsc[24];
  jsonEscapeTo(internetEsc, sizeof(internetEsc), ssidNow.c_str());
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

  char advance[200];
  snprintf(advance, sizeof(advance),
           "{\"temperature\":0,\"free_heap\":%u,\"rssi\":%d,"
           "\"state\":%d,\"connected_time\":%lu}",
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
  publishMQTT("state", relayLogicalOn ? "0" : "1", true);
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

// Eventos pontuais só no Serial — telemetria MQTT = basic + advance
void sendLogsMQTT(const char* logMessage) {
  Serial.print("[log] ");
  Serial.println(logMessage ? logMessage : "");
}

// ------------------------- Controle do relé (com JSON) -----------------------
// Publicar em home/<uuid>/cmd:
//   {"poke":1,"time":0}
// poke = 1 liga / 0 desliga | time = ms até inverter (0 = permanente)
//
// Ex.: já ligado + {"poke":1,"time":1000}
//   → mantém LIGADO e DESLIGA automaticamente após 1000 ms.

// Acha ponteiro logo após a chave (case-insensitive). Tolerante a "_" ou
// espaço na chave. Retorna nullptr se não achar.
static char toLowerAscii(char c) {
  if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 'a');
  return c;
}

const char* jsonFindKeyCI(const char* json, const char* key) {
  if (!json || !key || !*key) return nullptr;
  char keyNorm[48];
  size_t keyLen = 0;
  for (const char* k = key; *k && keyLen + 1 < sizeof(keyNorm); k++) {
    keyNorm[keyLen++] = toLowerAscii(*k);
  }
  keyNorm[keyLen] = 0;

  for (int variant = 0; variant < 2; variant++) {
    char needle[48];
    size_t nlen = 0;
    for (size_t i = 0; i < keyLen && nlen + 1 < sizeof(needle); i++) {
      char c = keyNorm[i];
      if (variant == 1 && c == '_') c = ' ';
      needle[nlen++] = c;
    }
    needle[nlen] = 0;

    const char* p = json;
    while (*p) {
      // Preferir "chave" entre aspas
      if (*p == '"') {
        const char* q = p + 1;
        size_t i = 0;
        while (needle[i] && q[i] && toLowerAscii(q[i]) == needle[i]) i++;
        if (needle[i] == 0 && q[i] == '"') {
          return q + i + 1;
        }
      }
      p++;
    }
  }
  return nullptr;
}

long jsonGetIntCI(const char* json, const char* key) {
  const char* p = jsonFindKeyCI(json, key);
  if (!p) return -1;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':' || *p == '"') p++;
  if (!*p) return -1;
  bool neg = false;
  if (*p == '-') { neg = true; p++; }
  long v = 0;
  bool any = false;
  while (*p >= '0' && *p <= '9') {
    v = v * 10 + (*p - '0');
    any = true;
    p++;
  }
  if (!any) return -1;
  return neg ? -v : v;
}

bool jsonGetStringCI(const char* json, const char* key, char* out, size_t outSize) {
  if (!out || outSize == 0) return false;
  out[0] = 0;
  const char* p = jsonFindKeyCI(json, key);
  if (!p) return false;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
  if (*p != '"') return false;
  p++;
  size_t j = 0;
  while (*p && *p != '"') {
    char c = *p;
    if (c == '\\' && p[1]) { p++; c = *p; }
    if (j + 1 < outSize) out[j++] = c;
    p++;
  }
  out[j] = 0;
  return j > 0;
}

// Compat web/legado: devolve String (borda HTTP / ack)
String jsonGetStringCI(const char* json, const char* key) {
  char buf[96];
  if (!jsonGetStringCI(json, key, buf, sizeof(buf))) return String("");
  return String(buf);
}

// Aplica o estado físico do relé (sempre ativo-baixo no hardware v4.0)
void setRelayOn(bool on) {
  // LOW = liga, HIGH = desliga. Nunca deixe GPIO0 LOW se for reiniciar!
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);
}

// LED onboard do ESP-01 (GPIO2) é ativo-baixo: LOW acende
void setLedOn(bool on) {
  digitalWrite(LED_PIN, on ? LOW : HIGH);
}

// Inicializa as saídas em estado seguro (relé desligado, LED apagado)
void initOutputs() {
  // GPIO0 HIGH primeiro — obrigatório para boot normal no próximo reset
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  relayLogicalOn = false;
}

void setAllOutputsOff() {
  digitalWrite(RELAY_PIN, HIGH);   // sempre HIGH = desligado + boot-safe
  relayLogicalOn = false;
}

// Reinício seguro no ESP-01: GPIO0 DEVE estar HIGH, senão entra em modo flash
void safeRestart() {
  Serial.println("Reinicio seguro: GPIO0 -> HIGH (boot normal)...");
  Serial.flush();
  relayScheduled = false;
  digitalWrite(RELAY_PIN, HIGH);          // relé OFF + pino de boot HIGH
  digitalWrite(LED_PIN, HIGH);
  pinMode(RELAY_PIN, INPUT_PULLUP);       // pull-up mantém HIGH durante o reset
  pinMode(LED_PIN, INPUT_PULLUP);
  WiFi.softAPdisconnect(true);
  delay(300);
  ESP.restart();
  while (true) { delay(1000); }
}

// Aplica o estado do relé (GPIO primeiro; MQTT depois — evita travar com GPIO0 LOW)
void applyRelayState(bool on) {
  relayLogicalOn = on;
  setRelayOn(on);   // hardware imediatamente
  Serial.print("Relé = ");
  Serial.println(on ? "LIGADO" : "DESLIGADO");
  publishMQTT("state", on ? "0" : "1", true);
  // estado também vai em home/logs/<uuid>/advance (campo "state") a cada 20 s
}

// Verifica e executa o timer do relé (chamar várias vezes no loop)
void checkRelaySchedule() {
  if (!relayScheduled) return;
  unsigned long elapsed = millis() - relayScheduleStart;
  if (elapsed < relayScheduleDuration) return;

  relayScheduled = false;
  bool next = relayScheduleNextOn;
  Serial.print("Timer expirado (");
  Serial.print(relayScheduleDuration);
  Serial.print(" ms) -> ");
  Serial.println(next ? "LIGAR" : "DESLIGAR");
  applyRelayState(next);
}

void blinkSelfTest() {
  Serial.println("Teste LED onboard (GPIO2): 3 piscadas...");
  for (int i = 0; i < 3; i++) {
    setLedOn(true);
    delay(250);
    setLedOn(false);
    delay(250);
  }
  Serial.println("Teste concluido.");
}

void handleRelayCommand(const char* msgIn) {
  if (!msgIn) return;
  const char* msg = msgIn;
  while (*msg == ' ' || *msg == '\t' || *msg == '\r' || *msg == '\n') msg++;
  const char* brace = strchr(msg, '{');
  if (brace) msg = brace;

  if (*msg != '{') {
    Serial.println("cmd: formato invalido (JSON).");
    return;
  }

  char devRaw[40];
  char dev[40];
  if (jsonGetStringCI(msg, "device", devRaw, sizeof(devRaw))) {
    sanitizeUUIDTo(devRaw, dev, sizeof(dev));
    if (dev[0] && !uuidEqualsCI(dev, uuid)) {
      Serial.print("cmd com device=");
      Serial.print(dev);
      Serial.println(" — ignorado.");
      return;
    }
  }

  long poke = jsonGetIntCI(msg, "poke");
  long tm = jsonGetIntCI(msg, "time");
  if (tm < 0) tm = 0;
  if (poke != 0 && poke != 1) {
    Serial.println("JSON invalido: poke deve ser 0 ou 1.");
    publishMQTT("ack", "erro:poke_invalido");
    return;
  }

  bool on = (poke == 1);
  unsigned long t = (unsigned long)tm;

  relayScheduled = false;
  relayLogicalOn = on;
  setRelayOn(on);

  if (t > 0) {
    relayScheduled = true;
    relayScheduleStart = millis();
    relayScheduleDuration = t;
    relayScheduleNextOn = !on;
    Serial.print("CMD -> ");
    Serial.print(on ? "LIGAR" : "DESLIGAR");
    Serial.print(" por ");
    Serial.print(t);
    Serial.print(" ms, depois ");
    Serial.println(relayScheduleNextOn ? "LIGAR" : "DESLIGAR");
  } else {
    Serial.print("CMD -> ");
    Serial.println(on ? "LIGAR (permanente)" : "DESLIGAR (permanente)");
  }

  publishStateAndBasic();
  char ack[80];
  snprintf(ack, sizeof(ack), "ok:poke=%d,time=%lu,next=%d,gpio=%s",
           on ? 1 : 0, t, relayScheduleNextOn ? 1 : 0,
           digitalRead(RELAY_PIN) ? "HIGH" : "LOW");
  publishMQTT("ack", ack);
}

void onRelayCommand(const char* payload, const size_t size) {
  static unsigned long lastMs = 0;
  static uint32_t lastHash = 0;
  uint32_t h = (uint32_t)size * 2654435761u;
  for (size_t i = 0; i < size; i++) h = (h * 131) + (uint8_t)payload[i];
  unsigned long now = millis();
  if (h == lastHash && (now - lastMs) < 80) return;
  lastMs = now;
  lastHash = h;

  char buf[513];
  size_t n = size < 512 ? size : 512;
  memcpy(buf, payload, n);
  buf[n] = 0;
  handleRelayCommand(buf);
}

// ------------------- Configuração via MQTT (home/<uuid>/config) -------------------
// PASS é obrigatório. Resposta em home/<uuid>/config/ack (JSON).

void publishConfigAck(const String& json) {
  publishMQTT("config/ack", json.c_str(), true);
  Serial.print("home/<uuid>/config -> ");
  Serial.println(json);
}

// Primeira chave string não vazia dentre as alternativas
bool jsonGetStringAny(const char* json, char* out, size_t outSize,
                      const char* k1, const char* k2 = nullptr, const char* k3 = nullptr) {
  if (jsonGetStringCI(json, k1, out, outSize) && out[0]) return true;
  if (k2 && jsonGetStringCI(json, k2, out, outSize) && out[0]) return true;
  if (k3 && jsonGetStringCI(json, k3, out, outSize) && out[0]) return true;
  out[0] = 0;
  return false;
}

void handleConfigCommand(const char* msgIn) {
  if (!msgIn) {
    publishConfigAck(F("{\"ok\":false,\"erro\":\"json_invalido\"}"));
    return;
  }
  const char* msg = msgIn;
  while (*msg == ' ' || *msg == '\t' || *msg == '\r' || *msg == '\n') msg++;
  const char* brace = strchr(msg, '{');
  if (brace) msg = brace;

  if (*msg != '{') {
    publishConfigAck(F("{\"ok\":false,\"erro\":\"json_invalido\"}"));
    return;
  }

  char pass[48];
  if (!jsonGetStringCI(msg, "PASS", pass, sizeof(pass)) || !pass[0]) {
    jsonGetStringCI(msg, "ADMIN", pass, sizeof(pass));
  }
  if (!pass[0] || strcmp(pass, adminPass) != 0) {
    Serial.println("config: senha admin incorreta.");
    publishConfigAck(F("{\"ok\":false,\"erro\":\"senha_invalida\"}"));
    return;
  }

  char targetRaw[40], target[40];
  if (jsonGetStringAny(msg, targetRaw, sizeof(targetRaw), "DEVICE", "TARGET", "DEV")) {
    sanitizeUUIDTo(targetRaw, target, sizeof(target));
    if (target[0] && !uuidEqualsCI(target, uuid)) {
      Serial.print("config com DEVICE=");
      Serial.print(target);
      Serial.println(" — ignorado.");
      return;
    }
  }

  publishStateAndBasic();

  String ack = "{\"ok\":true";
  bool changed = false;
  bool wifiChanged = false;
  bool mqttChanged = false;

  char tmp[96], tmp2[40];
  if (jsonGetStringCI(msg, "UUID", tmp, sizeof(tmp)) || jsonGetStringCI(msg, "NAME", tmp, sizeof(tmp))) {
    sanitizeUUIDTo(tmp, tmp2, sizeof(tmp2));
    if (tmp2[0] && !uuidEqualsCI(tmp2, uuid)) {
      copyStr(uuid, sizeof(uuid), tmp2);
      saveUUID(uuid);
      ack += ",\"uuid\":\"";
      ack += uuid;
      ack += "\"";
      changed = true;
    }
  }

  if (jsonGetStringAny(msg, tmp, sizeof(tmp), "USER_ID", "USERID", "user_id")) {
    String newUser = sanitizeUserId(String(tmp));
    if (newUser.length() > 0 && newUser != String(userId)) {
      copyStr(userId, sizeof(userId), newUser);
      saveUserId();
      ack += ",\"user_id\":\"";
      ack += userId;
      ack += "\"";
      changed = true;
    }
  }

  if (jsonGetStringAny(msg, tmp, sizeof(tmp), "MODELO", "MODEL", "BOARD")) {
    String newModelo = sanitizeModelo(String(tmp));
    if (newModelo.length() > 0 && newModelo != String(modelo)) {
      copyStr(modelo, sizeof(modelo), newModelo);
      saveModelo();
      ack += ",\"modelo\":\"";
      ack += modelo;
      ack += "\"";
      changed = true;
    }
  }

  char wssid[40], wpass[40];
  bool haveSsid = jsonGetStringAny(msg, wssid, sizeof(wssid), "WIFI_SSID", "SSID", "WIFI");
  bool havePass = jsonGetStringAny(msg, wpass, sizeof(wpass), "WIFI_PASS", "WIFI_PASSWORD", "WPASS");
  if (haveSsid && wssid[0]) {
    if (!havePass || !wpass[0]) copyStr(wpass, sizeof(wpass), wifiPass);
    if (adoptWiFiChange(wssid, wpass)) {
      ack += ",\"wifi\":\"";
      ack += wifiSSID;
      ack += "\",\"wifi_teste\":true";
    } else {
      ack += ",\"wifi\":\"";
      ack += wifiSSID;
      ack += "\"";
    }
    wifiChanged = true;
    changed = true;
  } else if (havePass && wpass[0] && wifiSSID[0]) {
    if (adoptWiFiChange(wifiSSID, wpass)) {
      ack += ",\"wifi_pass\":true,\"wifi_teste\":true";
    } else {
      ack += ",\"wifi_pass\":true";
    }
    wifiChanged = true;
    changed = true;
  }

  bool mqttDirty = false;
  char mhost[48];
  if (jsonGetStringAny(msg, mhost, sizeof(mhost), "MQTT_HOST", "MHOST", "HOST")) {
    String h = String(mhost);
    if (h.startsWith("wss://")) h = h.substring(6);
    else if (h.startsWith("ws://")) h = h.substring(5);
    int slash = h.indexOf('/');
    if (slash > 0) {
      String pathPart = h.substring(slash);
      h = h.substring(0, slash);
      char pathCheck[40];
      if (!jsonGetStringAny(msg, pathCheck, sizeof(pathCheck), "MQTT_PATH", "MPATH", "PATH")) {
        copyStr(mqttPath, sizeof(mqttPath), pathPart);
      }
    }
    int colon = h.indexOf(':');
    if (colon > 0) {
      long p = h.substring(colon + 1).toInt();
      h = h.substring(0, colon);
      if (p > 0 && p <= 65535 && jsonGetIntCI(msg, "MQTT_PORT") < 0 &&
          jsonGetIntCI(msg, "MPORT") < 0) {
        mqttPort = (int)p;
      }
    }
    copyStr(mqttHost, sizeof(mqttHost), h);
    mqttDirty = true;
  }

  long mport = jsonGetIntCI(msg, "MQTT_PORT");
  if (mport < 0) mport = jsonGetIntCI(msg, "MPORT");
  if (mport < 0) mport = jsonGetIntCI(msg, "PORT");
  if (mport > 0 && mport <= 65535 && (int)mport != mqttPort) {
    mqttPort = (int)mport;
    mqttDirty = true;
  }

  if (jsonGetStringCI(msg, "MQTT_USER", tmp, sizeof(tmp)) ||
      jsonGetStringCI(msg, "MUSER", tmp, sizeof(tmp))) {
    if (tmp[0]) {
      copyStr(mqttUser, sizeof(mqttUser), tmp);
      mqttDirty = true;
    }
  }

  if (jsonGetStringCI(msg, "MQTT_PASS", tmp, sizeof(tmp)) ||
      jsonGetStringCI(msg, "MPASS", tmp, sizeof(tmp)) ||
      jsonGetStringCI(msg, "BROKER_PASS", tmp, sizeof(tmp))) {
    if (tmp[0]) {
      copyStr(mqttPass, sizeof(mqttPass), tmp);
      mqttDirty = true;
    }
  }

  char mpath[40];
  if (jsonGetStringAny(msg, mpath, sizeof(mpath), "MQTT_PATH", "MPATH", "PATH") && mpath[0]) {
    if (mpath[0] != '/') {
      char withSlash[41];
      withSlash[0] = '/';
      copyStr(withSlash + 1, sizeof(withSlash) - 1, mpath);
      copyStr(mqttPath, sizeof(mqttPath), withSlash);
    } else {
      copyStr(mqttPath, sizeof(mqttPath), mpath);
    }
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

  long logica = jsonGetIntCI(msg, "LOGICA_DO_RELE");
  if (logica < 0) logica = jsonGetIntCI(msg, "LOGICA");
  if (logica < 0) logica = jsonGetIntCI(msg, "RELAYMODE");
  if (logica == 1) {
    ack += ",\"logica\":2,\"aviso\":\"esp01_somente_ativo_baixo\"";
  } else if (logica == 2) {
    if (relayActiveHigh) {
      relayActiveHigh = false;
      saveRelayPolarity();
      setAllOutputsOff();
      ack += ",\"logica\":2";
      changed = true;
    }
  }

  if (jsonGetStringAny(msg, tmp, sizeof(tmp), "NEW_PASS", "NEW_ADMIN", "ADMIN_PASS") && tmp[0]) {
    copyStr(adminPass, sizeof(adminPass), tmp);
    saveAdminConfig();
    ack += ",\"new_pass\":true";
    changed = true;
  }

  if (wifiChanged) ack += ",\"wifi_ok\":true";
  (void)mqttChanged;

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
    safeRestart();
  } else {
    publishConfigAck(F("{\"ok\":true,\"sem_alteracoes\":true}"));
  }
}

void onConfigCommand(const char* payload, const size_t size) {
  static unsigned long lastMs = 0;
  static uint32_t lastHash = 0;
  uint32_t h = (uint32_t)size * 2654435761u;
  for (size_t i = 0; i < size; i++) h = (h * 131) + (uint8_t)payload[i];
  unsigned long now = millis();
  if (h == lastHash && (now - lastMs) < 80) return;
  lastMs = now;
  lastHash = h;

  char buf[513];
  size_t n = size < 512 ? size : 512;
  memcpy(buf, payload, n);
  buf[n] = 0;
  handleConfigCommand(buf);
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
    mqtt.publish(topicBuf, relayLogicalOn ? "0" : "1", true);
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
  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleStyle() {
  webServer.sendHeader(F("Cache-Control"), F("public, max-age=86400"));
  webServer.send_P(200, PSTR("text/css; charset=utf-8"), PAGE_CSS);
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
  h += F(".local/ (mDNS), verifique o roteador. Se não conectar, o AP de "
         "configuração só aparece quando o Wi-Fi falha (senha ");
  h += F("C6m8n4d2d3).</p></div>");
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
    html += F("<input name='modelo' required value='ESP-01' "
              "placeholder='ESP-01'>");

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
  html.reserve(2400);
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
  html += F("<label>Modelo da placa</label>");
  html += F("<input name='modelo' value='");
  html += htmlEscape(String(modelo));
  html += F("' placeholder='ESP-01'>");
  html += F("<label>user_id (dono / pai)</label>");
  html += F("<input value='");
  html += htmlEscape(userId[0] ? String(userId) : String("(não definido)"));
  html += F("' disabled>");
  html += F("<p class='hint'>O user_id só pode ser alterado via MQTT "
            "<code>home/&lt;uuid&gt;/config</code> (campo USER_ID + PASS).</p>");
  html += F("<label>Lógica do relé</label>");
  html += F("<select name='relaymode' disabled>");
  html += F("<option value='0' selected>Ativo-baixo (LOW liga — fixo no ESP-01)</option>");
  html += F("</select>");
  html += F("<input type='hidden' name='relaymode' value='0'>");
  html += F("<p class='hint'>Relé v4.0 no GPIO0: só ativo-baixo. Ativo-alto "
            "trava o boot após reiniciar (GPIO0 LOW = modo gravação).</p>");
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
// Em portal AP o ESP-01 precisa de WIFI_AP_STA para enxergar redes.
void handleScan() {
  // Garante rádio STA ativo para scan (senão a lista fica vazia)
  WiFiMode_t mode = WiFi.getMode();
  if (mode == WIFI_AP || mode == WIFI_OFF) {
    WiFi.mode(WIFI_AP_STA);
    delay(200);
  }

  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    webServer.send(200, "application/json", "{\"networks\":[],\"scanning\":1}");
    return;
  }

  if (n == WIFI_SCAN_FAILED) {
    WiFi.scanDelete();
    wifiScanJson = String(F("{\"networks\":[]}"));
    webServer.send(200, "application/json", wifiScanJson);
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
    webServer.send(200, "application/json", wifiScanJson);
    return;
  }

  // Inicia scan assíncrono (não bloqueia)
  WiFi.scanNetworks(true, true);
  webServer.send(200, "application/json", "{\"networks\":[],\"scanning\":1}");
}

// Salva a configuração de uma seção (wifi, mqtt, device ou setup inicial).
void handleSave() {
  String section = webServer.arg("section");
  section.trim();
  bool testing = false;

  // ---------- Setup inicial (todas as opções obrigatórias) ----------
  if (section == "setup") {
    if (isProvisioned()) {
      sendHtml(errorPage("Dispositivo já provisionado. Use as telas Wi-Fi/MQTT/Dispositivo."));
      return;
    }

    String ssid = webServer.arg("wifi_ssid");
    String wpass = webServer.arg("wifi_pass");
    String uid = sanitizeUserId(webServer.arg("user_id"));
    String u = sanitizeUUID(webServer.arg("uuid"));
    String mod = sanitizeModelo(webServer.arg("modelo"));
    String host = webServer.arg("mhost");
    String port = webServer.arg("mport");
    String muser = webServer.arg("muser");
    String mpass = webServer.arg("mpass");
    String mpath = webServer.arg("mpath");
    String na = webServer.arg("newadmin");
    String na2 = webServer.arg("newadmin2");
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
    saveWiFiConfig(ssid.c_str(), wpass.c_str());

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
    safeRestart();
    return;
  }

  String admin = webServer.arg("admin");
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
    safeRestart();
    return;
  }

  if (section == "wifi" || section == "all") {
    String ssid = webServer.arg("wifi_ssid");
    String wpass = webServer.arg("wifi_pass");
    ssid.trim();
    wpass.trim();
    if (ssid.length() > 0) {
      if (strlen(wifiPass) == 0 && wpass.length() == 0) {
        sendHtml(errorPage("Digite a senha do Wi-Fi! Na primeira configuração a senha é obrigatória."));
        return;
      }
      if (wpass.length() == 0) wpass = String(wifiPass);
      if (adoptWiFiChange(ssid.c_str(), wpass.c_str())) testing = true;
    }
  }

  if (section == "mqtt" || section == "all") {
    bool mqttDirty = false;
    String host = webServer.arg("mhost");
    host.trim();
    if (host.length() > 0) {
      copyStr(mqttHost, sizeof(mqttHost), host);
      mqttDirty = true;
    }

    String port = webServer.arg("mport");
    port.trim();
    if (port.length() > 0) {
      int p = port.toInt();
      if (p > 0 && p <= 65535) {
        mqttPort = p;
        mqttDirty = true;
      }
    }

    String muser = webServer.arg("muser");
    muser.trim();
    if (webServer.hasArg("muser")) {
      copyStr(mqttUser, sizeof(mqttUser), muser);
      mqttDirty = true;
    }

    String mpass = webServer.arg("mpass");
    mpass.trim();
    if (mpass.length() > 0) {
      copyStr(mqttPass, sizeof(mqttPass), mpass);
      mqttDirty = true;
    }

    String mpath = webServer.arg("mpath");
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

  if (section == "device" || section == "all") {
    String u = sanitizeUUID(webServer.arg("uuid"));
    if (u.length() > 0 && u != String(uuid)) {
      copyStr(uuid, sizeof(uuid), u);
      saveUUID(String(uuid));
    }

    // modelo editável na página /device; user_id NÃO — só via home/config
    String m = sanitizeModelo(webServer.arg("modelo"));
    if (m.length() > 0 && m != String(modelo)) {
      copyStr(modelo, sizeof(modelo), m);
      saveModelo();
    }

    String rm = webServer.arg("relaymode");
    rm.trim();
    if (rm.length() > 0 || EEPROM.read(391) != 0) {
      relayActiveHigh = false;
      saveRelayPolarity();
    }

    String newAdmin = webServer.arg("newadmin");
    newAdmin.trim();
    if (newAdmin.length() > 0) {
      copyStr(adminPass, sizeof(adminPass), newAdmin);
      saveAdminConfig();
    }
  }

  Serial.println("=== Resumo do que foi salvo ===");
  Serial.print("WiFi SSID: ["); Serial.print(wifiSSID); Serial.println("]");
  Serial.print("UUID: ["); Serial.print(uuid); Serial.println("]");
  Serial.print("MQTT host: ["); Serial.print(mqttHost); Serial.println("]");
  if (testing) Serial.println("Modo CONTINGENCIA: testando candidato.");
  Serial.println("=================================");
  if (testing) {
    sendHtml(okPage("Candidato salvo. O dispositivo vai testar a nova "
                    "configuração (até 6 tentativas). Se falhar, restaura a anterior."));
  } else {
    sendHtml(okPage("Configurações salvas!"));
  }
  delay(800);
  safeRestart();
}

// Teste local do relé sem MQTT — /relay-test?poke=1&time=0
void handleRelayTest() {
  String pokeArg = webServer.arg("poke");
  pokeArg.trim();
  if (pokeArg != "0" && pokeArg != "1") {
    webServer.send(400, "text/plain",
      "Use /relay-test?poke=1 ou /relay-test?poke=0 [&time=ms]");
    return;
  }
  long tm = webServer.arg("time").toInt();
  if (tm < 0) tm = 0;
  char json[48];
  snprintf(json, sizeof(json), "{\"poke\":%s,\"time\":%ld}", pokeArg.c_str(), tm);
  handleRelayCommand(json);
  webServer.send(200, "text/plain",
    String("OK poke=") + pokeArg + " GPIO0 = " +
    (digitalRead(RELAY_PIN) ? "HIGH" : "LOW"));
}

void bindServerRoutes() {
  webServer.on("/", HTTP_GET, handleHome);
  webServer.on("/wifi", HTTP_GET, handleWifiPage);
  webServer.on("/mqtt", HTTP_GET, handleMqttPage);
  webServer.on("/device", HTTP_GET, handleDevicePage);
  webServer.on("/style.css", HTTP_GET, handleStyle);
  webServer.on("/scan", HTTP_GET, handleScan);
  webServer.on("/relay-test", HTTP_GET, handleRelayTest);
  webServer.on("/save", HTTP_POST, handleSave);
}

void startWebServer() {
  if (webServerRunning) return;
  bindServerRoutes();
  webServer.begin();
  webServerRunning = true;
  Serial.println("HTTP server iniciado (SoftAP portal).");
}

void stopWebServer() {
  if (!webServerRunning) return;
  webServer.stop();
  webServerRunning = false;
  Serial.println("HTTP server parado.");
}

// Portal SoftAP bloqueante. timeoutMs=0 → até /save reiniciar (ou provisionado+conectado).
void runConfigPortal(const char* apName, unsigned long timeoutMs) {
  if (apName && apName[0] && WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    WiFi.mode(WIFI_AP_STA);
    delay(100);
    WiFi.softAP(apName, AP_PASSWORD);
    delay(200);
  }
  startWebServer();
  Serial.print("Portal SoftAP ativo: ");
  Serial.print(apName ? apName : "?");
  Serial.print(" | http://");
  Serial.println(WiFi.softAPIP());

  unsigned long t0 = millis();
  while (true) {
    webServer.handleClient();
    yield();
    delay(2);
    checkRelaySchedule();

    if (timeoutMs == 0) {
      if (isProvisioned() && WiFi.status() == WL_CONNECTED) {
        Serial.println("Portal: provisionado e Wi-Fi conectado — saindo.");
        break;
      }
    } else if (millis() - t0 >= timeoutMs) {
      Serial.println("Portal SoftAP: timeout.");
      break;
    }
  }
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
  // ---------- Setup inicial: SEMPRE sobe o AP ----------
  if (!isProvisioned()) {
    Serial.println("Nao provisionado — forcando portal AP home-setup...");

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

    runConfigPortal(apName, 0);

    if (!isProvisioned() && WiFi.status() != WL_CONNECTED) {
      Serial.println("Setup nao concluido. Reiniciando para reabrir o AP...");
      delay(500);
      safeRestart();
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Conectado ao Wi-Fi! IP: ");
      Serial.println(WiFi.localIP());
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      stopWebServer();
      Serial.println("Config via MQTT/app (portal so no setup/recuperacao).");
    }
    return;
  }

  // ---------- Já provisionado ----------
  String apName = deviceHostname();

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
    if (configTestPending()) {
      Serial.println("Contingencia: Wi-Fi candidato falhou nesta passagem — teste continua no loop.");
    } else {
      Serial.println("Abrindo o portal de configuração (AP " + apName + ")...");
      WiFi.mode(WIFI_AP_STA);
      delay(100);
      WiFi.softAP(apName.c_str(), AP_PASSWORD);
      delay(200);
      runConfigPortal(apName.c_str(), 240000UL);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado ao Wi-Fi! IP: ");
    Serial.println(WiFi.localIP());
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    stopWebServer();

    if (MDNS.begin(apName.c_str())) {
      Serial.println("mDNS ativo: " + apName + ".local (sem HTTP portal)");
    } else {
      Serial.println("Falha ao iniciar o mDNS.");
    }
    Serial.println("Config via MQTT/app (portal so no setup/recuperacao).");
  } else if (!configTestPending()) {
    Serial.println("Sem conexão Wi-Fi após o portal. Reiniciando...");
    safeRestart();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (strlen(wifiSSID) == 0 && WiFi.SSID().length() > 0) {
      copyStr(wifiSSID, sizeof(wifiSSID), WiFi.SSID());
      Serial.print("SSID recuperado do rádio: ");
      Serial.println(wifiSSID);
    }
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

  // ESP-01: só STA (sem SoftAP paralelo — economiza RAM)
  WiFi.mode(WIFI_STA);
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
  // ANTES de tudo: GPIO0/GPIO2 HIGH — senão o próximo reset pode travar o boot
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Serial.begin(115200);
  delay(300);

  if (buttonPin >= 0) pinMode(buttonPin, INPUT_PULLUP);
  EEPROM.begin(EEPROM_SIZE);

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

  initOutputs();
  setAllOutputsOff();
  blinkSelfTest();

  Serial.println("=== HOME V010_ALPHA — ESP-01 Relé (sem WiFiManager) ===");
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

  connectWiFi();

  wsClient.beginSSL(mqttHost, mqttPort, mqttPath, (uint8_t*)NULL, "mqtt");
  wsClient.setReconnectInterval(MQTT_RETRY_MS);
  mqtt.begin(wsClient);
  mqtt.subscribe(onGlobalMqtt);   // backup: home/<user>/<uuid>/cmd|config
  mqttReady = true;

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

  // Timer do relé no início (não depende de Wi-Fi/MQTT)
  checkRelaySchedule();

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
        checkRelaySchedule();  // timer pode ter expirado durante o update
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

  if (webServerRunning) webServer.handleClient();
  if (wifiUp) MDNS.update();
  checkRelaySchedule();

  // Monitora o botão (4 s = reconfigurar Wi-Fi) — ESP-01 sem botão: desabilitado
  if (buttonPin >= 0 && digitalRead(buttonPin) == LOW) {
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
      delay(1000);
      safeRestart();
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
      setAllOutputsOff();   // relé desligado (estado seguro)

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
          setAllOutputsOff();   // desliga o relé (falha de internet)
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
