// Defina as credenciais Blynk conforme o seu painel:
#define BLYNK_TEMPLATE_ID "TMPL2tqHGUugZ"  // Seu Template ID no painel do Blynk
#define BLYNK_TEMPLATE_NAME "VENUE Sistema" // Nome do seu template no painel do Blynk
#define BLYNK_AUTH_TOKEN "spSbU0xw8Jm1SsvecRbJdfFQEskyetaj"  // Seu Token de autenticação do projeto Blynk

// Bibliotecas
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiManager.h>  // Biblioteca para Wi-Fi Manager
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// Pino do relé e botão
int relayPin = 0;  // GPIO0 (D3)
int buttonPin = 2; // GPIO2 (Botão)

// Definindo tempos de conexão/desconexão
unsigned long connectedTime = 0;
unsigned long disconnectedTime = 0;
unsigned long dailyConnectedTime[7] = {0};
unsigned long dailyDisconnectedTime[7] = {0};
unsigned long previousMillis = 0;
unsigned long previousSensorMillis = 0;
unsigned long previousPingMillis = 0;
unsigned long buttonPressTime = 0;  // Tempo de pressionamento do botão
bool buttonPressed = false;         // Estado do botão

// Intervalos para diferentes ações
const unsigned long intervalConnection = 120000;  // 2 minutos para atualizar os tempos de conexão
const unsigned long intervalSensors = 15000;      // 15 segundos para enviar dados dos sensores
const unsigned long pingInterval = 20000;         // 20 segundos para verificar a conexão com o Google

// Inicialização do NTPClient para sincronização de tempo
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // Atualiza a cada 60 segundos

// Logs de falhas
String logs[24];  // Armazena logs das últimas 24h

// Inicializando variáveis para monitoramento de falhas
unsigned long lastReconnectAttempt = 0;
bool isConnected = false;

// Função de controle do relé a partir do Blynk
BLYNK_WRITE(V0) {
  int pinValue = param.asInt();
  if (pinValue == 0) {
    digitalWrite(relayPin, LOW);  // Ativa o relé (LOW para ON)
  }
  if (pinValue == 1) {
    digitalWrite(relayPin, HIGH);  // Desativa o relé (HIGH para OFF)
  }
}

// Função para enviar logs para o Blynk
void sendLogsToBlynk(String logMessage) {
  String allLogs = "";
  for (int i = 0; i < 24; i++) {
    if (logs[i].length() > 0) {
      allLogs += logs[i] + "\n";
    }
  }

  // Adiciona o novo log
  allLogs += logMessage + "\n";
  // Limita os logs às últimas 24h
  if (allLogs.length() > 500) {
    allLogs = allLogs.substring(allLogs.length() - 500);  // Limita os logs para evitar excesso
  }

  // Envia os logs para o widget Terminal no Blynk
  Serial.println("Enviando logs para o Blynk...");
  Blynk.virtualWrite(V10, allLogs.c_str());
  // Atualiza a lista de logs
  for (int i = 0; i < 23; i++) {
    logs[i] = logs[i + 1];  // Desloca os logs
  }
  logs[23] = logMessage;  // Armazena o novo log na posição mais recente
}

// Função para atualizar o status de conexão
void updateConnectionStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.virtualWrite(V4, "Conectado");
  } else {
    Blynk.virtualWrite(V4, "Desconectado");
  }
}

// Função para conectar ao Wi-Fi com o WiFiManager
void connectWiFi() {
  WiFiManager wifiManager;

  // Tenta conectar-se automaticamente com o Wi-Fi armazenado ou abre o portal de configuração
  if (!wifiManager.autoConnect("VENUE_Lock")) {
    Serial.println("Falha ao conectar, iniciando o portal de configuração...");
    wifiManager.startConfigPortal("VENUE_Lock");
  } else {
    Serial.println("Conectado ao Wi-Fi!");
  }

  // Imprime o IP atribuído
  String ip = WiFi.localIP().toString();
  Serial.print("IP do ESP: ");
  Serial.println(ip);
  Blynk.virtualWrite(V6, ip);  // Envia o IP para o painel Blynk
}

