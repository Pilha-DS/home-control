//============================================================================
//  HOME Sistema — V1_FIXO (Gateway Java HTTP)
//
//  Igual a V1, com tempo de ativacao FIXO: ao ligar (poke=1) o rele
//  desliga sozinho apos 4 segundos. "time"/"timeMs" da API sao ignorados.
//  poke=0 desliga na hora (permanente ate novo poke=1).
//
//  Transporte unico:
//    POST /api/esp/{uuid}/events  +  GET /api/esp/{uuid}/poll
//
//  Host/porta na EEPROM = servidor Spring Boot (ex.: mqtt.omny.app.br:443).
//  Token: device_token gerado ao cadastrar o dispositivo no app.
//
//  SOMENTE ESP-01 / ESP-01S (fixo, sem seletor de modelo):
//    * Relé  - GPIO0 (ativo-baixo: LOW liga). GPIO0 é pino de boot.
//    * LED   - GPIO2 (onboard, pino de bootstrap — usado só no boot pulse)
//    * Botão - não existe no ESP-01
//
//  • Config do servidor Java SEPARADA da config do Wi-Fi (EEPROM independente)
//  • Pagina web de configuracao protegida por SENHA DE ADMINISTRADOR
//    (padrao: C6m8n4d2d3) — necessaria para mudar Wi-Fi, servidor, UUID e a
//    propria senha de admin
//  • Pelo navegador e possivel mudar a qualquer momento:
//       - UUID do dispositivo
//       - senha do Wi-Fi
//       - host/porta do backend Java (+ user/pass/path mantidos na EEPROM)
//       - senha de administrador
//
//  PINOS (fixos no codigo):
//    ESP-01/01S: rele GPIO0 (ativo-baixo), LED GPIO2 onboard, sem botao.
//
//  AVISO (ESP-01): GPIO0 e pino de boot — o rele pode "clicar" ao resetar e
//  o modelo fica fixo em ativo-baixo. Nao ha solucao 100% por software.
//
//  COMO USAR:
//    1a vez: conecte no Wi-Fi home-setup (senha C6m8n4d2d3) e abra
//            http://192.168.4.1 — setup completo (Wi-Fi, user_id, UUID).
//    Depois (provisionado): AP vira <uuid>-setup
//            ou http://<ip-do-esp>/ na mesma rede Wi-Fi.
//
//  EVENTOS (Java gateway):
//    status | state | ack | ip | config/ack | logs/basic | logs/advance
//
//  FABRICA (seco): so host/porta do backend Java pre-preenchidos. Setup
//  obrigatorio no AP home-setup (http://192.168.4.1) — todos os campos.
//
//  COMANDO (poll type=cmd) — V1_FIXO: liga sempre por 4s, depois desliga:
//    {"poke":1,"pin":4}   // liga 4s (time/timeMs da API ignorados)
//    {"poke":0}           // desliga na hora
//
//  CONFIG (poll type=config) — PASS obrigatorio:
//    {
//      "PASS":"C6m8n4d2d3",
//      "UUID":"novo_nome",
//      "USER_ID":"pai",
//      (MODELO ignorado — o firmware é sempre ESP-01)
//      "WIFI_SSID":"rede","WIFI_PASS":"senha",
//      "MQTT_HOST":"mqtt.omny.app.br","MQTT_PORT":443,
//      "MQTT_USER":"...","MQTT_PASS":"...","MQTT_PATH":"/",
//      "LOGICA_DO_RELE":2,
//      "AP_OPEN":1,
//      "NEW_PASS":"nova_admin"
//    }
//    (Chaves MQTT_* = host/porta/credenciais do backend Java na EEPROM —
//     nomes historicos mantidos por compatibilidade com o app/EEPROM.)
//
//  LOGS basic — a cada 20 s:
//    {"internet":"SSID","server":"host","modelo":"ESP-01","uuid":"...","user_id":"...","ip":"..."}
//  LOGS advance — a cada 20 s:
//    {"temperature":0,"free_heap":..,"rssi":..,"state":0|1,"connected_time":..}
//    state: 0=ligado, 1=desligado | temperature: 0 no ESP-01 (sem A0)
//
//  BIBLIOTECAS (Arduino IDE / Board Manager ESP8266):
//    ESP8266HTTPClient, WiFiClientSecure, ESP8266WebServer, DNSServer
//    (SEM WebSockets_Generic / MQTTPubSubClient_Generic)
//============================================================================

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

// ------------------------- Valores padrão -----------------------------------
// Usados na primeira gravação; depois podem ser alterados pela interface web
// (mantidos na EEPROM).
#define JAVA_DEFAULT_HOST   "mqtt.omny.app.br"
#define JAVA_DEFAULT_PORT   443

#define MQTT_DEFAULT_HOST   "mqtt.omny.app.br"  // EEPROM: host do backend Java
#define MQTT_DEFAULT_PORT   443
#define MQTT_DEFAULT_USER   "cardoso"                // legado EEPROM
#define MQTT_DEFAULT_PASS   "C6m8n4d2d3"             // fallback token se deviceToken vazio
#define MQTT_DEFAULT_PATH   "/"                  // legado EEPROM (nao usado no HTTP)

