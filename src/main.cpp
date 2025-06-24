#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SD.h>
#include <time.h>

// Configuración WiFi
const char* ssid = "xxxx";
const char* password = "xxxx";

// Configuración Telegram
#define BOT_TOKEN "xxxx"
#define CHAT_ID "xxxx"

// Configuración NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600; // +1 hora (CEST)
const int daylightOffset_sec = 3600; // Horario de verano

// Pines (lado izquierdo ESP32)
const int DOOR_SENSOR_PIN = 23;     // Sensor magnético puerta
const int RELAY_PIN = 4;            // Relé
const int STATUS_LED = 2;           // LED integrado
const int RGB_LED_PIN = 21;         // LED RGB WS2812
const int NUM_LEDS = 1;
const int RFID_SS_PIN = 15;         // RFID RC522 SS (SDA)
const int RFID_RST_PIN = 27;        // RFID RC522 RST
const int SD_CS_PIN = 16;           // Lector SD CS

// Pines SPI para RFID (VSPI)
#define RFID_SCK_PIN 5
#define RFID_MISO_PIN 19
#define RFID_MOSI_PIN 18

// Pines SPI para SD (HSPI)
#define SD_SCK_PIN 14
#define SD_MISO_PIN 12
#define SD_MOSI_PIN 13

// Configuración NeoPixel
Adafruit_NeoPixel strip(NUM_LEDS, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

// Configuración Telegram
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// Configuración RFID
MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);

// Configuración SD
#define SD_FILE "/access_log.txt"
#define USER_FILE "/users.txt"
String accessHistory[15]; // Máximo 15 registros en memoria
int historyCount = 0;

// Estructura para usuarios
struct User {
  String name;
  String pin;
  String uid;

  bool requiresPin() { return pin.length() == 4; }
  bool requiresRFID() { return uid.length() > 0; }
};

User authorizedUsers[10]; // Máximo 10 usuarios
int numAuthorizedUsers = 0;

// Variables del sensor
bool doorOpen = false;
bool relayState = false;

// Temporizador del relé
unsigned long relayTimerEnd = 0;

// Estado del LED
enum LEDState { RED, GREEN, YELLOW, BLINKING_RED };
LEDState currentLEDState = RED;

// Variables para el parpadeo
unsigned long previousMillis = 0;
const long blinkInterval = 500; // Intervalo de parpadeo en ms

// Variables para parpadeo del LED integrado
unsigned long lastBlink = 0;
int blinkCount = 0;
bool ledState = false;
int targetBlinks = 0;

// Contraseña para la lista de usuarios
const String ADMIN_PASSWORD = "admin";
AsyncWebServer server(80);

// Variables para alta de usuarios
bool waitingForRFID = false;
String tempName, tempPin, tempUID;
unsigned long rfidTimeout = 0;
const unsigned long RFID_TIMEOUT_MS = 30000; // 30 segundos

// Variables para manejo de Telegram
enum TelegramState { IDLE, WAITING_FOR_NAME, WAITING_FOR_PIN };
TelegramState telegramState = IDLE;
String telegramUserName;
String telegramChatId;
unsigned long telegramTimeout = 0;
const unsigned long TELEGRAM_TIMEOUT_MS = 60000; // 1 minuto para responder

// Prototipos de funciones
void setLEDColor(uint32_t color);
void initSDCard();
void loadUsers();
void loadAccessHistory();
void blinkLED(int times);
void sendTelegramNotification(const String& message, const String& chatId = CHAT_ID);
void handleTelegramMessages();
void checkDoorStatus();
void checkRelayTimer();
void checkRFID();
void updateRGBStatus();
String getCurrentTime();
void logAccess(const String& method, const String& id, const String& status, const String& userName = "N/A");
void handleRoot(AsyncWebServerRequest *request);
void handleSetTimer(AsyncWebServerRequest *request);
void handleAddUser(AsyncWebServerRequest *request);
void handleAddUserPost(AsyncWebServerRequest *request);
void handleEditUserGet(AsyncWebServerRequest *request);
void handleEditUserPost(AsyncWebServerRequest *request);
void handleDeleteUser(AsyncWebServerRequest *request);
void handleEnterPin(AsyncWebServerRequest *request);
void handleEnterPinPost(AsyncWebServerRequest *request);
void handleUsers(AsyncWebServerRequest *request);
void handleUsersPost(AsyncWebServerRequest *request);
void configureSPIPins(int sck, int miso, int mosi, int cs);

