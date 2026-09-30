// ============================================================================
//  VENUE Sistema — Versão MQTT sobre WebSocket Seguro (WSS)
//
//  - Conecta no MESMO broker que o MQTTX usa (wss://mosquito.omny.tec.br:443/)
//  - Visualização e controle via Node-RED (ou qualquer cliente MQTT)
//  - WiFiManager: portal "VENUE_Lock" configura APENAS o Wi-Fi
//  - Config do broker MQTT é feita em página SEPARADA: http://192.168.4.1/mqtt
//    (guardada na EEPROM, independente do Wi-Fi — um não afeta o outro)
//  - AP de configuração SEMPRE ativo, protegido pela senha: C6m8n4d2d3
//  - Botão (GPIO2) pressionado por 4s reseta as configurações e reabre o portal
//  - Relé em GPIO0 (D3): LOW = LIGADO, HIGH = DESLIGADO
//  - MQTT Last Will: se o ESP cair, o broker publica "offline" automaticamente
//
//  BIBLIOTECAS NECESSÁRIAS (Gerenciador de Bibliotecas do Arduino IDE):
//    1. WebSockets_Generic       (khoih-prog)
//    2. MQTTPubSubClient_Generic (khoih-prog)
//
//  TÓPICOS:
//    venue/relay/cmd           recebe 0 (liga) ou 1 (desliga)   [assinado]
//    venue/relay/state         estado atual do relé             [retido]
//    venue/status              online / offline / config_mode   [retido]
//    venue/logs                buffer de logs (últimas 24h)
//    venue/ip                  IP do dispositivo
//    venue/stats/connected_time   / rssi / voltage / temperature
//    venue/stats/free_heap        / sdk_version
//    venue/stats/availability     / unavailability
// ============================================================================

// Bibliotecas
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <EEPROM.h>

// MQTT sobre WebSocket Seguro (WSS) — conecta no mesmo broker que o MQTTX usa
#include <WebSocketsClient_Generic.h>   // DEVE vir antes do MQTT
#ifndef MQTTPUBSUBCLIENT_USE_WEBSOCKETS
#define MQTTPUBSUBCLIENT_USE_WEBSOCKETS true
#endif
#include <MQTTPubSubClient_Generic.h>

// ------------------------- Broker MQTT (valores padrão) ---------------------
// Usados na primeira gravação. Depois podem ser alterados pela página
// http://192.168.4.1/mqtt (mantidos na EEPROM).
#define MQTT_DEFAULT_HOST   "mosquito.omny.tec.br"   // Host/IP do seu broker MQTT
#define MQTT_DEFAULT_PORT   443             // Porta WSS (WebSocket Seguro, padrão 443)
#define MQTT_DEFAULT_USER   "cardodo"               // Usuário MQTT (vazio se não houver)
#define MQTT_DEFAULT_PASS   "C6m8n4d2d3"               // Senha MQTT (vazio se não houver)
#define MQTT_DEFAULT_PATH   "/"               // Path do WebSocket (igual ao do MQTTX)

// Prefixo dos tópicos
#define MQTT_TOPIC_PREFIX   "venue"

#define TOPIC_RELAY_CMD      MQTT_TOPIC_PREFIX "/relay/cmd"
#define TOPIC_RELAY_STATE    MQTT_TOPIC_PREFIX "/relay/state"
#define TOPIC_STATUS         MQTT_TOPIC_PREFIX "/status"
#define TOPIC_LOG            MQTT_TOPIC_PREFIX "/logs"
#define TOPIC_IP             MQTT_TOPIC_PREFIX "/ip"
#define TOPIC_TIME_CONNECTED MQTT_TOPIC_PREFIX "/stats/connected_time"
#define TOPIC_RSSI           MQTT_TOPIC_PREFIX "/stats/rssi"
#define TOPIC_VOLTAGE        MQTT_TOPIC_PREFIX "/stats/voltage"
#define TOPIC_TEMPERATURE    MQTT_TOPIC_PREFIX "/stats/temperature"
#define TOPIC_FREE_HEAP      MQTT_TOPIC_PREFIX "/stats/free_heap"
#define TOPIC_SDK_VERSION    MQTT_TOPIC_PREFIX "/stats/sdk_version"
#define TOPIC_AVAILABILITY   MQTT_TOPIC_PREFIX "/stats/availability"
#define TOPIC_UNAVAILABILITY MQTT_TOPIC_PREFIX "/stats/unavailability"

