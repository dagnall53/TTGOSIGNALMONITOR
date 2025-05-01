/*************************************************************
  This sketch implements a simple serial receive terminal
  program for monitoring  messages

TTGO comments
Does not use hardware scroling
DO NOT FORGET TO CHANGE DISPLAY SELECT in User_Setup_Select.h !
#include <User_Setups/Setup25_TTGO_T_Display.h>    // Setup file for ESP32 and TTGO T-Display ST7789V SPI bus TFT
Or use UserSetup 

use one of the LARGE non ota Partition schemes if trying Victron ble

use esp32 dev module
no ota 2M app 


 *************************************************************/
#include <WiFi.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>  // Hardware-specific library  DO NOT FORGET TO CHANGE DISPLAY SELECT in User_Setup_Select.h !
#include <SPI.h>

//#include <WebServer.h>
// include library to set structures and read and write from flash memory
#include "EEPROM.h"

#define On_Off ? "ON " : "OFF" 
#include "ESP_NOW_files.h" //  #ifndef ESP_NOW_FILES_H #include <esp_now.h>

extern uint8_t* espnowchannel;
extern wifi_second_chan_t*  secondch; 

#include "VICTRONBLE.h" //sets #ifndef Victronble_h

TFT_eSPI tft = TFT_eSPI();  // Invoke custom library

// The scrolling area must be a integral multiple of TEXT_HEIGHT
#define TEXT_HEIGHT 16     // Height of text in pixels  to be printed and scrolled
#define BOT_FIXED_AREA 1   // Number of Text Lines in bottom fixed area
#define TOP_FIXED_AREA 16  // Number of pixels lines in top fixed area

#define YMAX 135  // (size of) Bottom of screen in pixels
#define XMAX 240  // (size of) width of screen in pixels
#define Project "DMon"
#define BUTTON1PIN 35
#define BUTTON2PIN 0
bool ButtonPressed = false;
bool ModeUpdateFinished = true;
int Button_pressed;

#define BAUD_RATE 115200

WiFiUDP Udp;
char UDPPORT[10];  // to pass value from wifimanager
int UDP_PORT;      // see Default_Settings for default


// The initial y coordinate of the top of the scrolling area
uint16_t yStart = TOP_FIXED_AREA;
// yArea must be a integral multiple of TEXT_HEIGHT
uint16_t yBottom = YMAX - (BOT_FIXED_AREA * TEXT_HEIGHT);
uint8_t NumberoftextLines = (YMAX - TOP_FIXED_AREA - (BOT_FIXED_AREA * TEXT_HEIGHT)) / TEXT_HEIGHT;
// The initial y coordinate of the top main text line
uint16_t yDraw = yStart + TEXT_HEIGHT;

// Keep track of the drawing x coordinate
uint16_t xPos = 0;
uint8_t WriteLine;  // line of text (0.. )
// For the byte we read from the serial port
byte data = 0;

// A few test variables used during debugging
bool change_colour = 1;
bool selected = 1;

// We have to blank the top line each time the display is scrolled, but this takes up to 13 milliseconds
// for a full width line, meanwhile the serial buffer may be filling... and overflowing
// We can speed up scrolling of short text lines by just blanking the character we drew
int blank[19];  // We keep all the strings pixel lengths to optimise the speed of the top line blanking

MySettings Default_Settings = { 2, 2000, 1, false, true, true,true, 2, "GUESTBOAT", "12345678" };
MySettings Saved_Settings;
MySettings Current_Settings;




//*********** Button stuff *****************
void IRAM_ATTR toggleButton1() {
  ButtonPressed = true;
  Button_pressed = 1;
}

void IRAM_ATTR toggleButton2() {
  ButtonPressed = true;
  Button_pressed = 2;
}


#define BufferLength 500
char nmea_1[500];
char nmea_U[BufferLength];  // NMEA buffer for UDP input port
// *************  ESP-NOW variables and functions in ESP_NOW_files************
char nmea_EXT[500];
bool EspNowIsRunning = false;
// byte peerAddress[6];
// const byte peerAddress_def[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };  // all receive
// esp_now_peer_info_t peerInfo;
// bool EspNowIsRunning = false;
// unsigned long Last_EXT_Sent;


void StartTFT() {
  // Setup the TFT display
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.fillRect(0, 0, XMAX, 16, TFT_BLUE);
  tft.setCursor(0, 0, 2);
  tft.println(" Access AP to setup:");
}

