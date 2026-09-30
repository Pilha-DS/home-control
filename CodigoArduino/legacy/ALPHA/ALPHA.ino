// ============================================================================
//  USE_JAVA_GATEWAY=1 → ESP fala HTTP direto com o backend Java (PADRAO).
//  USE_JAVA_GATEWAY=0 → modo legado MQTT/WSS (Mosquitto).
#define USE_JAVA_GATEWAY 1

//  HOME Sistema — Gateway Java (HTTP) ou MQTT/WSS (legado)
//
//  Modo Java (padrao): POST /api/esp/{uuid}/events + GET /api/esp/{uuid}/poll
//  Host/porta na EEPROM = IP:8080 do PC/servidor Spring Boot.
//  Token: device_token gerado ao cadastrar o dispositivo no app.
//
//  MULTI-MODELO (ESP8266): ESP-01/01S, NodeMCU/Generic ESP8266, ESP-12S.
//
//  O modelo é escolhido no setup inicial obrigatório (portal). Cada modelo
//  traz seu próprio mapa de pinos, polaridade, botão e LED onboard:
//    * ESP-01/01S  - relé GPIO0 (ativo-baixo), LED GPIO2, sem botão
//    * NodeMCU     - saídas D1-D8 (relé padrão D1/GPIO5), botão D5 (GPIO14)
//    * ESP-12S     - relé padrão GPIO12 (saídas 4,5,12,13,14,16)
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
//       - lógica do relé (ativo-alto / ativo-baixo)
//       - senha de administrador
//
//  PINOS POR MODELO (definidos em PROFILES[] no código):
//    ESP-01/01S: relé GPIO0 (ativo-baixo), LED GPIO2 onboard, sem botão.
//    NodeMCU:    saídas D1..D8 (GPIO 5,4,0,2,14,12,13,15), botão D5 (GPIO14).
//    ESP-12S:    saídas GPIO 4,5,12,13,14,16 (evita bootstraps 0/2/15).
//
//  AVISO (ESP-01): GPIO0 é pino de boot — o relé pode "clicar" ao resetar e
//  o modelo fica fixo em ativo-baixo. Não há solução 100% por software.
//
//  COMO USAR:
//    1ª vez: conecte no Wi-Fi <b>home-setup</b> (senha C6m8n4d2d3) e abra
//            http://192.168.4.1 — setup completo (Wi-Fi, user_id, UUID, modelo).
//    Depois (provisionado): AP vira &lt;uuid&gt;-setup (ex.: genesis-setup)
//            ou http://<ip-do-esp>/ na mesma rede Wi-Fi.
//
//  TÓPICOS (por usuário + dispositivo):
//    home/<user_id>/<uuid>/cmd
//    home/<user_id>/<uuid>/config
//    home/<user_id>/<uuid>/state|ack|status|ip|config/ack
//    home/logs/<user_id>/<uuid>/basic|advance
//
//  FÁBRICA (seco): só broker MQTT pré-preenchido. Setup obrigatório no AP
//  &lt;uuid&gt;-setup (http://192.168.4.1) — todos os campos.
//
//  COMANDO (home/<user>/<uuid>/cmd):
//    {"poke":1,"time":0,"pin":4}   // "pin" opcional = saída lógica do modelo
//
//  CONFIG (home/<user>/<uuid>/config) — PASS obrigatório:
//    {
//      "PASS":"C6m8n4d2d3",
//      "UUID":"novo_nome",
//      "USER_ID":"pai",
//      "MODELO":"ESP-01 | NodeMCU | ESP-12S",
//      "WIFI_SSID":"rede","WIFI_PASS":"senha",
//      "MQTT_HOST":"mosquito.omny.tec.br","MQTT_PORT":443,
//      "MQTT_USER":"cardoso","MQTT_PASS":"xxx","MQTT_PATH":"/",
//      "LOGICA_DO_RELE":2,
//      "AP_OPEN":1,
//      "NEW_PASS":"nova_admin"
//    }
//    USER_ID = dono/pai (só via home/<uuid>/config; na página /device só leitura)
//    MODELO = nome da placa (escolha entre ESP-01, NodeMCU, ESP-12S)
//    LOGICA_DO_RELE = 1 (HIGH liga) ou 2 (LOW liga; fixo no ESP-01) | NEW_PASS = nova senha admin
//    AP_OPEN = 1 liga AP &lt;uuid&gt;-setup sempre | 0 desliga (só via /config ou portal)
//    Resposta em home/<uuid>/config/ack
//
//  LOGS basic (home/logs/<uuid>/basic) — a cada 20 s:
//    {"internet":"SSID","server":"host","modelo":"ESP-01","uuid":"...","user_id":"...","ip":"192.168.x.x"}
//  LOGS advance (home/logs/<uuid>/advance) — a cada 20 s (um único JSON):
//    {"temperature":0,"free_heap":..,"rssi":..,"state":0|1,"connected_time":..}
//    state: 0=ligado, 1=desligado | temperature: 0 no ESP-01 (sem A0)
//
//  BIBLIOTECAS (Gerenciador de Bibliotecas do Arduino IDE):
//    Modo Java (USE_JAVA_GATEWAY=1 — padrao):
//      1. Placa ESP8266 no Board Manager (ex.: Generic ESP8266 / NodeMCU 1.0)
//         (ESP8266WebServer + DNSServer ja vem no core — SEM WiFiManager)
//    Modo MQTT legado (USE_JAVA_GATEWAY=0):
//      + WebSockets_Generic (khoih-prog)
//      + MQTTPubSubClient_Generic (khoih-prog)
// ============================================================================

// Bibliotecas
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

#if USE_JAVA_GATEWAY
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#else
// MQTT sobre WebSocket Seguro (WSS) — DEVE vir antes do MQTT
#include <WebSocketsClient_Generic.h>
#ifndef MQTTPUBSUBCLIENT_USE_WEBSOCKETS
#define MQTTPUBSUBCLIENT_USE_WEBSOCKETS true
#endif
#include <MQTTPubSubClient_Generic.h>
#endif

// ------------------------- Valores padrão -----------------------------------
// Usados na primeira gravação; depois podem ser alterados pela interface web
// (mantidos na EEPROM).
#define JAVA_DEFAULT_HOST   "mqtt.omny.app.br"
#define JAVA_DEFAULT_PORT   443

#define MQTT_DEFAULT_HOST   "mqtt.omny.app.br"  // EEPROM: host do backend Java
#define MQTT_DEFAULT_PORT   443
#define MQTT_DEFAULT_USER   "cardoso"                // usuário do broker
#define MQTT_DEFAULT_PASS   "C6m8n4d2d3"             // senha do broker
#define MQTT_DEFAULT_PATH   "/"                  // path do WebSocket (este broker usa "/")

#define ADMIN_DEFAULT_PASS  "C6m8n4d2d3"   // senha para mudar as configurações
// AP fixo no setup/reset — sempre "home-setup" até concluir provisionamento.
#define FACTORY_SETUP_AP    "home-setup"
// Nome do AP de configuração é DINÂMICO: sempre &lt;uuid&gt;-setup (senha AP_PASSWORD).
#define AP_PASSWORD         "C6m8n4d2d3"   // senha do AP de configuração

// Nome do AP mantido em buffer estatico.
static char portalApName[33] = "";

#define MQTT_RETRY_MS       10000UL        // tenta reconectar a cada 10 s
// Comando/config por UUID (não usa mais home/cmd nem home/config globais)

// ------------------- Perfis de modelo de placa -------------------------------
// Cada modelo define: mapa de pinos lógicos (1..N) -> GPIO, pino padrão,
// botão (4s), LED onboard, polaridade padrão e restrições de boot.
// O modelo é escolhido no setup inicial e guardado na EEPROM (EE_MODELO).

struct BoardProfile {
  const char* name;          // id canônico (ex.: "ESP-01")
  const char* label;         // rótulo exibido no dropdown
  const char* desc;          // dica exibida no setup
  const int*  outGpio;       // mapa pin lógico (1..N) -> GPIO (-1 = inválido)
  int  outPinMin;
  int  outPinMax;
  int  outPinDefault;        // pin lógico padrão (relé/saída principal)
  int  buttonGpio;           // GPIO do botão (4s) ou -1
  int  ledGpio;              // GPIO do LED onboard ou -1
  bool ledActiveLow;         // LED acende em LOW?
  bool activeHighDefault;    // polaridade padrão do relé
  bool forceActiveLow;       // só ativo-baixo (ESP-01 GPIO0)
};

// ESP-01 / ESP-01S: única saída é GPIO0 (relé). GPIO2 = LED onboard (NÃO é saída).
const int OUT_ESP01[2]     = { -1, 0 };
// NodeMCU / Generic ESP8266 MOD: D1..D8
const int OUT_NODEMCU[9]   = { -1, 5, 4, 0, 2, 14, 12, 13, 15 };
// ESP-12S/12E/12F: GPIO seguros (12,13,14,16,5,4) — evita bootstraps 0/2/15
const int OUT_ESP12[7]     = { -1, 12, 13, 14, 16, 5, 4 };

const BoardProfile PROFILES[] = {
  { "ESP-01",  "ESP-01 / ESP-01S (relé v4.0)",
    "Relé GPIO0 (LOW liga) | LED GPIO2 | sem botão | só ativo-baixo",
    OUT_ESP01,   1, 1, 1,  -1,  2, true,  false, true  },
  { "NodeMCU", "NodeMCU / Generic ESP8266 MOD",
    "Saídas D1-D8 | relé padrão D1 (GPIO5) | botão D5 (GPIO14)",
    OUT_NODEMCU, 1, 8, 1,  14,  2, true,  true,  false },
  { "ESP-12S", "ESP-12S / ESP-12E / ESP-12F",
    "Relé padrão GPIO12 | saídas GPIO4,5,12,13,14,16 | sem LED/botão",
    OUT_ESP12,   1, 6, 1,  -1, -1, false, true,  false },
};
const int PROFILE_COUNT = sizeof(PROFILES) / sizeof(PROFILES[0]);
const int PROFILE_DEFAULT = 1;   // NodeMCU / Generic ESP8266

const BoardProfile* activeProfile = &PROFILES[PROFILE_DEFAULT];

