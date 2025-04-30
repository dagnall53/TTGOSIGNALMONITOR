
#ifndef Victronble_h
#define Victronble_h
#include <Arduino.h> // Include Arduino library

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// onResult includes serial print of results but will be changed once I understand it..
// class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks 
//   {public:
//   void onResult(BLEAdvertisedDevice advertisedDevice);
//   };

void BLEsetup();  // called from main void setup();
void BLEloop();   // called from main void loop() 

#endif