void setup() {
  Serial.begin(115200);

  // Configura pines
  pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // Inicializa NeoPixel
  strip.begin();
  strip.setBrightness(100);
  setLEDColor(strip.Color(255, 0, 0)); // Rojo
  strip.show();
  Serial.println("[SISTEMA] Estado LED: Rojo (Inicializando)");
  delay(300);

  // Inicializa SPI para RFID
  configureSPIPins(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  rfid.PCD_Init();
  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  Serial.println("[RFID] Lector inicializado. Versión: 0x" + String(version, HEX));
  if (version == 0x00 || version == 0xFF) {
    Serial.println("[ERROR] No se detectó el lector RFID. Verifica las conexiones.");
  } else {
    Serial.println("[RFID] Esperando tarjetas...");
  }

  // Inicializa SPI para SD
  configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  initSDCard();

  // Carga usuarios y historial desde SD
  loadUsers();
  loadAccessHistory();

  // Conecta WiFi
  WiFi.begin(ssid, password);
  Serial.print("[WIFI] Conectando a ");
  Serial.print(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    blinkLED(2);
  }
  Serial.print("\n[WIFI] Conectado! IP: ");
  Serial.println(WiFi.localIP());

  // Sincroniza hora con NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("[NTP] Hora sincronizada con servidor NTP");

  // Configura cliente seguro para Telegram
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  sendTelegramNotification("[BOT] Sistema de control de acceso iniciado");

  // Configura rutas del servidor web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/setTimer", HTTP_GET, handleSetTimer);
  server.on("/addUser", HTTP_GET, handleAddUser);
  server.on("/addUser", HTTP_POST, handleAddUserPost);
  server.on("/enterPin", HTTP_GET, handleEnterPin);
  server.on("/enterPin", HTTP_POST, handleEnterPinPost);
  server.on("/users", HTTP_GET, handleUsers);
  server.on("/users", HTTP_POST, handleUsersPost);
  server.on("/editUser", HTTP_GET, handleEditUserGet);
  server.on("/editUser", HTTP_POST, handleEditUserPost);
  server.on("/deleteUser", HTTP_GET, handleDeleteUser);
  server.begin();
  Serial.println("[WEB] Servidor iniciado");
}

void loop() {
  unsigned long currentMillis = millis();
  static unsigned long lastLoop = 0;
  const long LOOP_INTERVAL = 50;
  static unsigned long lastTelegramCheck = 0;
  const long TELEGRAM_CHECK_INTERVAL = 1000; // Revisar mensajes cada 1 segundo

  if (currentMillis - lastLoop >= LOOP_INTERVAL) {
    checkDoorStatus();
    checkRelayTimer();
    checkRFID();
    updateRGBStatus();
    strip.show();
    lastLoop = currentMillis;
  }

  if (currentMillis - lastTelegramCheck >= TELEGRAM_CHECK_INTERVAL) {
    handleTelegramMessages();
    lastTelegramCheck = currentMillis;
  }

  if (telegramState != IDLE && currentMillis > telegramTimeout) {
    telegramState = IDLE;
    sendTelegramNotification("Tiempo de espera agotado. Por favor, intenta de nuevo con /abrir.", telegramChatId);
    Serial.println("[TELEGRAM] Tiempo de espera para acceso expirado");
  }

  if (targetBlinks > 0 && currentMillis - lastBlink >= 150) {
    ledState = !ledState;
    digitalWrite(STATUS_LED, ledState ? HIGH : LOW);
    lastBlink = currentMillis;
    blinkCount++;
    if (blinkCount >= targetBlinks * 2) {
      blinkCount = 0;
      targetBlinks = 0;
      ledState = false;
      digitalWrite(STATUS_LED, LOW);
    }
  }
}

// === FUNCIONES PRINCIPALES ===

void configureSPIPins(int sck, int miso, int mosi, int cs) {
  SPI.end();
  SPI.begin(sck, miso, mosi, cs);
}

void setLEDColor(uint32_t color) {
  strip.setPixelColor(0, color);
}

void initSDCard() {
  configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[SD] Error al inicializar tarjeta SD");
    return;
  }
  Serial.println("[SD] Tarjeta SD inicializada correctamente");

  if (!SD.exists(SD_FILE)) {
    File file = SD.open(SD_FILE, FILE_WRITE);
    if (file) {
      file.println("Fecha y Hora,Método,ID,Usuario,Estado");
      file.close();
      Serial.println("[SD] Archivo de log creado");
    }
  }

  if (!SD.exists(USER_FILE)) {
    File file = SD.open(USER_FILE, FILE_WRITE);
    if (file) {
      file.println("Nombre,PIN,UID");
      file.close();
      Serial.println("[SD] Archivo de usuarios creado");
    }
  }
}

void loadUsers() {
  configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  File file = SD.open(USER_FILE, FILE_READ);
  if (!file) {
    Serial.println("[SD] Error al abrir archivo de usuarios");
    return;
  }

  if (file.available()) file.readStringUntil('\n');

  while (file.available() && numAuthorizedUsers < 10) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      int comma1 = line.indexOf(',');
      int comma2 = line.indexOf(',', comma1 + 1);
      String name = line.substring(0, comma1);
      String pin = line.substring(comma1 + 1, comma2);
      String uid = line.substring(comma2 + 1);
      authorizedUsers[numAuthorizedUsers++] = {name, pin, uid};
    }
  }
  file.close();
  Serial.println("[SD] Usuarios cargados (" + String(numAuthorizedUsers) + " usuarios)");
}