// Pinos do relé e botão
int relayPin = 0;   // GPIO0 (D3)
int buttonPin = 2;  // GPIO2 (Botão)

// Tempos de conexão/desconexão
unsigned long connectedTime = 0;
unsigned long dailyConnectedTime[7] = {0};
unsigned long dailyDisconnectedTime[7] = {0};
unsigned long previousMillis = 0;
unsigned long previousSensorMillis = 0;
unsigned long previousPingMillis = 0;
unsigned long buttonPressTime = 0;
bool buttonPressed = false;

// Intervalos
const unsigned long intervalConnection = 120000;  // 2 min (tempos de conexão)
const unsigned long intervalSensors = 15000;      // 15 s (telemetria)
const unsigned long pingInterval = 20000;         // 20 s (teste internet)

// Logs das últimas 24h
String logs[24];

// Monitoramento
unsigned long lastReconnectAttempt = 0;
unsigned long lastMqttAttempt = 0;
bool isConnected = false;
bool lastMqttState = false;   // estado anterior do MQTT (para logar só na transição)

// Backoff de reconexão do WebSocket (evita bombardear o servidor com conexões)
unsigned long wsFailCheckMs = 0;      // último instante que aumentou o backoff
unsigned int  wsFailStreak = 0;       // falhas consecutivas
unsigned int  mqttFailStreak = 0;     // falhas consecutivas do handshake MQTT
const unsigned long WS_RETRY_MIN_MS = 5000;    // 5 s entre tentativas (normal)
const unsigned long WS_RETRY_MAX_MS = 60000;   // 60 s no máximo (servidor fora)

// MQTT sobre WebSocket Seguro (WSS)
WebSocketsClient wsClient;
MQTTPubSub::PubSubClient<512> mqtt;   // buffer 512 para os logs

// Configuração do broker (char arrays)
char mqttHost[40] = MQTT_DEFAULT_HOST;
int  mqttPort     = MQTT_DEFAULT_PORT;
char mqttUser[30] = MQTT_DEFAULT_USER;
char mqttPass[30] = MQTT_DEFAULT_PASS;

// WiFiManager global (mantém o portal de configuração sempre disponível)
WiFiManager wifiManager;

// SSID e senha do AP de configuração (sempre aberto)
const char* apSSID = "VENUE_Lock";
const char* apPassword = "C6m8n4d2d3";

// ------------------------------- EEPROM -------------------------------------
// Endereços usados:
//   0      -> magic (0x56 = config do broker salva)
//   1..2   -> porta MQTT (uint16)
//   100..  -> host do broker (40 bytes)
//   140..  -> usuário MQTT (30 bytes)
//   170..  -> senha MQTT (30 bytes)
//   4..17  -> tempos diários (como no código original)

void writeStringEEPROM(int addr, String s, int maxLen) {
  if ((int)s.length() > maxLen - 1) s = s.substring(0, maxLen - 1);
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, i < (int)s.length() ? s.charAt(i) : 0);
  }
}

String readStringEEPROM(int addr, int maxLen) {
  String s = "";
  for (int i = 0; i < maxLen; i++) {
    char c = EEPROM.read(addr + i);
    if (c == 0) break;
    s += c;
  }
  return s;
}

void saveMQTTConfig() {
  EEPROM.write(0, 0x56);
  EEPROM.write(1, mqttPort & 0xFF);
  EEPROM.write(2, (mqttPort >> 8) & 0xFF);
  writeStringEEPROM(100, String(mqttHost), 40);
  writeStringEEPROM(140, String(mqttUser), 30);
  writeStringEEPROM(170, String(mqttPass), 30);
  EEPROM.commit();
  Serial.println("Configuração do broker salva na EEPROM.");
}

void loadMQTTConfig() {
  if (EEPROM.read(0) == 0x56) {
    int port = EEPROM.read(1) | (EEPROM.read(2) << 8);
    if (port > 0) mqttPort = port;
    String host = readStringEEPROM(100, 40);
    if (host.length() > 0) strncpy(mqttHost, host.c_str(), sizeof(mqttHost) - 1);
    String user = readStringEEPROM(140, 30);
    if (user.length() > 0) strncpy(mqttUser, user.c_str(), sizeof(mqttUser) - 1);
    String pass = readStringEEPROM(170, 30);
    if (pass.length() > 0) strncpy(mqttPass, pass.c_str(), sizeof(mqttPass) - 1);
  }
}

