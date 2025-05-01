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


BLEScan *pBLEScan;

// The data in the BLE advertising broadcast from the SmartSolar device is encrypted by a 128-bit
// AES-CTR key. The key is created when you pair your SmartSolar device with the VictronConnect
// application; it's needed by this code in order to decrypt the BLE broadcast data.
//
// To obtain the key for your device, do the following (Apple iOS app; instructions for other
// platforms may differ):
//   1) If you haven't already, pair your SmartSolar device with the VictronConnect app.
//   2) Connect to the SmartSolar device in the application; you should see the device's "STATUS" page.
//   3) Touch the gear icon at the upper right to get to the "Settings" page.
//   4) Touch the three-vertical-dot icon at the upper right to get a popup menu; select "Product info".
//   5) Scroll to the bottom of the Product info page and ensure "Instant readout via Bluetooth" is enabled.
//   6) Touch the "SHOW" button in the "Encryption data" section; you'll get a popup that shows
//      the device's MAC address (informational; not used by this code) and the encryption key that
//      you need.
//   7) Touching the Encryption Key in the iOS app puts it into your paste buffer. Put it into a comment
//      in your source by pasting or by typing it manually. If typing it by hand, double-check your work
//      because this has to be EXACT (although case is unimportant).
//   8) Convert the hex string into an ESP32/C byte array by splitting it into two-character pairs, adding
//      commas and '0x' as appropriate, etc.
//
// Here's my copy-pasted Victron SmartSolar charge controller encryption key:
//
//   dc73cb155351cf950f9f3a958b5cd96f
//
// And then split the key into two-character pairs:
//
//   dc 73 cb 15 53 51 cf 95 0f 9f 3a 95 8b 5c d9 6f
//
// And finally, reformatted into the array definition needed by this code:
//
//victron 300A shunt 
//   dc 73 cb 15 53 51 cf 95 0f 9f 3a 95 8b 5c d9 6f
//   e0 9d 8b 20 0c 61 23 8c 81 1a 62 1e 59 64 c4 4e
//
uint8_t key[16]={
    0xe0, 0x9d, 0x8b, 0x20, 0x0c, 0x61, 0x23, 0x8c,
    0x81, 0x1a, 0x62, 0x1e, 0x59, 0x64, 0xc4, 0x4e
};

// Note: In my own (non-demo) code I paste the encryption key into a quoted character string
// and then use a function I wrote to convert it into the actual byte array. This saves me
// tedium and risk of mistakes in this reformat step. I didn't do that here so I could keep
// the code simple, so this is left as an excercise for the reader once you get things working.


int keyBits=128;  // Number of bits for AES-CTR decrypt.
int scanTime = 1; // BLE scan time (seconds)

char savedDeviceName[32];   // cached copy of the device name (31 chars max) + \0

// Victron docs on the manufacturer data in advertisement packets can be found at:
//   https://community.victronenergy.com/storage/attachments/48745-extra-manufacturer-data-2022-12-14.pdf
//


// Usage/style note: I use uint16_t in places where I need to force 16-bit unsigned integers
// instead of whatever the compiler/architecture might decide to use. I might not need to do
// the same with byte variables, but I'll do it anyway just to be at least a little consistent.