void loadAccessHistory() {
  configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  File file = SD.open(SD_FILE, FILE_READ);
  if (!file) {
    Serial.println("[SD] Error al abrir archivo de historial");
    return;
  }

  if (file.available()) file.readStringUntil('\n'); // Skip header

  // Store lines in a temporary array to get the last 15
  String tempHistory[15];
  int tempCount = 0;

  while (file.available() && tempCount < 15) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      tempHistory[tempCount] = line;
      tempCount++;
    }
  }

  // If more lines are available, keep reading to get the last 15
  if (file.available()) {
    while (file.available()) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        // Shift array and add new line
        for (int i = 0; i < 14; i++) {
          tempHistory[i] = tempHistory[i + 1];
        }
        tempHistory[14] = line;
      }
    }
  }

  // Copy to accessHistory
  historyCount = min(tempCount, 15);
  for (int i = 0; i < historyCount; i++) {
    accessHistory[i] = tempHistory[i];
  }

  file.close();
  Serial.println("[SD] Historial cargado (" + String(historyCount) + " registros)");
}

String getTagUID() {
  String tagUID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    tagUID += (rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    tagUID += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) tagUID += " ";
  }
  tagUID.toUpperCase();
  return tagUID;
}

void addUser(const String& name, const String& pin, const String& uid) {
  if (numAuthorizedUsers >= 10) {
    Serial.println("[USER] Error: Límite de usuarios alcanzado");
    return;
  }

  if (pin.length() != 4 && uid.length() == 0) {
    Serial.println("[USER] Error: Se debe proporcionar al menos un PIN o un UID");
    return;
  }

  authorizedUsers[numAuthorizedUsers++] = {name, pin, uid};
  configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  File file = SD.open(USER_FILE, FILE_APPEND);
  if (file) {
    file.println(name + "," + pin + "," + uid);
    file.close();
    Serial.println("[USER] Usuario añadido: " + name + ", PIN: " + pin + ", UID: " + uid);
  } else {
    Serial.println("[SD] Error al escribir en archivo de usuarios");
  }
}

void deleteUser(int index) {
  if (index >= 0 && index < numAuthorizedUsers) {
    for (int i = index; i < numAuthorizedUsers - 1; i++) {
      authorizedUsers[i] = authorizedUsers[i + 1];
    }
    numAuthorizedUsers--;
    configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    File file = SD.open(USER_FILE, FILE_WRITE);
    if (file) {
      file.println("Nombre,PIN,UID");
      for (int i = 0; i < numAuthorizedUsers; i++) {
        file.println(authorizedUsers[i].name + "," + authorizedUsers[i].pin + "," + authorizedUsers[i].uid);
      }
      file.close();
    }
  }
}

void updateUser(int index, const String& name, const String& pin, const String& uid) {
  if (index >= 0 && index < numAuthorizedUsers) {
    authorizedUsers[index] = {name, pin, uid};
    configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    File file = SD.open(USER_FILE, FILE_WRITE);
    if (file) {
      file.println("Nombre,PIN,UID");
      for (int i = 0; i < numAuthorizedUsers; i++) {
        file.println(authorizedUsers[i].name + "," + authorizedUsers[i].pin + "," + authorizedUsers[i].uid);
      }
      file.close();
    }
  }
}