#define ADMIN_DEFAULT_PASS  "C6m8n4d2d3"   // senha para mudar as configurações
// AP fixo no setup/reset — sempre "home-setup" até concluir provisionamento.
#define FACTORY_SETUP_AP    "home-setup"
// Nome do AP de configuração é DINÂMICO: sempre &lt;uuid&gt;-setup (senha AP_PASSWORD).
#define AP_PASSWORD         "C6m8n4d2d3"   // senha do AP de configuração

// Nome do AP mantido em buffer estatico.
static char portalApName[33] = "";

#define MQTT_RETRY_MS       10000UL        // intervalo de reconexao Wi-Fi / gateway
// Comando/config por UUID (não usa mais home/cmd nem home/config globais)

// ------------------------- Pinos fixos do ESP-01 / ESP-01S -------------------
// Relé  -> GPIO0 (ativo-baixo: LOW liga). GPIO0 é pino de boot — o relé pode
//          "clicar" ao resetar e a polaridade é FIXA em ativo-baixo.
// LED   -> GPIO2 (onboard, pino de bootstrap — usado só no boot pulse).
// Botão -> não existe no ESP-01.
#define RELAY_GPIO        0
#define RELAY_LOGICAL_PIN 1
#define LED_BOOT_GPIO     2

int relayPin = RELAY_GPIO;               // GPIO ativo (sempre GPIO0 no ESP-01)
int relayLogicalPin = RELAY_LOGICAL_PIN; // pin lógico (1 = GPIO0)

// Polaridade do relé: ESP-01 é FIXO em ativo-baixo (LOW liga).
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

// Intervalos
const unsigned long intervalConnection = 120000;  // 2 min (tempos de conexão)
const unsigned long intervalSensors = 60000;      // 60 s (telemetria — HTTPS esquenta)
const unsigned long pingInterval = 60000;         // 60 s (teste de internet)
const unsigned long intervalAvailability = 120000; // 2 min (disponibilidade)

// Logs das ultimas 24h (legado local; telemetria usa basic/advance)
String logs[24];

// Identidade: vazia de fábrica — preenchida no setup inicial (portal)
char userId[40] = "";
bool deviceProvisioned = false;

// Monitoramento
unsigned long lastReconnectAttempt = 0;
uint8_t wifiReconnectFails = 0;
uint8_t internetFailCount = 0;
bool isConnected = false;
bool lastPingOk = true;   // estado anterior do teste de internet (para não repetir log)

// Agendamento do relé — V1_FIXO: poke=1 sempre 4000 ms, depois desliga
#define RELAY_FIXED_ON_MS 4000UL
bool relayScheduled = false;
unsigned long relayScheduleStart = 0;
unsigned long relayScheduleDuration = 0;
bool relayScheduleNextOn = false;   // estado a aplicar quando o tempo expirar

void checkRelaySchedule();  // protótipo — definida junto ao controle do relé

// --------------------- Servidor Java (EEPROM) -----------------------

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
// Se preenchido, e usado como X-User-Secret / X-Device-Token no HTTP.
char deviceToken[48] = "";   // espSecret (ate 47 chars; backend gera 32–40)

// UUID do dispositivo (identidade; usado no client ID e nos tópicos)
char uuid[40] = "";

// Buffer para montar os tópicos home/<user>/<uuid>/…
char topicBuf[96];

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
static bool serverRecoveryApActive = false;
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
//   703    magic token (0x5F) — token de aplicacao por dispositivo
//   704..  token do dispositivo (40)
//
// Contingência (CANDIDATO — nunca apaga a estável até confirmar):
//   400    magic (0x5B)
//   401    flags: bit0=wifi_pendente, bit1=mqtt_pendente
//   402    tentativas já feitas (0..3)
//   404..  Wi-Fi SSID candidato (40)
//   444..  Wi-Fi senha candidata (40)
//   484..  MQTT porta candidata (2)
//   486..  MQTT host (40)
//   526..  MQTT user (30)
//   556..  MQTT pass (30)
//   586..  MQTT path (30)

#define EEPROM_SIZE            1024
#define CFG_MAX_ATTEMPTS       3
#define CFG_ATTEMPT_WINDOW_MS  20000UL
// Quando só o servidor mudou (Wi-Fi já está de pé), o poll falha rápido
// (HTTP -1 em ~3s). Não precisa esperar 20s por tentativa — rollback sai
// em ~3×4s = 12s em vez de minutos.
#define CFG_ATTEMPT_WINDOW_FAST_MS  4000UL

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
void restartJavaGateway();
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
int configApChannel();
void forceWifiReconnect(bool hard = false);
extern bool javaGatewayReady;
extern bool javaServerUnreachable;
extern bool javaServerOk;
bool javaPostEvent(const char* type, const char* payload);
void javaQueueEvent(const char* type, const char* payload);
void javaResetHttpClient();
bool connectJavaGateway();
void javaGatewayLoop();

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

// Host do servidor válido: não vazio e sem caracteres que quebram a URL
// ("host:porta", ":", "https://", espaços, "/path"). Host inválido NÃO
// pode ser gravado — caso contrário o ESP fica em loop de HTTP -1.
bool isValidJavaHost(const String& h) {
  if (h.length() == 0) return false;
  for (size_t i = 0; i < h.length(); i++) {
    char c = h.charAt(i);
    if (c == ':' || c == '/' || c == ' ' || c == '\\') return false;
  }
  return true;
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
    restartJavaGateway();
  } else {
    Serial.println("Estavel tambem falhou — abrindo portal.");
    startConfigPortalAp();
  }
}

