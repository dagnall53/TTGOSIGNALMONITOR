/*
  from https://github.com/hoberman/Victron_BLE_Advertising_example

  Initial BLE code adapted from Examples->BLE->Beacon_Scanner.
  Victron decryption code snippets from:
  
    https://github.com/Fabian-Schmidt/esphome-victron_ble

  Information on the "extra manufacturer data" that we're picking up from Victron SmartSolar
  BLE advertising beacons can be found at:
  
    https://community.victronenergy.com/storage/attachments/48745-extra-manufacturer-data-2022-12-14.pdf
  
  Thanks, Victron, for providing both the beacon and the documentation on its contents!
*/ 
#include <Arduino_GFX_Library.h> 
// also includes Arduino etc, so variable names are understood
#include "Structures.h"
#include "VICTRONBLE.h"

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// The Espressif people decided to use String instead of std::string in newer versions
// (3.0 and later?) of their ESP32 libraries. Check your BLEAdvertisedDevice.h file to see
// if this is the case for getManufacturerData(); if so, then uncomment this line so we'll
// use String code in the callback.  

//#define USE_String  for version 3 compiler!

#include <aes/esp_aes.h>        // AES library for decrypting the Victron manufacturer data.
 extern char VICTRONRES[];   // to get the data out as a string char 

BLEScan *pBLEScan;


#define AES_KEY_BITS 128

int scanTime = 1; // BLE scan time (seconds)

char savedDeviceName[32];   // cached copy of the device name (31 chars max) + \0

// Victron docs on the manufacturer data in advertisement packets can be found at:
//   https://community.victronenergy.com/storage/attachments/48745-extra-manufacturer-data-2022-12-14.pdf
//

// extra braces around each "designated initializer" element needed by some compiler versions.
victronDevice victronDevices[] = {
  { { .charMacAddr = "ea9df3ebc625" }, { .charKey = "e09d8b200c61238c811a621e5964c44e" }, { .comment = "300A" } },
  { { .charMacAddr = "f944913298e8" }, { .charKey = "40ef2093aa678238147091c7657daa54" }, { .comment = "dummy" } },
  { { .charMacAddr = "cc5b284e8ae6" }, { .charKey = "2b6d51d4a74c3b83749303d87fa17bd9" }, { .comment = "dummy2" } }
};
int  knownvictronDeviceCount = sizeof(victronDevices) / sizeof(victronDevices[0]);

int bestRSSI = -200;
int selectedvictronDeviceIndex = -1;

time_t lastLEDBlinkTime=0;
time_t lastTick=0;
int displayRotation=3;
bool packetReceived=false;

char chargeStateNames[][6] = {
  "  off",
  "   1?",
  "   2?",
  " bulk",
  "  abs",
  "float",
  "   6?",
  "equal"
};
byte hexCharToByte(char hexChar) {
  if (hexChar >= '0' && hexChar <='9') {          // 0-9
    hexChar=hexChar - '0';
  } else if (hexChar >= 'a' && hexChar <= 'f') {   // a-f
    hexChar=hexChar - 'a' + 10;
  } else if (hexChar >= 'A' && hexChar <= 'F') {  // A-F
    hexChar=hexChar - 'A' + 10;
  } else {
    hexChar=255;
  }
  return hexChar;
}
void hexCharStrToByteArray(char * hexCharStr, byte * byteArray) {
  bool returnVal=false;

  int hexCharStrLength=strlen(hexCharStr);

  // There are simpler ways of doing this without the fancy nibble-munching,
  // but I do it this way so I parse things like colon-separated MAC addresses.
  // BUT: be aware that this expects digits in pairs and byte values need to be
  // zero-filled. i.e., a MAC address like 8:0:2b:xx:xx:xx won't come out the way
  // you want it.
  int byteArrayIndex=0;
  bool oddByte=true;
  byte hiNibble;
  for (int i=0; i<hexCharStrLength; i++) {
    byte nibble=hexCharToByte(hexCharStr[i]);
    if (nibble!=255) {
      if (oddByte) {
        hiNibble=nibble;
      } else {
        byteArray[byteArrayIndex]=(hiNibble<<4) | nibble;
        byteArrayIndex++;
      }
      oddByte=!oddByte;
    }
  }
  // do we have a leftover nibble? I guess we'll assume it's a low nibble?
  if (! oddByte) {
    byteArray[byteArrayIndex]=hiNibble;
  }
}