void checkRFID() {
  configureSPIPins(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String tagUID = getTagUID();
    Serial.println("[RFID] Tarjeta detectada - UID: " + tagUID);
    String userName = "N/A";
    bool authorized = false;
    for (int i = 0; i < numAuthorizedUsers; i++) {
      if (tagUID == authorizedUsers[i].uid) {
        authorized = true;
        userName = authorizedUsers[i].name;
        break;
      }
    }

    if (waitingForRFID) {
      if (millis() > rfidTimeout) {
        waitingForRFID = false;
        Serial.println("[RFID] Tiempo de espera para escaneo RFID expirado");
      } else {
        tempUID = tagUID;
        addUser(tempName, tempPin, tempUID);
        waitingForRFID = false;
        sendTelegramNotification("[USER] Nuevo usuario añadido: " + tempName + " (UID: " + tagUID + ")");
      }
    } else if (authorized) {
      relayState = true;
      digitalWrite(RELAY_PIN, HIGH);
      relayTimerEnd = millis() + 10000;
      logAccess("RFID", tagUID, "Acceso concedido", userName);
      sendTelegramNotification("[ACCESO] Concedido por RFID: " + tagUID + " (" + userName + ")");
    } else {
      logAccess("RFID", tagUID, "Acceso denegado", userName);
      sendTelegramNotification("[ACCESO] Denegado por RFID: " + tagUID);
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    delay(1000);
  }
}

void sendTelegramNotification(const String& message, const String& chatId) {
  if (bot.sendMessage(chatId, message, "Markdown")) {
    Serial.println("[TELEGRAM] Notificación enviada: " + message);
  } else {
    Serial.println("[TELEGRAM] Error al enviar notificación a chat: " + chatId);
  }
}

void handleTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;

    // Handle /ip command
    if (text == "/ip" && chat_id == CHAT_ID) {
      String ip = WiFi.localIP().toString();
      sendTelegramNotification("Dirección IP del ESP32: *" + ip + "*", chat_id);
      Serial.println("[TELEGRAM] Solicitud de IP enviada: " + ip);
      continue;
    }

    // Handle /abrir command
    if (telegramState == IDLE && text == "/abrir" && chat_id == CHAT_ID) {
      telegramState = WAITING_FOR_NAME;
      telegramChatId = chat_id;
      telegramTimeout = millis() + TELEGRAM_TIMEOUT_MS;
      sendTelegramNotification("Por favor, ingresa el nombre de usuario.", chat_id);
      Serial.println("[TELEGRAM] Solicitud de apertura recibida, esperando nombre");
    } else if (telegramState == WAITING_FOR_NAME && chat_id == telegramChatId) {
      telegramUserName = text;
      bool userFound = false;
      bool hasPin = false;
      int userIndex = -1;

      for (int j = 0; j < numAuthorizedUsers; j++) {
        if (telegramUserName == authorizedUsers[j].name) {
          userFound = true;
          if (authorizedUsers[j].requiresPin()) {
            hasPin = true;
            userIndex = j;
          }
          break;
        }
      }

      if (!userFound) {
        logAccess("TELEGRAM", "N/A", "Acceso denegado", telegramUserName);
        sendTelegramNotification("Usuario *" + telegramUserName + "* no encontrado.", chat_id);
        telegramState = IDLE;
      } else if (!hasPin) {
        logAccess("TELEGRAM", "N/A", "Acceso denegado", telegramUserName);
        sendTelegramNotification("El usuario *" + telegramUserName + "* no tiene un PIN configurado.", chat_id);
        telegramState = IDLE;
      } else {
        telegramState = WAITING_FOR_PIN;
        telegramTimeout = millis() + TELEGRAM_TIMEOUT_MS;
        sendTelegramNotification("Por favor, ingresa el PIN de 4 dígitos para *" + telegramUserName + "*.", chat_id);
        Serial.println("[TELEGRAM] Nombre recibido: " + telegramUserName + ", esperando PIN");
      }
    } else if (telegramState == WAITING_FOR_PIN && chat_id == telegramChatId) {
      String enteredPin = text;
      bool authorized = false;
      String userName = telegramUserName;

      if (enteredPin.length() == 4 && enteredPin.toInt() >= 0 && enteredPin.toInt() <= 9999) {
        for (int j = 0; j < numAuthorizedUsers; j++) {
          if (telegramUserName == authorizedUsers[j].name && enteredPin == authorizedUsers[j].pin) {
            authorized = true;
            break;
          }
        }
      }

      if (authorized) {
        relayState = true;
        digitalWrite(RELAY_PIN, HIGH);
        relayTimerEnd = millis() + 10000;
        logAccess("TELEGRAM", enteredPin, "Acceso concedido", userName);
        sendTelegramNotification("[ACCESO] Concedido por Telegram para *" + userName + "*.", chat_id);
        Serial.println("[TELEGRAM] Acceso concedido para: " + userName);
      } else {
        logAccess("TELEGRAM", enteredPin, "Acceso denegado", userName);
        sendTelegramNotification("PIN incorrecto o inválido para *" + userName + "*. Acceso denegado.", chat_id);
        Serial.println("[TELEGRAM] Acceso denegado para: " + userName + ", PIN: " + enteredPin);
      }
      telegramState = IDLE;
    }
  }
}

void updateRGBStatus() {
  static LEDState lastLEDState = RED;
  static bool ledOn = false;
  unsigned long currentMillis = millis();
  LEDState newLEDState;

  if (doorOpen && !relayState) {
    newLEDState = BLINKING_RED;
    if (currentMillis - previousMillis >= blinkInterval) {
      previousMillis = currentMillis;
      ledOn = !ledOn;
      if (ledOn) {
        setLEDColor(strip.Color(255, 0, 0));
      } else {
        setLEDColor(strip.Color(0, 0, 0));
      }
    }
  } else if (!relayState) {
    newLEDState = RED;
    setLEDColor(strip.Color(255, 0, 0));
  } else if (relayState && !doorOpen) {
    newLEDState = GREEN;
    setLEDColor(strip.Color(0, 255, 0));
  } else {
    newLEDState = YELLOW;
    setLEDColor(strip.Color(255, 255, 0));
  }

  if (newLEDState != lastLEDState) {
    switch (newLEDState) {
      case RED:
        Serial.println("[LED] Estado cambiado a Rojo (Acceso restringido)");
        break;
      case GREEN:
        Serial.println("[LED] Estado cambiado a Verde (Acceso concedido)");
        break;
      case YELLOW:
        Serial.println("[LED] Estado cambiado a Amarillo (Puerta abierta)");
        break;
      case BLINKING_RED:
        Serial.println("[LED] Estado cambiado a Rojo Parpadeante (Intrusión detectada)");
        break;
    }
    lastLEDState = newLEDState;
    currentLEDState = newLEDState;
  }
}

void checkRelayTimer() {
  if (relayState && relayTimerEnd > 0 && millis() >= relayTimerEnd) {
    relayState = false;
    digitalWrite(RELAY_PIN, LOW);
    relayTimerEnd = 0;
    Serial.println("[RELE] Temporizador finalizado - Acceso desactivado");
  }
}

