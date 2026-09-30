#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <MQTTPubSubClient.h>
#include <ArduinoJson.h>

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID = "Jeronimo 7G";
const char* WIFI_PASSWORD = "@C6m8n4d2d3";


// =====================================================
// MQTT
// =====================================================

const char* MQTT_HOST = "mosquito.omny.tec.br";

// WSS normalmente usa 443
const uint16_t MQTT_PORT = 443;

// Caminho WebSocket.
// Mosquitto frequentemente usa /mqtt.
// Se não conectar, pode ser necessário alterar este valor.
const char* MQTT_PATH = "/mqtt";

const char* MQTT_USER = "cardoso";
const char* MQTT_PASSWORD = "C6m8n4d2d3";

const char* MQTT_TOPIC = "ada";


// =====================================================
// LED
// =====================================================

// LED interno do ESP8266.
// Na maioria dos ESP8266/NodeMCU é GPIO2.
#define LED_PIN LED_BUILTIN


// =====================================================
// CLIENTES
// =====================================================

WebSocketsClient websocket;
MQTTPubSubClient mqtt;


// =====================================================
// CALLBACK MQTT
// =====================================================

void receberMensagem(const String& payload, const size_t size) {

  Serial.println();
  Serial.println("Mensagem MQTT recebida:");

  Serial.println(payload);

  JsonDocument doc;

  DeserializationError error =
      deserializeJson(doc, payload);

  if (error) {

    Serial.print("Erro ao ler JSON: ");
    Serial.println(error.c_str());

    return;
  }


  // ===================================================
  // LER STATUS
  // ===================================================

  int status = doc["status"] | 0;

  Serial.print("Status recebido: ");
  Serial.println(status);


  // ===================================================
  // CONTROLAR LED
  // ===================================================

  // IMPORTANTE:
  // LED_BUILTIN do ESP8266 normalmente é ACTIVE LOW.
  //
  // LOW  = LED ligado
  // HIGH = LED desligado

  if (status == 1) {

    digitalWrite(LED_PIN, LOW);

    Serial.println("LED LIGADO");

  } else {

    digitalWrite(LED_PIN, HIGH);

    Serial.println("LED DESLIGADO");
  }
}


// =====================================================
// CONECTAR WIFI
// =====================================================

void conectarWiFi() {

  Serial.println();
  Serial.print("Conectando ao WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);

  WiFi.begin(
      WIFI_SSID,
      WIFI_PASSWORD
  );


  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");
  }


  Serial.println();
  Serial.println("WiFi conectado!");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}


// =====================================================
// CONECTAR WEBSOCKET
// =====================================================

void conectarWebSocket() {

  Serial.println();
  Serial.println("Configurando WebSocket...");


  // ===================================================
  // WSS
  // ===================================================

  websocket.beginSSL(
      MQTT_HOST,
      MQTT_PORT,
      MQTT_PATH
  );


  // ===================================================
  // SUBPROTOCOLO MQTT
  // ===================================================

  websocket.setExtraHeaders(
      "Sec-WebSocket-Protocol: mqtt"
  );


  // Reconecta automaticamente
  websocket.setReconnectInterval(5000);


  Serial.println("WebSocket configurado.");
}


// =====================================================
// CONECTAR MQTT
// =====================================================

void conectarMQTT() {

  Serial.println("Conectando MQTT...");


  // ===================================================
  // CONECTA MQTT AO WEBSOCKET
  // ===================================================

  mqtt.begin(websocket);


  // ===================================================
  // CONECTA AO BROKER
  // ===================================================

  while (!mqtt.connect(
      "ESP8266-ADA",
      MQTT_USER,
      MQTT_PASSWORD
  )) {

    Serial.println("Falha MQTT.");

    Serial.println("Tentando novamente...");

    websocket.loop();

    delay(2000);
  }


  Serial.println();
  Serial.println("MQTT CONECTADO!");

  Serial.print("Broker: ");
  Serial.println(MQTT_HOST);

  Serial.print("Topico: ");
  Serial.println(MQTT_TOPIC);


  // ===================================================
  // SUBSCRIBE
  // ===================================================

  mqtt.subscribe(
      MQTT_TOPIC,
      receberMensagem
  );


  Serial.println("Inscrito no topico ADA!");
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);


  Serial.println();
  Serial.println("=============================");
  Serial.println("ESP8266 MQTT WSS");
  Serial.println("=============================");


  // ===================================================
  // LED
  // ===================================================

  pinMode(LED_PIN, OUTPUT);

  // começa desligado
  digitalWrite(LED_PIN, HIGH);


  // ===================================================
  // WIFI
  // ===================================================

  conectarWiFi();


  // ===================================================
  // WEBSOCKET
  // ===================================================

  conectarWebSocket();


  // ===================================================
  // MQTT
  // ===================================================

  conectarMQTT();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  websocket.loop();

  mqtt.update();


  // ===================================================
  // RECONECTAR WIFI
  // ===================================================

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi desconectado!");

    conectarWiFi();
  }


  // ===================================================
  // RECONECTAR MQTT
  // ===================================================

  if (!mqtt.isConnected()) {

    Serial.println("MQTT desconectado!");

    conectarMQTT();
  }
}