int relayPin = -1;          // GPIO ativo no momento (resolvido do perfil)
int relayLogicalPin = 1;    // pin lógico atual (1..N do perfil)
int buttonPin = -1;         // GPIO do botão (ou -1 = sem botão)
int ledGpio = -1;           // GPIO do LED onboard (ou -1)
bool ledActiveLow = true;   // LED onboard acende em LOW?

// Polaridade do relé: true = ativo-alto (HIGH liga), false = ativo-baixo (LOW liga)
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
const unsigned long intervalSensors = 60000;      // 60 s (telemetria — HTTPS esquenta)
const unsigned long pingInterval = 60000;         // 60 s (teste de internet)
const unsigned long intervalAvailability = 120000; // 2 min (disponibilidade)

// Logs das últimas 24h (legado local; MQTT usa só basic/advance)
String logs[24];

// Identidade: vazia de fábrica — preenchida no setup inicial (portal)
char userId[40] = "";
char modelo[30] = "";
bool deviceProvisioned = false;

// Monitoramento
unsigned long lastReconnectAttempt = 0;
#if !USE_JAVA_GATEWAY
unsigned long lastMqttAttempt = 0;
unsigned long lastWsRestart = 0;
#endif
uint8_t wifiReconnectFails = 0;
uint8_t internetFailCount = 0;
bool isConnected = false;
#if !USE_JAVA_GATEWAY
bool lastMqttState = false;
#endif
bool lastPingOk = true;   // estado anterior do teste de internet (para não repetir log)

// Agendamento do relé (comando JSON com "time" > 0)
bool relayScheduled = false;
unsigned long relayScheduleStart = 0;
unsigned long relayScheduleDuration = 0;
bool relayScheduleNextOn = false;   // estado a aplicar quando o tempo expirar

void checkRelaySchedule();  // protótipo — definida junto ao controle do relé

// ------------------------------ MQTT (WSS) ----------------------------------
#if !USE_JAVA_GATEWAY
WebSocketsClient wsClient;
MQTTPubSub::PubSubClient<512> mqtt;   // buffer 512 para os logs
#endif

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

// Token de aplicação por dispositivo (gerado pelo backend; opcional).
// Se preenchido, comandos/config via MQTT precisam trazer "token" igual.
char deviceToken[48] = "";   // espSecret (ate 47 chars; backend gera 32–40)

// UUID do dispositivo (identidade; usado no client ID e nos tópicos)
char uuid[40] = "";

// Buffer para montar os tópicos home/<user>/<uuid>/…
char topicBuf[96];

// MQTT só pode ser consultado depois do mqtt.begin()
#if !USE_JAVA_GATEWAY
bool mqttReady = false;
#endif

// Servidor web nativo (mais estável que WiFiManager no softAP)
ESP8266WebServer webServer(80);
DNSServer dnsServer;
static bool dnsServerActive = false;
static unsigned long configPortalTimeoutMs = 0;  // 0 = sem timeout
static bool mdnsActive = false;
static String mdnsActiveHost = "";
static IPAddress mdnsActiveIp(0, 0, 0, 0);
static unsigned long lastMdnsRetry = 0;
static bool configWebPortalActive = false;
static bool recoveryApActive = false;
static bool wifiRecoveryMode = false;
static unsigned long wifiLostAt = 0;

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
//   703    magic token (0x5F) — token de aplicacao por dispositivo
//   704..  token do dispositivo (40)
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
#define EE_TOKEN_MAGIC         703   // 0x5F = token de aplicacao presente
#define EE_TOKEN               704   // espSecret (48 bytes; antes 40 — cabia so 39 no RAM)
#define EE_AP_OPEN_MAGIC       760   // 0xA1 = preferencia do AP salva (era 745)
#define EE_AP_OPEN             761   // 1 = AP config sempre aberto (padrao)
#define EE_TOKEN_LEN           48

bool cfgWifiPending = false;
bool cfgMqttPending = false;
uint8_t cfgAttempts = 0;

// AP <uuid>-setup: desligado por padrao (AP+STA esquenta o ESP8266).
bool maintenanceApEnabled = false;  // AP+STA esquenta muito; so sob demanda

// Protótipos — usadas pela contingência antes das definições completas
void publishMQTT(const char* suffix, const char* msg, bool retained = false);
void sendLogsMQTT(String logMessage);
void restartMqttTransport();
String configApName();
String deviceHostname();
String mdnsHostname();
void refreshPortalApName();
void startConfigPortalAp();
void restartMdns();
void ensureMdnsIfNeeded();
void loadMaintenanceApSetting();
void saveMaintenanceApSetting(bool enabled);
void applyWifiPowerPolicy();
void stopMaintenanceAP();
void ensureMaintenanceAP();
void ensureRecoveryAP();
void ensureConfigWebServer();
void wifiServerProcess();
bool bringUpConfigAP(bool withSta);
void forceWifiReconnect(bool hard = false);
#if USE_JAVA_GATEWAY
extern bool javaGatewayReady;
bool javaPostEvent(const char* type, const char* payload);
void javaQueueEvent(const char* type, const char* payload);
void javaResetHttpClient();
bool connectJavaGateway();
void javaGatewayLoop();
#endif

void writeStringEEPROM(int addr, const String& s, int maxLen) {
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, i < (int)s.length() ? s.charAt(i) : 0);
  }
}

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
    // Migração DNS: API saiu de automacao → mqtt.omny.app.br
    if (strcmp(mqttHost, "automacao.omny.app.br") == 0) {
      copyStr(mqttHost, sizeof(mqttHost), String(JAVA_DEFAULT_HOST));
      if (mqttPort != 443) mqttPort = JAVA_DEFAULT_PORT;
      saveMQTTConfig();
      Serial.println("Host migrado: automacao.omny.app.br -> mqtt.omny.app.br");
    }
  }
}

void saveDeviceToken() {
  EEPROM.write(EE_TOKEN_MAGIC, 0x5F);
  writeStringEEPROM(EE_TOKEN, String(deviceToken), EE_TOKEN_LEN);
  EEPROM.commit();
  Serial.println("Token do dispositivo salvo na EEPROM.");
}

void loadDeviceToken() {
  if (EEPROM.read(EE_TOKEN_MAGIC) == 0x5F) {
    String v = readStringEEPROM(EE_TOKEN, EE_TOKEN_LEN);
    if (v.length() > 0) copyStr(deviceToken, sizeof(deviceToken), v);
  }
}

void clearDeviceToken() {
  deviceToken[0] = 0;
  EEPROM.write(EE_TOKEN_MAGIC, 0);
  writeStringEEPROM(EE_TOKEN, "", EE_TOKEN_LEN);
}

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
    ensureMaintenanceAP();
    ensureConfigWebServer();
    restartMdns();
    restartMqttTransport();
  } else {
    Serial.println("Estavel tambem falhou — abrindo portal.");
    startConfigPortalAp();
  }
}

void processConfigTest() {
  if (!configTestPending()) return;
#if USE_JAVA_GATEWAY
  if (!javaGatewayReady && cfgMqttPending) return;
#else
  if (!mqttReady && cfgMqttPending) return;
#endif

  static unsigned long attemptStarted = 0;
  unsigned long now = millis();
  if (attemptStarted == 0) attemptStarted = now;

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
#if USE_JAVA_GATEWAY
  bool mqttOk = !cfgMqttPending || javaGatewayReady;
#else
  bool mqttOk = mqttReady && mqtt.isConnected();
#endif

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

bool adoptWiFiChange(const String& ssid, const String& pass) {
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
  // De fábrica fica vazio — preenchido antes do AP (ensureDefaultUUID)
}

// Garante UUID sugerido no formulário (RAM). Só grava EEPROM no setup inicial.
void ensureDefaultUUID() {
  if (strlen(uuid) > 0) return;
  String u = allocateHomeName();
  copyStr(uuid, sizeof(uuid), u);
  Serial.print("UUID sugerido (formulario): ");
  Serial.println(uuid);
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

  // limpa contingência
  EEPROM.write(EE_CFG_MAGIC, 0);
  cfgWifiPending = false;
  cfgMqttPending = false;
  cfgAttempts = 0;

  saveProvisioned(false);
  saveMaintenanceApSetting(true);
  clearDeviceToken();
  EEPROM.commit();

  // Apaga Wi-Fi guardado pelo SDK (senão reconecta sozinho e o AP some)
  WiFi.persistent(true);
  WiFi.disconnect(true);
  delay(100);
  ESP.eraseConfig();
  delay(200);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  Serial.println("Reset seco concluido. Apos reiniciar: Wi-Fi home-setup / http://192.168.4.1");
}

// Nome do AP de configuração: home-setup (seco/reset) ou <uuid>-setup (provisionado).
String configApName() {
  if (!isProvisioned()) {
    return String(F(FACTORY_SETUP_AP));
  }
  String base;
  if (strlen(uuid) > 0) {
    base = sanitizeUUID(String(uuid));
  } else {
    char buf[16];
    snprintf(buf, sizeof(buf), "esp%06x", (unsigned)(ESP.getChipId() & 0xFFFFFF));
    base = String(buf);
  }
  base.toLowerCase();
  base.replace("_", "-");
  if (base.length() == 0) base = "esp";
  const int suffixLen = 6;  // "-setup" — SSID Wi-Fi max 32 chars
  if ((int)base.length() > 32 - suffixLen) {
    base = base.substring(0, 32 - suffixLen);
  }
  return base + "-setup";
}

void refreshPortalApName() {
  String ap = configApName();
  copyStr(portalApName, sizeof(portalApName), ap);
}

// Portal de configuração: softAP + web nativo (sem WiFiManager).
void startConfigPortalAp() {
  if (!bringUpConfigAP(false)) {
    Serial.println("FALHA CRITICA: AP home-setup nao subiu — verifique alimentacao.");
    return;
  }
  recoveryApActive = true;
  ensureConfigWebServer();

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  dnsServerActive = true;

  Serial.print("Conecte no Wi-Fi ");
  Serial.print(portalApName);
  Serial.print(" (senha ");
  Serial.print(AP_PASSWORD);
  Serial.println(") e abra http://192.168.4.1");

  unsigned long t0 = millis();
  while (true) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    yield();
    // Setup grava e reinicia — se voltar aqui, sai por timeout ou Wi-Fi.
    if (isProvisioned() && WiFi.status() == WL_CONNECTED) break;
    if (configPortalTimeoutMs > 0 && (millis() - t0) >= configPortalTimeoutMs) {
      Serial.println("Timeout do portal de configuração.");
      break;
    }
    delay(2);
  }

  dnsServer.stop();
  dnsServerActive = false;
}

