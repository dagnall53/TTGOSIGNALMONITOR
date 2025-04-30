#ifndef eeprom_h
#define eeprom_h
#include <Arduino.h> // Include Arduino library
#include <EEPROM.h>
#include "Structures.h"

#define EEPROM_SIZE 512
//A structure EpromKEY,UDP_PORT,Mode,UDP_ON,Serial_on ESP_NOW_ON   to save stuff for eeprom and modes




bool EEPROM_WRITE(MySettings Settings ) {
  // save my settings
 // Serial.println("SAVING EEPROM");
  EEPROM.put(0, Settings);
  EEPROM.commit();
  delay(100);
  return true;
}
bool EEPROM_READ(MySettings &Settings) {
  EEPROM.begin(512);
  EEPROM.get(0, Settings);
  return true;
}

#endif