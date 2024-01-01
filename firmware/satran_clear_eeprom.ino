/********************************************
 * Satran ESP8266 EEPROM-wiper
 * Daniel Nikolajsen 2022 <satran@danaco.se>
 * www.satran.io
 * Creative Commons BY-NC-SA 2.0
 *******************************************/
 
 #include <EEPROM.h>

void setup() {
  // USED TO CLEAR THE EEPROM BEFORE FIRST FIRMWARE UPLOAD
  Serial.begin(9600); 
  EEPROM.begin(512);
  delay(100);
  for (int i = 0; i < 120; ++i) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit(); 
    delay(500);
  Serial.println("Memory cleared");

}

void loop() {

}