void checkDoorStatus() {
  static bool lastDoorState = false;
  bool currentState = digitalRead(DOOR_SENSOR_PIN) == HIGH;
  if (currentState != lastDoorState) {
    doorOpen = currentState;
    Serial.print("[PUERTA] Estado cambiado a: ");
    Serial.println(doorOpen ? "ABIERTA" : "CERRADA");
    if (doorOpen && !relayState) {
      sendTelegramNotification("*🚨 ¡ALERTA DE INTRUSIÓN! 🚨*\nPuerta abierta sin autorización.");
      logAccess("SENSOR", "N/A", "Intento de intrusión", "Ladrón");
    }
    lastDoorState = currentState;
  }
}

void blinkLED(int times) {
  targetBlinks = times;
  blinkCount = 0;
  ledState = false;
  lastBlink = millis();
}

void handleRoot(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud recibida para /");
  String html = "<!DOCTYPE html><html lang='es'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial; text-align:center;} .card{background:#f2f2f2; border-radius:10px; padding:20px; margin:10px; display:inline-block; width:200px;}";
  html += "input[type=number],input[type=text]{width:100px; padding:5px; margin:5px;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}";
  html += "table{border-collapse:collapse; width:80%; margin:20px auto;} th,td{border:1px solid #ddd; padding:8px;} th{background:#4CAF50; color:white;}";
  html += "</style>";
  html += "<meta http-equiv='refresh' content='4'></head><body>";
  html += "<h1>Control de Acceso ESP32</h1>";
  html += "<div class='card'><h2>Estado de la Puerta</h2><div style='color:" + String(doorOpen ? "red" : "green") + ";font-size:24px;'>" + String(doorOpen ? "ABIERTA" : "CERRADA") + "</div></div>";
  html += "<div class='card'><h2>Temporizador de Acceso</h2>";
  html += "<input type='number' id='timerInput' min='1' max='3600' placeholder='Segundos'>";
  html += "<button onclick=\"window.location.href='/setTimer?time='+document.getElementById('timerInput').value;\">Activar Acceso</button></div>";
  html += "<div class='card'><h2>Añadir Usuario</h2>";
  html += "<a href='/addUser'><button>Añadir Nuevo Usuario</button></a></div>";
  html += "<div class='card'><h2>Ingresar PIN</h2>";
  html += "<a href='/enterPin'><button>Ingresar PIN</button></a></div>";
  html += "<div class='card'><h2>Lista de Usuarios</h2>";
  html += "<a href='/users'><button>Ver Usuarios</button></a></div>";
  html += "<div><h2>Últimos Accesos</h2>";
  html += "<table><tr><th>Fecha y Hora</th><th>Método</th><th>ID</th><th>Usuario</th><th>Estado</th></tr>";
  int startIndex = max(0, historyCount - 15); // Start from the last 15 entries
  for (int i = historyCount - 1; i >= startIndex; i--) {
    int comma1 = accessHistory[i].indexOf(',');
    int comma2 = accessHistory[i].indexOf(',', comma1 + 1);
    int comma3 = accessHistory[i].indexOf(',', comma2 + 1);
    int comma4 = accessHistory[i].indexOf(',', comma3 + 1);
    String timestamp = accessHistory[i].substring(0, comma1);
    String method = accessHistory[i].substring(comma1 + 1, comma2);
    String id = accessHistory[i].substring(comma2 + 1, comma3);
    String user = accessHistory[i].substring(comma3 + 1, comma4);
    String status = accessHistory[i].substring(comma4 + 1);
    html += "<tr><td>" + timestamp + "</td><td>" + method + "</td><td>" + id + "</td><td>" + user + "</td><td>" + status + "</td></tr>";
  }
  html += "</table></div>";
  html += "</body></html>";
  request->send(200, "text/html", html);
}

void handleSetTimer(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud recibida para /setTimer");
  if (request->hasParam("time")) {
    String timeStr = request->getParam("time")->value();
    int seconds = timeStr.toInt();
    if (seconds > 0 && seconds <= 3600) {
      relayState = true;
      digitalWrite(RELAY_PIN, HIGH);
      relayTimerEnd = millis() + (seconds * 1000UL);
      logAccess("WEB", "N/A", "Acceso concedido", "N/A");
      sendTelegramNotification("[WEB] Acceso concedido por " + String(seconds) + " segundos");
    }
  }
  request->redirect("/");
}

