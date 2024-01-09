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

/* Initial minimum azimuth sensor value, corresponds to 0 degrees north. */
int minAzValue = 200;
/* Initial maximum azimuth sensor value, corresponds to 360 degrees north. */
int maxAzValue = 700;
/* Initial minimum elevation sensor value, corresponds to 0 degrees from horizon. */
int minElValue = 350;
/* Initial maximum elevation sensor value, corresponds to 90 degrees from horizon (zenith). */
int maxElValue = 650;

/* Target azimuth in degrees to which we want to point. */
int targetAzDeg = 0;
/* Target elevation in degrees to which we want to point. */
int targetElDeg = 0;

String error;
int errorAz = 0;
int errorEl = 0;

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
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, data);
          if (!error) {
            targetAzDeg = doc["targetAzDeg"];
            targetElDeg = doc["targetElDeg"];
            log_info("Received new target: %d %d", targetAzDeg, targetElDeg);
          }
        } else {
          log_info("Problem deserializing incoming WS message, %s", error);
        }
      }
      break;

    default:
      break;
  }
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
    log_info("ERROR on mounting filesystem. It will be formatted!");
    FILESYSTEM.format();
    ESP.restart();
  }
  return false;
}


////////////////////  Load and save application configuration from filesystem  ////////////////////
void saveApplicationConfig() {
  File file = server.getConfigFile("r");
  DynamicJsonDocument doc(file.size() * 1.33);
  doc["minAzValue"] = minAzValue;
  doc["maxAzValue"] = maxAzValue;
  doc["minElValue"] = minElValue;
  doc["maxElValue"] = maxElValue;
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
      minAzValue = doc["minAzValue"];
      maxAzValue = doc["maxAzValue"];
      minElValue = doc["minElValue"];
      maxElValue = doc["maxElValue"];
      return true;
    } else {
      log_info("Failed to deserialize JSON. Error: %s", error.c_str());
    }
  }
  return false;
}


void setup() {
  pinMode(PIN_AZ1, OUTPUT);
  pinMode(PIN_AZ2, OUTPUT);
  pinMode(PIN_EL1, OUTPUT);
  pinMode(PIN_EL2, OUTPUT);
  pinMode(PIN_EN_MOTORS, OUTPUT);
  haltMotors();

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
    // Load configuration (if not present, default will be created when web server will start)
    if (loadApplicationConfig())
      log_info("Application options loaded");
    else
      log_info("Application options NOT loaded!");
  }

  // Configure /setup page
  server.setSetupPageTitle("SATRAN Setup");
  server.addOptionBox("SATRAN Configuration");
  server.addOption("minAzValue", minAzValue);
  server.addOption("maxAzValue", maxAzValue);
  server.addOption("minElValue", minElValue);
  server.addOption("maxElValue", maxElValue);

  // Enable ACE FS file web editor and add FS info callback function
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

  targetAzDeg = readPosition("azimuth");
  targetElDeg = readPosition("elevation");

  /* Check that motors and sensors are working, then point north */
  initializeRotator();

  // Init with custom WebSocket event handler and start server
  server.init(onWsEvent);

  log_info("SATRAN Web Server started on IP Address: %s", WiFi.localIP().toString().c_str());
  log_info("Open /setup page to configure optional parameters.");
  log_info("Open /edit page to view, edit or upload example or your custom web server source files.");

  // Set hostname
#ifdef ESP8266
  WiFi.hostname(hostname);
#elif defined(ESP32)
  WiFi.setHostname(hostname);
