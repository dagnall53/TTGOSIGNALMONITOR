
#include "ESP_NOW_files.h"
#include <esp_wifi.h>
#include <esp_now.h>
#include <Arduino.h>  // for the char ? definition?
//byte peerAddress[6];
//const byte peerAddress_def[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };  // all receive
esp_now_peer_info_t peerInfo;
//bool EspNowIsRunning = false;
byte peerAddress[6];
const byte peerAddress_def[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };  // all receive
unsigned long Last_EXT_Sent;                                            // used to time a heartbeat

uint8_t* espnowchannel;
wifi_second_chan_t* secondch;
/* values 
 enumerator WIFI_SECOND_CHAN_NONE  the channel width is HT20
 enumerator WIFI_SECOND_CHAN_ABOVE  the channel width is HT40 and the secondary channel is above the primary channel
 enumerator WIFI_SECOND_CHAN_BELOW the channel width is HT40 and the secondary channel is below the primary channel
 */


bool Start_ESP_EXT() {  // start espnow and run Test_EspNOW() when data is seen / sent
  bool success = false;
  EspNowIsRunning = false;
  memcpy(peerInfo.peer_addr, peerAddress_def, 6);
  peerInfo.encrypt = false;
  peerInfo.channel = 0;
  if (esp_now_init() == ESP_OK) { EspNowIsRunning = true; }
  esp_now_register_recv_cb(Test_EspNOW);
  if (esp_now_add_peer(&peerInfo) == ESP_OK) { success = true; }
  esp_wifi_get_channel(espnowchannel, secondch);
  return success;
}
bool donotdisturb;
char nmea_ext_buffer[1000];
char space[10];
//functions "esp_now"  this function accepts data on interrupts and saves in nmea_EXT and (used to! )set 'line_EXT'
void Test_EspNOW(const uint8_t* mac, const uint8_t* incomingData, int len) {
  EspNowIsRunning = true;
  char rxdata[249];  //incoming ESP seem to be always 248 long, so make sure we are big enough
  if (!donotdisturb) {
    if (strlen(nmea_ext_buffer) <= 752) {  // add the esp now to the buffer .. if we have room
      memcpy(&rxdata, incomingData, sizeof(rxdata));
      strcat(nmea_ext_buffer, rxdata);
    }
  }
}

void DebugBufChars(const char* msg, char* buf, int i) {
  Serial.printf("\n %s strlen %i Chars at %i are..i-2..[]...i+4 <%i><%i>[%i]<%i><%i><%i><%i><%i>\n", msg,
                strlen(buf), i, buf[i - 2], buf[i - 1], buf[i], buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4], buf[i + 5]);
}
bool UpdateEspNow() {
  bool _gotFirstLine;
  int offset = 0;
  //   / DO NOT WANT interrupt to corrupt/add to nmea_ext_buffer whilst we are fiddling with it
  //DebugBufChars("NMEABUFFER start", nmea_ext_buffer, 2);
  if (nmea_EXT[0] == 0) {  // nmea_EXT EMPTY..  get another line from buffer?
    if (nmea_ext_buffer[0] != 0) {
      _gotFirstLine = false;
      donotdisturb = true;
      offset = 0;
      for (int i = 0; i <= sizeof(nmea_ext_buffer); i++) {
        if (_gotFirstLine) {
          nmea_ext_buffer[offset] = nmea_ext_buffer[i];
          offset++;
        }  // copy the rest back into the buffer, but shifted 'forwards/left'
        else {
          nmea_EXT[i] = nmea_ext_buffer[i];                                                           // will be this on first loop..(!_gotfirstLine)  // build nmea_EXT..
          if (nmea_ext_buffer[i] == 0x0A) {                                                           //DebugBufChars("at 0A found:",nmea_ext_buffer, i); // got end of line..
            _gotFirstLine = true, nmea_EXT[i + 1] = 0;                                                //nmea_EXT[i+2] = 0;nmea_EXT[i+3] = 0;nmea_EXT[i+4] = 0;// unneeded extra eof 0's?
            if ((nmea_ext_buffer[i + 1] == 0x0D) && (nmea_ext_buffer[i + 2] == 0x0A)) { i = i + 2; }  // sometimes we get CRLFCRLF.. ignore it
          }
        }
      }
      nmea_ext_buffer[offset] = 0;
    }
    // if (strlen(nmea_ext_buffer)<=4){nmea_ext_buffer[0]=0;} // remove any strange remaining bits
    //DebugBufChars("NMEABUFFER after", nmea_ext_buffer, 2);
    //DebugBufChars("NMEAExT starts",nmea_EXT, 2); // show start of nmea_EXT
    //donotdisturb = false;
    //return _gotFirstLine;
  }
  donotdisturb = false;
  return _gotFirstLine;
}
void EXTHeartbeat() {
  if (!EspNowIsRunning) { return; }
  if (Last_EXT_Sent + 10000 <= millis()) {                        // 10 sec.
                                                                  //Serial.println("Sending heartbeat");
    Last_EXT_Sent = millis();                                     // but we also update Last_EXT_Sent in the EXT send..
                                                                  //EXTSENDf("_%s_:ch%d_\r\n", Project, WiFi.channel());
                                                                  // BUT We must send with a \r\n!!
    EXTSENDf("_TTGO_Disp_CH<%i>_Heartbeat_\r\n", espnowchannel);  // if using in library does not have access to wifi project etc  data
  }
}
void EXTSEND(const char* buf) {  // same format as TCP send etc..  83 size for NMEA 250 max for ESP-NOW link  NOT BIG ENOUGH FOR SOME RAW!!
  if (!EspNowIsRunning) { return; }
  char myData[248];                       // max size of esp_now_send messages
  strcpy(myData, buf);                    // to avoid overflowing in this function..?
  strcat(myData, "\r\n");                 // Adding CRLF wass eqivalent of Println.. and we now "print" everywhere
  if (strlen(myData) >= 247) { return; }  // // sendAdvice("Message too big for ESP_EXT link");}but test before going further!
  if (strlen(myData) <= 5) { return; }    // should never be less than 5 !! Just to try to catch extra CRLF's ?
  // sendAdvicef(" esp-now Sending: (%s) Wifich<%i>  peer_ch<%i> ", myData, WiFi.channel(), peerInfo.channel);
  //sendAdvicef(" esp-now Sending: (length %i) Wifich<%i>  peer_ch<%i> ", strlen(myData), WiFi.channel(), peerInfo.channel);
  Last_EXT_Sent = millis();
  esp_err_t result = esp_now_send(peerInfo.peer_addr, (uint8_t*)&myData, sizeof(myData));
  //if (result == ESP_OK) {Serial.println(" esp-now SENT");}// <%s> (%i long) in msg max [%i] long \n",myData,strlen(myData), sizeof(myData));}else{Serial.println(" ESP failed to send");}
  //if (result != ESP_OK) {EspNowIsRunning=false;}   // but then we need something in Loop to re-establish the link ??
}

void EXTSENDf(const char* fmt, ...) {  //complete object type suitable for holding the information needed by the macros va_start, va_copy, va_arg, and va_end.
  if (!EspNowIsRunning) { return; }
  static char msg[300] = { '\0' };  // used in message buildup
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, 128, fmt, args);
  va_end(args);
  // add checksum?
  int len = strlen(msg);
  EXTSEND(msg);
}
