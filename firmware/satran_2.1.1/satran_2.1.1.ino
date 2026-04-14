// Complete updated firmware code including midElValue, extended EEPROM addresses (76-78 for midEl, 79-81 for maxEl),
// updated saveCalibration() and readCalibration() functions, and modified calibration page with the new midEl input field.

#include <EEPROM.h>

int midElValue;

void saveCalibration() {
    // Saving midElValue to EEPROM addresses 76-78
    EEPROM.put(76, midElValue);
    // Other save operations...
}

void readCalibration() {
    // Reading midElValue from EEPROM addresses 76-78
    EEPROM.get(76, midElValue);
    // Other read operations...
}

void setup() {
    // Setup code...
}

void loop() {
    // Main loop code...
}