// Nome mDNS = UUID sanitizado (http://&lt;uuid&gt;.local/). Fallback = configApName sem sufixo.
String deviceHostname() {
  String h = mdnsHostname();
  if (h.length() > 0) return h;
  String ap = configApName();
  if (ap.endsWith("-setup")) ap.remove(ap.length() - 6);
  return ap;
}

// Hostname mDNS a partir do UUID (RFC: minúsculas, _ vira -).
String mdnsHostname() {
  if (!isProvisioned() || strlen(uuid) == 0) return String("");
  String n = sanitizeUUID(String(uuid));
  n.toLowerCase();
  n.replace("_", "-");
  if (n.length() == 0) return String("");
  return n;
}

void resetPortalRuntimeState() {
  if (dnsServerActive) {
    dnsServer.stop();
    dnsServerActive = false;
  }
  configWebPortalActive = false;
  recoveryApActive = false;
  wifiRecoveryMode = false;
  wifiLostAt = 0;
}

// Sobe softAP <uuid>-setup. Sem STA = modo AP puro (estável p/ celular conectar).
bool bringUpConfigAP(bool withSta) {
  refreshPortalApName();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.persistent(false);

  bool ok = false;
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (withSta && WiFi.status() == WL_CONNECTED) {
      if (WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
        delay(150);
      }
    } else {
      WiFi.disconnect(true);
      delay(100);
      WiFi.mode(WIFI_AP);
      delay(150);
    }

    WiFi.softAPdisconnect(true);
    delay(80);

    const IPAddress apIp(192, 168, 4, 1);
    const IPAddress gateway(192, 168, 4, 1);
    const IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, gateway, subnet);

    ok = WiFi.softAP(portalApName, AP_PASSWORD, 6, 0, 4);
    delay(300);
    ok = ok && (WiFi.softAPIP() == apIp);
    if (ok) break;
    Serial.print("softAP tentativa ");
    Serial.print(attempt);
    Serial.println(" falhou — repetindo...");
    delay(400);
  }

  Serial.print("softAP ");
  Serial.print(portalApName);
  Serial.print(ok ? " OK" : " FALHOU");
  Serial.print(withSta ? " (AP+STA)" : " (so AP)");
  Serial.print(" | IP ");
  Serial.println(WiFi.softAPIP());
  if (ok) {
    Serial.print("Senha AP: ");
    Serial.println(AP_PASSWORD);
  }
  return ok;
}

void restartMdns() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mdnsActive) {
      MDNS.end();
      mdnsActive = false;
      mdnsActiveHost = "";
      mdnsActiveIp = IPAddress(0, 0, 0, 0);
    }
    return;
  }
  String host = mdnsHostname();
  if (host.length() == 0) return;

  IPAddress staIp = WiFi.localIP();
  if (staIp == IPAddress(0, 0, 0, 0)) return;

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.hostname(host.c_str());

  bool apSta = (WiFi.getMode() == WIFI_AP_STA &&
                WiFi.softAPIP() == IPAddress(192, 168, 4, 1));
  // Igual BETA: não reinicia mDNS se host/IP não mudaram (evita derrubar anúncio).
  if (mdnsActive && mdnsActiveHost == host && mdnsActiveIp == staIp) return;

  if (mdnsActive) {
    MDNS.end();
    mdnsActive = false;
    mdnsActiveHost = "";
    mdnsActiveIp = IPAddress(0, 0, 0, 0);
    delay(10);
  }

  bool ok = false;
  if (apSta) {
    // AP+STA: tenta IP da LAN; fallback ao estilo BETA se a core falhar.
    ok = MDNS.begin(host.c_str(), staIp);
    if (!ok) ok = MDNS.begin(host.c_str());
  } else {
    ok = MDNS.begin(host.c_str());
  }

  if (ok) {
    MDNS.addService("http", "tcp", 80);
    mdnsActive = true;
    mdnsActiveHost = host;
    mdnsActiveIp = staIp;
    Serial.print("mDNS ativo: http://");
    Serial.print(host);
    Serial.print(".local/ → ");
    Serial.println(staIp);
  } else {
    Serial.print("Falha ao iniciar mDNS para ");
    Serial.print(host);
    Serial.print(" (IP ");
    Serial.print(staIp);
    Serial.println(")");
  }
}

void ensureMdnsIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || !isProvisioned()) return;
  unsigned long now = millis();
  if (!mdnsActive && now - lastMdnsRetry >= 30000UL) {
    lastMdnsRetry = now;
    restartMdns();
  }
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
  bool hasSaved = (EEPROM.read(390) == 0x5A);
  bool savedHigh = (EEPROM.read(391) == 1);
  if (activeProfile && activeProfile->forceActiveLow) {
    relayActiveHigh = false;   // modelo exige ativo-baixo (GPIO0 é pino de boot)
  } else if (hasSaved) {
    relayActiveHigh = savedHigh;
  } else {
    relayActiveHigh = activeProfile ? activeProfile->activeHighDefault : true;
  }
  saveRelayPolarity();
  Serial.print("Polaridade: ");
  Serial.print(relayActiveHigh ? "ativo-alto (HIGH liga)" : "ativo-baixo (LOW liga)");
  Serial.print(" | Relé GPIO");
  Serial.print(relayPin);
  Serial.print(" (pin ");
  Serial.print(relayLogicalPin);
  Serial.println(")");
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
#if USE_JAVA_GATEWAY
  (void)retained;
  javaQueueEvent(suffix, msg);
#else
  if (mqttReady && mqtt.isConnected()) {
    buildTopic(suffix);
    mqtt.publish(topicBuf, msg, retained);
  }
#endif
}

void publishAbsolute(const char* topic, const char* msg, bool retained = false) {
#if USE_JAVA_GATEWAY
  (void)retained;
  (void)topic;
  // logs: home/logs/<user>/<uuid>/<suffix>
  const char* p = strrchr(topic, '/');
  if (p && p[1]) {
    char typeBuf[24];
    if (strstr(topic, "/logs/") != nullptr) {
      snprintf(typeBuf, sizeof(typeBuf), "logs/%s", p + 1);
      javaQueueEvent(typeBuf, msg);
    }
  }
#else
  if (mqttReady && mqtt.isConnected()) {
    mqtt.publish(topic, msg, retained);
  }
#endif
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
#if USE_JAVA_GATEWAY
  return WiFi.status() == WL_CONNECTED && isProvisioned();
#else
  return mqttReady && mqtt.isConnected();
#endif
}

// Eventos pontuais só no Serial — telemetria MQTT = basic + advance
void sendLogsMQTT(String logMessage) {
  Serial.print("[log] ");
  Serial.println(logMessage);
}

// ------------------------- Controle do relé (com JSON) -----------------------
// Publicar em home/<uuid>/cmd:
//   {"poke":1,"time":0}
// poke = 1 liga / 0 desliga | time = ms até inverter (0 = permanente)
//
// Ex.: já ligado + {"poke":1,"time":1000}
//   → mantém LIGADO e DESLIGA automaticamente após 1000 ms.

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

// Valida o secret do usuario (opcional). Se deviceToken estiver vazio,
// aceita qualquer comando (comportamento antigo). Caso contrario, exige
// o campo "token" igual ao salvo na EEPROM (secret unico por conta).
bool tokenOk(const String& json) {
  if (strlen(deviceToken) == 0) return true;
  String tok = jsonGetStringCI(json, "token");
  if (tok.length() == 0) tok = jsonGetStringCI(json, "sig");
  // Sem campo token no JSON: confia no transporte (poll HTTP / topico MQTT ACL).
  if (tok.length() == 0) return true;
  return tok == String(deviceToken);
}

// ------------------------- Perfis de modelo -------------------------------
// Resolve o perfil a partir do nome salvo (case-insensitive).
const BoardProfile* findProfile(const char* name) {
  if (!name) return nullptr;
  for (int i = 0; i < PROFILE_COUNT; i++) {
    const char* a = PROFILES[i].name;
    const char* b = name;
    while (*a && *b) {
      char ca = *a; if (ca >= 'a' && ca <= 'z') ca -= 32;
      char cb = *b; if (cb >= 'a' && cb <= 'z') cb -= 32;
      if (ca != cb) break;
      a++; b++;
    }
    if (*a == 0 && *b == 0) return &PROFILES[i];
  }
  return nullptr;
}

// Converte pin lógico (1..N do perfil) -> GPIO; -1 se inválido.
int logicalPinToGpio(int logicalPin) {
  if (!activeProfile) return -1;
  if (logicalPin < activeProfile->outPinMin || logicalPin > activeProfile->outPinMax) return -1;
  return activeProfile->outGpio[logicalPin];
}

// Seleciona a saída ativa pelo pin lógico.
void selectOutputPin(int logicalPin) {
  int gpio = logicalPinToGpio(logicalPin);
  if (gpio < 0) return;
  relayLogicalPin = logicalPin;
  relayPin = gpio;
  pinMode(relayPin, OUTPUT);
}

// Pinos de bootstrap do ESP8266 (GPIO0, GPIO2, GPIO15). Não podem ser
// acionados/alterados no início: GPIO0/2 em LOW entra em modo flash.
bool isBootstrapGpio(int gpio) {
  return gpio == 0 || gpio == 2 || gpio == 15;
}

// Aplica o perfil de modelo: pinos, polaridade, botão e LED onboard.
void applyProfile(const BoardProfile* p) {
  if (!p) return;
  activeProfile = p;
  relayActiveHigh = p->forceActiveLow ? false : p->activeHighDefault;
  buttonPin = p->buttonGpio;
  ledGpio = p->ledGpio;
  ledActiveLow = p->ledActiveLow;
  relayLogicalPin = p->outPinDefault;
  relayPin = logicalPinToGpio(relayLogicalPin);
  // LED onboard em pino de bootstrap (ex.: GPIO2 no ESP-01/NodeMCU) fica
  // intocado — não configura OUTPUT nem escreve nada no início.
  if (ledGpio >= 0 && !isBootstrapGpio(ledGpio)) {
    pinMode(ledGpio, OUTPUT);
    digitalWrite(ledGpio, ledActiveLow ? HIGH : LOW);
  }
  if (buttonPin >= 0) pinMode(buttonPin, INPUT_PULLUP);
}

