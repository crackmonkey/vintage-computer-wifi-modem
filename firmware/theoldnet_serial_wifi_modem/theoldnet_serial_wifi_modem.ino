/*
   TheOldNet.com RS232 Serial WIFI Modem
   Copyright (C) 2020 Richard Bettridge (@theoldnet)

   based on https://github.com/jsalin/esp8266_modem

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <lwip/napt.h>
#include <lwip/dns.h>
#include <lwip/netif.h>
#include <netif/ppp/ppp.h>
#include <netif/ppp/pppos.h>

#include <IPAddress.h>
#include <ESP8266WiFi.h>

#include <EEPROM.h>
#include <ESP8266mDNS.h>

#define VERSIONA 0
#define VERSIONB 2
#define VERSION_ADDRESS 0    // EEPROM address
#define VERSION_LEN     2    // Length in bytesF
#define SSID_ADDRESS    2
#define SSID_LEN        32
#define PASS_ADDRESS    34
#define PASS_LEN        63
#define IP_TYPE_ADDRESS 97   // for future use
#define STATIC_IP_ADDRESS 98 // length 4, for future use
#define STATIC_GW       102  // length 4, for future use
#define STATIC_DNS      106  // length 4, for future use
#define STATIC_MASK     110  // length 4, for future use
#define BAUD_ADDRESS    111
#define ECHO_ADDRESS    112
#define SERVER_PORT_ADDRESS 113 // 2 bytes
#define AUTO_ANSWER_ADDRESS 115 // 1 byte
#define TELNET_ADDRESS  116     // 1 byte
#define VERBOSE_ADDRESS 117
#define FLOW_CONTROL_ADDRESS 119
#define PIN_POLARITY_ADDRESS 120
#define QUIET_MODE_ADDRESS 121
#define DIAL0_ADDRESS   200
#define DIAL1_ADDRESS   250
#define DIAL2_ADDRESS   300
#define DIAL3_ADDRESS   350
#define DIAL4_ADDRESS   400
#define DIAL5_ADDRESS   450
#define DIAL6_ADDRESS   500
#define DIAL7_ADDRESS   550
#define DIAL8_ADDRESS   600
#define DIAL9_ADDRESS   650
#define BUSY_MSG_ADDRESS 700
#define BUSY_MSG_LEN    80
#define LAST_ADDRESS    780

#define FLASH_BUTTON 0       // GPIO0 (programmind mode pin)
#define LED_PIN 16          // Status LED
#define LED_ESP_PIN 2
#define DCD_PIN 2          // DCD Carrier Status
//#define RTS_PIN 4         // RTS Request to Send, connect to host's CTS pin
//#define CTS_PIN 5         // CTS Clear to Send, connect to host's RTS pin


//Then when a DTE (such as a computer) wants to stop the data sending into it, it sets RTS to LOW. 
//The low state of the RTS (Request To Send) signal equal to -12V means "Do not send to me" (stop sending). 
//When the computer is ready to receive some bytes it sets RTS to HIGH (+12V) and the flow of bytes to it resumes. 
//Flow control signals are always sent in a direction opposite to the flow of bytes that is being controlled. 
//DCE equipment (modems) works the same way but sends the stop signal out the CTS pin. 
//If you don't need the flow control then you may not connect corresponding pins. In the most simple case you may connect 3 pins only.

//https://www.best-microcontroller-projects.com/how-rs232-works.html#handshake
//Looks like for bidirectional flow control DTR and DSR will be required. 
//!!! PIN 13 is labelled as CTS on the PCB and 15 as RTS. I believe these are backwards now by looking at a DTE to DCS straight through diagram
//I believe the hardware traces are correct:
//RTS 13 -> R2OUT -> R2IN -> DB9 PIN 7
//CTS 15 -> T2IN -> T2OUT -> DB9 PIN 8
//RTS IS INBOUND FROM PC
//CTS IS OUTBOUND FROM PC
#define RTS_PIN 13         // RTS Request to Send, connect to host's CTS pin RTS is DB9 PIN 7
#define CTS_PIN 15         // CTS Clear to Send, connect to host's RTS pin CTS is DB9 PIN 8

// Global variables
String build = "01102023";
String cmd = "";           // Gather a new AT command to this string from serial
bool cmdMode = true;       // Are we in AT command mode or connected mode
bool callConnected = false;// Are we currently in a call
bool telnet = false;       // Is telnet control code handling enabled
bool verboseResults = false;
bool firmwareUpdating = false;
//#define DEBUG 1          // Print additional debug information to serial channel
#undef DEBUG
#define LISTEN_PORT 23   // Listen to this if not connected. Set to zero to disable.
int tcpServerPort = LISTEN_PORT;
#define RING_INTERVAL 3000 // How often to print RING when having a new incoming connection (ms)
unsigned long lastRingMs = 0; // Time of last "RING" message (millis())
//long myBps;                // What is the current BPS setting
#define MAX_CMD_LENGTH 256 // Maximum length for AT command

#define LED_TIME 15         // How many ms to keep LED on at activity
unsigned long ledTime = 0;

const int speedDialAddresses[] = { DIAL0_ADDRESS, DIAL1_ADDRESS, DIAL2_ADDRESS, DIAL3_ADDRESS, DIAL4_ADDRESS, DIAL5_ADDRESS, DIAL6_ADDRESS, DIAL7_ADDRESS, DIAL8_ADDRESS, DIAL9_ADDRESS };
String speedDials[10];
const int bauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
byte serialspeed;
bool echo = true;
bool autoAnswer = false;
String ssid, password, busyMsg;
byte ringCount = 0;
String resultCodes[] = { "OK", "CONNECT", "RING", "NO CARRIER", "ERROR", "", "NO DIALTONE", "BUSY", "NO ANSWER" };
enum resultCodes_t { R_OK, R_CONNECT, R_RING, R_NOCARRIER, R_ERROR, R_NONE, R_NODIALTONE, R_BUSY, R_NOANSWER };
unsigned long connectTime = 0;
bool hex = false;
enum flowControl_t { F_NONE, F_HARDWARE, F_SOFTWARE };
byte flowControl = F_NONE;      // Use flow control
bool txPaused = false;          // Has flow control asked us to pause?
enum pinPolarity_t { P_INVERTED, P_NORMAL }; // Is LOW (0) or HIGH (1) active?
byte pinPolarity = P_NORMAL;
bool quietMode = false;

// Telnet codes
#define DO 0xfd
#define WONT 0xfc
#define WILL 0xfb
#define DONT 0xfe

WiFiClient tcpClient;
WiFiServer tcpServer(tcpServerPort);

MDNSResponder mdns;
ppp_pcb *ppp;
struct netif ppp_netif;

void sendResult(int resultCode) {
  Serial.print("\r\n");
  if (quietMode == 1) {
    return;
  }
  if (verboseResults == 0) {
    Serial.println(resultCode);
    return;
  }
  if (resultCode == R_CONNECT) {
    Serial.print(String(resultCodes[R_CONNECT]) + " " + String(bauds[serialspeed]));
  } else if (resultCode == R_NOCARRIER) {
    Serial.print(String(resultCodes[R_NOCARRIER]) + " (" + connectTimeString() + ")");
  } else {
    Serial.print(String(resultCodes[resultCode]));
  }
  Serial.print("\r\n");
}

void sendString(String msg) {
  Serial.print("\r\n");
  Serial.print(msg);
  Serial.print("\r\n");
}

void setCarrierDCDPin(byte carrier) {
  if (pinPolarity == P_NORMAL) carrier = !carrier;
  digitalWrite(DCD_PIN, carrier);
}

void waitForSpace() {
  Serial.print("PRESS SPACE");
  char c = 0;
  while (c != 0x20) {
    if (Serial.available() > 0) {
      c = Serial.read();
    }
  }
  Serial.print("\r");
}

void welcome() {
  Serial.println();
  Serial.println("TheOldNet.com");
  Serial.println("SERIAL WIFI MODEM EMULATOR");
  Serial.println("BUILD " + build + "");
  Serial.println("GPL3 GITHUB.COM/SSSHAKE/VINTAGE-COMPUTER-WIFI-MODEM");
  Serial.println();
}

/**
   Arduino main init function
*/
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // off
  
  pinMode(FLASH_BUTTON, INPUT);
  digitalWrite(FLASH_BUTTON, HIGH);

  // the esp fork of LWIP doesn't automatically init when enabling nat, so just do it in setup
  ip_napt_init(IP_NAPT_MAX, IP_PORTMAP_MAX);

  //why? was this part of the eeprom upgrade code or not, it preceeded the comments for it
  EEPROM.begin(LAST_ADDRESS + 1);
  delay(10);
  
  eepromUpgradeToDeprecate();
  
  readSettings();

  serialSetup();

  //waitForFirstInput();
  
  welcome();
  
  wifiSetup();

  webserverSetup();
  
}

