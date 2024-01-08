#if defined(ESP8266)
#include <ESP8266mDNS.h>
#elif defined(ESP32)
#include <ESPmDNS.h>
#endif
#include <FS.h>
#include <LittleFS.h>
#include <AsyncFsWebServer.h>  // https://github.com/cotestatnt/async-esp-fs-webserver
#include <SerialLog.h>

#define FILESYSTEM LittleFS

char const* hostname = "satran";
AsyncFsWebServer server(80, FILESYSTEM, hostname);

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

#define PIN_CLEAR_WIFI_BUTTON 16
#define PIN_SENS_AZ 5
#define PIN_SENS_EL 4
#define PIN_EL2 0
#define PIN_LED LED_BUILTIN
#define PIN_AZ2 14
#define PIN_AZ1 12
#define PIN_EL1 13
#define PIN_EN_MOTORS 15  // PIN_PWM

int minAzValue = 200;
int maxAzValue = 700;
int minElValue = 350;
int maxElValue = 650;

// Log messages both on Serial and WebSocket clients
void wsLogPrintf(bool toSerial, const char* format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, 128, format, args);
  va_end(args);
  server.wsBroadcast(buffer);
  if (toSerial)
    log_info("%s", buffer);
}

// In this example a custom websocket event handler is used instead default
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      client->printf("{\"Websocket connected\": true, \"clients\": %u}", client->id());
      log_info("Websocket client %u connected", client->id());
      break;

    case WS_EVT_DISCONNECT:
      log_info("Websocket client %u disconnected", client->id());
      break;

    case WS_EVT_DATA:
      {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->opcode == WS_TEXT) {
          char msg[len + 1];
          msg[len] = '\0';
          memcpy(msg, data, len);
          log_info("Received message \"%s\"", msg);
        }
      }
      break;

    default:
      break;
  }
}

// Test "config" values
String option1 = "Test option String";
uint32_t option2 = 1234567890;
uint8_t ledPin = LED_BUILTIN;

// Timezone definition to get properly time from NTP server
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"
struct tm Time;


////////////////////////////////  NTP Time  /////////////////////////////////////
void getUpdatedtime(const uint32_t timeout) {
  uint32_t start = millis();
  log_info("Sync time...");
  while (millis() - start < timeout && Time.tm_year <= (1970 - 1900)) {
    time_t now = time(nullptr);
    Time = *localtime(&now);
    delay(5);
  }
  log_info(" done.");
}


////////////////////////////////  Filesystem  /////////////////////////////////////////
void listDir(fs::FS& fs, const char* dirname, uint8_t levels) {
  log_info("Listing directory: %s", dirname);
  File root = fs.open(dirname, "r");
  if (!root) {
    log_info("- failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    log_info(" - not a directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      if (levels) {
#ifdef ESP32
        listDir(fs, file.path(), levels - 1);
#elif defined(ESP8266)
        listDir(fs, file.fullName(), levels - 1);
#endif
      }
    } else {
      log_info("|__ FILE: %s (%d bytes)", file.name(), file.size());
    }
    file = root.openNextFile();
  }
}