// Inicializa todas as saídas do perfil em estado seguro (desligado).
void initAllOutputPins() {
  if (!activeProfile) return;
  for (int p = activeProfile->outPinMin; p <= activeProfile->outPinMax; p++) {
    int gpio = activeProfile->outGpio[p];
    if (gpio < 0 || gpio == buttonPin) continue;
    pinMode(gpio, OUTPUT);
    if (relayActiveHigh) digitalWrite(gpio, LOW);
    else digitalWrite(gpio, HIGH);
  }
  selectOutputPin(activeProfile->outPinDefault);
}

// Aplica o estado físico do relé respeitando a polaridade do perfil.
void setRelayOn(bool on) {
  if (relayPin < 0) return;
  if (relayActiveHigh) digitalWrite(relayPin, on ? HIGH : LOW);
  else digitalWrite(relayPin, on ? LOW : HIGH);
}

// LED onboard (quando existir). ledActiveLow indica se acende em LOW.
void setLedOn(bool on) {
  if (ledGpio < 0) return;
  int level = ledActiveLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW);
  digitalWrite(ledGpio, level);
}

// Inicializa as saídas em estado seguro (relé desligado, LED apagado).
void initOutputs() {
  initAllOutputPins();
  setAllOutputsOff();
  if (ledGpio >= 0 && !isBootstrapGpio(ledGpio)) {
    pinMode(ledGpio, OUTPUT);
    digitalWrite(ledGpio, ledActiveLow ? HIGH : LOW);
  }
}

void setAllOutputsOff() {
  if (!activeProfile) { relayLogicalOn = false; return; }
  for (int p = activeProfile->outPinMin; p <= activeProfile->outPinMax; p++) {
    int gpio = activeProfile->outGpio[p];
    if (gpio < 0 || gpio == buttonPin) continue;
    if (relayActiveHigh) digitalWrite(gpio, LOW);
    else digitalWrite(gpio, HIGH);
  }
  relayLogicalOn = false;
}

// Reinício seguro: deixa os pinos de boot do ESP8266 em nível correto.
void safeRestart() {
  Serial.println("Reinicio seguro (pinos de boot em estado seguro)...");
  Serial.flush();
  relayScheduled = false;
  pinMode(0, INPUT_PULLUP);    // GPIO0 -> HIGH (boot normal)
  pinMode(2, INPUT_PULLUP);    // GPIO2 -> HIGH (boot normal)
  pinMode(15, INPUT);          // GPIO15 -> LOW (pull-down externo)
  WiFi.softAPdisconnect(true);
  if (dnsServerActive) {
    dnsServer.stop();
    dnsServerActive = false;
  }
  WiFi.mode(WIFI_OFF);
  delay(300);
  ESP.restart();
  while (true) { delay(1000); }
}