// read https://github.com/hoberman/Victron_BLE_Scanner_Display/blob/main/BLE_Adv_Callback.ino for comments. 
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {

      #define manDataSizeMax 31     // BLE specs say no more than 31 bytes, but see comments below!
      // See if we have manufacturer data and then look to see if it's coming from a Victron device.
      if (advertisedDevice.haveManufacturerData() == true) {
      
        uint8_t manCharBuf[manDataSizeMax+1];

        #ifdef USE_String
          String manData = advertisedDevice.getManufacturerData();      // lib code returns String.
        #else
          std::string manData = advertisedDevice.getManufacturerData(); // lib code returns std::string
        #endif
        int manDataSize=manData.length(); // This does not include a null at the end.

        // Limit size just in case we get a malformed packet.
        if (manDataSize > manDataSizeMax) {
          Serial.printf("  Note: Truncating malformed %2d byte manufacturer data to max %d byte array size\n",manDataSize,manDataSizeMax);
          manDataSize=manDataSizeMax;
        }
        // Now copy the data from the String to a byte array. Must have the +1 so we
        // don't lose the last character to the null terminator.
        #ifdef USE_String
          //manData.toCharArray((char *)manCharBuf,manDataSize+1);
          memcpy(manCharBuf,manData.c_str(),manDataSize);
        #else
          //manData.copy((char *)manCharBuf, manDataSize + 1);
          manData.copy((char *)manCharBuf, manDataSize);
        #endif
                // Now let's use a struct to get to the data more cleanly.
        victronManufacturerData * vicData=(victronManufacturerData *)manCharBuf;
        // ignore this packet if the Vendor ID isn't Victron.
        if (vicData->vendorID!=0x02e1) {
          return;
        }
        // ignore this packet if it isn't KNOWN ? (was based originally on only Solar Charger
        // type 0x01 (Solar Charger).
        // smartshunt is 0x10 according to AI on google! int 2 is the actual reported returned value
        int KnownDataType;
        KnownDataType=-1;

        if (vicData->victronRecordType == 0x01) {KnownDataType=1;} //Solar Charger
        if (vicData->victronRecordType == 2) {KnownDataType=2;}    //smart shunt
        if (KnownDataType==-1){
          Serial.printf("*** Victron device recordtype <%i> seen, not understood\n",vicData->victronRecordType);
          return;
        }
        // Get the MAC address of the device we're hearing, and then use that to look up the encryption key
        // for the device.
        //
        // We go through a bit of trouble here to turn the String MAC address that we get from the BLE
        // code ("08:00:2b:xx:xx:xx") into a byte array. I'm (hobermann) divided on this... I could have just (and still might!)
        // left this as a string and just done a strcmp() match. This would have saved me some coding and execution time
        // in exchange for having to format the MAC addresses in my victronDevices list using the embedded colons.
        char receivedMacStr[18];
        strcpy(receivedMacStr,advertisedDevice.getAddress().toString().c_str());
       //  Serial.printf("received mac string <%s> \n",receivedMacStr);
        byte receivedMacByte[6];
        hexCharStrToByteArray(receivedMacStr,receivedMacByte);

        int victronDeviceIndex=-1;
        //Serial.printf("   becomes macaddress(BYTES) %X %X %X %X %X %X  \n",receivedMacByte[0],receivedMacByte[1],receivedMacByte[2],receivedMacByte[3],receivedMacByte[4],receivedMacByte[5]);
        for (int tryvictronDeviceIndex=0; tryvictronDeviceIndex<knownvictronDeviceCount; tryvictronDeviceIndex++) {
       /*     Serial.printf("   checking index:%i has macaddress(BYTES) %X %X %X %X %X %X  \n",tryvictronDeviceIndex,
            victronDevices[tryvictronDeviceIndex].byteMacAddr[0],
            victronDevices[tryvictronDeviceIndex].byteMacAddr[1],
            victronDevices[tryvictronDeviceIndex].byteMacAddr[2],
            victronDevices[tryvictronDeviceIndex].byteMacAddr[3],
            victronDevices[tryvictronDeviceIndex].byteMacAddr[4],
            victronDevices[tryvictronDeviceIndex].byteMacAddr[5]);
       */     
          bool matchedMac=true;
          for (int i=0; i<6; i++) {
            if (receivedMacByte[i] != victronDevices[tryvictronDeviceIndex].byteMacAddr[i]) {
              matchedMac=false;
              break;
            }
          }
          if (matchedMac) {
            victronDeviceIndex=tryvictronDeviceIndex;
            //Serial.printf(" Got index <%i> to this device from my list\n",victronDeviceIndex);
            break;
          }
        }
        
        // Get the device name (if there's one in this packet).
        char deviceName[32]; // 31 characters + \0
        strcpy(deviceName,"(unknown device name)");
        bool deviceNameFound=false;

        if (advertisedDevice.haveName()) {
          // This works the same whether getName() returns String or std::string.
          strcpy(deviceName,advertisedDevice.getName().c_str());
          //Serial.printf(" rx device name <%s>",deviceName);
          deviceNameFound=true;
        }
        // We didn't do this test earlier because we might want to print out a name - if we got one.
        if (victronDeviceIndex == -1) {
          Serial.printf("Discarding packet from unconfigured Victron device %s at MAC %s\n",deviceName,receivedMacStr);
          //delay(5000);
          return;
        }
        // If we found a device name, cache it for later display.
        if (deviceNameFound) {
          strcpy(victronDevices[victronDeviceIndex].cachedDeviceName,deviceName);
        }

        // The manufacturer data from Victron contains a byte that's supposed to match the first byte
        // of the device's encryption key. If they don't match, when we don't have the right key for
        // this device and we just have to throw the data away. ALTERNATELY, we can go ahead and decrypt
        // the data - incorrectly - and use the crazy values to indicate that we have a problem.


        if (vicData->encryptKeyMatch != victronDevices[victronDeviceIndex].byteKey[0]) {
          Serial.printf("Encryption key mismatch for %s at MAC %s\n",
            victronDevices[victronDeviceIndex].cachedDeviceName,receivedMacStr);
          return;
        }

        // Get the signal strength (RSSI) of the beacon.
        int RSSI=advertisedDevice.getRSSI();
        //Serial.printf(" RSSI: %i \n",RSSI);
        // Now that the packet received has met all the criteria for being displayed,
        // let's decrypt and decode the manufacturer data.

        byte inputData[16];
        byte outputData[16]={0};
        victronPanelData * victronData = (victronPanelData *) outputData;

        // The number of encrypted bytes is given by the number of bytes in the manufacturer
        // data as a while minus the number of bytes (10) in the header part of the data.
        int encrDataSize=manDataSize-10;
        for (int i=0; i<encrDataSize; i++) {
          inputData[i]=vicData->victronEncryptedData[i];   // copy for our decrypt below while I figure this out.
        }

        esp_aes_context ctx;
        esp_aes_init(&ctx);

        auto status = esp_aes_setkey(&ctx, victronDevices[victronDeviceIndex].byteKey, AES_KEY_BITS);
        if (status != 0) {
          Serial.printf("  Error during esp_aes_setkey operation (%i).\n",status);
          esp_aes_free(&ctx);
          return;
        }
        
        byte data_counter_lsb=(vicData->nonceDataCounter) & 0xff;
        byte data_counter_msb=((vicData->nonceDataCounter) >> 8) & 0xff;
        u_int8_t nonce_counter[16] = {data_counter_lsb, data_counter_msb, 0};
        u_int8_t stream_block[16] = {0};

        size_t nonce_offset=0;
        status = esp_aes_crypt_ctr(&ctx, encrDataSize, &nonce_offset, nonce_counter, stream_block, inputData, outputData);
        if (status != 0) {
          Serial.printf("Error during esp_aes_crypt_ctr operation (%i).",status);
          esp_aes_free(&ctx);
          return;
        }
        esp_aes_free(&ctx);

        byte deviceState=victronData->deviceState;  // this is really more like "Charger State"
        byte errorCode=victronData->errorCode;
        float batteryVoltage=float(victronData->batteryVoltage)*0.01;  //OK for smart shunt as well 
        float batteryCurrent=float(victronData->batteryCurrent)*0.1;  // not battery current on smart shunt.. 
        float todayYield=float(victronData->todayYield)*0.01*1000; // solar charger use 
        float starterBattery=float(victronData->todayYield)*0.01;// starter battery for Shunt 
        uint16_t inputPower=victronData->inputPower;   
        int16_t temp= ~victronData->inputPower +1; // 2's complement using magic and two steps!!
        float shuntCurrent= float(temp)*-0.00025;// did not (could not!!) do 2's complement conversion all in one. emperical /4000 for 300A shunt.
                   // Getting the (solar charger) output current takes some magic.
        int integerOutputCurrent=((victronData->outputCurrentHi & 0x01)<<9) | victronData->outputCurrentLo;
        float outputCurrent=float(integerOutputCurrent)*0.1;
               // The Victron docs say Device State but it's really a Charger State.
        char chargeStateName[6];
        sprintf(chargeStateName,"%4d?",deviceState);
        if (deviceState >=0 && deviceState<=7) {
          strcpy(chargeStateName,chargeStateNames[deviceState]);
        }
       Serial.printf("Charge state DS<%i> and error state ec<%x>  charge state %s\n",deviceState,errorCode,chargeStateName);
        if (KnownDataType==2) {Serial.printf("%s SHUNT %2.3Fv  %2.3FA  sb%2.2Fv\n",deviceName,batteryVoltage,shuntCurrent,starterBattery);}
        if (KnownDataType==1) {Serial.printf("%s SOLAR %2.3Fv  %2.3FA  output current%2.2FA\n",deviceName,batteryVoltage,batteryCurrent,outputCurrent);}


        /*  uint8_t deviceState;
  uint8_t errorCode;
  int16_t batteryVoltage;
  int16_t batteryCurrent;
  uint16_t todayYield;
  uint16_t inputPower;
  uint8_t outputCurrentLo;  // Low 8 bits of output current (in 0.1 Amp increments)
  uint8_t outputCurrentHi;  // High 1 bit of ourput current (must mask off unused bits)
  uint8_t unused[4];*/
  #if debug
 Serial.printf(" RAW DATA EC<%X> DS<%X><%s>  BV<%X> BC<%X> TY<%X> IP<%X> OCL<%X> OCH<%X> unused<%X> \n",
  victronData->errorCode,
  victronData->deviceState,chargeStateName,
  victronData->batteryVoltage,
  victronData->batteryCurrent,
  victronData->todayYield,
  victronData->inputPower,
  victronData->outputCurrentLo,  // Low 8 bits of output current (in 0.1 Amp increments)
  victronData->outputCurrentHi,  // High 1 bit of ourput current (must mask off unused bits)
  victronData->unused[4]);      
 #endif 

        // Serial.printf("%-31s Battery: %6.2f Volts %6.2f Amps  \nSolar: %6d Watts  Yield: %4.0f Wh  \nLoad: %5.1f Amps  Charger: %-13s Err: %2d RSSI: %d\n",
        //   victronDevices[victronDeviceIndex].cachedDeviceName,
        //   batteryVoltage, batteryCurrent,
        //   inputPower, todayYield,
        //   outputCurrent, chargeStateName, errorCode, RSSI
        // );
        // for smartshunt Batt voltage = batteryVoltage CURRENT = ->inputPower
       if (VICTRONRES[0]==0) {snprintf(VICTRONRES,120, "VictronData,%i,%s,%2.3Fv,%2.3FA,%2.2Fv,%2.3F,%2.3F\n",KnownDataType,deviceName, batteryVoltage,shuntCurrent,starterBattery,batteryCurrent,outputCurrent);}
           



        packetReceived=true;
      }
    }
};







void BLEsetup() {
  Serial.printf("Controller count: %d\n", knownvictronDeviceCount);

   for (int i = 0; i < knownvictronDeviceCount; i++) {
    hexCharStrToByteArray(victronDevices[i].charMacAddr, victronDevices[i].byteMacAddr);
    hexCharStrToByteArray(victronDevices[i].charKey, victronDevices[i].byteKey);
    strcpy(victronDevices[i].cachedDeviceName, "(unknown)");
  }

  for (int i = 0; i < knownvictronDeviceCount; i++) {
    Serial.printf("  %-16s", victronDevices[i].comment);
    Serial.printf("  Mac:   ");
    for (int j = 0; j < 6; j++) {
      Serial.printf(" %2.2x", victronDevices[i].byteMacAddr[j]);
    }

    Serial.printf("    Key: ");
    for (int j = 0; j < 16; j++) {
      Serial.printf("%2.2x", victronDevices[i].byteKey[j]);
    }
    Serial.println();
  }
  Serial.println();
  Serial.println();

  delay(2000);
   BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but gets results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value


  Serial.println(F(" BLE setup() complete."));
}
void BLEloop() {
 // Serial.print(" BLE Scanning...");
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory

}

