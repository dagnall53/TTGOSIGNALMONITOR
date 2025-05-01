#ifndef STRUCTURES_H
#define STRUCTURES_H


struct MySettings {
  int EpromKEY;  // to allow check for clean EEprom and no data stored
  int UDP_PORT;
  int Mode;
  bool UDP_ON;
  bool Serial_on;
  bool ESP_NOW_ON;
  bool Victron_ON;
  uint8_t ListTextSize;  // fontsize for listed text (2 is readable, 1 gives more lines)// all off
  char ssid[16];
  char password[16];
};




// Must use the "packed" attribute to make sure the compiler doesn't add any padding to deal with
// word alignment.

// // victron ble structures


// typedef struct {                       // ONLY FOR SOLAR Controllers??
//   uint8_t deviceState;
//   uint8_t errorCode;
//   int16_t batteryVoltage;
//   int16_t batteryCurrent;
//   uint16_t todayYield;
//   uint16_t inputPower;
//   uint8_t outputCurrentLo;  // Low 8 bits of output current (in 0.1 Amp increments)
//   uint8_t outputCurrentHi;  // High 1 bit of ourput current (must mask off unused bits)
//   uint8_t unused[4];
// } __attribute__((packed)) victronPanelData;

// typedef struct {
//   uint16_t vendorID;                 // vendor ID
//   uint8_t beaconType;                // Should be 0x10 (Product Advertisement) for the packets we want
//   uint8_t unknownData1[3];           // Unknown data
//   uint8_t victronRecordType;         // Should be 0x01 (Solar Charger) for the packets we want
//   uint16_t nonceDataCounter;         // Nonce
//   uint8_t encryptKeyMatch;           // Should match pre-shared encryption key byte 0
//   uint8_t victronEncryptedData[21];  // (31 bytes max per BLE spec - size of previous elements)
//   uint8_t nullPad;                   // extra byte because toCharArray() adds a \0 byte.
// } __attribute__((packed)) victronManufacturerData;


// typedef struct {
//   char charMacAddr[13];       // 12 character MAC + \0 (initialized as quoted strings below for convenience)
//   char charKey[33];           // 32 character keys + \0 (initialized as quoted strings below for convenience)
//   char comment[16];           // 16 character comment (name) for printing during setup()
//   byte byteMacAddr[6];        // 6 bytes for MAC - initialized by setup() from quoted strings
//   byte byteKey[16];           // 16 bytes for encryption key - initialized by setup() from quoted strings
//   char cachedDeviceName[32];  // 31 characters + \0 (filled in as we receive advertisements)
// } victronDevice;



// char chargeStateNames[][6] = {
//   "  off",
//   "   1?",
//   "   2?",
//   " bulk",
//   "  abs",
//   "float",
//   "   6?",
//   "equal"
// };


#endif