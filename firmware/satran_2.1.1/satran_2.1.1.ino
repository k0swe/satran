/********************************************
 * Satran Firmware MK2/3 v2.1.1
 * Daniel Nikolajsen 2022 <satran@danaco.se>
 * www.satran.io
 * Creative Commons BY-NC-SA 2.0
 *******************************************/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

/* Fallback sensor min/max values */
int minAzValue = 200;
int maxAzValue = 700;
int minElValue = 350;
int maxElValue = 650;

/* Web and HTTP server */
ESP8266WebServer server(80);
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);
String ssid;
String pass;
String ssidList;
String hostName = "SATRAN_VhfSUghPX";

/* Hamlib/rotctl TCP server */
WiFiServer server2(4533);
WiFiClient client1;
String rotctl;

String page;
String page2;
String calibrationPage;
String versioncode = "SATRAN firmware 2.1.1<br/>&copy; 2022-10-19 Danaco";
String formvalues = "<script>(function() { var queries = new URLSearchParams(window.location.search); var az = document.getElementById('az'); var el = document.getElementById('el'); az.value = queries.get('az'); el.value = queries.get('el');})();</script>";
String favicon = "data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiA/PjxzdmcgaWQ9IkxheWVyXzEiIHN0eWxlPSJlbmFibGUtYmFja2dyb3VuZDpuZXcgMCAwIDI0IDI0OyIgdmVyc2lvbj0iMS4xIiB2aWV3Qm94PSIwIDAgMjQgMjQiIHhtbDpzcGFjZT0icHJlc2VydmUiIHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiPjxzdHlsZSB0eXBlPSJ0ZXh0L2NzcyI+Cgkuc3Qwe2ZpbGw6IzFFMjMyRDt9Cjwvc3R5bGU+PHBhdGggY2xhc3M9InN0MCIgZD0iTTEyLDR2MmMzLDAsNiwzLDYsNmgyQzIwLDgsMTYsNCwxMiw0eiBNMTIsMHYyYzYsMCwxMCw0LDEwLDEwaDJDMjQsNSwxOSwwLDEyLDB6IE0xMy41LDExLjlsMS4yLTEuMiAgYzAuNC0wLjQsMC40LTEsMC0xLjRjLTAuNC0wLjQtMS0wLjQtMS40LDBsLTEuMiwxLjJDOS41LDguMSw2LjMsNi4zLDIuNyw1LjVMMiw1LjRMMS42LDZDMC42LDcuOCwwLDkuOSwwLDEyYzAsNi42LDUuNCwxMiwxMiwxMiAgYzIuMSwwLDQuMi0wLjYsNi0xLjZsMC42LTAuNGwtMC4yLTAuN0MxNy43LDE3LjcsMTUuOSwxNC41LDEzLjUsMTEuOXogTTEyLDIyQzYuNSwyMiwyLDE3LjUsMiwxMmMwLTEuNSwwLjMtMywxLTQuMyAgQzkuNSw5LjQsMTQuNiwxNC41LDE2LjMsMjFDMTUsMjEuNywxMy41LDIyLDEyLDIyeiIvPjwvc3ZnPg==";
String pageheader = "<!DOCTYPE html><html><head><title>SATRAN</title><meta name='viewport' content='width=device-width, initial-scale=1.0'><link rel='icon' href='" + favicon + "'><style>body{font-family:verdana,helvetica,sans-serif;padding:25px;}input,select{width:100%;max-width:350px;padding:6px 12px;margin-bottom:10px; box-sizing: border-box;}.btn{cursor:pointer;padding:10px;background:#eee;border:1px solid #777;border-radius:4px;margin:10px;min-width:200px;}#submit{margin:20px auto;}</style></head>";
String error;
int errorAz = 0;
int errorEl = 0;