#endif

  // Start MDNS responder
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

  turnMotors(targetAzDeg, targetElDeg);

  static uint32_t sendToClientTime;
  if (millis() - sendToClientTime > 1000) {
    sendToClientTime = millis();
    JsonDocument doc;
    char jsonStr[100];
    doc["azSensor"] = readSensor("azimuth");
    doc["azDeg"] = readPosition("azimuth");
    doc["elSensor"] = readSensor("elevation");
    doc["elDeg"] = readPosition("elevation");
    doc["targetAzDeg"] = targetAzDeg;
    doc["targetElDeg"] = targetElDeg;
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

void initializeRotator(void) { /* Test connection and function of motors and sensors */

  //  If connection is established with sensors
  if (readSensor("azimuth") > 10 && readSensor("elevation") > 10) {
    int posAz = readPosition("azimuth");
    int posEl = readPosition("elevation");
    int startAz = posAz;
    int startEl = posEl;

    // Rotate motors
    int targetAz;
    int targetEl;
    if (startAz < 180) {
      targetAz = startAz + 20;
    } else {
      targetAz = startAz - 20;
    }
    if (startEl < 45) {
      targetEl = 50;
    } else {
      targetEl = 30;
    }
    turnMotors(targetAz, targetEl);
    int newAz = readPosition("azimuth");
    int readAzChange = abs(startAz - newAz);
    int newEl = readPosition("elevation");
    int readElChange = abs(startEl - newEl);

    // Return true only if the sensors registered motion
    if (readAzChange < 5) {
      error = "Azimuth sensor did not register motion when initializing (Measured change: " + (String)readAzChange + ")";
    } else if (readElChange < 5) {
      error = "Elevation sensor did not register motion when initializing (Measured change: " + (String)readElChange + ")";
    } else {
      // If no errors and rotator has been previously calibrated; move to north
      if (minAzValue != 200 || maxAzValue != 700 || minElValue != 350 || maxElValue != 650) {
        delay(500);
        turnMotors(0, 5);
      }
    }
    posAz = readPosition("azimuth");
    posEl = readPosition("elevation");

  } else {
    String az = String(readSensor("azimuth"));
    String el = String(readSensor("elevation"));
    error = "could not initialize, missing sensor connection (Azimuth " + az + " Elevation " + el + ")";
  }
  log_info("Rotator initialized successfully");
}

void turnMotors(int azTarget, int elTarget) { /* Move motor towards target in degrees */

  int azPos;
  int elPos;
  bool motionAz = false;
  bool motionEl = false;
  int newAz = readPosition("azimuth");
  int newEl = readPosition("elevation");

  // Enable motors
  analogWrite(PIN_EN_MOTORS, 250);


  // Set actual current position to the one measured and not the last known
  azPos = newAz;
  elPos = newEl;

  if (azPos < azTarget && abs(azPos - azTarget) > 4) {
    // turn right CW
    digitalWrite(PIN_AZ1, HIGH);
    motionAz = true;
  } else if (abs(azPos - azTarget) > 4) {
    // turn left CCW
    digitalWrite(PIN_AZ2, HIGH);
    motionAz = true;
  }

  if (elPos > elTarget && abs(elPos - elTarget) > 2) {
    // turn down
    digitalWrite(PIN_EL1, HIGH);
    motionEl = true;
  } else if (abs(elPos - elTarget) > 2) {
    // turn up
    digitalWrite(PIN_EL2, HIGH);
    motionEl = true;
  }

  int n = 0;
  while (motionAz == true || motionEl == true) {
    if (motionAz == true) {
      newAz = readPosition("azimuth");

      errorAz = 0;
      if (azPos < azTarget && newAz > (azTarget - 5)) {
        digitalWrite(PIN_AZ1, LOW);
        digitalWrite(PIN_AZ2, LOW);
        motionAz = false;
      }
      if (azPos > azTarget && newAz < (azTarget + 5)) {
        digitalWrite(PIN_AZ1, LOW);
        digitalWrite(PIN_AZ2, LOW);
        motionAz = false;
      }
    }

    if (motionEl == true) {
      newEl = readPosition("elevation");

      errorEl = 0;
      if (elPos > elTarget && newEl < (elTarget + 3)) {
        digitalWrite(PIN_EL1, LOW);
        digitalWrite(PIN_EL2, LOW);
        motionEl = false;
      }
      if (elPos < elTarget && newEl > (elTarget - 3)) {
        digitalWrite(PIN_EL1, LOW);
        digitalWrite(PIN_EL2, LOW);
        motionEl = false;
      }
    }

    delay(1);
    if (n == 6000) { /*timeout*/
      error = "Motion timeout. Check cables/mechanics and restart device.";
      motionAz = false;
      motionEl = false;
    }
    n++;
  }  //endwhile
  haltMotors();
}

void haltMotors(void) { /* Turn off all motor output pins, and stop motor */
  digitalWrite(PIN_AZ1, LOW);
  digitalWrite(PIN_AZ2, LOW);
  digitalWrite(PIN_EL1, LOW);
  digitalWrite(PIN_EL2, LOW);
  analogWrite(PIN_EN_MOTORS, 0); /* Disable the h-bridge */
}