void handleAddUser(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud recibida para /addUser");
  String html = "<!DOCTYPE html><html lang='es'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Añadir Usuario</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; text-align: center; margin: 20px; }";
  html += ".card { background: #f5f5f5; border-radius: 10px; padding: 20px; margin: 20px auto; max-width: 400px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += "label { display: block; margin: 10px 0 5px; font-weight: bold; }";
  html += "input[type=text], input[type=number] { width: 100%; padding: 8px; margin: 5px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }";
  html += "input[type=checkbox] { margin: 10px 5px; }";
  html += "button { padding: 10px 20px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; margin-top: 10px; }";
  html += "button:hover { background: #45a049; }";
  html += "p { color: #555; }";
  html += "</style></head><body>";
  html += "<h1>Añadir Nuevo Usuario</h1>";
  html += "<div class='card'>";
  html += "<form action='/addUser' method='POST'>";
  html += "<label for='name'>Nombre:</label>";
  html += "<input type='text' id='name' name='name' placeholder='Nombre del usuario' required><br>";
  html += "<label>Métodos de autenticación:</label><br>";
  html += "<input type='checkbox' id='usePin' name='usePin' checked>";
  html += "<label for='usePin'>Usar PIN</label><br>";
  html += "<input type='number' id='pinField' name='pin' placeholder='PIN (4 dígitos)' min='0000' max='9999'><br>";
  html += "<input type='checkbox' id='useRFID' name='useRFID'>";
  html += "<label for='useRFID'>Usar RFID</label><br>";
  html += "<p id='rfidInfo' style='display:none;'>Pase la tarjeta RFID después de enviar el formulario.</p>";
  html += "<button type='submit'>Registrar Usuario</button>";
  html += "</form>";
  html += "<a href='/'><button type='button'>Volver</button></a>";
  html += "</div>";
  html += "<script>";
  html += "document.getElementById('usePin').addEventListener('change', function() {";
  html += "  document.getElementById('pinField').disabled = !this.checked;";
  html += "  if (!this.checked && !document.getElementById('useRFID').checked) {";
  html += "    document.getElementById('useRFID').checked = true;";
  html += "    document.getElementById('rfidInfo').style.display = 'block';";
  html += "  }";
  html += "});";
  html += "document.getElementById('useRFID').addEventListener('change', function() {";
  html += "  document.getElementById('rfidInfo').style.display = this.checked ? 'block' : 'none';";
  html += "  if (!this.checked && !document.getElementById('usePin').checked) {";
  html += "    document.getElementById('usePin').checked = true;";
  html += "    document.getElementById('pinField').disabled = false;";
  html += "  }";
  html += "});";
  html += "</script>";
  html += "</body></html>";
  request->send(200, "text/html", html);
}