// Função para enviar uma mensagem ao monitor do Blynk e garantir que seja processada
void sendMessageToBlynkAndProcess() {
  // Envia mensagem informando que o sistema entrará em modo de configuração
  Blynk.virtualWrite(V10, "Entrando em modo de configuração...");

  // Processa imediatamente para garantir que a mensagem seja enviada
  unsigned long messageWaitTime = millis();
  while (millis() - messageWaitTime < 2000) {
    Blynk.run();  // Processa a comunicação com o Blynk
    delay(50);    // Pequeno delay para garantir que o Blynk tenha tempo de processar
  }
}

// Função para verificar a conectividade com o Google
bool pingGoogle() {
  WiFiClient client;
  return client.connect("www.google.com", 80);  // Tenta conectar ao Google na porta 80 (HTTP)
}

void setup() {
  Serial.begin(115200);

  // Configura o pino do relé e do botão
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH);  // Desativa o relé inicialmente (HIGH para OFF)
  pinMode(buttonPin, INPUT_PULLUP);  // Configura o botão como entrada com pull-up interno

  // Conecta ao Wi-Fi utilizando o WiFiManager
  connectWiFi();

  // Inicia o Blynk
  Blynk.begin(BLYNK_AUTH_TOKEN, WiFi.SSID().c_str(), WiFi.psk().c_str());
  delay(1000);  // Aguarda 1 segundo para a inicialização

  // Sincroniza com o NTP
  timeClient.begin();
  timeClient.update();  // Sincroniza imediatamente

  // Configura EEPROM para logs
  EEPROM.begin(512);
  for (int i = 0; i < 7; i++) {
    dailyConnectedTime[i] = EEPROM.read(4 + i);
    dailyDisconnectedTime[i] = EEPROM.read(11 + i);
  }

  sendLogsToBlynk("Sistema iniciado com sucesso.");
  updateConnectionStatus();  // Chama a função para atualizar o status de conexão
}