// Aplica o estado do relé (GPIO primeiro; MQTT depois — evita travar com GPIO0 LOW)
void applyRelayState(bool on) {
  relayLogicalOn = on;
  setRelayOn(on);   // hardware imediatamente
  Serial.print("Saída GPIO");
  Serial.print(relayPin);
  Serial.print(" (pin ");
  Serial.print(relayLogicalPin);
  Serial.print(") = ");
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
  if (ledGpio < 0) {
    Serial.println("Sem LED onboard neste modelo — pulando self-test.");
    return;
  }
  // Pinos de bootstrap do ESP8266 (GPIO0/2/15) não podem ser acionados no
  // início. Nunca piscar neles.
  if (isBootstrapGpio(ledGpio)) {
    Serial.println("LED onboard em pino de bootstrap — pulando self-test.");
    return;
  }
  // Não piscar no GPIO do relé (evita "pulso" no relé durante o boot).
  if (ledGpio == relayPin) {
    Serial.println("LED onboard no mesmo GPIO do relé — pulando self-test.");
    return;
  }
  Serial.print("Teste LED GPIO");
  Serial.print(ledGpio);
  Serial.println(": 3 piscadas...");
  for (int i = 0; i < 3; i++) {
    setLedOn(true);
    delay(250);
    setLedOn(false);
    delay(250);
  }
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

#if !USE_JAVA_GATEWAY
  // MQTT legado: exige token no JSON. No Java gateway o poll HTTP ja autenticou.
  if (!tokenOk(msg)) {
    Serial.println("cmd: token invalido — ignorado.");
    publishMQTT("ack", "erro:token_invalido");
    return;
  }
#else
  // Se o backend mandar USER_SECRET/token novo, atualiza EEPROM (rotacao).
  {
    String incoming = jsonGetStringCI(msg, "USER_SECRET");
    if (incoming.length() == 0) incoming = jsonGetStringCI(msg, "USER_TOKEN");
    if (incoming.length() == 0) incoming = jsonGetStringCI(msg, "ESP_SECRET");
    if (incoming.length() == 0) incoming = jsonGetStringCI(msg, "DEVICE_TOKEN");
    if (incoming.length() == 0) incoming = jsonGetStringCI(msg, "TOKEN");
    if (incoming.length() == 0) incoming = jsonGetStringCI(msg, "token");
    if (incoming.length() >= 8 && incoming != String(deviceToken) &&
        incoming.length() < (int)sizeof(deviceToken)) {
      copyStr(deviceToken, sizeof(deviceToken), incoming);
      saveDeviceToken();
      Serial.println("cmd: espSecret atualizado via poll.");
    }
  }
#endif

  // Tópico já é home/<uuid>/cmd — não precisa de "device" no JSON
  // (se vier device e for outro UUID, ignora por segurança)
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

  // "pin" opcional: seleciona a saída lógica do perfil (1..N)
  if (pinArg >= 0) {
    if (logicalPinToGpio((int)pinArg) < 0) {
      Serial.println("JSON invalido: pin fora do modelo.");
      publishMQTT("ack", "erro:pin_invalido");
      return;
    }
    selectOutputPin((int)pinArg);
  }

  bool on = (poke == 1);
  unsigned long t = (unsigned long)tm;

  // 1) Hardware + timer ANTES de MQTT/logs (mais seguro)
  relayScheduled = false;
  relayLogicalOn = on;
  setRelayOn(on);

  if (t > 0) {
    relayScheduled = true;
    relayScheduleStart = millis();
    relayScheduleDuration = t;
    relayScheduleNextOn = !on;   // após o tempo, inverte (liga→desliga / desliga→liga)
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

  char ack[96];
  snprintf(ack, sizeof(ack), "ok:poke=%d,time=%lu,pin=%d,gpio=%d,next=%d",
           on ? 1 : 0, t, relayLogicalPin, relayPin, relayScheduleNextOn ? 1 : 0);

#if USE_JAVA_GATEWAY
  // So state+ack. logs/basic no poke = +1 TLS e o chip cai.
  publishMQTT("state", relayLogicalOn ? "0" : "1", true);
  publishMQTT("ack", ack);
#else
  publishStateAndBasic();
  publishMQTT("ack", ack);
#endif
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

// ------------------- Configuração via MQTT (home/<uuid>/config) -------------------
// PASS é obrigatório. Resposta em home/<uuid>/config/ack (JSON).

void publishConfigAck(const String& json) {
  publishMQTT("config/ack", json.c_str(), true);
  Serial.print("home/<uuid>/config -> ");
  Serial.println(json);
}

// Primeira chave string não vazia dentre as alternativas
String jsonGetStringAny(const String& json, const char* k1,
                        const char* k2 = nullptr, const char* k3 = nullptr,
                        const char* k4 = nullptr, const char* k5 = nullptr) {
  String v = jsonGetStringCI(json, k1);
  if (v.length() > 0) return v;
  if (k2) { v = jsonGetStringCI(json, k2); if (v.length() > 0) return v; }
  if (k3) { v = jsonGetStringCI(json, k3); if (v.length() > 0) return v; }
  if (k4) { v = jsonGetStringCI(json, k4); if (v.length() > 0) return v; }
  if (k5) { v = jsonGetStringCI(json, k5); if (v.length() > 0) return v; }
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

  // Secret do usuario (unico por conta). PASS admin ja validou acima.
  String newUserSecret = jsonGetStringAny(msg, "USER_SECRET", "USER_TOKEN", "ESP_SECRET",
                                          "DEVICE_TOKEN", "TOKEN");
  if (strlen(deviceToken) > 0) {
    bool rotating = newUserSecret.length() > 0 && newUserSecret != String(deviceToken);
    if (!tokenOk(msg) && !rotating) {
      Serial.println("config: secret invalido — ignorado.");
      publishConfigAck(F("{\"ok\":false,\"erro\":\"token_invalido\"}"));
      return;
    }
  }
  if (newUserSecret.length() > 0) {
    copyStr(deviceToken, sizeof(deviceToken), newUserSecret);
    saveDeviceToken();
    Serial.println("config: secret do usuario gravado.");
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

  String newUser = sanitizeUserId(jsonGetStringAny(msg, "USER_ID", "USERID", "user_id"));
  if (newUser.length() > 0 && newUser != String(userId)) {
    copyStr(userId, sizeof(userId), newUser);
    saveUserId();
    ack += ",\"user_id\":\"" + String(userId) + "\"";
    changed = true;
  }

  String newModelo = sanitizeModelo(jsonGetStringAny(msg, "MODELO", "MODEL", "BOARD"));
  if (newModelo.length() > 0 && newModelo != String(modelo)) {
    copyStr(modelo, sizeof(modelo), newModelo);
    saveModelo();
    const BoardProfile* prof = findProfile(modelo);
    if (prof) applyProfile(prof);
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

  // 5) Servidor (Java gateway ou broker MQTT)
  bool mqttDirty = false;
  String mhost = jsonGetStringAny(msg, "MQTT_HOST", "MHOST", "HOST");
#if USE_JAVA_GATEWAY
  if (mhost.length() == 0) mhost = jsonGetStringAny(msg, "JAVA_HOST", "SERVER_HOST", "SERVER");
#endif
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
#if USE_JAVA_GATEWAY
  if (mport < 0) mport = jsonGetIntCI(msg, "JAVA_PORT");
#endif
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

  // 6) Lógica do relé — 1 = ativo-alto, 2 = ativo-baixo (ESP-01 só aceita 2)
  long logica = jsonGetIntCI(msg, "LOGICA_DO_RELE");
  if (logica < 0) logica = jsonGetIntCI(msg, "LOGICA");
  if (logica < 0) logica = jsonGetIntCI(msg, "RELAYMODE");
  if (logica == 1 || logica == 2) {
    bool novo = (logica == 1);
    if (activeProfile && activeProfile->forceActiveLow && novo) {
      ack += ",\"logica\":2,\"aviso\":\"modelo_somente_ativo_baixo\"";
    } else if (novo != relayActiveHigh) {
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

  // 8) AP &lt;uuid&gt;-setup sempre aberto (1) ou desligado (0) — aplica na hora, sem reboot
  bool apChanged = false;
  long apOpen = jsonGetIntCI(msg, "AP_OPEN");
  if (apOpen < 0) apOpen = jsonGetIntCI(msg, "MAINTENANCE_AP");
  if (apOpen < 0) apOpen = jsonGetIntCI(msg, "AP_MODE");
  if (apOpen < 0) {
    String apStr = jsonGetStringAny(msg, "AP_OPEN", "MAINTENANCE_AP", "AP_MODE");
    apStr.toLowerCase();
    if (apStr == "1" || apStr == "true" || apStr == "on" || apStr == "sim") apOpen = 1;
    else if (apStr == "0" || apStr == "false" || apStr == "off" || apStr == "nao" ||
             apStr == "não") apOpen = 0;
  }
  if (apOpen == 0 || apOpen == 1) {
    bool want = (apOpen == 1);
    if (want != maintenanceApEnabled) {
      saveMaintenanceApSetting(want);
      if (want && WiFi.status() == WL_CONNECTED) ensureMaintenanceAP();
      else if (!want) stopMaintenanceAP();
      apChanged = true;
      changed = true;
    }
    ack += ",\"ap_open\":";
    ack += want ? "true" : "false";
  }

  if (wifiChanged) ack += ",\"wifi_ok\":true";

  // Reinicia só se houve mudança que exige reboot (AP sozinho aplica ao vivo)
  bool restartNeeded = wifiChanged || mqttChanged ||
                       (ack.indexOf("\"uuid\"") >= 0) ||
                       (ack.indexOf("\"user_id\"") >= 0) ||
                       (ack.indexOf("\"modelo\"") >= 0) ||
                       (ack.indexOf("\"logica\"") >= 0) ||
                       (ack.indexOf("\"new_pass\"") >= 0);

  if (changed) {
    if (configTestPending()) {
      ack += ",\"teste\":true,\"tentativas_max\":";
      ack += String(CFG_MAX_ATTEMPTS);
    }
    if (apChanged && !restartNeeded) ack += ",\"ap_aplicado\":true";
    ack += ",\"aplicado\":true}";
    publishConfigAck(ack);
    if (restartNeeded) {
      delay(600);
      safeRestart();
    }
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
#if !USE_JAVA_GATEWAY
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
#endif

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
#if USE_JAVA_GATEWAY
    h += F(">Servidor</a><a href='/device'");
#else
    h += F(">MQTT</a><a href='/device'");
#endif if (strcmp(active, "/device") == 0) h += F(" class='on'");
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

// Gera o JS dos modelos (dropdown -> dica). Usado no setup e na tela device.
String modelInfoScript() {
  String s = F("<script>var MODELS={");
  for (int i = 0; i < PROFILE_COUNT; i++) {
    if (i) s += F(",");
    s += F("\"");
    s += jsonEscape(PROFILES[i].name);
    s += F("\":\"");
    s += jsonEscape(PROFILES[i].desc);
    s += F("\"");
  }
  s += F("};function atualizaModelo(){var e=document.getElementById('modelo');"
         "var d=document.getElementById('model_info');"
         "if(e&&d){d.textContent=MODELS[e.value]||'';}}"
         "atualizaModelo();</script>");
  return s;
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
    html += F("<p class='hint'>Dispositivo seco: servidor Java vem "
              "pré-preenchido. Copie o <b>espSecret</b> do app "
              "(Configurações → Secret ESP). "
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
    html += F("<input name='uuid' required value='");
    html += htmlEscape(String(uuid));
    html += F("' placeholder='ex.: portao, lampada'>");
    html += F("<label>Modelo da placa *</label>");
    html += F("<select name='modelo' id='modelo' required onchange='atualizaModelo()'>");
    for (int i = 0; i < PROFILE_COUNT; i++) {
      html += F("<option value='");
      html += PROFILES[i].name;
      html += F("'");
      if (i == PROFILE_DEFAULT) html += F(" selected");
      html += F(">");
      html += htmlEscape(PROFILES[i].label);
      html += F("</option>");
    }
    html += F("</select>");
    html += F("<div id='model_info' class='hint'></div>");

#if USE_JAVA_GATEWAY
    html += F("<h1 style='font-size:16px;margin:18px 0 8px'>3. Servidor Java</h1>");
    html += F("<p class='hint'>Backend HTTP/HTTPS — mesmo host do app "
              "(ex.: mqtt.omny.app.br:443).</p>");
    html += F("<label>Host *</label>");
    html += F("<input name='mhost' required value='");
    html += htmlEscape(String(mqttHost));
    html += F("' placeholder='mqtt.omny.app.br'>");
    html += F("<label>Porta *</label>");
    html += F("<input name='mport' required type='number' value='");
    html += String(mqttPort);
    html += F("' placeholder='443'>");
    html += F("<label>Secret do usuário (espSecret) *</label>");
    html += F("<input type='password' name='esecret' required minlength='8' maxlength='47' "
              "placeholder='Copie do app → Configurações → Secret ESP'>");
    html += F("<p class='hint'>Cole o secret completo (Copiar no app). "
              "Nao use o valor mascarado (xxxx…yyyy). Enviado em X-User-Secret.</p>");
#else
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
#endif

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
    html += modelInfoScript();
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
  html.reserve(1800);
  html += F("<div class='hero'>");
  html += F("<div class='logo-lg'>HOME</div>");
  html += F("<p>");
  html += htmlEscape(String(userId));
  html += F(" / ");
  html += htmlEscape(String(uuid));
  html += F("</p></div>");

  html += F("<div class='card' style='margin-bottom:14px'>");
  String host = mdnsHostname();
  if (host.length() > 0) {
    html += F("<p class='hint' style='margin:0 0 10px;font-size:15px'>"
              "Configuração local (mesma rede Wi-Fi):<br><a href='http://");
    html += host;
    html += F(".local/'><b>http://");
    html += htmlEscape(host);
    html += F(".local/</b></a></p>");
  }
  if (WiFi.status() == WL_CONNECTED) {
    html += F("<p class='hint' style='margin:0 0 6px'>Alternativa por IP: "
              "<a href='http://");
    html += WiFi.localIP().toString();
    html += F("/'>http://");
    html += WiFi.localIP().toString();
    html += F("/</a></p>");
  }
  html += F("<p class='hint' style='margin:0'>");
  if (maintenanceApEnabled) {
    html += F("Wi-Fi da placa <b>");
    html += htmlEscape(configApName());
    html += F("</b> sempre aberto (senha ");
    html += F(AP_PASSWORD);
    html += F(") → <a href='http://192.168.4.1/'>http://192.168.4.1/</a>");
  } else {
    html += F("Wi-Fi da placa desligado — ative em <a href='/wifi'>Wi-Fi</a>.");
  }
  html += F("</p>");
  html += F("</div>");

  html += F("<div class='menu'>");
  html += F("<a href='/wifi'><span class='ico'>Wi</span>Wi-Fi</a>");
#if USE_JAVA_GATEWAY
  html += F("<a href='/mqtt'><span class='ico'>SV</span>Servidor Java</a>");
#else
  html += F("<a href='/mqtt'><span class='ico'>MQ</span>Broker MQTT</a>");
#endif
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

  html += F("<div class='card' style='margin-top:16px'>");
  html += F("<h1 style='font-size:16px;margin:0 0 8px'>Wi-Fi da placa (AP)</h1>");
  html += F("<p class='hint'>Por padrão a rede <b>");
  html += htmlEscape(configApName());
  html += F("</b> fica sempre aberta para configurar via http://192.168.4.1 "
            "(senha ");
  html += F(AP_PASSWORD);
  html += F("). Desative se não quiser expor o AP na sua casa.</p>");
  html += F("<form method='POST' action='/save'>");
  html += F("<input type='hidden' name='section' value='ap_mode'>");
  html += F("<label>Rede Wi-Fi do ESP</label>");
  html += F("<select name='ap_open'>");
  html += F("<option value='1'");
  if (maintenanceApEnabled) html += F(" selected");
  html += F(">Sempre aberta (padrão)</option>");
  html += F("<option value='0'");
  if (!maintenanceApEnabled) html += F(" selected");
  html += F(">Desligada (só IP / ");
  html += htmlEscape(mdnsHostname());
  html += F(".local)</option>");
  html += F("</select>");
  html += F("<label>Senha de administrador</label>");
  html += F("<input type='password' name='admin' required "
            "placeholder='Necessária para salvar'>");
  html += F("<input type='submit' class='btn' value='Salvar preferência do AP'>");
  html += F("</form></div>");

  html += FPSTR(WIFI_SCAN_JS);
  html += pageBottom();
  sendHtml(html);
}

// ---------------------------- Telas MQTT e Dispositivo ----------------------
void handleMqttPage() {
  if (!isProvisioned()) { handleHome(); return; }
#if USE_JAVA_GATEWAY
  String html = pageTop("Servidor Java — HOME", "/mqtt");
  html.reserve(2200);
  html += F("<h1>Servidor Java (HTTP)</h1>");
  html += F("<p class='sub'>Host/porta usados em POST /api/esp/{uuid}/events "
            "e GET /poll. Separado do Wi-Fi.</p>");
  html += F("<div class='card'>");
  html += F("<form method='POST' action='/save'>");
  html += F("<input type='hidden' name='section' value='mqtt'>");
  html += F("<label>Host do backend</label>");
  html += F("<input name='mhost' value='");
  html += htmlEscape(String(mqttHost));
  html += F("' placeholder='mqtt.omny.app.br'>");
  html += F("<label>Porta (443 = HTTPS)</label>");
  html += F("<input name='mport' value='");
  html += String(mqttPort);
  html += F("'>");
  html += F("<label>Secret do usuário (espSecret)</label>");
  html += F("<input type='password' name='esecret' placeholder='Deixe vazio para manter o atual'>");
  html += F("<p class='hint'>Copie do app → Configurações → Secret ESP.</p>");
  html += F("<label>Senha de administrador</label>");
  html += F("<input type='password' name='admin' required placeholder='Necessária para salvar'>");
  html += F("<input type='submit' class='btn' value='Salvar e reiniciar'>");
  html += F("</form></div>");
#else
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
#endif
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
  html += F("<select name='modelo' id='modelo' onchange='atualizaModelo()'>");
  for (int i = 0; i < PROFILE_COUNT; i++) {
    html += F("<option value='");
    html += PROFILES[i].name;
    html += F("'");
    if (findProfile(modelo) == &PROFILES[i]) html += F(" selected");
    html += F(">");
    html += htmlEscape(PROFILES[i].label);
    html += F("</option>");
  }
  html += F("</select>");
  html += F("<div id='model_info' class='hint'></div>");

  html += F("<label>user_id (dono / pai)</label>");
  html += F("<input value='");
  html += htmlEscape(userId[0] ? String(userId) : String("(não definido)"));
  html += F("' disabled>");
  html += F("<p class='hint'>O user_id só pode ser alterado via MQTT "
            "<code>home/&lt;uuid&gt;/config</code> (campo USER_ID + PASS).</p>");

  html += F("<label>Lógica do relé</label>");
  if (activeProfile && activeProfile->forceActiveLow) {
    html += F("<select name='relaymode' disabled>");
    html += F("<option value='0' selected>Ativo-baixo (LOW liga — fixo neste modelo)</option>");
    html += F("</select>");
    html += F("<input type='hidden' name='relaymode' value='0'>");
    html += F("<p class='hint'>Este modelo (");
    html += htmlEscape(String(modelo));
    html += F(") só aceita ativo-baixo: o GPIO0 é pino de boot.</p>");
  } else {
    html += F("<select name='relaymode'>");
    html += F("<option value='1'");
    if (relayActiveHigh) html += F(" selected");
    html += F(">Ativo-alto (HIGH liga)</option>");
    html += F("<option value='0'");
    if (!relayActiveHigh) html += F(" selected");
    html += F(">Ativo-baixo (LOW liga)</option>");
    html += F("</select>");
    html += F("<p class='hint'>GPIO0/GPIO2/GPIO15 são pinos de boot — "
              "evite ativo-alto neles.</p>");
  }
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
            "<code>");
  html += htmlEscape(configApName());
  html += F("</code> para novo setup completo.</p>");
  html += F("<form method='POST' action='/save' "
            "onsubmit=\"return confirm('Resetar para seco? Todo o setup será apagado.');\">");
  html += F("<input type='hidden' name='section' value='factory_reset'>");
  html += F("<label>Senha de administrador *</label>");
  html += F("<input type='password' name='admin' required "
            "placeholder='Senha admin atual'>");
  html += F("<input type='submit' class='btn' style='background:#f87171;color:#111' "
            "value='Resetar para seco'>");
  html += F("</form></div>");

  html += modelInfoScript();
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
    String esecret = webServer.arg("esecret");
    String na = webServer.arg("newadmin");
    String na2 = webServer.arg("newadmin2");
    ssid.trim(); wpass.trim(); host.trim(); port.trim();
    muser.trim(); mpass.trim(); mpath.trim(); esecret.trim(); na.trim(); na2.trim();

#if USE_JAVA_GATEWAY
    if (ssid.length() == 0 || wpass.length() == 0 ||
        uid.length() == 0 || u.length() == 0 || mod.length() == 0 ||
        host.length() == 0 || port.length() == 0 ||
        esecret.length() < 8 || na.length() < 6) {
      sendHtml(errorPage("Preencha todos os campos (espSecret mín. 8, senha admin mín. 6)."));
      return;
    }
    if (esecret.length() > (int)(sizeof(deviceToken) - 1)) {
      sendHtml(errorPage("espSecret longo demais (máx. 47). Gere um novo no app."));
      return;
    }
#else
    if (ssid.length() == 0 || wpass.length() == 0 ||
        uid.length() == 0 || u.length() == 0 || mod.length() == 0 ||
        host.length() == 0 || port.length() == 0 ||
        muser.length() == 0 || mpass.length() == 0 || mpath.length() == 0 ||
        na.length() < 6) {
      sendHtml(errorPage("Preencha todos os campos (senha admin mín. 6 caracteres)."));
      return;
    }
#endif
    if (na != na2) {
      sendHtml(errorPage("A confirmação da senha admin não confere."));
      return;
    }

    const BoardProfile* prof = findProfile(mod.c_str());
    if (!prof) {
      sendHtml(errorPage("Modelo de placa inválido. Escolha um da lista."));
      return;
    }

    copyStr(userId, sizeof(userId), uid);
    saveUserId();
    copyStr(uuid, sizeof(uuid), u);
    saveUUID(String(uuid));
    copyStr(modelo, sizeof(modelo), mod);
    saveModelo();
    applyProfile(prof);
    saveRelayPolarity();

    copyStr(mqttHost, sizeof(mqttHost), host);
    int p = port.toInt();
    if (p > 0 && p <= 65535) mqttPort = p;
#if USE_JAVA_GATEWAY
    copyStr(mqttPath, sizeof(mqttPath), String("/"));
    copyStr(deviceToken, sizeof(deviceToken), esecret);
    saveDeviceToken();
#else
    if (mpath.charAt(0) != '/') mpath = "/" + mpath;
    copyStr(mqttUser, sizeof(mqttUser), muser);
    copyStr(mqttPass, sizeof(mqttPass), mpass);
    copyStr(mqttPath, sizeof(mqttPath), mpath);
#endif
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
    sendHtml(okPage(String(F("Reset seco concluido. Reiniciando — conecte no Wi-Fi <b>")) +
                    htmlEscape(String(F(FACTORY_SETUP_AP))) +
                    F("</b> (senha C6m8n4d2d3) e abra http://192.168.4.1")));
    delay(1000);
    safeRestart();
    return;
  }

  if (section == "ap_mode") {
    bool on = webServer.arg("ap_open") == "1";
    saveMaintenanceApSetting(on);
    if (on && WiFi.status() == WL_CONNECTED) ensureMaintenanceAP();
    else stopMaintenanceAP();
    String h = pageTop("Salvo", "/wifi");
    h += F("<h1 class='ok'>Preferência salva</h1>");
    h += F("<div class='card'><p style='font-size:14px'>Wi-Fi da placa <b>");
    h += htmlEscape(configApName());
    h += F("</b> ");
    h += on ? F("ativado") : F("desativado");
    h += F(".</p></div>");
    h += F("<a href='/' class='btn secondary'>Início</a> ");
    h += F("<a href='/wifi' class='btn secondary'>Wi-Fi</a>");
    h += pageBottom();
    sendHtml(h);
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
      if (adoptWiFiChange(ssid, wpass)) testing = true;
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

#if USE_JAVA_GATEWAY
    String esecret = webServer.arg("esecret");
    esecret.trim();
    if (esecret.length() >= 8) {
      copyStr(deviceToken, sizeof(deviceToken), esecret);
      saveDeviceToken();
    }
#endif

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

    // Modelo editável: aplica o perfil correspondente (pinos/polaridade).
    String m = sanitizeModelo(webServer.arg("modelo"));
    if (m.length() > 0 && m != String(modelo)) {
      copyStr(modelo, sizeof(modelo), m);
      saveModelo();
      const BoardProfile* prof = findProfile(modelo);
      if (prof) {
        applyProfile(prof);
        saveRelayPolarity();
      }
    }

    // Lógica do relé (respeita modelo que só aceita ativo-baixo).
    String rm = webServer.arg("relaymode");
    rm.trim();
    if (rm == "1" || rm == "0") {
      bool novo = (rm == "1");
      if (activeProfile && activeProfile->forceActiveLow) novo = false;
      if (novo != relayActiveHigh) {
        relayActiveHigh = novo;
        saveRelayPolarity();
        setAllOutputsOff();
      }
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

void onWifiManagerSave() {
  // Removido — WiFiManager não é mais usado. Mantido vazio por compat.
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
  String json = String("{\"poke\":") + pokeArg + ",\"time\":" + String(tm) + "}";
  handleRelayCommand(json);
  webServer.send(200, "text/plain",
    String("OK poke=") + pokeArg + " GPIO" + String(relayPin) + " = " +
    (digitalRead(relayPin) ? "HIGH" : "LOW"));
}

// Captive portal: Android/iOS/Windows pedem estas URLs ao conectar no AP.
void handleCaptiveProbe() {
  webServer.sendHeader(F("Location"), F("http://192.168.4.1/"), true);
  webServer.send(302, F("text/plain"), F(""));
}

// Registra as rotas do servidor web
void bindServerCallback() {
  webServer.on("/", HTTP_GET, handleHome);
  webServer.on("/wifi", HTTP_GET, handleWifiPage);
  webServer.on("/mqtt", HTTP_GET, handleMqttPage);
  webServer.on("/device", HTTP_GET, handleDevicePage);
  webServer.on("/style.css", HTTP_GET, handleStyle);
  webServer.on("/scan", HTTP_GET, handleScan);
  webServer.on("/relay-test", HTTP_GET, handleRelayTest);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.on("/generate_204", HTTP_GET, handleCaptiveProbe);
  webServer.on("/gen_204", HTTP_GET, handleCaptiveProbe);
  webServer.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
  webServer.on("/connecttest.txt", HTTP_GET, handleCaptiveProbe);
  webServer.on("/ncsi.txt", HTTP_GET, handleCaptiveProbe);
  webServer.on("/fwlink", HTTP_GET, handleCaptiveProbe);
  webServer.onNotFound(handleHome);
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

void loadMaintenanceApSetting() {
  // 0xA2 = preferencia nova (padrao AP off). 0xA1 antigo com AP on → migra 1x p/ off
  // (AP+STA + HTTPS deixava o ESP8266 muito quente).
  uint8_t magic = EEPROM.read(EE_AP_OPEN_MAGIC);
  if (magic == 0xA2) {
    maintenanceApEnabled = (EEPROM.read(EE_AP_OPEN) != 0);
  } else if (magic == 0xA1) {
    maintenanceApEnabled = false;
    EEPROM.write(EE_AP_OPEN_MAGIC, 0xA2);
    EEPROM.write(EE_AP_OPEN, 0);
    EEPROM.commit();
    Serial.println("AP manutencao desligado (menos calor). Reative no portal se precisar.");
  } else {
    maintenanceApEnabled = false;
  }
}

void applyWifiPowerPolicy() {
#if USE_JAVA_GATEWAY
  // HTTPS + modem-sleep no ESP8266 trava o TLS (so volta com reset fisico).
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
  // Modem sleep so funciona bem em STA puro; com SoftAP ligado nao use.
  if (maintenanceApEnabled || recoveryApActive || wifiRecoveryMode) {
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
  } else {
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  }
#endif
}

void saveMaintenanceApSetting(bool enabled) {
  maintenanceApEnabled = enabled;
  EEPROM.write(EE_AP_OPEN_MAGIC, 0xA2);
  EEPROM.write(EE_AP_OPEN, enabled ? 1 : 0);
  EEPROM.commit();
  applyWifiPowerPolicy();
  Serial.print("AP config sempre aberto: ");
  Serial.println(enabled ? "sim" : "nao");
}

void stopMaintenanceAP() {
  if (WiFi.getMode() == WIFI_AP_STA || WiFi.getMode() == WIFI_AP) {
    WiFi.softAPdisconnect(true);
    recoveryApActive = false;
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }
  applyWifiPowerPolicy();
  Serial.println("AP de config desligado.");
}

// Sobe (ou mantém) o AP <uuid>-setup — não recria se já estiver ok.
bool startMaintenanceAP() {
  refreshPortalApName();
  // Já no ar com o nome certo? Não mexer no rádio (evita derrubar o STA).
  if (WiFi.softAPIP() == IPAddress(192, 168, 4, 1) &&
      WiFi.softAPSSID() == String(portalApName)) {
    recoveryApActive = true;
    return true;
  }
  // Com Wi-Fi ok: softAP simples (fluxo BETA) — menos instável que recriar o rádio.
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    if (WiFi.getMode() != WIFI_AP_STA) {
      WiFi.mode(WIFI_AP_STA);
      delay(100);
    }
    bool ok = WiFi.softAP(portalApName, AP_PASSWORD);
    delay(200);
    recoveryApActive = ok;
    if (ok) {
      Serial.print("softAP manutencao OK (BETA): ");
      Serial.println(portalApName);
    }
    return ok;
  }
  bool ok = bringUpConfigAP(true);
  recoveryApActive = ok;
  return ok;
}

void ensureMaintenanceAP() {
  if (!maintenanceApEnabled) {
    stopMaintenanceAP();
    if (WiFi.status() == WL_CONNECTED && isProvisioned()) restartMdns();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;
  startMaintenanceAP();
  if (isProvisioned()) restartMdns();
}

// AP de recuperação: sobe <uuid>-setup mesmo sem Wi-Fi (nunca deixa sem acesso local).
void ensureRecoveryAP() {
  if (!isProvisioned()) return;
  if (recoveryApActive && WiFi.softAPIP() == IPAddress(192, 168, 4, 1)) {
    ensureConfigWebServer();
    return;
  }

  recoveryApActive = bringUpConfigAP(false);
  if (recoveryApActive) {
    ensureConfigWebServer();
    if (!dnsServerActive) {
      dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
      dnsServer.start(53, "*", WiFi.softAPIP());
      dnsServerActive = true;
    }
    Serial.print("Modo recuperacao — conecte em ");
    Serial.print(portalApName);
    Serial.print(" (senha ");
    Serial.print(AP_PASSWORD);
    Serial.println(") → http://192.168.4.1/");
  }
}

void ensureConfigWebServer() {
  if (configWebPortalActive) return;
  bindServerCallback();
  webServer.begin();
  configWebPortalActive = true;
  Serial.println("Portal web ativo (http://192.168.4.1 ou IP da rede).");
}

void wifiServerProcess() {
  if (dnsServerActive) dnsServer.processNextRequest();
  if (configWebPortalActive) webServer.handleClient();
}

// Usa a configuração salva na EEPROM; sem provisionamento força AP &lt;uuid&gt;-setup
// (mesmo se o SDK ainda tiver Wi-Fi antigo na flash).
void connectWiFi() {
  // ---------- Setup inicial: SEMPRE sobe o AP ----------
  if (!isProvisioned()) {
    resetPortalRuntimeState();
    ensureDefaultUUID();
    Serial.print("Nao provisionado — forcando portal AP ");
    Serial.println(configApName());
    configPortalTimeoutMs = 0;  // sem timeout até concluir o setup

    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(200);

    startConfigPortalAp();

    // Se ainda nao provisionou / nao conectou, reinicia e mostra o AP de novo
    if (!isProvisioned() && WiFi.status() != WL_CONNECTED) {
      Serial.println("Setup nao concluido. Reiniciando para reabrir o AP...");
      delay(500);
      safeRestart();
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("Conectado ao Wi-Fi! IP: ");
      Serial.println(WiFi.localIP());
      startMaintenanceAP();
      ensureConfigWebServer();
      restartMdns();
    }
    return;
  }

  // ---------- Já provisionado ----------
  configPortalTimeoutMs = 240000UL;  // 4 min

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
    // Cold boot / apos USB: radio demora mais — 25 s
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 25000) {
      delay(200);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("Falha ao conectar no Wi-Fi: ");
      Serial.println(wifiStatusText(WiFi.status()));
    }
  }

  String apName = mdnsHostname();

  if (WiFi.status() != WL_CONNECTED) {
    if (configTestPending()) {
      Serial.println("Contingencia: Wi-Fi candidato falhou nesta passagem — teste continua no loop.");
    } else {
      // NÃO usar portal AP-only + safeRestart (loop infinito ao tirar do USB / blip).
      // Sobe AP+STA e deixa o loop() reconectar.
      Serial.println("Wi-Fi falhou no boot — modo recuperacao AP+STA (sem reinicio).");
      wifiLostAt = millis();
      wifiRecoveryMode = true;
      WiFi.setSleepMode(WIFI_NONE_SLEEP);
      WiFi.mode(WIFI_AP_STA);
      delay(100);
      refreshPortalApName();
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                        IPAddress(255, 255, 255, 0));
      recoveryApActive = WiFi.softAP(portalApName, AP_PASSWORD, 6, 0, 4);
      ensureConfigWebServer();
      if (strlen(wifiSSID) > 0) {
        WiFi.begin(wifiSSID, wifiPass);
      }
      Serial.print("AP ");
      Serial.print(portalApName);
      Serial.println(" ativo; reconectando STA em background.");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado ao Wi-Fi! IP: ");
    Serial.println(WiFi.localIP());
    ensureMaintenanceAP();
    ensureConfigWebServer();
    restartMdns();
  }
  // Provisionado sem Wi-Fi: NÃO reinicia — loop() cuida da reconexão.

  if (WiFi.status() == WL_CONNECTED) {
    if (strlen(wifiSSID) == 0 && WiFi.SSID().length() > 0) {
      copyStr(wifiSSID, sizeof(wifiSSID), WiFi.SSID());
      Serial.print("SSID recuperado do rádio: ");
      Serial.println(wifiSSID);
    }
    Serial.print("Interface de configuração: http://");
    Serial.print(apName.length() > 0 ? apName : String(uuid));
    Serial.println(".local/ (mDNS — use o UUID do dispositivo)");
    Serial.print("ou IP http://");
    Serial.print(WiFi.localIP().toString());
    Serial.println("/");
    Serial.print("ou AP ");
    Serial.print(configApName());
    Serial.println(maintenanceApEnabled ? " → http://192.168.4.1/" : " (desligado no portal)");
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

// Reinicia o transporte (WSS legado ou Java gateway HTTP)
void restartMqttTransport() {
  if (!isProvisioned()) return;
#if USE_JAVA_GATEWAY
  Serial.println("Reconectando Java gateway...");
  javaGatewayReady = false;
  connectJavaGateway();
#else
  Serial.println("Reiniciando transporte MQTT (WSS)...");
  wsClient.disconnect();
  delay(50);
  wsClient.beginSSL(mqttHost, mqttPort, mqttPath, (uint8_t*)NULL, "mqtt");
  wsClient.setReconnectInterval(MQTT_RETRY_MS);
  lastWsRestart = millis();
  lastMqttAttempt = 0;
#endif
}

// Força nova associação Wi-Fi com as credenciais da EEPROM.
// WiFi.reconnect() sozinho falha com frequência no ESP8266 (STA travado).
void forceWifiReconnect(bool hard) {
  if (strlen(wifiSSID) == 0) {
    Serial.println("Sem SSID salvo — abrindo AP de recuperacao.");
    if (isProvisioned()) {
      ensureRecoveryAP();
      ensureConfigWebServer();
    }
    return;
  }
  Serial.print(hard ? "Reconexao Wi-Fi FORCADA: " : "Reconectando Wi-Fi: ");
  Serial.println(wifiSSID);

  // Em recuperacao ou com AP de manutencao: manter softAP (AP+STA).
  if (isProvisioned() && (maintenanceApEnabled || wifiRecoveryMode || recoveryApActive)) {
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAPIP() != IPAddress(192, 168, 4, 1)) {
      refreshPortalApName();
      WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                        IPAddress(255, 255, 255, 0));
      recoveryApActive = WiFi.softAP(portalApName, AP_PASSWORD, 6, 0, 4);
    }
  } else {
    WiFi.mode(WIFI_STA);
  }
  // false = não apaga softAP
  WiFi.disconnect(false);
  delay(hard ? 200 : 100);
  WiFi.begin(wifiSSID, wifiPass);
}

// Chamado quando o Wi-Fi cai: limpa sockets MQTT/WS para não ficarem zumbis
void onWifiLostCleanup() {
#if !USE_JAVA_GATEWAY
  wsClient.disconnect();
#else
  javaGatewayReady = false;
  javaResetHttpClient();
#endif
}

// Pisca GPIO2 (LED onboard ESP-01/NodeMCU) — prova que o sketch rodou mesmo sem Serial.
void bootLedPulse() {
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  for (int i = 0; i < 5; i++) {
    digitalWrite(2, LOW);
    delay(150);
    digitalWrite(2, HIGH);
    delay(150);
  }
}

// -------------------------------- SETUP -------------------------------------
void setup() {
  bootLedPulse();

  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println(F("=== BOOT HOME (115200) ==="));
  Serial.print(F("Reset: "));
  Serial.println(ESP.getResetInfo());
  Serial.flush();

  // Pinos de boot do ESP8266 em estado seguro ANTES de qualquer coisa.
  pinMode(0, INPUT_PULLUP);
  pinMode(2, INPUT_PULLUP);
  pinMode(15, INPUT);

  EEPROM.begin(EEPROM_SIZE);

  loadMQTTConfig();
  loadDeviceToken();
  loadWiFiConfig();
  loadAdminConfig();
  loadUUID();
  ensureDefaultUUID();
  loadUserId();
  loadModelo();
  // Aplica o perfil do modelo salvo (pinos/polaridade padrão) ANTES da polaridade.
  const BoardProfile* prof = findProfile(modelo);
  applyProfile(prof ? prof : &PROFILES[PROFILE_DEFAULT]);
  loadRelayPolarity();
  loadProvisioned();
  loadMaintenanceApSetting();
  applyWifiPowerPolicy();
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

  Serial.println("=== HOME Sistema — Multi-modelo ===");
#if USE_JAVA_GATEWAY
  Serial.println("Transporte: Java gateway (HTTP)");
#else
  Serial.println("Transporte: MQTT (Mosquitto/WSS)");
#endif
  Serial.print("Provisionado: ");
  Serial.println(isProvisioned() ? "sim" : "NAO (setup no AP <uuid>-setup)");
  Serial.print("UUID: ");
  Serial.println(uuid[0] ? uuid : "(vazio)");
  Serial.print("AP config: ");
  Serial.println(configApName());
  Serial.print("user_id: ");
  Serial.println(userId[0] ? userId : "(vazio)");
  Serial.print("modelo: ");
  Serial.println(modelo[0] ? modelo : "(vazio)");
  Serial.print("AP ");
  Serial.print(configApName());
  Serial.print(" sempre aberto: ");
  Serial.println(maintenanceApEnabled ? "sim" : "nao");
  if (activeProfile) {
    Serial.print("perfil: relé GPIO");
    Serial.print(relayPin);
    Serial.print(" (pin ");
    Serial.print(relayLogicalPin);
    Serial.print("), botão GPIO");
    Serial.print(buttonPin);
    Serial.print(", LED GPIO");
    Serial.println(ledGpio);
  }
#if USE_JAVA_GATEWAY
  Serial.print("Servidor: ");
#else
  Serial.print("Broker: wss://");
#endif
  Serial.print(mqttHost);
  Serial.print(":");
  Serial.print(mqttPort);
#if !USE_JAVA_GATEWAY
  Serial.println(mqttPath);
#else
  Serial.println(" (HTTP /api/esp/...)");
#endif
  if (configTestPending()) {
    Serial.println("*** MODO CONTINGENCIA: testando configuracao candidata ***");
  }

  connectWiFi();
  applyWifiPowerPolicy();

#if USE_JAVA_GATEWAY
  Serial.println("Modo Java gateway (HTTP) — sem MQTT.");
  if (WiFi.status() == WL_CONNECTED) {
    connectJavaGateway();
  }
#else
  wsClient.beginSSL(mqttHost, mqttPort, mqttPath, (uint8_t*)NULL, "mqtt");
  wsClient.setReconnectInterval(MQTT_RETRY_MS);
  mqtt.begin(wsClient);
  mqtt.subscribe(onGlobalMqtt);   // backup: home/<user>/<uuid>/cmd|config
  mqttReady = true;

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }
#endif

  for (int i = 0; i < 7; i++) {
    dailyConnectedTime[i] = EEPROM.read(4 + i);
    dailyDisconnectedTime[i] = EEPROM.read(11 + i);
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

#if USE_JAVA_GATEWAY
  if (wifiUp) {
    if (!wifiWasUp) {
      Serial.println("Wi-Fi restabelecido — reconectando Java gateway...");
      wifiReconnectFails = 0;
      connectJavaGateway();
    }
    javaGatewayLoop();
  } else {
    if (wifiWasUp) onWifiLostCleanup();
  }
  wifiWasUp = wifiUp;
#else
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
#endif

  wifiServerProcess();   // mantém o servidor web + DNS captive portal
#if !USE_JAVA_GATEWAY
  if (wifiUp) {
    MDNS.update();
    ensureMdnsIfNeeded();
  }
#endif
  // Java+HTTPS: MDNS compete com BearSSL e derruba poll/telemetria.
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
      WiFi.persistent(true);
      WiFi.disconnect(true);
      ESP.eraseConfig();
      WiFi.persistent(false);
      delay(1000);
      safeRestart();
    }
  } else {
    buttonPressed = false;
  }

  // ------------------- Sem Wi-Fi -------------------
  if (!wifiUp) {
    if (wifiLostAt == 0) wifiLostAt = now;

    // Só sobe AP de config após 60 s offline (evita ciclo por blip).
    // Mantém AP+STA — NÃO mata o cliente Wi-Fi. Nunca reinicia em loop.
    if (isProvisioned() && (now - wifiLostAt > 60000UL)) {
      if (!wifiRecoveryMode) {
        wifiRecoveryMode = true;
        Serial.println("=== Wi-Fi offline >60s — AP de config + reconectar ===");
        Serial.print("=== Rede ");
        Serial.print(configApName());
        Serial.println(" → http://192.168.4.1 ===");
      }
      refreshPortalApName();
      if (WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
        delay(50);
      }
      if (WiFi.softAPIP() != IPAddress(192, 168, 4, 1) ||
          WiFi.softAPSSID() != String(portalApName)) {
        WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                          IPAddress(255, 255, 255, 0));
        recoveryApActive = WiFi.softAP(portalApName, AP_PASSWORD, 6, 0, 4);
      }
      ensureConfigWebServer();
    }

    // Relé só desliga após 30 s sem Wi-Fi (não a cada blip)
    if (isConnected && (now - wifiLostAt > 30000UL)) {
      isConnected = false;
      Serial.println("Desconectado da Internet (>30s).");
      dailyDisconnectedTime[currentDay] += 2;
      relayScheduled = false;
      setAllOutputsOff();

      for (int i = 0; i < 7; i++) {
        EEPROM.write(4 + i, dailyConnectedTime[i]);
        EEPROM.write(11 + i, dailyDisconnectedTime[i]);
      }
      EEPROM.commit();

      sendLogsMQTT("Falha na conexão com a internet.");
    }

    // Sempre tenta reconectar STA (não zera wifiRecoveryMode — AP deve permanecer)
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
    wifiLostAt = 0;
    if (wifiRecoveryMode) {
      wifiRecoveryMode = false;
      recoveryApActive = false;
      if (dnsServerActive) {
        dnsServer.stop();
        dnsServerActive = false;
      }
      Serial.println("Wi-Fi restabelecido — saindo do modo recuperacao.");
    }
    // ------------------- Com Wi-Fi -------------------
    if (!isConnected) {
      isConnected = true;
      Serial.println("Conexão com a Internet estabelecida.");
      sendLogsMQTT("Conexão com a Internet estabelecida.");
      connectedTime = 0;
      if (isProvisioned()) {
        ensureMaintenanceAP();
        ensureConfigWebServer();
        restartMdns();
      }
      // MQTT será reestabelecido quando o WebSocket subir (bloco acima)
    }

#if !USE_JAVA_GATEWAY
    // Teste de internet — NÃO desliga o relé a cada falha (causava liga/desliga).
    if (now - previousPingMillis >= pingInterval) {
      previousPingMillis = now;
      bool ok = pingGoogle();
      if (ok != lastPingOk) {
        lastPingOk = ok;
        if (ok) {
          internetFailCount = 0;
          Serial.println("Conexão com a Internet OK...");
          sendLogsMQTT("Conexão com a Internet OK.");
          if (!mqttConnected()) restartMqttTransport();
        } else {
          Serial.println("Falha ao acessar o Google (relé mantido).");
          sendLogsMQTT("Falha ao acessar o Google.");
        }
      }
      if (!ok) {
        internetFailCount++;
        if (internetFailCount >= 3 && (now - lastWsRestart >= 30000UL)) {
          Serial.println("Internet instavel — reiniciando WSS.");
          restartMqttTransport();
          internetFailCount = 0;
        }
      } else {
        internetFailCount = 0;
      }
    }
#endif

    // Log da conexão com o broker apenas na mudança de estado
#if !USE_JAVA_GATEWAY
    if (mqtt.isConnected() != lastMqttState) {
      lastMqttState = mqtt.isConnected();
      if (lastMqttState) {
        Serial.println("Conectado ao broker MQTT!");
      } else {
        Serial.println("Desconectado do broker MQTT!");
      }
    }
#endif

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

    // Telemetria — Java: so advance (leve); basic a cada 5 ciclos. Menos HTTPS = menos hang.
    if (now - previousSensorMillis >= intervalSensors) {
      previousSensorMillis = now;
#if USE_JAVA_GATEWAY
      publishLogsAdvance();
      static uint8_t javaBasicEvery = 0;
      if (++javaBasicEvery >= 5) {
        javaBasicEvery = 0;
        publishLogsBasic();
      }
#else
      publishLogsTelemetry();
#endif
    }

#if !USE_JAVA_GATEWAY
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
#endif
  }
  yield();
}