void handleAddUserPost(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud POST recibida para /addUser");
  if (!request->hasParam("name", true)) {
    Serial.println("[WEB] Error: Nombre no proporcionado");
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Nombre obligatorio</h1><a href='/addUser'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  String name = request->getParam("name", true)->value();
  bool usePin = request->hasParam("usePin", true);
  String pin = usePin ? request->getParam("pin", true)->value() : "";
  bool useRFID = request->hasParam("useRFID", true);

  if (!usePin && !useRFID) {
    Serial.println("[WEB] Error: No se seleccionó ningún método de autenticación");
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Seleccione al menos un método de autenticación</h1><a href='/addUser'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  if (usePin && (pin.length() != 4 || pin.toInt() < 0 || pin.toInt() > 9999)) {
    Serial.println("[WEB] Error: PIN inválido");
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: PIN debe ser de 4 dígitos</h1><a href='/addUser'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  tempName = name;
  tempPin = pin;
  tempUID = "";

  if (useRFID) {
    waitingForRFID = true;
    rfidTimeout = millis() + RFID_TIMEOUT_MS;
    Serial.println("[WEB] Esperando tarjeta RFID para usuario: " + name);
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;}</style>";
    html += "</head><body><h1>Escanea la tarjeta RFID ahora</h1><p>Tiempo restante: 30 segundos</p><script>setTimeout(() => {window.location.href='/'}, 30000);</script></body></html>";
    request->send(200, "text/html", html);
  } else {
    addUser(tempName, tempPin, tempUID);
    sendTelegramNotification("[WEB] Nuevo usuario registrado: " + tempName);
    request->redirect("/users");
  }
}

void handleEnterPin(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud recibida para /enterPin");
  String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial; text-align:center;} .card{background:#f2f2f2; border-radius:10px; padding:20px; margin:10px; display:inline-block; width:300px;}";
  html += "input[type=number]{width:200px; padding:5px; margin:5px;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}";
  html += "</style></head><body>";
  html += "<h1>Ingresar PIN</h1>";
  html += "<div class='card'>";
  html += "<form action='/enterPin' method='POST'>";
  html += "<label>PIN (4 dígitos):</label><br><input type='number' name='pin' min='0000' max='9999' placeholder='Ingresar PIN' required><br>";
  html += "<button type='submit'>Validar PIN</button>";
  html += "</form>";
  html += "<a href='/'><button type='button'>Volver</button></a>";
  html += "</div>";
  html += "</body></html>";
  request->send(200, "text/html", html);
}

void handleEnterPinPost(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud POST recibida para /enterPin");
  if (request->hasParam("pin", true)) {
    String enteredPin = request->getParam("pin", true)->value();
    String userName = "N/A";
    bool authorized = false;

    if (enteredPin.length() == 4 && enteredPin.toInt() >= 0 && enteredPin.toInt() <= 9999) {
      for (int i = 0; i < numAuthorizedUsers; i++) {
        if (enteredPin == authorizedUsers[i].pin) {
          authorized = true;
          userName = authorizedUsers[i].name;
          break;
        }
      }
      if (authorized) {
        relayState = true;
        digitalWrite(RELAY_PIN, HIGH);
        relayTimerEnd = millis() + 10000;
        logAccess("PIN", enteredPin, "Acceso concedido", userName);
        sendTelegramNotification("[ACCESO] Concedido por PIN: " + userName);
        request->redirect("/"); // Redirect to home page on successful PIN entry
      } else {
        logAccess("PIN", enteredPin, "Acceso denegado", userName);
        sendTelegramNotification("[ACCESO] Denegado por PIN");
        String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
        html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
        html += "</head><body><h1>Acceso denegado</h1><p>PIN incorrecto.</p><a href='/enterPin'><button>Volver</button></a></body></html>";
        request->send(200, "text/html", html);
      }
    } else {
      String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
      html += "</head><body><h1>Error: PIN debe ser de 4 dígitos</h1><a href='/enterPin'><button>Volver</button></a></body></html>";
      request->send(200, "text/html", html);
    }
  } else {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: PIN no proporcionado</h1><a href='/enterPin'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
  }
}

void handleUsers(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud recibida para /users");
  String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial; text-align:center;} .card{background:#f2f2f2; border-radius:10px; padding:20px; margin:10px; display:inline-block; width:300px;}";
  html += "input[type=password]{width:200px; padding:5px; margin:5px;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}";
  html += "</style></head><body>";
  html += "<h1>Acceso a Lista de Usuarios</h1>";
  html += "<div class='card'>";
  html += "<form action='/users' method='POST'>";
  html += "<label>Contraseña:</label><br><input type='password' name='password' required><br>";
  html += "<button type='submit'>Acceder</button>";
  html += "</form>";
  html += "<a href='/'><button>Volver</button></a>";
  html += "</div></body></html>";
  request->send(200, "text/html", html);
}

void handleUsersPost(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud POST recibida para /users");
  if (request->hasParam("password", true)) {
    String pwd = request->getParam("password", true)->value();
    if (pwd == ADMIN_PASSWORD) {
      String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      html += "<style>body{font-family:Arial; text-align:center;}";
      html += ".card{background:#f2f2f2; border-radius:10px; padding:20px; margin:10px auto; max-width:600px;}";
      html += "table{border-collapse:collapse; width:100%; margin:20px 0;} th,td{border:1px solid #ddd; padding:8px;}";
      html += "th{background:#4CAF50; color:white;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}";
      html += "button.delete{background:#ff4444;} button.delete:hover{background:#cc0000;}";
      html += "</style></head><body>";
      html += "<h1>Lista de Usuarios</h1>";
      html += "<div class='card'>";
      html += "<table><tr><th>Nombre</th><th>PIN</th><th>UID RFID</th><th>Acciones</th></tr>";
      for (int i = 0; i < numAuthorizedUsers; i++) {
        html += "<tr>";
        html += "<td>" + authorizedUsers[i].name + "</td>";
        html += "<td>" + (authorizedUsers[i].pin.length() > 0 ? authorizedUsers[i].pin : "N/A") + "</td>";
        html += "<td>" + (authorizedUsers[i].uid.length() > 0 ? authorizedUsers[i].uid : "N/A") + "</td>";
        html += "<td>";
        html += "<a href='/editUser?index=" + String(i) + "'><button>Editar</button></a> ";
        html += "<a href='/deleteUser?index=" + String(i) + "'><button class='delete'>Eliminar</button></a>";
        html += "</td></tr>";
      }
      html += "</table>";
      html += "<a href='/'><button>Volver</button></a>";
      html += "</div></body></html>";
      request->send(200, "text/html", html);
    } else {
      String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
      html += "</head><body><h1>Contraseña incorrecta</h1><a href='/users'><button>Volver</button></a></body></html>";
      request->send(200, "text/html", html);
    }
  } else {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Contraseña no proporcionada</h1><a href='/users'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
  }
}

void handleEditUserGet(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud recibida para /editUser");
  if (!request->hasParam("index")) {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Índice no proporcionado</h1><a href='/users'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  int index = request->getParam("index")->value().toInt();
  if (index < 0 || index >= numAuthorizedUsers) {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Índice inválido</h1><a href='/users'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  String html = "<!DOCTYPE html><html lang='es'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Editar Usuario</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; text-align: center; margin: 20px; }";
  html += ".card { background: #f5f5f5; border-radius: 10px; padding: 20px; margin: 20px auto; max-width: 400px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }";
  html += "label { display: block; margin: 10px 0 5px; font-weight: bold; }";
  html += "input[type=text], input[type=number] { width: 100%; padding: 8px; margin: 5px 0; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; }";
  html += "input[type=checkbox] { margin: 10px 5px; }";
  html += "button { padding: 10px 20px; background: #4CAF50; color: white; border: none; border-radius: 5px; cursor: pointer; margin-top: 10px; }";
  html += "button:hover { background: #45a049; }";
  html += "p { color: #555; }";
  html += "</style></head><body>";
  html += "<h1>Editar Usuario</h1>";
  html += "<div class='card'>";
  html += "<form action='/editUser' method='POST'>";
  html += "<input type='hidden' name='index' value='" + String(index) + "'>";
  html += "<label for='name'>Nombre:</label>";
  html += "<input type='text' id='name' name='name' value='" + authorizedUsers[index].name + "' required><br>";
  html += "<label>Métodos de autenticación:</label><br>";
  html += "<input type='checkbox' id='usePin' name='usePin' " + String(authorizedUsers[index].pin.length() > 0 ? "checked" : "") + ">";
  html += "<label for='usePin'>Usar PIN</label><br>";
  html += "<input type='number' id='pinField' name='pin' value='" + authorizedUsers[index].pin + "' placeholder='PIN (4 dígitos)' min='0000' max='9999'><br>";
  html += "<input type='checkbox' id='useRFID' name='useRFID' " + String(authorizedUsers[index].uid.length() > 0 ? "checked" : "") + ">";
  html += "<label for='useRFID'>Usar RFID</label><br>";
  html += "<p id='rfidInfo' style='display:" + String(authorizedUsers[index].uid.length() > 0 ? "block" : "none") + ";'>Pase la tarjeta RFID después de enviar el formulario.</p>";
  html += "<button type='submit'>Actualizar Usuario</button>";
  html += "</form>";
  html += "<a href='/users'><button type='button'>Volver</button></a>";
  html += "</div>";
  html += "<script>";
  html += "document.getElementById('usePin').addEventListener('change', function() {";
  html += "  document.getElementById('pinField').disabled = !this.checked;";
  html += "  if (!this.checked && !document.getElementById('useRFID').checked) {";
  html += "    document.getElementById('useRFID').checked = true;";
  html += "    document.getElementById('rfidInfo').style.display = 'block';";
  html += "  }";
  html += "});";
  html += "document.getElementById('useRFID').addEventListener('change', function() {";
  html += "  document.getElementById('rfidInfo').style.display = this.checked ? 'block' : 'none';";
  html += "  if (!this.checked && !document.getElementById('usePin').checked) {";
  html += "    document.getElementById('usePin').checked = true;";
  html += "    document.getElementById('pinField').disabled = false;";
  html += "  }";
  html += "});";
  html += "</script>";
  html += "</body></html>";
  request->send(200, "text/html", html);
}

void handleEditUserPost(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud POST recibida para /editUser");
  if (!request->hasParam("index", true) || !request->hasParam("name", true)) {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Parámetros incompletos</h1><a href='/users'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  int index = request->getParam("index", true)->value().toInt();
  String name = request->getParam("name", true)->value();
  bool usePin = request->hasParam("usePin", true);
  String pin = usePin ? request->getParam("pin", true)->value() : "";
  bool useRFID = request->hasParam("useRFID", true);
  String uid = authorizedUsers[index].uid; // Preserve existing UID unless RFID is re-scanned

  if (!usePin && !useRFID) {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Seleccione al menos un método de autenticación</h1><a href='/editUser?index=" + String(index) + "'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  if (usePin && (pin.length() != 4 || pin.toInt() < 0 || pin.toInt() > 9999)) {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: PIN debe ser de 4 dígitos</h1><a href='/editUser?index=" + String(index) + "'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
    return;
  }

  if (useRFID && uid.length() == 0) {
    waitingForRFID = true;
    rfidTimeout = millis() + RFID_TIMEOUT_MS;
    tempName = name;
    tempPin = pin;
    tempUID = "";
    Serial.println("[WEB] Esperando tarjeta RFID para editar usuario: " + name);
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;}</style>";
    html += "</head><body><h1>Escanea la tarjeta RFID ahora</h1><p>Tiempo restante: 30 segundos</p><script>setTimeout(() => {window.location.href='/users'}, 30000);</script></body></html>";
    request->send(200, "text/html", html);
  } else {
    updateUser(index, name, pin, uid);
    sendTelegramNotification("[WEB] Usuario actualizado: " + name);
    request->redirect("/users");
  }
}

void handleDeleteUser(AsyncWebServerRequest *request) {
  Serial.println("[WEB] Solicitud recibida para /deleteUser");
  if (request->hasParam("index")) {
    int index = request->getParam("index")->value().toInt();
    if (index >= 0 && index < numAuthorizedUsers) {
      String userName = authorizedUsers[index].name;
      deleteUser(index);
      sendTelegramNotification("[WEB] Usuario eliminado: " + userName);
      request->redirect("/users");
    } else {
      String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
      html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
      html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
      html += "</head><body><h1>Error: Índice inválido</h1><a href='/users'><button>Volver</button></a></body></html>";
      request->send(400, "text/html", html);
    }
  } else {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial; text-align:center;} button{padding:10px 20px; border-radius:5px; border:none; background:#4CAF50; color:white; cursor:pointer;}</style>";
    html += "</head><body><h1>Error: Índice no proporcionado</h1><a href='/users'><button>Volver</button></a></body></html>";
    request->send(400, "text/html", html);
  }
}

String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("[NTP] Error al obtener la hora");
    return "N/A";
  }
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void logAccess(const String& method, const String& id, const String& status, const String& userName) {
  String timestamp = getCurrentTime();
  String entry = timestamp + "," + method + "," + id + "," + userName + "," + status;

  if (historyCount < 15) {
    accessHistory[historyCount] = entry;
    historyCount++;
  } else {
    for (int i = 0; i < 14; i++) {
      accessHistory[i] = accessHistory[i + 1];
    }
    accessHistory[14] = entry;
  }

  configureSPIPins(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  File file = SD.open(SD_FILE, FILE_APPEND);
  if (file) {
    file.println(entry);
    file.close();
    Serial.println("[LOG] Registro almacenado: " + entry);
  } else {
    Serial.println("[SD] Error al escribir en archivo de log");
  }
}