void connectwithsettings() {
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(Current_Settings.ssid, Current_Settings.password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  StartTFT();
  EEPROM_READ(Saved_Settings);  // setup and read eeprom into Saved_Settings
  dataline(Saved_Settings, "Saved");
  dataline(Default_Settings, "Default");
  Current_Settings = Saved_Settings;
  if (Current_Settings.EpromKEY != 128) {  // change for clean start
    Current_Settings = Default_Settings;
    EEPROM_WRITE(Current_Settings);
  }
  dataline(Current_Settings, "currently ");


  pinMode(BUTTON1PIN, INPUT);
  pinMode(BUTTON2PIN, INPUT);
  attachInterrupt(BUTTON1PIN, toggleButton1, FALLING);
  attachInterrupt(BUTTON2PIN, toggleButton2, FALLING);
 
  connectwithsettings();
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.fillRect(0, 0, XMAX, 16, TFT_BLUE);
  tft.setCursor(0, 0, 2);
  tft.printf("%s:", Project);
  tft.print(WiFi.SSID());
  tft.print(" ");
  tft.println(WiFi.localIP());
  Serial.printf(" *Running with:  ssid<%s> psk<%s> Ch<%i>\n", WiFi.SSID(), WiFi.psk(),WiFi.channel());
  strcpy(Current_Settings.ssid, WiFi.SSID().c_str());
  strcpy(Current_Settings.password, WiFi.psk().c_str());
  // Current_Settings.ssid=WiFi.SSID().c_str();
  // Current_Settings.password=WiFi.psk().c_str();
  WriteLine = 1;

  Udp.begin(Current_Settings.UDP_PORT);
  #ifdef ESP_NOW_FILES_H
  Serial.println("Setting ESP-NOW");
  tft.println("\nStarting ESP_now");
  if (Start_ESP_EXT()) {Serial.printf("   Success to add peer channel<%i>",espnowchannel);tft.println("   Success to add peer"); }
  else{     Serial.println("   Failed to add peer");  tft.println("   Failed to add peer");}
  if (EspNowIsRunning) {   Serial.printf("   ESP-now running  channel<%i>",espnowchannel); tft.println("ESP-NOW Init Success");}
  else {    Serial.println("   ESP-NOW Init Failed"); tft.println("ESP-NOW Init Failed");}
  #endif
  Serial.print("   Mac Address: ");
  Serial.println(WiFi.macAddress());
  tft.println(WiFi.macAddress());
  //Show current housekeeping settings
  dataline(Current_Settings, "SET");
#ifdef Victronble_h
  BLEsetup();
#endif
  // Change colour for  text zone
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

void ModeFunctions() {
  static double loopdelaytime;
  if (millis() <= loopdelaytime){return;}
  loopdelaytime=millis()+1000;
  if (ButtonPressed && ModeUpdateFinished) {
    ModeUpdate(Button_pressed);
    ButtonPressed = false;
  };
  // V simple configure to allow both buttons to trigger EEPROM save and  WiFiManager reset
  if ((digitalRead(BUTTON1PIN) == LOW) && (digitalRead(BUTTON2PIN) == LOW)) {
    Serial.println("Reset EEPROM");
    DrawON_line(0, "Reset EEprom: Hold for WIFi", 3, TFT_BLUE);
    Current_Settings = Default_Settings;
    EEPROM_WRITE(Current_Settings);

    delay(3000);  // wait before testing pins again reset delay hold
    if ((digitalRead(BUTTON1PIN) == LOW) && (digitalRead(BUTTON2PIN) == LOW)) {
      DrawON_line(3, "Button Held", 2, TFT_BLUE);
      delay(1000);
      DrawON_line(4, "Erasing Config, restarting", 2, TFT_BLUE);
      delay(1000);
      // tbd 
      ESP.restart();
    }
  }
}

void ModeUpdate(uint8_t button) {// wraps around varions esp/serial text size settings
  if (ButtonPressed) {
    ModeUpdateFinished = false;  // token to prevent multiple calls
    WriteLine = 0;
    switch (button) {
     case 1:
        Current_Settings.Mode = Current_Settings.Mode + 1;
      if (Current_Settings.Mode > 10) { Current_Settings.Mode = 0; }
     break;
     case 2:
             Current_Settings.Mode = Current_Settings.Mode - 1;
      if (Current_Settings.Mode < 0) { Current_Settings.Mode = 10; }
     break;
    }

    if (Current_Settings.Mode > 10) { Current_Settings.Mode = 10; }
    if (Current_Settings.Mode < 0) { Current_Settings.Mode = 0; }
    //change display
    //tft.printf("Button %d Pressed! Current_Settings.Mode: %d",button,Current_Settings.Mode);

       switch (Current_Settings.Mode) {
             case 0:
      Current_Settings.Serial_on = false;
      Current_Settings.UDP_ON = false;
      Current_Settings.ESP_NOW_ON = true;
       Current_Settings.Victron_ON = true;
      Current_Settings.ListTextSize = 2;
     break;
     case 1:
           Current_Settings.Serial_on = false;
      Current_Settings.UDP_ON = false;
      Current_Settings.ESP_NOW_ON = true;
       Current_Settings.Victron_ON = false;
      Current_Settings.ListTextSize = 2;

     break;
     case 2:
      Current_Settings.Serial_on = false;
      Current_Settings.UDP_ON = true;
      Current_Settings.ESP_NOW_ON = true;
      Current_Settings.Victron_ON = true;
      Current_Settings.ListTextSize = 2;

     break;

     case 3:
      Current_Settings.Serial_on = false;
      Current_Settings.UDP_ON = true;
      Current_Settings.ESP_NOW_ON = false;
       Current_Settings.Victron_ON = true;
      Current_Settings.ListTextSize = 1;

     break;

      case 4:
      Current_Settings.Serial_on = false;
      Current_Settings.UDP_ON = true;
      Current_Settings.ESP_NOW_ON = true;
       Current_Settings.Victron_ON = false;
      Current_Settings.ListTextSize = 1;

     break;

      case 5:
            Current_Settings.Serial_on = false;
      Current_Settings.UDP_ON = false;
      Current_Settings.ESP_NOW_ON = true;
       Current_Settings.Victron_ON = true;
      Current_Settings.ListTextSize = 2;

     break;

      case 6:
      Current_Settings.Serial_on = false;
      Current_Settings.UDP_ON = true;
      Current_Settings.ESP_NOW_ON = true;
       Current_Settings.Victron_ON = true;
      Current_Settings.ListTextSize = 2;
     break;

      case 7:
       Current_Settings.Serial_on = true;
      Current_Settings.UDP_ON = true;
      Current_Settings.ESP_NOW_ON = true;
       Current_Settings.Victron_ON = true;
      Current_Settings.ListTextSize = 2;

     break;

      case 8:

     break;

      case 9:

     break;
      case 10:
      Current_Settings.Serial_on = true;
      Current_Settings.UDP_ON = true;
      Current_Settings.ESP_NOW_ON = true;
       Current_Settings.Victron_ON = true;
      Current_Settings.ListTextSize = 2;

     break;

    }
    


    // fill in bottom line of setup
    dataline(Current_Settings, "ModeUpdate");
    ButtonPressed = false;
    EEPROM_WRITE(Current_Settings);  // nothing fancy, just save it
    ModeUpdateFinished = true;
  }
}

void dataline(MySettings A, String Text) {
  tft.fillRect(0, TOP_FIXED_AREA, XMAX, YMAX - TOP_FIXED_AREA, TFT_BLACK);
  tft.setCursor(0, yBottom);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.printf("%d, %d S<%d>", A.EpromKEY, A.Mode, A.Serial_on);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.printf("U<%d><%d>", A.UDP_PORT, A.UDP_ON);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.printf("E<%d>", A.ESP_NOW_ON);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.printf("Vi<%d>", A.Victron_ON);
  Serial.printf("%d Dataline display %s: Mode<%d> Ser<%d> UDPPORT<%d> UDP<%d>  ESP<%d>  Victron<%d>", A.EpromKEY, Text, A.Mode, A.Serial_on, A.UDP_PORT, A.UDP_ON, A.ESP_NOW_ON,A.Victron_ON);
  Serial.print("SSID <");
  Serial.print(A.ssid);
  Serial.print(">  Password <");
  Serial.print(A.password);
  Serial.println("> ");
}
boolean CompStruct(MySettings A, MySettings B) {  // does not check ssid and password
  bool same = false;
  // have to check each variable individually
  if (A.EpromKEY == B.EpromKEY) { same = true; }
  if (A.UDP_PORT == B.UDP_PORT) { same = true; }
  if (A.UDP_ON == B.UDP_ON) { same = true; }
  if (A.ESP_NOW_ON == B.ESP_NOW_ON) { same = true; }
  if (A.Serial_on == B.Serial_on) { same = true; }
  if (A.Mode == B.Mode) { same = true; }
  if (A.ListTextSize == B.ListTextSize) { same = true; }
  return same;
}

void DrawON_line(int Line, String text, uint8_t font, uint32_t TEXT_Colour) {
  int32_t ypos = Line * TEXT_HEIGHT;
  tft.setTextColor(TEXT_Colour, TFT_BLACK);
  tft.fillRect(0, ypos, XMAX, TEXT_HEIGHT, TFT_BLACK);
  tft.drawString(text, 0, ypos, font);
}

void ShowData(char* buf, uint8_t font, uint32_t TEXT_Colour) {  // show and reset buf[0] to zero indicating data used
  if (buf[0] == 0) {return;}
 // if (strlen(buf)<=4) {buf[0]=0; return;} // only accept lines with >4 characters ?
    WriteLine = WriteLine + 1;
    if (WriteLine * font > (NumberoftextLines * 2)) { WriteLine = 1; }
    int32_t ypos = 8 + (WriteLine * font * (TEXT_HEIGHT / 2));
    tft.setTextColor(TEXT_Colour, TFT_BLACK);
    tft.fillRect(0, ypos, XMAX, font * (TEXT_HEIGHT / 2), TFT_BLACK);
    tft.drawString(buf, 0, ypos, font); delay(10);
    if (TEXT_Colour == TFT_GREEN) {
      Serial.printf("esp_now :%s", buf);
      buf[0] = 0;
      return;
    }
    if (TEXT_Colour == TFT_BLUE) {
      Serial.printf("UDP     :%s", buf);
      buf[0] = 0;
      return;
    }
    if (TEXT_Colour == TFT_WHITE) {
      Serial.printf("Serial  :%s", buf);
      buf[0] = 0;
      return;
    }
}


void TestInputsOutputs() {
  if (Current_Settings.ESP_NOW_ON) {while (UpdateEspNow()) {ShowData(nmea_EXT, Current_Settings.ListTextSize, TFT_GREEN);} }
  if (Current_Settings.Serial_on) { Test_Serial_1();ShowData(nmea_1, Current_Settings.ListTextSize, TFT_WHITE); }
  if (Current_Settings.UDP_ON) {Test_U(); ShowData(nmea_U, Current_Settings.ListTextSize, TFT_BLUE);
  }
}

void loop(void) {
  //ESPEssentials::handle();
  //EventTiming("START");
//victron test
#ifdef Victronble_h
  if(Current_Settings.Victron_ON){BLEloop();}
#endif 
#ifdef ESP_NOW_FILES_H
  EXTHeartbeat();
  #endif
  TestInputsOutputs();
 
  ModeFunctions();
}



void Test_Serial_1() {  // UART0 port P1
  static bool LineReading_1 = false;
  static int Skip_1 = 1;
  static int i_1;
  byte b;
  if (nmea_1[0]==0) {                  // ONLY get characters if we are NOT still processing the last message!
    while (Serial.available()) {  // get the character
      b = Serial.read();
      if (LineReading_1 == false) {
        nmea_1[0] = b;
        i_1 = 1;
        LineReading_1 = true;
      }  // Place first character of line in buffer location [0]
      else {
        nmea_1[i_1] = b;
        i_1 = i_1 + 1;
        if (b == 0x0A) {       //0A is LF
          nmea_1[i_1] = 0x00;  // put end in buffer.
          LineReading_1 = false;
          return;
        }
        if (i_1 > 150) {
          LineReading_1 = false;
          i_1 = 0;
          return;
        }
      }
    }
  }
}

void Test_U() {  // check if udp packet  has arrived
  static int Skip_U = 1;
  if (nmea_U[0]==0) {  // only process if we have dealt with the last line.
    nmea_U[0] = 0x00; // redundant 
    int packetSize = Udp.parsePacket();
    if (packetSize) {  // Deal with UDP packet
      if (packetSize >= (BufferLength + 4)) {
        Udp.flush();
        return;
      }  // Simply discard if too long
      int len = Udp.read(nmea_U, BufferLength);
      byte b = nmea_U[0];
      nmea_U[len] = 0;
      
    }  // udp PACKET DEALT WITH
  }
}