void loop() {
  Blynk.run();  // Executa o loop do Blynk
  unsigned long currentMillis = millis();
  int currentDay = (currentMillis / 86400000) % 7;

  // Monitorar pressionamento do botão
  if (digitalRead(buttonPin) == LOW) {  // Se o botão estiver pressionado
    if (!buttonPressed) {  // Se é a primeira vez que detecta o pressionamento
      buttonPressTime = millis();  // Armazena o tempo em que o botão foi pressionado
      buttonPressed = true;
      Serial.println("Botão pressionado.");
    }

    // Verifica se o botão foi pressionado por mais de 4 segundos
    if (millis() - buttonPressTime > 4000) {  // 4000ms = 4 segundos
      Serial.println("Botão pressionado por mais de 4 segundos. Entrando em modo de configuração.");

      // Envia a mensagem ao Blynk imediatamente
      sendMessageToBlynkAndProcess();  // Envia a mensagem e aguarda 2 segundos para garantir que ela foi processada

      // Desliga o relé ao entrar em modo AP
      digitalWrite(relayPin, HIGH);  // Desativa o relé (HIGH para OFF)

      // Adiciona um pequeno delay antes de entrar no modo AP
      delay(2000);  // 2 segundos de delay antes de iniciar o portal de configuração

      // Reseta configurações Wi-Fi e entra no modo AP
      WiFiManager wifiManager;
      wifiManager.resetSettings();  // Reseta as configurações salvas

      // Inicia o portal de configuração fallback
      Serial.println("Iniciando o portal de configuração...");
      wifiManager.startConfigPortal("VENUE_Lock");
    }
  } else {
    buttonPressed = false;  // Reseta o estado do botão quando não está pressionado
  }

  // Verifica se está conectado ao Wi-Fi
  if (WiFi.status() != WL_CONNECTED) {
    if (isConnected) {
      isConnected = false;
      Serial.println("Desconectado da Internet.");
      dailyDisconnectedTime[currentDay] += 2;  // Incrementa o tempo de desconexão diário
      disconnectedTime += 2;  // Incrementa o tempo de desconexão
      digitalWrite(relayPin, HIGH);  // Ativa o relé (HIGH para OFF)

      // Salva os dados diários na EEPROM
      for (int i = 0; i < 7; i++) {
        EEPROM.write(4 + i, dailyConnectedTime[i]);
        EEPROM.write(11 + i, dailyDisconnectedTime[i]);
      }
      EEPROM.commit();

      // Envia log de falha para o Blynk
      sendLogsToBlynk("Falha na conexão com a internet.");
    }

    // Tenta reconectar ao Wi-Fi
    Serial.println("Tentando reconectar ao Wi-Fi...");
    sendLogsToBlynk("Tentando reconectar ao Wi-Fi...");
    WiFi.reconnect();  // Tenta reconectar ao Wi-Fi
    unsigned long startReconnect = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startReconnect < 10000) {
      delay(500);
      Serial.print("...");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Conectado ao Wi-Fi.");
      sendLogsToBlynk("Conectado ao Wi-Fi.");
      Blynk.begin(BLYNK_AUTH_TOKEN, WiFi.SSID().c_str(), WiFi.psk().c_str());  // Reestabelece a conexão com o Blynk
      delay(1000);  // Aguardar a inicialização do Blynk
    }
  } else {
    if (!isConnected) {
      isConnected = true;
      Serial.println("Conexão com a Internet estabelecida.");
      sendLogsToBlynk("Conexão com a Internet estabelecida.");
      connectedTime = 0;
      digitalWrite(relayPin, LOW);  // Desativa o relé (LOW para ON)
    }

    // Verifica a conexão com o Google
    if (currentMillis - previousPingMillis >= pingInterval) {
      previousPingMillis = currentMillis;
      if (!pingGoogle()) {
        Serial.println("Falha ao acessar o Google.");
        sendLogsToBlynk("Falha ao acessar o Google.");
        digitalWrite(relayPin, HIGH);  // Ativa o relé (HIGH para OFF)
      } else {
        Serial.println("Conexão com a Internet OK...");
        sendLogsToBlynk("Conexão com a Internet OK...");
        digitalWrite(relayPin, LOW);  // Desativa o relé (LOW para ON)
      }
    }

    // Verificação da conexão com o servidor Blynk
    if (Blynk.connected()) {
      Serial.println("Conectado ao servidor Blynk!");
    } else {
      Serial.println("Desconectado do servidor Blynk!");
    }

    // Atualiza os dados de conexão a cada 2 minutos
    if (currentMillis - previousMillis >= intervalConnection) {
      previousMillis = currentMillis;
      connectedTime += 2;
      dailyConnectedTime[currentDay] += 2;

      // Salva os dados diários na EEPROM
      for (int i = 0; i < 7; i++) {
        EEPROM.write(4 + i, dailyConnectedTime[i]);
        EEPROM.write(11 + i, dailyDisconnectedTime[i]);
      }
      EEPROM.commit();
    }

    // Atualiza os dados do sinal, voltagem, memória e temperatura a cada 15 segundos
    if (currentMillis - previousSensorMillis >= intervalSensors) {
      previousSensorMillis = currentMillis;

      // Envio de dados para o Blynk
      Blynk.virtualWrite(V1, connectedTime);
      Blynk.virtualWrite(V2, WiFi.RSSI());
      Blynk.virtualWrite(V3, analogRead(A0) * (3.3 / 1024.0));  // Voltagem do sensor

      // Temperatura do Chip
      float chipTemp = (analogRead(A0) / 1024.0) * 100;
      Blynk.virtualWrite(V7, chipTemp);
      Serial.print("Temperatura do Chip: ");
      Serial.println(chipTemp);

      // Uso de Memória
      Blynk.virtualWrite(V8, ESP.getFreeHeap());
      Serial.print("Memória Livre: ");
      Serial.println(ESP.getFreeHeap());

      // Versão do Firmware
      Blynk.virtualWrite(V9, ESP.getSdkVersion());
      Serial.print("Versão do Firmware: ");
      Serial.println(ESP.getSdkVersion());
    }

    // Verificação de Disponibilidade
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
      Blynk.virtualWrite(V4, availability);
      Blynk.virtualWrite(V5, unavailability);
    }
  }
}