class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
      #define manDataSizeMax 31     // BLE specs say no more than 31 bytes, but see comments below!
      // See if we have manufacturer data and then look to see if it's coming from a Victron device.
      if (advertisedDevice.haveManufacturerData() == true) {
       
        // Here's the thing: BLE specs say our manufacturer data can be a max of 31 bytes.
        // But: The library code puts this data into a String, which we will then copy to
        // a character (i.e., byte) buffer using String.toCharArray(). Assuming we have the
        // full 31 bytes of manufacturer data allowed by the BLE spec, we'll need to size our
        // buffer with an extra byte for a null terminator. Our toCharArray() call will need
        // to specify *32* bytes so it will copy 31 bytes of data with a null terminator
        // at the end.
        //
        // Having said all that, I need to backtrack a bit. We're NOT going to be using String.toCharArray()
        // because it turns out that under some circumstances the manufacturer data may contain bytes
        // with a value of zero (0x00). A zero byte causes String.toCharArray() (as well as String.getBytes())
        // to terminate early and/or do other bizarre things that have the effect of corrupting the data.
        //
        // This was pointed out by surfermarty in Issue #6, and I verified the odd behavior by constructing a
        // String with embeded zero-valued bytes and trying various ways of copying the data.
        // As suggested by surfermarty, we can call String.c_str() to give us a pointer to the actual byte
        // data buried in the String object and then use memcpy() to copy the data to our own buffer that we can
        // access as intended.
        //
        // That being the case, we really don't need the +1 nonsense any more, other than it's there (for now)
        // to accomodate users who might be still on an older version of the ESP32 library code. At some point
        // I'll just remove the legacy compatibility and pare these comments down to just a discussion about using
        // String.c_str()+memcpy() vs the more obvious-but-broken String.toCharArray().
        //

        uint8_t manCharBuf[manDataSizeMax+1];

        #ifdef USE_String
          String manData = advertisedDevice.getManufacturerData();      // v3 lib code returns String.
        #else
          std::string manData = advertisedDevice.getManufacturerData(); // prior lib code returns std::string
        #endif
        int manDataSize=manData.length(); // This does not count the null at the end.
                                          // Note: I *think* this is the actual length of the data,
                                          // and not the count up to the first zero byte. (Otherwise
                                          // the following code wouldn't work right.)

        // Copy the data from the std::string or String object to a byte array.
        #ifdef USE_String
          // String.toCharArray won't work. See above!
          // manData.toCharArray((char *)manCharBuf,manDataSize+1);
          memcpy(manCharBuf,manData.c_str(),manDataSize);
        #else
          // I had +1 here... but shouldn't have. There was no reason to copy() an extra byte,
          // and I believe that could cause problems. I suppose someone still using the older ESP32 libraries
          // will tell me if I'm wrong about this.
          //manData.copy((char *)manCharBuf, manDataSize+1);
          manData.copy((char *)manCharBuf, manDataSize);
        #endif

        // Now let's setup a pointer to a struct to get to the data more cleanly.
        victronManufacturerData * vicData=(victronManufacturerData *)manCharBuf;

        // ignore this packet if the Vendor ID isn't Victron.
        if (vicData->vendorID!=0x02e1) {
          return;
        }

        // ignore this packet if it isn't type 0x01 (Solar Charger).
        if (vicData->victronRecordType != 0x01) {
          return;
        }

        // Not all packets contain a device name, so if we get one we'll save it and use it from now on.
        if (advertisedDevice.haveName()) {
          // This works the same whether getName() returns String or std::string.
          strcpy(savedDeviceName,advertisedDevice.getName().c_str());
        }
        
        if (vicData->encryptKeyMatch != key[0]) {
          Serial.printf("Packet encryption key byte 0x%2.2x doesn't match configured key[0] byte 0x%2.2x\n",
              vicData->encryptKeyMatch, key[0]);
          return;
        }

        uint8_t inputData[16];
        uint8_t outputData[16]={0};  // i don't really need to initialize the output.

        // The number of encrypted bytes is given by the number of bytes in the manufacturer
        // data as a whole minus the number of bytes (10) in the header part of the data.
        int encrDataSize=manDataSize-10;
        for (int i=0; i<encrDataSize; i++) {
          inputData[i]=vicData->victronEncryptedData[i];   // copy for our decrypt below while I figure this out.
        }

        esp_aes_context ctx;
        esp_aes_init(&ctx);

        auto status = esp_aes_setkey(&ctx, key, keyBits);
        if (status != 0) {
          Serial.printf("  Error during esp_aes_setkey operation (%i).\n",status);
          esp_aes_free(&ctx);
          return;
        }
        
        // construct the 16-byte nonce counter array by piecing it together byte-by-byte.
        uint8_t data_counter_lsb=(vicData->nonceDataCounter) & 0xff;
        uint8_t data_counter_msb=((vicData->nonceDataCounter) >> 8) & 0xff;
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

        // Now do our same struct magic so we can get to the data more easily.
        victronPanelData * victronData = (victronPanelData *) outputData;

        // Getting to these elements is easier using the struct instead of
        // hacking around with outputData[x] references.
        uint8_t deviceState=victronData->deviceState;
        uint8_t errorCode=victronData->errorCode;
        float batteryVoltage=float(victronData->batteryVoltage)*0.01;
        float batteryCurrent=float(victronData->batteryCurrent)*0.1;
        float todayYield=float(victronData->todayYield)*0.01*1000;
        uint16_t inputPower=victronData->inputPower;  // this is in watts; no conversion needed

        // Getting the output current takes some magic because of the way they have the
        // 9-bit value packed into two bytes. The first byte has the low 8 bits of the count
        // and the second byte has the upper (most significant) bit of the 9-bit value plus some
        // There's some other junk in the remaining 7 bits - i'm not sure if it's useful for
        // anything else but we can't use it here! - so we will mask them off. Then combine the
        // two bye components to get an integer value in 0.1 Amp increments.
        int integerOutputCurrent=((victronData->outputCurrentHi & 0x01)<<9) | victronData->outputCurrentLo;
        float outputCurrent=float(integerOutputCurrent)*0.1;

        // I don't know why, but every so often we'll get half-corrupted data from the Victron. As
        // far as I can tell it's not a decryption issue because we (usually) get voltage data that
        // agrees with non-corrupted records.
        //
        // Towards the goal of filtering out this noise, I've found that I've rarely (or never) seen
        // corrupted data when the 'unused' bits of the outputCurrent MSB equal 0xfe. We'll use this
        // as a litmus test here.
        uint8_t unusedBits=victronData->outputCurrentHi & 0xfe;
        if (unusedBits != 0xfe) {
          return;
        }

        Serial.printf("%-31s  Battery: %6.2f Volts %6.2f Amps  Solar: %6d Watts Yield: %6.0f Wh  Load: %6.1f Amps  State: %3d\n",
          savedDeviceName,
          batteryVoltage, batteryCurrent,
          inputPower, todayYield,
          outputCurrent, deviceState
        );
      }
    }
};
//   dc73cb155351cf950f9f3a958b5cd96f
// from next example for multiples.. 
// Split that up and turn it into an array whose equivalent definition would be like this:
//
//   byte key[]={ 0xdc, 0x73, 0xcb, ... 0xd9, 0x6f };
//
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