void waitForFirstInput(){
  char c;
  unsigned long startMillis = millis();
  while (c != 8 && c != 127 && c!= 20) { // Check for the backspace key to begin
    while (c != 32) { // Check for space to begin
      //disabled the wait before welcome
      while (c != 0x0a && c != 0x0d) {
        if (Serial.available() > 0) {
          c = Serial.read();
        }
        if (checkButton() == 1) {
          break; // button pressed, we're setting to 300 baud and moving on
        }
        //if (millis() - startMillis > 2000) {
          //digitalWrite(LED_PIN, !digitalRead(LED_PIN));
          //startMillis = millis();
        //}
        yield();
      }
    }
  }
}

String ipToString(IPAddress ip) {
  char s[16];
  sprintf(s, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return s;
}

void hangUp() {
  if (ppp) {
    ppp_close(ppp, 0);
  } else {
    tcpClient.stop();
  }
  callConnected = false;
  setCarrierDCDPin(callConnected);
  sendResult(R_NOCARRIER);
  connectTime = 0;
}

void answerCall() {
  tcpClient = tcpServer.available();
  tcpClient.setNoDelay(true); // try to disable naggle
  //tcpServer.stop();
  sendResult(R_CONNECT);
  connectTime = millis();
  cmdMode = false;
  callConnected = true;
  setCarrierDCDPin(callConnected);
  Serial.flush();
}

void handleIncomingConnection() {
  if (callConnected == 1 || (autoAnswer == false && ringCount > 3)) {
    // We're in a call already or didn't answer the call after three rings
    // We didn't answer the call. Notify our party we're busy and disconnect
    ringCount = lastRingMs = 0;
    WiFiClient anotherClient = tcpServer.available();
    anotherClient.print(busyMsg);
    anotherClient.print("\r\n");
    anotherClient.print("CURRENT CALL LENGTH: ");
    anotherClient.print(connectTimeString());
    anotherClient.print("\r\n");
    anotherClient.print("\r\n");
    anotherClient.flush();
    anotherClient.stop();
    return;
  }

  if (autoAnswer == false) {
    if (millis() - lastRingMs > 6000 || lastRingMs == 0) {
      lastRingMs = millis();
      sendResult(R_RING);
      ringCount++;
    }
    return;
  }

  if (autoAnswer == true) {
    tcpClient = tcpServer.available();
    if (verboseResults == 1) {
      sendString(String("RING ") + ipToString(tcpClient.remoteIP()));
    }
    delay(1000);
    sendResult(R_CONNECT);
    connectTime = millis();
    cmdMode = false;
    tcpClient.flush();
    callConnected = true;
    setCarrierDCDPin(callConnected);
  }
}

// RTS/CTS protocol is a method of handshaking which uses one wire in each direction to allow each
// device to indicate to the other whether or not it is ready to receive data at any given moment.
// One device sends on RTS and listens on CTS; the other does the reverse. A device should drive
// its handshake-output wire low when it is ready to receive data, and high when it is not. A device
// that wishes to send data should not start sending any bytes while the handshake-input wire is low;
// if it sees the handshake wire go high, it should finish transmitting the current byte and then wait
// for the handshake wire to go low before transmitting any more.
// http://electronics.stackexchange.com/questions/38022/what-is-rts-and-cts-flow-control
void handleFlowControl() {
  if (flowControl == F_NONE) return;
  //disabled in case it's accidentally pausing
//  if (flowControl == F_HARDWARE) {
//    if (digitalRead(CTS_PIN) == pinPolarity) txPaused = true;
//    else txPaused = false;
//  }
  if (flowControl == F_SOFTWARE) {
    
  }
}

void handleCommandMode(){
// In command mode - don't exchange with TCP but gather characters to a string
    if (Serial.available())
    {
      char chr = Serial.read();

      // Convert uppercase PETSCII to lowercase ASCII (C64) in command mode only
      if ((chr >= 193) && (chr <= 218)) {
        chr-= 96;
      }

      // Return, enter, new line, carriage return.. anything goes to end the command
      if ((chr == '\n') || (chr == '\r'))
      {
        command();
      }
      // Backspace or delete deletes previous character
      else if ((chr == 8) || (chr == 127) || (chr == 20))
      {
        cmd.remove(cmd.length() - 1);
        if (echo == true) {
          Serial.write(chr);
        }
      }
      else
      {
        if (cmd.length() < MAX_CMD_LENGTH) cmd.concat(chr);
        if (echo == true) {
          Serial.write(chr);
        }
        if (hex) {
          Serial.print(chr, HEX);
        }
      }
    }
}

void restoreCommandModeIfDisconnected(){
    // Go to command mode if both TCP and PPP are disconnected and not in command mode
  if ((!tcpClient.connected() && ppp == NULL) && (cmdMode == false) && callConnected == true)
  {
    cmdMode = true;
    sendResult(R_NOCARRIER);
    connectTime = 0;
    callConnected = false;
    setCarrierDCDPin(callConnected);
    //if (tcpServerPort > 0) tcpServer.begin();
  }
}

/**
   Arduino main loop function
*/
void loop()
{
  if (firmwareUpdating == true){
    handleOTAFirmware();
    return;
  }
  
  handleFlowControl();
    
  handleWebServer();

  checkButton();

  // No idea what this is
  // New unanswered incoming connection on server listen socket
  if (tcpServer.hasClient()) {
    handleIncomingConnection();
  }

  if (cmdMode == true)
  {
    handleCommandMode();
  }
  else
  {
    handleConnectedMode();
  }

  restoreCommandModeIfDisconnected();

  handleLEDState();
}