int Pin_LED = 2;
int Pin_PWM = 15;
int Pin_Az1 = 12;
int Pin_Az2 = 14;
int Pin_SensAz = 5;
int Pin_El1 = 13;
int Pin_El2 = 0;
int Pin_SensEl = 4;

int readAz = 1;  // Measured sensor values
int readEl = 1;
int posAz = 1;  // Last (correct) position, used to prevent radio interference or sensor glitch
int posEl = 1;

int ResetState = 1;
int Pin_Reset = 16;

void setup(void) {

  Serial.begin(9600);
  EEPROM.begin(512);

  /* Initialize IOs */
  pinMode(Pin_Az1, OUTPUT);
  pinMode(Pin_Az2, OUTPUT);
  pinMode(Pin_El1, OUTPUT);
  pinMode(Pin_El2, OUTPUT);
  pinMode(Pin_PWM, OUTPUT);
  haltMotors();
  pinMode(Pin_LED, OUTPUT);
  digitalWrite(Pin_LED, HIGH); /* Turn off status LED. Reverse logic! */
  pinMode(Pin_SensAz, OUTPUT); /* Outputs that later are read by ADC pin */
  pinMode(Pin_SensEl, OUTPUT);
  digitalWrite(Pin_SensAz, LOW);
  digitalWrite(Pin_SensEl, LOW);

  readCalibration(); /* Update fallback values with stored sensor limits */
  posAz = readPosition("azimuth");
  posEl = readPosition("elevation");

  /* Scan available networks */
  int n = WiFi.scanNetworks();
  delay(50);
  ssidList = "<select id=\"newssid\" name=\"newssid\">";
  for (int i = 0; i < n; ++i) {
    ssidList += "<option value=\"";
    ssidList += WiFi.SSID(i);
    ssidList += "\">";
    ssidList += WiFi.SSID(i);
    ssidList += "</option>";
  }
  ssidList += "</select>";

  /* Setup WLAN */
  ssid = getWifiCredential("ssid");
  pass = getWifiCredential("pass");
  if (ssid != "n/a" && pass != "n/a") {
    if (!wifiConnect(ssid, pass)) {
      // create HOTSPOT if cant connect to Wifi
      WiFi.hostname(hostName);
      WiFi.mode(WIFI_AP);  // Enable AP
      WiFi.softAPConfig(apIP, apIP, netMsk);
      WiFi.softAP(hostName);  // Start AP
    }
  } else {
    // No Wifi credentials found, instead create HOTSPOT
    WiFi.hostname(hostName);
    WiFi.mode(WIFI_AP);  // Enable AP
    WiFi.softAPConfig(apIP, apIP, netMsk);
    WiFi.softAP(hostName);  // Start AP
  }

  /* Check that motors and sensors are working, then point north */
  initializeRotator();

  /* Server web page listeners */
  server.on("/", []() {
    int newAz = readPosition("azimuth");
    int newEl = readPosition("elevation");
    if (error == "") {
      page = pageheader + "<body style='text-align:center;'><h1>SATRAN Rotator</h1><p><form action='./manual' method='get'>Azimuth (1 - 360)<br/><input type='number' id='az' name='az' min='1' max='360' value=\"" + posAz + "\" placeholder=\"" + posAz + "\"><br/>Elevation (1 - 90)<br/><input type='number' id='el' name='el' min='1' max='90' value=\"" + posEl + "\" placeholder=\"" + posEl + "\"><br/><input type='submit' value='Execute'></form></p><p style='font-size:0.8em;color:#888;'>" + versioncode + "<br/>Current position: Azimuth " + newAz + " (memory " + posAz + ") / Elevation " + newEl + " (memory " + posEl + ") degrees<br/>Sensor values: Azimuth " + readAz + " / Elevation " + readEl + "</p>" + formvalues + "</body></html>";
    } else {
      page = pageheader + "<body style='text-align:center;'><h1>SATRAN Rotator</h1><p><strong>" + error + "</strong></p><p style='font-size:0.8em;color:#888;'>" + versioncode + "</p></body></html>";
    }
    server.send(200, "text/html", page);
  });

  server.on("/ping", []() {
    server.send(200, "text/html", "SATRAN");  //WiFi.localIP()
  });

  server.on("/manual", []() {
    // Receive coordinates via GET and turn the rotor
    int azimuth = server.arg("az").toInt();
    int elevation = server.arg("el").toInt();

    if (azimuth > 0 && elevation > 0) { turnMotors(azimuth, elevation); }

    int newAz = readPosition("azimuth");
    int newEl = readPosition("elevation");
    if (error == "") {
      page = pageheader + "<body style='text-align:center;'><h1>SATRAN Rotator</h1><p><form action='./manual' method='get'>Azimuth (1 - 360)<br/><input type='number' id='az' name='az' min='1' max='360' value=\"" + newAz + "\"><br/>Elevation (1 - 90)<br/><input type='number' id='el' name='el' min='1' max='90' value=\"" + newEl + "\"><br/><input type='submit' value='Execute'></form></p><p style='font-size:0.8em;color:#888;'>" + versioncode + "<br/>Current position: Azimuth " + newAz + " (memory " + posAz + ") / Elevation " + newEl + " (memory " + posEl + ") degrees<br/>Sensor values: Azimuth " + readAz + " / Elevation " + readEl + "</p>" + formvalues + "</body></html>";
    } else {
      page = pageheader + "<body style='text-align:center;'><h1>SATRAN Rotator</h1><p><strong>" + error + "</strong></p><p style='font-size:0.8em;color:#888;'>" + versioncode + "</p></body></html>";
    }
    server.send(200, "text/html", page);
  });

  server.on("/tracker", []() {
    if (error == "") {
      // Receive coordinates via GET and turn the rotor
      int azimuth = server.arg("az").toInt();
      int elevation = server.arg("el").toInt();

      if (azimuth > -1 && azimuth < 361 && elevation > -1 && elevation < 91) {
        turnMotors(azimuth, elevation);
      } else {
        server.send(200, "text/html", "Incorrect target values submitted (Azimuth 0-360, Elevation 0-90)");
      }

      server.send(200, "text/html", "success");
    } else {
      server.send(404, "text/html", "Error : " + error);
    }
  });


  // SETUP PAGE
  page2 = pageheader + "<body style='text-align:center;'><em>Saving and restarting. Please connect your device back to your wifi.</em></body></html>";

  server.on("/setup", []() {
    page = pageheader + "<body style='text-align:center;'><h1>SATRAN Rotator - WLAN Setup</h1><p>" + error + "</p><p><form action='/savesettings' method='get'>Network<br/>" + ssidList + "<br/><br/>Password<br/><input type='text' id='newpass' name='newpass'><br/><br/><input type='submit' id='submit' value='Save'></form>";
    page += "</p><p style='font-size:0.8em;color:#888;'>Can't find your access point?<br/> SATRAN only supports b/g/n-type networks.<br/><br/>" + versioncode + "</p></body></html>";

    server.send(200, "text/html", page);
  });
  server.on("/savesettings", []() {
    //Saving Wifi Credentials

    // Clean inputs
    String newSsid = server.arg("newssid");
    newSsid.trim();
    String newPass = server.arg("newpass");
    newPass.trim();

    if (saveWifiCredentials(newSsid, newPass)) {
      //Saving and restarting
      server.send(200, "text/html", page2);
      delay(3000);
    } else {
      error = "Could not save wifi credentials. Restarting unit.";
    }
    ESP.restart();
  });

  server.on("/calibration", []() {
    readAz = readSensor("azimuth");
    readEl = readSensor("elevation");
    calibrationPage = pageheader + "<body style='text-align:center;'><script>function rotate(command){ fetch('./microstep?action='+command).then(data => { console.log(data); location.reload(); }); }</script>";
    calibrationPage = calibrationPage + error + "<h1>SATRAN Calibration</h1>";
    calibrationPage = calibrationPage + "<button type='button' onclick='rotate(\"ccw\");' class='btn'>Azimuth CCW</button> <input type='text' value='" + readAz + "' readonly style='width:70px;'> <button type='button' onclick='rotate(\"cw\");' class='btn'>Azimuth CW</button><br><button type='button' onclick='rotate(\"down\");' class='btn'>Elevation Down</button> <input type='text' value='" + readEl + "' readonly style='width:70px;'> <button type='button' onclick='rotate(\"up\");' class='btn'>Elevation Up</button><br><br>";
    calibrationPage = calibrationPage + "<FORM action='/storecalibration' method='get'>Azimuth min (0 degrees north): <input type='text' id='minAz' name='minAz' value='" + minAzValue + "' style='width:80px;'><br>Azimuth max (360 degrees north): <input type='text' id='maxAz' name='maxAz' value='" + maxAzValue + "' style='width:80px;'><br>Elevation min (0 degrees, horizontal): <input type='text' id='minEl' name='minEl' value='" + minElValue + "' style='width:80px;'><br>Elevation max (90 degrees, vertical): <input type='text' id='maxEl' name='maxEl' value='" + maxElValue + "' style='width:80px;'><br><input type='submit' id='submit' value='Save calibration'></FORM></body></html>";
    error = ""; /* Reset any messages already sent to browser */
    server.send(200, "text/html", calibrationPage);
  });

  server.on("/microstep", []() {
    if (error == "") {
      // Receive coordinates via GET and turn the rotor
      String command = server.arg("action");
      analogWrite(Pin_PWM, 255);
      delay(10);
      if (command == "cw") {
        digitalWrite(Pin_Az1, HIGH);
        delay(20);
        haltMotors();
        delay(200);
        int readAz = readSensor("azimuth"); /* Read the new values */
      } else if (command == "ccw") {
        digitalWrite(Pin_Az2, HIGH);
        delay(20);
        haltMotors();
        delay(200);
        int readAz = readSensor("azimuth"); /* Read the new values */
      } else if (command == "down") {
        digitalWrite(Pin_El1, HIGH);
        delay(20);
        haltMotors();
        delay(200);
        int readEl = readSensor("elevation");
      } else if (command == "up") {
        digitalWrite(Pin_El2, HIGH);
        delay(20);
        haltMotors();
        delay(200);
        int readEl = readSensor("elevation");
      }

      calibrationPage = pageheader + "<body style='text-align:center;'><script>function rotate(command){ fetch('./manual?action='+command).then(data => { console.log(data); location.reload(); }); }</script>";
      calibrationPage = calibrationPage + error + "<h1>SATRAN Calibration</h1>";
      calibrationPage = calibrationPage + "<button type='button' onclick='rotate(\"ccw\");' class='btn'>Azimuth CCW</button> <input type='text' value='" + readAz + "' readonly style='width:70px;'> <button type='button' onclick='rotate(\"cw\");' class='btn'>Azimuth CW</button><br><button type='button' onclick='rotate(\"down\");' class='btn'>Elevation Down</button> <input type='text' value='" + readEl + "' readonly style='width:70px;'> <button type='button' onclick='rotate(\"up\");' class='btn'>Elevation Up</button><br><br>";
      calibrationPage = calibrationPage + "<FORM action='/storecalibration' method='get'>Azimuth min (0 degrees north): <input type='text' id='minAz' name='minAz' value='" + minAzValue + "' style='width:80px;'><br>Azimuth max (360 degrees north): <input type='text' id='maxAz' name='maxAz' value='" + maxAzValue + "' style='width:80px;'><br>Elevation min (0 degrees, horizontal): <input type='text' id='minEl' name='minEl' value='" + minElValue + "' style='width:80px;'><br>Elevation max (90 degrees, vertical): <input type='text' id='maxEl' name='maxEl' value='" + maxElValue + "' style='width:80px;'><br><input type='submit' id='submit' value='Save calibration'></FORM></body></html>";

      server.send(200, "text/html", "success");
    } else {
      server.send(200, "text/html", "error");
    }
  });

  server.on("/storecalibration", []() {
    String MinAz = server.arg("minAz");
    String MaxAz = server.arg("maxAz");
    String MinEl = server.arg("minEl");
    String MaxEl = server.arg("maxEl");

    if (saveCalibration(MinAz, MaxAz, MinEl, MaxEl)) {
      error = "Settings saved!";
    } else {
      error = "Error! Could not save calibration values.";
    }
    server.send(200, "text/html", "<head><meta http-equiv=\"refresh\" content=\"0;url=./calibration\"></head>");
  });

  server.begin();
  server2.begin();

}  // End setup