// ------------------------------- Helpers MQTT --------------------------------
void publishMQTT(const char* topic, const char* msg, bool retained = false) {
  if (mqtt.isConnected()) {
    mqtt.publish(topic, msg, retained);
  }
}

// Função para enviar logs para o tópico MQTT
void sendLogsMQTT(String logMessage) {
  String allLogs = "";
  for (int i = 0; i < 24; i++) {
    if (logs[i].length() > 0) {
      allLogs += logs[i] + "\n";
    }
  }
  allLogs += logMessage + "\n";
  if (allLogs.length() > 500) {
    allLogs = allLogs.substring(allLogs.length() - 500);  // limita o log
  }
  Serial.println("Enviando logs para MQTT...");
  publishMQTT(TOPIC_LOG, allLogs.c_str());
  // Atualiza a lista de logs
  for (int i = 0; i < 23; i++) {
    logs[i] = logs[i + 1];
  }
  logs[23] = logMessage;
}

// Controla o relé a partir do comando recebido por MQTT
void handleRelayCommand(String msg) {
  Serial.print("MQTT recebido [");
  Serial.print(TOPIC_RELAY_CMD);
  Serial.print("]: ");
  Serial.println(msg);

  if (msg == "0") {
    digitalWrite(relayPin, LOW);   // relé LIGADO
    mqtt.publish(TOPIC_RELAY_STATE, "0", true);   // publica estado retido
    sendLogsMQTT("Relé LIGADO (comando MQTT).");
  } else if (msg == "1") {
    digitalWrite(relayPin, HIGH);  // relé DESLIGADO
    mqtt.publish(TOPIC_RELAY_STATE, "1", true);
    sendLogsMQTT("Relé DESLIGADO (comando MQTT).");
  }
}

// Callback chamado quando chega comando no tópico venue/relay/cmd
void onRelayCommand(const char* payload, const size_t size) {
  String msg;
  for (size_t i = 0; i < size; i++) msg += payload[i];
  handleRelayCommand(msg);
}

// Conecta ao broker MQTT (handshake MQTT sobre o WebSocket já estabelecido)
bool connectMQTT() {
  if (!wsClient.isConnected()) {
    return false;   // WebSocket ainda não conectou
  }

  String clientId = "VENUE_" + String((uint32_t)ESP.getChipId(), HEX);

  bool ok = mqtt.connect(clientId.c_str(), mqttUser, mqttPass);
  if (ok) {
    mqttFailStreak = 0;
    wsFailStreak = 0;
    wsClient.setReconnectInterval(WS_RETRY_MIN_MS);
    Serial.println("MQTT conectado via WSS: " + String(mqttHost) + ":" + String(mqttPort));
    mqtt.subscribe(TOPIC_RELAY_CMD, onRelayCommand);
    mqtt.publish(TOPIC_STATUS, "online", true);
    mqtt.publish(TOPIC_IP, WiFi.localIP().toString().c_str(), true);
    mqtt.publish(TOPIC_RELAY_STATE, (digitalRead(relayPin) == LOW) ? "0" : "1", true);
  } else {
    mqttFailStreak++;
    // Credenciais/erro: desacelera também o WebSocket para não insistir
    unsigned long retryMs = WS_RETRY_MIN_MS * (1 << min<unsigned int>(mqttFailStreak, 3));
    if (retryMs > WS_RETRY_MAX_MS) retryMs = WS_RETRY_MAX_MS;
    wsClient.setReconnectInterval(retryMs);
    Serial.print("Falha MQTT em wss://");
    Serial.print(mqttHost);
    Serial.print(":");
    Serial.print(mqttPort);
    Serial.print("/ (lastError=");
    Serial.print(mqtt.getLastError());
    Serial.print(", returnCode=");
    Serial.print(mqtt.getReturnCode());
    Serial.println(")");
  }
  return ok;
}

// Atualiza o status de conexão
void updateConnectionStatus() {
  if (WiFi.status() == WL_CONNECTED && mqtt.isConnected()) {
    publishMQTT(TOPIC_STATUS, "online", true);
  } else {
    publishMQTT(TOPIC_STATUS, "offline", true);
  }
}

// ----------------------- Página web de config MQTT --------------------------
// A configuração MQTT é feita numa página separada (/mqtt) do portal.
// Assim os dados do broker NUNCA passam pelo arquivo de config do WiFi,
// e um erro de senha MQTT não interfere na conexão Wi-Fi.

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

void handleMQTTConfigPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Config MQTT</title></head><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;padding:20px'>";
  html += "<h2>Configuração do Broker MQTT</h2>";
  html += "<p>Salvo na EEPROM, separado do Wi-Fi.</p>";
  html += "<p style='color:#8fd3fe'>Conexão WSS (WebSocket Seguro). Path fixo: / &nbsp;•&nbsp; Porta típica: 443</p>";
  html += "<form method='POST' action='/mqttsave'>";
  html += "<p><label>Host/IP do broker:</label><br><input name='host' value='" + htmlEscape(String(mqttHost)) + "' style='width:100%;padding:8px'></p>";
  html += "<p><label>Porta:</label><br><input name='port' value='" + String(mqttPort) + "' style='width:100%;padding:8px'></p>";
  html += "<p><label>Usuário (opcional):</label><br><input name='user' value='" + htmlEscape(String(mqttUser)) + "' style='width:100%;padding:8px'></p>";
  html += "<p><label>Senha (opcional):</label><br><input type='password' name='pass' value='" + htmlEscape(String(mqttPass)) + "' style='width:100%;padding:8px'></p>";
  html += "<p><input type='submit' value='Salvar' style='padding:10px 20px;background:#4CAF50;color:#fff;border:none'></p>";
  html += "</form><p><a href='/' style='color:#4CAF50'>Voltar</a></p></body></html>";
  wifiManager.server->send(200, "text/html", html);
}

void handleMQTTSave() {
  String host = wifiManager.server->arg("host");
  String port = wifiManager.server->arg("port");
  String user = wifiManager.server->arg("user");
  String pass = wifiManager.server->arg("pass");

  host.trim();
  port.trim();
  user.trim();
  pass.trim();

  if (host.length() > 0) {
    strncpy(mqttHost, host.c_str(), sizeof(mqttHost) - 1);
    mqttHost[sizeof(mqttHost) - 1] = 0;
  }
  mqttPort = port.toInt();
  if (mqttPort <= 0 || mqttPort > 65535) mqttPort = MQTT_DEFAULT_PORT;
  strncpy(mqttUser, user.c_str(), sizeof(mqttUser) - 1);
  mqttUser[sizeof(mqttUser) - 1] = 0;
  strncpy(mqttPass, pass.c_str(), sizeof(mqttPass) - 1);
  mqttPass[sizeof(mqttPass) - 1] = 0;

  saveMQTTConfig();

  Serial.println("Config MQTT atualizada via página /mqtt. Reiniciando...");
  wifiManager.server->send(200, "text/html",
    "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;padding:20px'>"
    "<h2>Configuração salva!</h2><p>Reiniciando o dispositivo...</p></body></html>");
  delay(500);
  ESP.restart();
}

// Registra as rotas customizadas no portal web
void bindServerCallback() {
  wifiManager.server->on("/mqtt", HTTP_GET, handleMQTTConfigPage);
  wifiManager.server->on("/mqttsave", HTTP_POST, handleMQTTSave);
}

// ------------------------------- WiFi ---------------------------------------
// Conecta ao Wi-Fi com WiFiManager (portal "VENUE_Lock")
void connectWiFi() {
  wifiManager.setWebServerCallback(bindServerCallback);

  if (!wifiManager.autoConnect(apSSID, apPassword)) {
    Serial.println("Falha ao conectar, iniciando o portal de configuração...");
    wifiManager.startConfigPortal(apSSID, apPassword);
  } else {
    Serial.println("Conectado ao Wi-Fi!");
  }

  // Mantém o AP de configuração sempre aberto, com senha
  WiFi.softAP(apSSID, apPassword);
  wifiManager.startWebPortal();

  String ip = WiFi.localIP().toString();
  Serial.print("IP do ESP: ");
  Serial.println(ip);
  Serial.println("Portal de configuração disponível em http://192.168.4.1");
  Serial.println("Config MQTT em http://192.168.4.1/mqtt");
}

// ------------------------------- Utilidades ---------------------------------
// Envia mensagem de configuração e processa a comunicação por 2 segundos
void sendMessageMQTTAndProcess() {
  publishMQTT(TOPIC_LOG, "Entrando em modo de configuração...");
  publishMQTT(TOPIC_STATUS, "config_mode", true);

  unsigned long waitStart = millis();
  while (millis() - waitStart < 2000) {
    wsClient.loop();     // processa o WebSocket
    mqtt.update();       // processa o MQTT
    delay(50);
  }
}