void BLEsetup()
{
  // Serial.begin(115200);
  // delay(1000);
  // Serial.println();
  // Serial.println();
  // Serial.println("Reset.");
  // Serial.println();
  // Serial.printf("Source file: %s\n",__FILE__);
  // Serial.printf(" Build time: %s\n",__TIMESTAMP__);
  // Serial.println();
  // delay(1000);

  Serial.print(F("Using encryption key: "));
  for (int i=0; i<16; i++) {
    Serial.printf(" %2.2x",key[i]);
  }
   Serial.println();
  // Serial.println();
  // Serial.println();
  
  strcpy(savedDeviceName,"(unknown device name)");

  // Code from Examples->BLE->Beacon_Scanner. This sets up a timed scan watching for BLE beacons.
  // During a scan the receipt of a beacon will trigger a call to MyAdvertisedDeviceCallbacks().
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value

  Serial.println(F("setup() complete."));
}
void BLEloop() {
  Serial.print(" BLE Scanning...");
/*  static double loopdelaytime;
  if (millis() <= loopdelaytime){return;}
  loopdelaytime=millis()+10000;*/
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
/
  pBLEScan->clearResults(); // delete results fromBLEScan buffer to release memory
  Serial.printf(" buffer cleared %i.\n",foundDevices); 
}