/***************************************
 * Here comes all the various functions
 ***************************************/


bool wifiConnect(String ssid, String pass) {
  /* Connect to a Wifi network and returns true or false */
  if (ssid.length() > 0 && pass.length() > 0) {
    /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostName);  // Custom wifi device name, set in header
    WiFi.begin(ssid, pass);   //begin WiFi connection

    // Wait for connection
    int conn_count = 0;
    while (WiFi.status() != WL_CONNECTED && conn_count < 30) {
      delay(1000);
      conn_count++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      //Serial.println(WiFi.localIP());
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}


bool saveWifiCredentials(String ssid, String pass) {

  if (ssid.length() == 0 || pass.length() == 0) { return false; }

  // Clear EEPROM memory first
  for (int i = 0; i < 64; ++i) {
    EEPROM.write(i, 0);
  }

  // Save SSID
  if (ssid.length() < 10) {
    EEPROM.write(0, 0);
    EEPROM.write(1, ssid.length());
  } else {
    int firstchar = 0;
    int lastchar = 0;
    if (ssid.length() > 20) {
      /* 20-29 */
      firstchar = 2;
      lastchar = ssid.length() - 20;
    } else {
      /* 10-19 */
      firstchar = 1;
      lastchar = ssid.length() - 10;
    }
    EEPROM.write(0, firstchar);
    EEPROM.write(1, lastchar);
  }
  for (int i = 2; i < ssid.length() + 2; ++i) {
    int n = i - 2;
    EEPROM.write(i, ssid[n]);
  }

  // Save password
  if (pass.length() < 10) {
    EEPROM.write(32, 0);
    EEPROM.write(33, pass.length());
  } else {
    int firstchar = 0;
    int lastchar = 0;
    if (pass.length() > 19) {
      /* 20-29 */
      firstchar = 2;
      lastchar = pass.length() - 20;
    } else {
      /* 10-19 */
      firstchar = 1;
      lastchar = pass.length() - 10;
    }
    EEPROM.write(32, firstchar);
    EEPROM.write(33, lastchar);
  }
  for (int i = 34; i < pass.length() + 34; ++i) {
    int n = i - 34;
    EEPROM.write(i, pass[n]);
  }
  EEPROM.commit();

  return true;
}


String getWifiCredential(String value) { /* Fetch wifi login details stored in memory */
  if (value == "ssid") {
    if (EEPROM.read(0) + EEPROM.read(1) > 0) {
      //Reading saved wifi credentials
      // First two bytes contains length of string
      int len = char(EEPROM.read(0)) * 10 + char(EEPROM.read(1));
      len = len + 2;
      for (int i = 2; i < len; ++i) {
        ssid += char(EEPROM.read(i));
      }
      return ssid;
    } else {
      return "n/a";
    }

  } else if (value == "pass") {
    if (EEPROM.read(32) + EEPROM.read(33) > 0) {
      int len = char(EEPROM.read(32)) * 10 + char(EEPROM.read(33));
      len = len + 34;
      for (int i = 34; i < len; ++i) {
        pass += char(EEPROM.read(i));
      }
      return pass;
    } else {
      return "n/a";
    }
  }
  return "n/a";
}


bool saveCalibration(String MinAz, String MaxAz, String MinEl, String MaxEl) {
  /* Clear EEPROM memory first */
  for (int i = 64; i < 76; ++i) {
    EEPROM.write(i, 0);
  }

  /* TODO check for length of variables to fit inside max/min */
  if (MinAz.length() == 1) { MinAz = "00" + MinAz; }
  if (MinAz.length() == 2) { MinAz = "0" + MinAz; }
  if (MaxAz.length() == 1) { MaxAz = "00" + MaxAz; }
  if (MaxAz.length() == 2) { MaxAz = "0" + MaxAz; }
  if (MinEl.length() == 1) { MinEl = "00" + MinEl; }
  if (MinEl.length() == 2) { MinEl = "0" + MinEl; }
  if (MaxEl.length() == 1) { MaxEl = "00" + MaxEl; }
  if (MaxEl.length() == 2) { MaxEl = "0" + MaxEl; }

  if (MinAz.length() == 3) {
    EEPROM.write(64, char(MinAz[0]));
    EEPROM.write(65, char(MinAz[1]));
    EEPROM.write(66, char(MinAz[2]));
  } else {
    return false;
  }
  if (MaxAz.length() == 3) {
    EEPROM.write(67, MaxAz[0]);
    EEPROM.write(68, MaxAz[1]);
    EEPROM.write(69, MaxAz[2]);
  } else {
    return false;
  }
  if (MinEl.length() == 3) {
    EEPROM.write(70, MinEl[0]);
    EEPROM.write(71, MinEl[1]);
    EEPROM.write(72, MinEl[2]);
  } else {
    return false;
  }
  if (MaxEl.length() == 3) {
    EEPROM.write(73, MaxEl[0]);
    EEPROM.write(74, MaxEl[1]);
    EEPROM.write(75, MaxEl[2]);
  } else {
    return false;
  }

  EEPROM.commit();
  delay(10);
  minAzValue = MinAz.toInt();
  maxAzValue = MaxAz.toInt();
  minElValue = MinEl.toInt();
  maxElValue = MaxEl.toInt();

  return true;
}

void readCalibration(void) {
  String value = (String) char(EEPROM.read(64)) + char(EEPROM.read(65)) + char(EEPROM.read(66));
  if (value.toInt() > 30 && value.toInt() < 1000) { minAzValue = value.toInt(); }

  value = (String) char(EEPROM.read(67)) + char(EEPROM.read(68)) + char(EEPROM.read(69));
  if (value.toInt() > 30 && value.toInt() < 1000) { maxAzValue = value.toInt(); }

  value = (String) char(EEPROM.read(70)) + char(EEPROM.read(71)) + char(EEPROM.read(72));
  if (value.toInt() > 30 && value.toInt() < 1000) { minElValue = value.toInt(); }

  value = (String) char(EEPROM.read(73)) + char(EEPROM.read(74)) + char(EEPROM.read(75));
  if (value.toInt() > 30 && value.toInt() < 1000) { maxElValue = value.toInt(); }
}


void initializeRotator(void) { /* Test connection and function of motors and sensors */

  //  If connection is established with sensors
  if (readSensor("azimuth") > 10 && readSensor("elevation") > 10) {
    posAz = readPosition("azimuth");
    posEl = readPosition("elevation");
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
}


void readResetButton(void) { /* Check if reset button has been pressed */
  ResetState = digitalRead(Pin_Reset);
  if (ResetState == LOW) {
    delay(3000);
    ResetState = digitalRead(Pin_Reset);
    if (ResetState == LOW) {
      // turn LED on
      digitalWrite(Pin_LED, LOW);

      // Reset EEPROM (wifi configs only)
      for (int i = 0; i < 64; ++i) {
        EEPROM.write(i, 0);
      }
      EEPROM.commit();

      delay(5000);
      // Turn off LED after five seconds and restart
      digitalWrite(Pin_LED, HIGH);
      ESP.restart();
    }
  }
}


int readSensor(String value) { /* Read actual values "azimuth" or "elevation" from potentiometers */
  int reading = 1;
  if (value == "azimuth") {
    // azimuth
    digitalWrite(Pin_SensAz, HIGH);
    delay(5); /* time to normalize voltage before reading */
    reading = analogRead(0);
    for (int x = 0; x < 12; x++) {
      delay(5);
      reading = (reading * 0.6) + (analogRead(0) * 0.4); /* running average reduces sensor fluctuation */
    }
    digitalWrite(Pin_SensAz, LOW);
  } else if (value == "elevation") {
    // elevation
    digitalWrite(Pin_SensEl, HIGH);
    delay(5); /* time to normalize voltage before reading */
    reading = analogRead(0);
    for (int x = 0; x < 12; x++) {
      delay(5);
      reading = (reading * 0.6) + (analogRead(0) * 0.4); /* running average reduces sensor fluctuation */
    }
    digitalWrite(Pin_SensEl, LOW);
  }
  return reading;
}


int readPosition(String value) { /* Get position ("azimuth" or "elevation") in degrees */
  double readPos = 0;
  int reading = 0;
  if (value == "azimuth") {
    readAz = readSensor("azimuth");
    readPos = (readAz - minAzValue) * 360 / (maxAzValue - minAzValue);
    reading = round(readPos);
  } else if (value == "elevation") {
    readEl = readSensor("elevation");
    readPos = (readEl - minElValue) * 90 / (maxElValue - minElValue);
    reading = round(readPos);
  }
  return reading;
}


void turnMotors(int azTarget, int elTarget) { /* Move motor towards target in degrees */

  int azPos = posAz;
  int elPos = posEl;
  bool motionAz = false;
  bool motionEl = false;
  int newAz = readPosition("azimuth");
  int newEl = readPosition("elevation");

  // Enable motors
  analogWrite(Pin_PWM, 250);

  if (abs(newAz - posAz) < 40 && abs(newEl - posEl) < 20 && readAz > 10 && readEl > 10) {  // Dont move if measured sensors differs too much from last known position or lost connection

    // Set actual current position to the one measured and not the last known
    azPos = newAz;
    elPos = newEl;

    if (azPos < azTarget && abs(azPos - azTarget) > 4) {
      // turn right CW
      digitalWrite(Pin_Az1, HIGH);
      motionAz = true;
    } else if (abs(azPos - azTarget) > 4) {
      // turn left CCW
      digitalWrite(Pin_Az2, HIGH);
      motionAz = true;
    }

    if (elPos > elTarget && abs(elPos - elTarget) > 2) {
      // turn down
      digitalWrite(Pin_El1, HIGH);
      motionEl = true;
    } else if (abs(elPos - elTarget) > 2) {
      // turn up
      digitalWrite(Pin_El2, HIGH);
      motionEl = true;
    }
  }

  int n = 0;
  while (motionAz == true || motionEl == true) {
    if (motionAz == true) {
      newAz = readPosition("azimuth");
      if (abs(newAz - posAz) > 40) {  // If measured position differs more than 40 degrees from last read, stop! EMI or sensor error
        errorAz++;
        if (errorAz > 50) { error = "Azimuth sensor, read " + String(newAz) + " memory " + String(posAz); }
        digitalWrite(Pin_Az1, LOW);
        digitalWrite(Pin_Az2, LOW);
        motionAz = false;
      } else {
        errorAz = 0;
        if (azPos < azTarget && newAz > (azTarget - 5)) {
          digitalWrite(Pin_Az1, LOW);
          digitalWrite(Pin_Az2, LOW);
          motionAz = false;
        }
        if (azPos > azTarget && newAz < (azTarget + 5)) {
          digitalWrite(Pin_Az1, LOW);
          digitalWrite(Pin_Az2, LOW);
          motionAz = false;
        }
        posAz = newAz;
      }
    }

    if (motionEl == true) {
      newEl = readPosition("elevation");
      if (abs(newEl - posEl) > 15) {  // If measured position differs more than 10 degrees from last read, stop! EMI or sensor error
        errorEl++;
        if (errorEl > 60) { error = "Elevation sensor, read " + String(newEl) + " memory " + String(posEl); }
        digitalWrite(Pin_El1, LOW);
        digitalWrite(Pin_El2, LOW);
        motionEl = false;
      } else {
        errorEl = 0;
        if (elPos > elTarget && newEl < (elTarget + 3)) {
          digitalWrite(Pin_El1, LOW);
          digitalWrite(Pin_El2, LOW);
          motionEl = false;
        }
        if (elPos < elTarget && newEl > (elTarget - 3)) {
          digitalWrite(Pin_El1, LOW);
          digitalWrite(Pin_El2, LOW);
          motionEl = false;
        }
        posEl = newEl;
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
  digitalWrite(Pin_Az1, LOW);
  digitalWrite(Pin_Az2, LOW);
  digitalWrite(Pin_El1, LOW);
  digitalWrite(Pin_El2, LOW);
  analogWrite(Pin_PWM, 0); /* Disable the h-bridge */
}


/********************************
 * The main thread
********************************/

void loop(void) {
  server.handleClient();
  readResetButton();

  /* Hamlib/rotctl TCP socket */
  client1 = server2.available();
  while (client1 && client1.connected()) {
    rotctl = client1.readStringUntil('\n');
    Serial.println(rotctl);
    if (char(rotctl[0]) == 'p' || rotctl.indexOf("get_pos") > 0) {
      int azPos = readPosition("azimuth");
      int elPos = readPosition("elevation");
      if (azPos > -1 && azPos < 361 && elPos > -1 && elPos < 91) {
        client1.print(String(azPos) + ".0\n" + String(elPos) + ".0\n");
      } else {
        client1.print("RPRT -6\n");
      }
    }
    if (char(rotctl[0]) == 'P' || rotctl.indexOf("set_pos") > 0) {
      int divider;
      if (char(rotctl[0]) == 'P') {
        divider = 2;
      } else {
        divider = 8;
      }
      String part1 = rotctl.substring(divider, rotctl.indexOf(" ", divider));
      String part2 = rotctl.substring(rotctl.indexOf(" ", divider), rotctl.length());
      int newAz = part1.toInt();
      int newEl = part2.toInt();
      if (newAz > -1 && newAz < 361 && newEl > -1 && newEl < 91) {
        turnMotors(newAz, newEl);
        client1.print("RPRT 0\n");
      } else {
        client1.print("RPRT -6\n");
      }
    }
    if (char(rotctl[0]) == 'M' || rotctl.indexOf("move") > 0) { client1.print("RPRT -4\n"); /* command not supported by satran */ }
    if (char(rotctl[0]) == 'C' || rotctl.indexOf("set_conf") > 0) { client1.print("RPRT -4\n"); /* command not supported by satran */ }
    if (char(rotctl[0]) == 'S' || rotctl.indexOf("stop") > 0) { /* Stop */
      haltMotors();
      client1.print("RPRT 0\n");
    }
    if (char(rotctl[0]) == 'K' || rotctl.indexOf("park") > 0) { /* Park */
      initializeRotator();
      client1.print("RPRT 0\n");
    }
    if (char(rotctl[0]) == '_' || rotctl.indexOf("get_info") > 0) { client1.print("Info SATRAN v.2.1.1 az/el rotator\n"); }
    if (char(rotctl[0]) == 'R' || rotctl.indexOf("reset") > 0) {
      client1.stop();
      ESP.restart();
    }
    rotctl = "";
    delay(200);
  }
  client1.stop();
}