// Verifica conectividade com a internet (conexão TCP com o Google)
bool pingGoogle() {
  WiFiClient client;
  return client.connect("www.google.com", 80);
}

// -------------------------------- SETUP -------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);   // relé desligado no boot
  pinMode(buttonPin, INPUT_PULLUP);

  EEPROM.begin(512);
  loadMQTTConfig();               // carrega a config do broker da EEPROM

  Serial.print("Config MQTT carregada: wss://");
  Serial.print(mqttHost);
  Serial.print(":");
  Serial.print(mqttPort);
  Serial.println("/");

  connectWiFi();                  // Wi-Fi + portal de configuração

  // Configura o MQTT sobre WebSocket Seguro (WSS)
  wsClient.beginSSL(mqttHost, mqttPort, MQTT_DEFAULT_PATH, (uint8_t*)NULL, "mqtt");
  wsClient.setReconnectInterval(WS_RETRY_MIN_MS);   // reconexão com backoff progressivo
  mqtt.begin(wsClient);
  // Last Will: se o dispositivo cair sem avisar, o broker publica "offline" (retido)
  mqtt.setWill(TOPIC_STATUS, "offline", true, 0);

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
  unsigned long currentMillis = millis();
  int currentDay = (currentMillis / 86400000) % 7;

  // Mantém o WebSocket vivo e reconecta com backoff progressivo
  if (WiFi.status() == WL_CONNECTED) {
    wsClient.loop();

    if (wsClient.isConnected()) {
      // Conectado: volta o intervalo do WebSocket ao mínimo (recuperação rápida)
      wsFailStreak = 0;
      wsClient.setReconnectInterval(WS_RETRY_MIN_MS);

      // WebSocket conectado, mas MQTT ainda não: tenta o handshake com backoff
      if (!mqtt.isConnected()) {
        unsigned long mqttRetry = WS_RETRY_MIN_MS * (1 << min<unsigned int>(mqttFailStreak, 3));
        if (mqttRetry > WS_RETRY_MAX_MS) mqttRetry = WS_RETRY_MAX_MS;
        if (currentMillis - lastMqttAttempt >= mqttRetry) {
          lastMqttAttempt = currentMillis;
          connectMQTT();
        }
      } else {
        mqtt.update();   // processa mensagens e mantém keep-alive
      }
    } else {
      // Desconectado: dobra o intervalo a cada 5 s de falha (5s→10s→20s→40s→60s máx)
      if (currentMillis - wsFailCheckMs >= 5000) {
        wsFailCheckMs = currentMillis;
        wsFailStreak++;
        unsigned long retryMs = WS_RETRY_MIN_MS * (1 << min<unsigned int>(wsFailStreak, 3));
        if (retryMs > WS_RETRY_MAX_MS) retryMs = WS_RETRY_MAX_MS;
        wsClient.setReconnectInterval(retryMs);
        Serial.print("Broker indisponível. Próxima tentativa em ");
        Serial.print(retryMs / 1000);
        Serial.println(" s");
      }
    }
  }
  wifiManager.process();   // mantém o portal de configuração ativo

  // Monitora o pressionamento do botão (4 s = modo de configuração)
  if (digitalRead(buttonPin) == LOW) {
    if (!buttonPressed) {
      buttonPressTime = millis();
      buttonPressed = true;
      Serial.println("Botão pressionado.");
    }
    if (millis() - buttonPressTime > 4000) {
      Serial.println("Botão pressionado por mais de 4 segundos. Entrando em modo de configuração.");

      sendMessageMQTTAndProcess();

      digitalWrite(relayPin, HIGH);   // desliga o relé ao entrar no modo AP

      delay(2000);

      wifiManager.resetSettings();    // limpa apenas o Wi-Fi salvo (config MQTT fica na EEPROM)
      Serial.println("Iniciando o portal de configuração...");
      wifiManager.startConfigPortal(apSSID, apPassword);

      // Após sair do portal, o WebSocket reconecta sozinho com a config atual
      wsClient.disconnect();
    }
  } else {
    buttonPressed = false;
  }

  // ---------- Desconectado do Wi-Fi ----------
  if (WiFi.status() != WL_CONNECTED) {
    if (isConnected) {
      isConnected = false;
      Serial.println("Desconectado da Internet.");
      dailyDisconnectedTime[currentDay] += 2;
      digitalWrite(relayPin, HIGH);   // relé desligado (estado seguro)

      // Salva os dados diários na EEPROM
      for (int i = 0; i < 7; i++) {
        EEPROM.write(4 + i, dailyConnectedTime[i]);
        EEPROM.write(11 + i, dailyDisconnectedTime[i]);
      }
      EEPROM.commit();

      sendLogsMQTT("Falha na conexão com a internet.");
    }

    // Tenta reconectar periodicamente (a cada 10 s, sem travar o loop)
    if (currentMillis - lastReconnectAttempt >= 10000) {
      lastReconnectAttempt = currentMillis;
      Serial.println("Tentando reconectar ao Wi-Fi...");
      WiFi.reconnect();
      unsigned long startReconnect = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - startReconnect < 8000) {
        delay(100);
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Conectado ao Wi-Fi.");
        sendLogsMQTT("Conectado ao Wi-Fi.");
      }
    }
  } else {
    // ---------- Conectado ao Wi-Fi ----------
    if (!isConnected) {
      isConnected = true;
      Serial.println("Conexão com a Internet estabelecida.");
      sendLogsMQTT("Conexão com a Internet estabelecida.");
      connectedTime = 0;
      digitalWrite(relayPin, LOW);    // relé ligado
      connectMQTT();
    }

    // Verifica a conexão com o Google (internet)
    if (currentMillis - previousPingMillis >= pingInterval) {
      previousPingMillis = currentMillis;
      if (!pingGoogle()) {
        Serial.println("Falha ao acessar o Google.");
        sendLogsMQTT("Falha ao acessar o Google.");
        digitalWrite(relayPin, HIGH);   // relé desligado
      } else {
        Serial.println("Conexão com a Internet OK...");
        sendLogsMQTT("Conexão com a Internet OK...");
        digitalWrite(relayPin, LOW);    // relé ligado
      }
    }

    // Verifica a conexão com o broker MQTT (log só na mudança de estado)
    if (mqtt.isConnected() != lastMqttState) {
      lastMqttState = mqtt.isConnected();
      if (lastMqttState) {
        Serial.println("Conectado ao broker MQTT!");
      } else {
        Serial.println("Desconectado do broker MQTT!");
      }
    }

    // Atualiza os tempos de conexão a cada 2 minutos
    if (currentMillis - previousMillis >= intervalConnection) {
      previousMillis = currentMillis;
      connectedTime += 2;
      dailyConnectedTime[currentDay] += 2;

      for (int i = 0; i < 7; i++) {
        EEPROM.write(4 + i, dailyConnectedTime[i]);
        EEPROM.write(11 + i, dailyDisconnectedTime[i]);
      }
      EEPROM.commit();
    }

    // Envia a telemetria a cada 15 segundos
    if (currentMillis - previousSensorMillis >= intervalSensors) {
      previousSensorMillis = currentMillis;

      publishMQTT(TOPIC_TIME_CONNECTED, String(connectedTime).c_str());
      publishMQTT(TOPIC_RSSI, String(WiFi.RSSI()).c_str());

      char buf[16];
      dtostrf(analogRead(A0) * (3.3 / 1024.0), 1, 2, buf);   // voltagem A0
      publishMQTT(TOPIC_VOLTAGE, buf);

      float chipTemp = (analogRead(A0) / 1024.0) * 100;       // (estimativa)
      dtostrf(chipTemp, 1, 2, buf);
      publishMQTT(TOPIC_TEMPERATURE, buf);
      Serial.print("Temperatura do Chip: ");
      Serial.println(chipTemp);

      publishMQTT(TOPIC_FREE_HEAP, String(ESP.getFreeHeap()).c_str());
      Serial.print("Memória Livre: ");
      Serial.println(ESP.getFreeHeap());

      publishMQTT(TOPIC_SDK_VERSION, ESP.getSdkVersion());
      Serial.print("Versão do Firmware: ");
      Serial.println(ESP.getSdkVersion());
    }

    // Disponibilidade (últimos 7 dias)
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
      publishMQTT(TOPIC_AVAILABILITY, buf);
      dtostrf(unavailability, 1, 2, buf);
      publishMQTT(TOPIC_UNAVAILABILITY, buf);
    }
  }
}