void processConfigTest() {
  if (!configTestPending()) return;
  if (!javaGatewayReady && cfgMqttPending) return;

  static unsigned long attemptStarted = 0;
  unsigned long now = millis();
  if (attemptStarted == 0) attemptStarted = now;

  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  // Servidor OK só com poll HTTP 200 real (javaServerOk). javaGatewayReady
  // apenas reflete "Wi-Fi conectado" — não valida o servidor (host vazio/
  // errado ainda passaria) e causava COMMIT prematuro do candidato.
  bool mqttOk = !cfgMqttPending || javaServerOk;

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

  // Janela de tentativa: se só o servidor mudou e o Wi-Fi já está de pé,
  // o poll falha rápido — usa janela curta para não segurar o rollback.
  // Se há candidato de Wi-Fi (ou Wi-Fi fora), mantém a janela longa para
  // dar tempo do rádio associar.
  unsigned long window = CFG_ATTEMPT_WINDOW_MS;
  if (cfgMqttPending && !cfgWifiPending && wifiOk) {
    window = CFG_ATTEMPT_WINDOW_FAST_MS;
  }

  if ((now - attemptStarted) < window) return;

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
    restartJavaGateway();
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
  // Host inválido (vazio/":" etc.) nunca pode ser promovido — mantém o atual.
  if (!isValidJavaHost(String(mqttHost))) {
    Serial.println("adoptMqtt: host invalido na RAM — mantendo config estavel.");
    copyStr(mqttHost, sizeof(mqttHost), readStringEEPROM(100, 40));
    return false;
  }
  if (hasStableMQTT()) {
    // Só entra em contingência se host/porta realmente mudaram em relação
    // à config estável. Salvar a página Servidor sem alterar nada não pode
    // derrubar o gateway nem disparar o teste (reinício desnecessário).
    String stableHost = readStringEEPROM(100, 40);
    int stablePort = EEPROM.read(1) | (EEPROM.read(2) << 8);
    bool hostChanged = stableHost != String(mqttHost);
    bool portChanged = (stablePort != mqttPort);
    if (hostChanged || portChanged) {
      proposeMqttCandidate();
      return true;
    }
    saveMQTTConfig();
    return false;
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

// Volta ao estado de fabrica "seco": so host/porta Java nos defaults.
void factoryResetToDry() {
  Serial.println("=== FACTORY RESET → seco ===");

  uuid[0] = 0;
  EEPROM.write(312, 0);
  writeStringEEPROM(313, "", 40);

  userId[0] = 0;
  EEPROM.write(EE_USER_MAGIC, 0);
  writeStringEEPROM(EE_USER_ID, "", 40);

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

// Canal do softAP. Quando o STA está conectado, usa o MESMO canal do roteador:
// o ESP8266 é single-channel — se o AP fica em canal diferente (ex.: fixo 6),
// o rádio alterna entre os dois canais (channel hopping), degradando o STA
// (telemetria cai) e o AP fica intermitente. Com dois ESPs próximos, cada um
// pulando canais, a interferência piora e um "derruba" o outro.
int configApChannel() {
  if (WiFi.status() == WL_CONNECTED) {
    int ch = WiFi.channel();
    if (ch >= 1 && ch <= 13) return ch;
  }
  return 6;   // sem STA conectado: canal padrão
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

    ok = WiFi.softAP(portalApName, AP_PASSWORD, configApChannel(), 0, 4);
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

// --- Polaridade do relé (ESP-01: FIXA em ativo-baixo) ---
void loadRelayPolarity() {
  relayActiveHigh = false;   // GPIO0 é pino de boot — só ativo-baixo
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

// ------------------------- Helpers de transporte ----------------------------
void buildTopic(const char* suffix) {
  snprintf(topicBuf, sizeof(topicBuf), "home/%s/%s/%s", userId, uuid, suffix);
}

void publishMQTT(const char* suffix, const char* msg, bool retained) {
  (void)retained;
  javaQueueEvent(suffix, msg);
}

void publishAbsolute(const char* topic, const char* msg, bool retained = false) {
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

  char internetEsc[48], serverEsc[48], uuidEsc[48], userEsc[48], ipEsc[24];
  jsonEscapeTo(internetEsc, sizeof(internetEsc), ssidNow.c_str());
  jsonEscapeTo(serverEsc, sizeof(serverEsc), mqttHost);
  jsonEscapeTo(uuidEsc, sizeof(uuidEsc), uuid);
  jsonEscapeTo(userEsc, sizeof(userEsc), userId);
  String ipStr = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
  jsonEscapeTo(ipEsc, sizeof(ipEsc), ipStr.c_str());

  char basic[400];
  snprintf(basic, sizeof(basic),
           "{\"internet\":\"%s\",\"server\":\"%s\",\"modelo\":\"ESP-01\","
           "\"uuid\":\"%s\",\"user_id\":\"%s\",\"ip\":\"%s\"}",
           internetEsc, serverEsc, uuidEsc, userEsc, ipEsc);

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
  return WiFi.status() == WL_CONNECTED && isProvisioned();
}

// Eventos pontuais so no Serial — telemetria = logs/basic + logs/advance
void sendLogsMQTT(String logMessage) {
  Serial.print("[log] ");
  Serial.println(logMessage);
}

// ------------------------- Controle do relé (com JSON) -----------------------
// Publicar em home/<uuid>/cmd:
//   {"poke":1,"time":0}
// poke = 1 liga / 0 desliga | time = ms ate desligar sozinho ao LIGAR.
// time = 0 = permanente. DESLIGAR ignora time (sempre permanente).
//
// Ex.: ligado + {"poke":1,"time":1000}
//   → liga e DESLIGA automaticamente após 1000 ms.

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

// ------------------------- Controle dos pinos (ESP-01) ----------------------
// Aplica o estado físico do relé (polaridade fixa ativo-baixo).
void setRelayOn(bool on) {
  digitalWrite(RELAY_GPIO, on ? LOW : HIGH);
}

// Inicializa as saídas em estado seguro (relé desligado).
void initOutputs() {
  pinMode(RELAY_GPIO, OUTPUT);
  digitalWrite(RELAY_GPIO, HIGH);   // ativo-baixo: desligado = HIGH
}

void setAllOutputsOff() {
  relayLogicalOn = false;
  digitalWrite(RELAY_GPIO, HIGH);   // ativo-baixo: desligado = HIGH
}

// Reinício seguro: deixa os pinos de boot do ESP8266 em nível correto.
void safeRestart() {
  Serial.println("Reinicio seguro (pinos de boot em estado seguro)...");
  Serial.flush();
  relayScheduled = false;
  // ESP-01: rele em GPIO0 ativo-baixo — se reiniciar com rele LIGADO,
  // GPIO0 fica LOW e o boot cai em modo flash (dispositivo "some").
  setAllOutputsOff();
  delay(350);
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
  if (relayPin == 0) {
    delay(40);
    ESP.wdtFeed();
    yield();
  }
  Serial.print("Saída GPIO");
  Serial.print(relayPin);
  Serial.print(" (pin ");
  Serial.print(relayLogicalPin);
  Serial.print(") = ");
  Serial.println(on ? "LIGADO" : "DESLIGADO");
  // Toda ação reporta state + status (1 POST por ciclo no gateway).
  publishMQTT("state", on ? "0" : "1", true);
  publishMQTT("status", "online", true);
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

// Pausa POST HTTP apos ligar GPIO0 (TLS com rele ON derruba ESP-01).
extern unsigned long javaPauseHttpUntil;

void handleRelayCommand(String msg) {
  msg.trim();

  int jsonStart = msg.indexOf('{');
  if (jsonStart > 0) msg = msg.substring(jsonStart);

  if (!msg.startsWith("{")) {
    Serial.println("cmd: formato invalido (JSON).");
    return;
  }

  // Deduplica poll/app reenviando o mesmo JSON em <300 ms.
  static unsigned long lastCmdMs = 0;
  static uint32_t lastCmdHash = 0;
  uint32_t h = msg.length() * 2654435761u;
  for (unsigned i = 0; i < msg.length(); i++) h = (h * 131) + (uint8_t)msg[i];
  unsigned long nowMs = millis();
  if (h == lastCmdHash && (nowMs - lastCmdMs) < 300UL) {
    Serial.println("V1_FIXO: cmd duplicado ignorado.");
    return;
  }
  lastCmdMs = nowMs;
  lastCmdHash = h;

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

  String dev = sanitizeUUID(jsonGetStringCI(msg, "device"));
  if (dev.length() > 0 && !uuidEqualsCI(dev, uuid)) {
    Serial.print("cmd com device=");
    Serial.print(dev);
    Serial.println(" — ignorado.");
    return;
  }

  long poke = jsonGetIntCI(msg, "poke");
  long pinArg = jsonGetIntCI(msg, "pin");
  if (poke != 0 && poke != 1) {
    Serial.println("JSON invalido: poke deve ser 0 ou 1.");
    publishMQTT("ack", "erro:poke_invalido");
    return;
  }

  // Timer ja venceu mas ainda marcado: finaliza antes de aceitar novo cmd.
  if (relayScheduled) {
    unsigned long elapsed = millis() - relayScheduleStart;
    if (elapsed >= relayScheduleDuration) {
      relayScheduled = false;
      applyRelayState(relayScheduleNextOn);
    }
  }

  // Ciclo ativo: mantem o 1o comando; qualquer outro e ignorado (incl. poke=0).
  if (relayScheduled) {
    unsigned long left = relayScheduleDuration - (millis() - relayScheduleStart);
    Serial.print("V1_FIXO: cmd ignorado — ciclo em andamento, resta ");
    Serial.print(left);
    Serial.println(" ms");
    char busyAck[64];
    snprintf(busyAck, sizeof(busyAck), "erro:ocupado,restam_ms=%lu", left);
    publishMQTT("ack", busyAck);
    return;
  }

  // ESP-01 tem uma única saída (pin 1 = GPIO0). Aceita "pin":1 ou ausente.
  if (pinArg >= 0 && pinArg != RELAY_LOGICAL_PIN) {
    Serial.println("JSON invalido: pin fora do modelo.");
    publishMQTT("ack", "erro:pin_invalido");
    return;
  }

  bool on = (poke == 1);
  unsigned long t = on ? RELAY_FIXED_ON_MS : 0UL;

  if (relayPin >= 0) pinMode(relayPin, OUTPUT);

  // Arma o timer ANTES de qualquer yield — evita reentrada / 2o cmd.
  if (on) {
    relayScheduled = true;
    relayScheduleStart = millis();
    relayScheduleDuration = t;
    relayScheduleNextOn = false;
  } else {
    relayScheduled = false;
    relayScheduleNextOn = false;
  }

  relayLogicalOn = on;
  setRelayOn(on);
  if (relayPin == 0) {
    // ESP-01: enquanto o relé fica LIGADO (GPIO0 LOW), o TLS fica instável e
    // POSTs lentos bloqueiam o loop, atrasando o auto-off. Pausa o HTTP
    // durante todo o ciclo para o timer do relé disparar em ~4s exatos.
    javaPauseHttpUntil = millis() + (on ? RELAY_FIXED_ON_MS + 400UL : 400UL);
    delay(40);
    ESP.wdtFeed();
    yield();
  }

  if (on) {
    Serial.print("CMD -> LIGAR por ");
    Serial.print(t);
    Serial.println(" ms (fixo), depois DESLIGAR");
  } else {
    Serial.println("CMD -> DESLIGAR (permanente)");
  }

  char ack[96];
  snprintf(ack, sizeof(ack), "ok:poke=%d,time=%lu,pin=%d,gpio=%d,next=%d",
           on ? 1 : 0, t, relayLogicalPin, relayPin, relayScheduleNextOn ? 1 : 0);

  publishMQTT("ack", ack);
  publishMQTT("state", relayLogicalOn ? "0" : "1", true);
  publishMQTT("status", "online", true);
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

// ------------------- Configuracao via poll type=config -------------------
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

  // MODELO ignorado: firmware é sempre ESP-01 (relé GPIO0, ativo-baixo).
  {
    String mIgnorado = jsonGetStringAny(msg, "MODELO", "MODEL", "BOARD");
    if (mIgnorado.length() > 0) {
      ack += ",\"modelo\":\"ESP-01\",\"aviso\":\"modelo_fixo\"";
    }
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

  // 5) Servidor Java (host/porta na EEPROM)
  bool mqttDirty = false;
  String mhost = jsonGetStringAny(msg, "MQTT_HOST", "MHOST", "HOST");
  if (mhost.length() == 0) mhost = jsonGetStringAny(msg, "JAVA_HOST", "SERVER_HOST", "SERVER");
  if (mhost.length() > 0) {
    if (mhost.startsWith("wss://")) mhost = mhost.substring(6);
    else if (mhost.startsWith("ws://")) mhost = mhost.substring(5);
    else if (mhost.startsWith("https://")) mhost = mhost.substring(8);
    else if (mhost.startsWith("http://")) mhost = mhost.substring(7);
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
    // Só grava se o host final for um hostname válido. Host vazio, ":" ou
    // "host:" (colon==0) ou "https" quebrado NÃO pode sobrescrever o atual —
    // senão o ESP fica em loop de HTTP -1 com host inválido.
    if (isValidJavaHost(mhost)) {
      copyStr(mqttHost, sizeof(mqttHost), mhost);
      mqttDirty = true;
    } else {
      Serial.print("config: host invalido ignorado [");
      Serial.print(mhost);
      Serial.println("]");
    }
  }

  long mport = jsonGetIntCI(msg, "MQTT_PORT");
  if (mport < 0) mport = jsonGetIntCI(msg, "MPORT");
  if (mport < 0) mport = jsonGetIntCI(msg, "JAVA_PORT");
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

  // 6) Lógica do relé — ESP-01 é FIXO em ativo-baixo (2). Qualquer pedido
  //    retorna o aviso; nunca altera a polaridade.
  long logica = jsonGetIntCI(msg, "LOGICA_DO_RELE");
  if (logica < 0) logica = jsonGetIntCI(msg, "LOGICA");
  if (logica < 0) logica = jsonGetIntCI(msg, "RELAYMODE");
  if (logica == 1 || logica == 2) {
    ack += ",\"logica\":2,\"aviso\":\"modelo_somente_ativo_baixo\"";
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

void updateConnectionStatus() {
  if (WiFi.status() == WL_CONNECTED && mqttConnected()) {
    publishMQTT("status", "online", true);
  } else {
    publishMQTT("status", "offline", true);
  }
}

// ------------------------- Página web de configuração -----------------------
// A configuração é feita pelo navegador e protegida pela senha de admin.
// O Wi-Fi e o servidor Java ficam em areas SEPARADAS da EEPROM.

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
    h += F(">Servidor</a><a href='/device'");
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
// Página de setup inicial (dispositivo seco): formulário único obrigatório.
// Se errMsg não for vazio, mostra o aviso no topo do formulário.
String renderSetupInitialPage(const String& errMsg) {
  String html = pageTop("Setup inicial — HOME", "/", true);
  html.reserve(6500);
  html += F("<div class='hero'>");
  html += F("<div class='logo-lg'>HOME</div>");
  html += F("<p>Setup inicial — preencha <b>todos</b> os campos</p>");
  html += F("</div>");
  if (errMsg.length() > 0) {
    html += F("<div class='card' style='border-color:#f87171;margin-bottom:14px'>");
    html += F("<p style='color:#f87171;font-size:14px;margin:0'>");
    html += htmlEscape(errMsg);
    html += F("</p></div>");
  }
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
    html += F("<label>Modelo da placa</label>");
    html += F("<input value='ESP-01 / ESP-01S (fixo)' disabled>");
    html += F("<p class='hint'>Modelo fixo: relé GPIO0 (ativo-baixo — LOW liga), "
              "LED GPIO2. Ao ligar (poke=1), desliga sozinho após 4s.</p>");

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
  return html;
}

void handleHome() {
  if (!isProvisioned()) {
    sendHtml(renderSetupInitialPage(""));
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
  html += F("<a href='/mqtt'><span class='ico'>SV</span>Servidor Java</a>");
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

// ----------------------- Telas Servidor e Dispositivo ----------------------
void handleMqttPage() {
  if (!isProvisioned()) { handleHome(); return; }
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
  html += pageBottom();
  sendHtml(html);
}

void handleDevicePage() {
  if (!isProvisioned()) { handleHome(); return; }
  String html = pageTop("Dispositivo — HOME", "/device");
  html.reserve(2400);
  html += F("<h1>Identificação do dispositivo</h1>");
  html += F("<p class='sub'>O UUID identifica este dispositivo no backend Java "
            "(/api/esp/{uuid}/...). Muda-lo altera a identidade na API.</p>");
  html += F("<div class='card'>");
  html += F("<form method='POST' action='/save'>");
  html += F("<input type='hidden' name='section' value='device'>");
  html += F("<label>UUID do dispositivo</label>");
  html += F("<input name='uuid' value='");
  html += htmlEscape(String(uuid));
  html += F("' placeholder='Identificador único'>");
  html += F("<label>Modelo da placa</label>");
  html += F("<input value='ESP-01 / ESP-01S (fixo)' disabled>");
  html += F("<p class='hint'>Fixo: relé GPIO0 (ativo-baixo — LOW liga, GPIO0 é pino "
            "de boot), LED GPIO2. Ao ligar (poke=1), desliga sozinho após 4s.</p>");

  html += F("<label>user_id (dono / pai)</label>");
  html += F("<input name='user_id' value='");
  html += htmlEscape(userId[0] ? String(userId) : String(""));
  html += F("' placeholder='Identificador do dono / casa'>");
  html += F("<p class='hint'>Identifica a conta/casa no backend Java. "
            "Usado no endereço home/&lt;user_id&gt;/&lt;uuid&gt;/... "
            "Mudar altera o roteamento dos eventos e logs.</p>");

  html += F("<label>Lógica do relé</label>");
  html += F("<select name='relaymode' disabled>");
  html += F("<option value='0' selected>Ativo-baixo (LOW liga — fixo neste modelo)</option>");
  html += F("</select>");
  html += F("<input type='hidden' name='relaymode' value='0'>");
  html += F("<p class='hint'>Fixo em ativo-baixo: GPIO0 é pino de boot.</p>");
  html += F("<label>Senha de administrador (atual)</label>");
  html += F("<input type='password' name='admin' required placeholder='Necessária para salvar'>");
  html += F("<label>Nova senha de administrador</label>");
  html += F("<input type='password' name='newadmin' placeholder='Deixe vazio para manter a atual'>");
  html += F("<input type='submit' class='btn' value='Salvar e reiniciar'>");
  html += F("</form></div>");

  html += F("<div class='card' style='margin-top:16px;border:1px solid #f87171'>");
  html += F("<h1 style='font-size:16px;color:#f87171'>Reset de fábrica (seco)</h1>");
  html += F("<p class='hint'>Apaga Wi-Fi, user_id, uuid e senha admin. "
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
  bool changed = false;

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

    if (ssid.length() == 0 || wpass.length() == 0 ||
        uid.length() == 0 || u.length() == 0 ||
        host.length() == 0 || port.length() == 0 ||
        esecret.length() < 8 || na.length() < 6) {
      sendHtml(renderSetupInitialPage(
        "Preencha todos os campos (espSecret mín. 8, senha admin mín. 6)."));
      return;
    }
    if (!isValidJavaHost(host)) {
      sendHtml(renderSetupInitialPage(
        "Host do servidor inválido. Use só o hostname "
        "(ex.: mqtt.omny.app.br), sem http://, porta ou caminho."));
      return;
    }
    if (esecret.length() > (int)(sizeof(deviceToken) - 1)) {
      sendHtml(renderSetupInitialPage(
        "espSecret longo demais (máx. 47). Gere um novo no app."));
      return;
    }
    if (na != na2) {
      sendHtml(renderSetupInitialPage(
        "A confirmação da senha admin não confere."));
      return;
    }

    copyStr(userId, sizeof(userId), uid);
    saveUserId();
    copyStr(uuid, sizeof(uuid), u);
    saveUUID(String(uuid));

    copyStr(mqttHost, sizeof(mqttHost), host);
    int p = port.toInt();
    if (p > 0 && p <= 65535) mqttPort = p;
    copyStr(mqttPath, sizeof(mqttPath), String("/"));
    copyStr(deviceToken, sizeof(deviceToken), esecret);
    saveDeviceToken();
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
    loadWiFiConfig();
    loadProvisioned();
    debugProvisionState("pos-setup");
    if (!isProvisioned()) {
      sendHtml(renderSetupInitialPage(
        "Falha ao gravar o setup na EEPROM. Tente de novo."));
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

  // Reset de fabrica -> estado seco (so servidor Java default)
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
      if (ssid != String(wifiSSID) || wpass != String(wifiPass)) changed = true;
      if (adoptWiFiChange(ssid, wpass)) testing = true;
    }
  }

  if (section == "mqtt" || section == "all") {
    bool mqttDirty = false;
    String host = webServer.arg("mhost");
    host.trim();
    if (host.startsWith("https://")) host = host.substring(8);
    else if (host.startsWith("http://")) host = host.substring(7);
    else if (host.startsWith("wss://")) host = host.substring(6);
    else if (host.startsWith("ws://")) host = host.substring(5);
    int slash = host.indexOf('/');
    if (slash > 0) host = host.substring(0, slash);
    int colon = host.indexOf(':');
    if (colon > 0) host = host.substring(0, colon);
    if (host.length() > 0 && isValidJavaHost(host)) {
      copyStr(mqttHost, sizeof(mqttHost), host);
      mqttDirty = true;
    } else if (host.length() > 0) {
      sendHtml(errorPage("Host do servidor inválido. Use só o nome do host "
                         "(ex.: mqtt.omny.app.br), sem porta ou caminho."));
      return;
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

    String esecret = webServer.arg("esecret");
    esecret.trim();
    if (esecret.length() >= 8) {
      copyStr(deviceToken, sizeof(deviceToken), esecret);
      saveDeviceToken();
    }

    if (mqttDirty) {
      if (adoptMqttChange()) testing = true;
      changed = true;
    }
  }

  if (section == "device" || section == "all") {
    String u = sanitizeUUID(webServer.arg("uuid"));
    if (u.length() > 0 && u != String(uuid)) {
      copyStr(uuid, sizeof(uuid), u);
      saveUUID(String(uuid));
      changed = true;
    }

    String newUser = sanitizeUserId(webServer.arg("user_id"));
    if (newUser.length() > 0 && newUser != String(userId)) {
      copyStr(userId, sizeof(userId), newUser);
      saveUserId();
      changed = true;
    }

    // Modelo e lógica do relé são FIXOS no ESP-01 (relé GPIO0 ativo-baixo).
    // O campo relaymode vem oculto/desabilitado — força ativo-baixo.

    String newAdmin = webServer.arg("newadmin");
    newAdmin.trim();
    if (newAdmin.length() > 0) {
      copyStr(adminPass, sizeof(adminPass), newAdmin);
      saveAdminConfig();
      changed = true;
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
                    "configuração (até 3 tentativas). Se falhar, restaura a anterior."));
  } else if (!changed) {
    sendHtml(okPage("Nada foi alterado — mantive as configurações atuais."));
    return;
  } else {
    sendHtml(okPage("Configurações salvas!"));
  }
  delay(800);
  safeRestart();
}

void onWifiManagerSave() {
  // Removido — WiFiManager não é mais usado. Mantido vazio por compat.
}

// Teste local do rele — /relay-test?poke=1 (V1_FIXO: liga 4s fixo)
void handleRelayTest() {
  String pokeArg = webServer.arg("poke");
  pokeArg.trim();
  if (pokeArg != "0" && pokeArg != "1") {
    webServer.send(400, "text/plain",
      "Use /relay-test?poke=1 (4s fixo) ou /relay-test?poke=0");
    return;
  }
  String json = String("{\"poke\":") + pokeArg + "}";
  handleRelayCommand(json);
  webServer.send(200, "text/plain",
    String("OK poke=") + pokeArg + " GPIO" + String(relayPin) + " = " +
    (digitalRead(relayPin) ? "HIGH" : "LOW") +
    (pokeArg == "1" ? " (auto-off 4s)" : ""));
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
  // HTTPS + modem-sleep no ESP8266 trava o TLS (so volta com reset fisico).
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
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
    bool ok = WiFi.softAP(portalApName, AP_PASSWORD, configApChannel(), 0, 4);
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
      recoveryApActive = WiFi.softAP(portalApName, AP_PASSWORD, configApChannel(), 0, 4);
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

// Reinicia o gateway Java (HTTP)
void restartJavaGateway() {
  if (!isProvisioned()) return;
  Serial.println("Reconectando Java gateway...");
  javaGatewayReady = false;
  connectJavaGateway();
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
      recoveryApActive = WiFi.softAP(portalApName, AP_PASSWORD, configApChannel(), 0, 4);
    }
  } else {
    WiFi.mode(WIFI_STA);
  }
  // false = não apaga softAP
  WiFi.disconnect(false);
  delay(hard ? 200 : 100);
  WiFi.begin(wifiSSID, wifiPass);
}

// Chamado quando o Wi-Fi cai: fecha sockets HTTP/TLS
void onWifiLostCleanup() {
  javaGatewayReady = false;
  javaServerOk = false;
  javaResetHttpClient();
}

// Pisca GPIO2 (LED onboard do ESP-01) — prova que o sketch rodou mesmo sem Serial.
void bootLedPulse() {
  pinMode(LED_BOOT_GPIO, OUTPUT);
  digitalWrite(LED_BOOT_GPIO, HIGH);
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_BOOT_GPIO, LOW);
    delay(150);
    digitalWrite(LED_BOOT_GPIO, HIGH);
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
  // ESP-01: pinos/polaridade são fixos (relé GPIO0 ativo-baixo).
  loadRelayPolarity();
  loadProvisioned();
  loadMaintenanceApSetting();
  applyWifiPowerPolicy();
  // Migração: se já tem identidade completa (ex. config antiga), marca provisionado
  if (!deviceProvisioned &&
      strlen(uuid) > 0 && strlen(userId) > 0 &&
      strlen(wifiSSID) > 0) {
    saveProvisioned(true);
  }
  // Só desfaz se faltar identidade (não só wifi — o SDK pode ter SSID)
  if (deviceProvisioned &&
      (strlen(uuid) == 0 || strlen(userId) == 0)) {
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

  Serial.println("=== HOME Sistema — ESP-01 (V1_FIXO) ===");
  Serial.println("Transporte: Java gateway (HTTP)");
  Serial.print("Provisionado: ");
  Serial.println(isProvisioned() ? "sim" : "NAO (setup no AP <uuid>-setup)");
  Serial.print("UUID: ");
  Serial.println(uuid[0] ? uuid : "(vazio)");
  Serial.print("AP config: ");
  Serial.println(configApName());
  Serial.print("user_id: ");
  Serial.println(userId[0] ? userId : "(vazio)");
  Serial.print("modelo: ");
  Serial.println("ESP-01 (fixo)");
  Serial.print("AP ");
  Serial.print(configApName());
  Serial.print(" sempre aberto: ");
  Serial.println(maintenanceApEnabled ? "sim" : "nao");
  Serial.print("relé: GPIO");
  Serial.print(relayPin);
  Serial.print(" (pin ");
  Serial.print(relayLogicalPin);
  Serial.print("), ativo-baixo (LOW liga), auto-off ");
  Serial.print(RELAY_FIXED_ON_MS / 1000UL);
  Serial.println("s");
  Serial.print("Servidor: ");
  Serial.print(mqttHost);
  Serial.print(":");
  Serial.print(mqttPort);
  Serial.println(" (HTTP /api/esp/...)");
  if (configTestPending()) {
    Serial.println("*** MODO CONTINGENCIA: testando configuracao candidata ***");
  }

  connectWiFi();
  applyWifiPowerPolicy();

  Serial.println("Modo Java gateway (HTTP).");
  if (WiFi.status() == WL_CONNECTED) {
    connectJavaGateway();
  }

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

  // Timer do rele no inicio (nao depende de Wi-Fi/gateway)
  checkRelaySchedule();

  processConfigTest();

  static bool wifiWasUp = true;   // setup já deixou o Wi-Fi conectado
  bool wifiUp = (WiFi.status() == WL_CONNECTED);

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

  wifiServerProcess();   // mantém o servidor web + DNS captive portal
  // Java+HTTPS: MDNS compete com BearSSL e derruba poll/telemetria.

  // ESP-01 não tem botão — sem monitoramento de botão.

  // ------------------- Sem Wi-Fi -------------------
  if (!wifiUp) {
    if (wifiLostAt == 0) wifiLostAt = now;

    // Sobe AP de config <uuid>-setup após 5 tentativas de reconexão falhas
    // (fallback: 60 s offline). Mantém AP+STA — NÃO mata o cliente Wi-Fi.
    // Nunca reinicia em loop.
    bool apDueByTries = (wifiReconnectFails >= 5);
    if (isProvisioned() && (apDueByTries || (now - wifiLostAt > 60000UL))) {
      if (!wifiRecoveryMode) {
        wifiRecoveryMode = true;
        if (apDueByTries) {
          Serial.println("=== Wi-Fi falhou 5x — abrindo AP <uuid>-setup ===");
        } else {
          Serial.println("=== Wi-Fi offline >60s — AP de config + reconectar ===");
        }
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
        recoveryApActive = WiFi.softAP(portalApName, AP_PASSWORD, configApChannel(), 0, 4);
      }
      ensureConfigWebServer();
      // Captive portal: qualquer URL no AP cai no http://192.168.4.1
      if (!dnsServerActive) {
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", WiFi.softAPIP());
        dnsServerActive = true;
      }
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
    }

    // Servidor Java inacessível com Wi-Fi ok (5x HTTP -1): abre o AP
    // <uuid>-setup para reconfigurar o host, sem derrubar a STA — que
    // continua tentando reconectar. Fecha sozinho quando o servidor volta.
    if (javaServerUnreachable) {
      if (!serverRecoveryApActive) {
        serverRecoveryApActive = true;
        Serial.println("=== Servidor inacessivel 5x — abrindo AP <uuid>-setup ===");
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
        recoveryApActive = WiFi.softAP(portalApName, AP_PASSWORD, configApChannel(), 0, 4);
      }
      ensureConfigWebServer();
      if (!dnsServerActive) {
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", WiFi.softAPIP());
        dnsServerActive = true;
      }
    } else if (serverRecoveryApActive) {
      serverRecoveryApActive = false;
      recoveryApActive = false;
      if (dnsServerActive) {
        dnsServer.stop();
        dnsServerActive = false;
      }
      Serial.println("Servidor respondeu — fechando AP <uuid>-setup.");
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

    // Telemetria — Java: so advance (leve); basic a cada 5 ciclos. Menos HTTPS = menos hang.
    if (now - previousSensorMillis >= intervalSensors) {
      previousSensorMillis = now;
      publishLogsAdvance();
      static uint8_t javaBasicEvery = 0;
      if (++javaBasicEvery >= 5) {
        javaBasicEvery = 0;
        publishLogsBasic();
      }
    }

  }
  yield();
}