bool startFilesystem() {
  if (FILESYSTEM.begin()) {
    listDir(LittleFS, "/", 1);
    return true;
  } else {
    log_info("ERROR on mounting filesystem. It will be formmatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
  return false;
}


////////////////////  Load and save application configuration from filesystem  ////////////////////
void saveApplicationConfig() {
  File file = server.getConfigFile("r");
  DynamicJsonDocument doc(file.size() * 1.33);
  doc["Option 1"] = option1;
  doc["Option 2"] = option2;
  doc["LED Pin"] = ledPin;
  serializeJsonPretty(doc, file);
  file.close();
  delay(1000);
  ESP.restart();
}

bool loadApplicationConfig() {
  if (FILESYSTEM.exists(server.getConfiFileName())) {
    File file = server.getConfigFile("r");
    DynamicJsonDocument doc(file.size() * 1.33);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (!error) {
      option1 = doc["Option 1"].as<String>();
      option2 = doc["Option 2"];
      ledPin = doc["LED Pin"];
      return true;
    } else {
      log_info("Failed to deserialize JSON. Error: %s", error.c_str());
    }
  }
  return false;
}


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_SENS_AZ, OUTPUT);
  pinMode(PIN_SENS_EL, OUTPUT);
  pinMode(PIN_CLEAR_WIFI_BUTTON, INPUT_PULLDOWN_16);


  Serial.begin(115200);
  delay(1000);

  // Try to connect to stored SSID, start AP if fails after timeout
  IPAddress myIP = server.startWiFi(15000, "SATRAN", "");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  log_info("WiFi connected");
  log_info("IP address: %s", WiFi.localIP().toString().c_str());

  // FILESYSTEM INIT
  if (startFilesystem()) {
    // Load configuration (if not present, default will be created when webserver will start)
    if (loadApplicationConfig())
      log_info("Application option loaded");
    else
      log_info("Application options NOT loaded!");
  }

  // Configure /setup page
  server.addOptionBox("My Options");
  server.addOption("LED Pin", ledPin);
  server.addOption("Option 1", option1.c_str());
  server.addOption("Option 2", option2);

  // Enable ACE FS file web editor and add FS info callback fucntion
  server.enableFsCodeEditor();
  /*
  * Getting FS info (total and free bytes) is strictly related to
  * filesystem library used (LittleFS, FFat, SPIFFS etc etc)
  * (On ESP8266 will be used "built-in" fsInfo data type)
  */
#ifdef ESP32
  server.setFsInfoCallback([](fsInfo_t* fsInfo) {
    fsInfo->totalBytes = LittleFS.totalBytes();
    fsInfo->usedBytes = LittleFS.usedBytes();
    strcpy(fsInfo->fsName, "LittleFS");
  });
#endif

  // Init with custom WebSocket event handler and start server
  server.init(onWsEvent);

  log_info("SATRAN Web Server started on IP Address: %s", WiFi.localIP().toString().c_str());
  log_info("Open /setup page to configure optional parameters.");
  log_info("Open /edit page to view, edit or upload example or your custom webserver source files.");

  // Set hostname
#ifdef ESP8266
  WiFi.hostname(hostname);
  configTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#elif defined(ESP32)
  WiFi.setHostname(hostname);
  configTzTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
#endif

  // Start MDSN responder
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin(hostname)) {
      log_info("MDNS responder started.");
      log_info("You should be able to connect with address  http://%s.local/", hostname);
      // Add service to MDNS-SD
      MDNS.addService("http", "tcp", 80);
    }
  }
}


void loop() {

  if (digitalRead(PIN_CLEAR_WIFI_BUTTON) == HIGH) {
    wsLogPrintf(true, "Button on GPIO %d clicked", PIN_CLEAR_WIFI_BUTTON);
    delay(1000);
  }

  if (WiFi.status() == WL_CONNECTED) {
#ifdef ESP8266
    MDNS.update();
#endif
  }

  static uint32_t sendToClientTime;
  if (millis() - sendToClientTime > 1000) {
    sendToClientTime = millis();
    JsonDocument doc;
    char jsonStr[100];
    doc["azSensor"] = readSensor("azimuth");
    doc["azDeg"] = readPosition("azimuth");
    doc["elSensor"] = readSensor("elevation");
    doc["elDeg"] = readPosition("elevation");
    serializeJson(doc, jsonStr);
    wsLogPrintf(false, "%s", jsonStr);
  }
}

int readSensor(String value) { /* Read actual values "azimuth" or "elevation" from potentiometers */
  int reading = 1;
  if (value == "azimuth") {
    // azimuth
    digitalWrite(PIN_SENS_AZ, HIGH);
    delay(5); /* time to normalize voltage before reading */
    reading = analogRead(0);
    for (int x = 0; x < 12; x++) {
      delay(5);
      reading = (reading * 0.6) + (analogRead(0) * 0.4); /* running average reduces sensor fluctuation */
    }
    digitalWrite(PIN_SENS_AZ, LOW);
  } else if (value == "elevation") {
    // elevation
    digitalWrite(PIN_SENS_EL, HIGH);
    delay(5); /* time to normalize voltage before reading */
    reading = analogRead(0);
    for (int x = 0; x < 12; x++) {
      delay(5);
      reading = (reading * 0.6) + (analogRead(0) * 0.4); /* running average reduces sensor fluctuation */
    }
    digitalWrite(PIN_SENS_EL, LOW);
  }
  return reading;
}


int readPosition(String value) { /* Get position ("azimuth" or "elevation") in degrees */
  double readPos = 0;
  int reading = 0;
  if (value == "azimuth") {
    int readAz = readSensor("azimuth");
    readPos = (readAz - minAzValue) * 360 / (maxAzValue - minAzValue);
    reading = round(readPos);
  } else if (value == "elevation") {
    int readEl = readSensor("elevation");
    readPos = (readEl - minElValue) * 90 / (maxElValue - minElValue);
    reading = round(readPos);
  }
  return reading;
}
