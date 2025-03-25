/*
  WiFiTelnetToSerial - Example Transparent UART to Telnet Server for ESP32

  Copyright (c) 2017 Hristo Gochkov. All rights reserved.
  This file is part of the ESP32 WiFi library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WebSerial.h>
#include <ESPAsyncWebServer.h>
#include <TimeLib.h>
#include <ElegantOTA.h>
#include <Elog.h>
#include <ElogMacros.h>
#include <ESPNexUpload.h>
#include <HTTPClient.h>
#include "soc/rtc_wdt.h"

const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;


// PaperTrail
#define PAPERTRAIL_HOST "logs6.papertrailapp.com"
#define PAPERTRAIL_PORT 21858
#define PL_LOG 0  // PoolMaster Logs
#define WD_LOG 1  // WatchDog Logs

//how many clients should be able to telnet to this ESP32
#define MAX_SRV_CLIENTS 2
#define BUFFER_SIZE 400
#define LOG_BUFFER_SIZE 400

// Upgrade triggers
volatile bool mustUpgradeNextion = false;

// Upgrade binary storage (HTTP Server)
const char* upgrade_host          = "192.168.86.250:8123";
const char* upgrade_url_nextion   = "/local/poolmaster/Nextion.tft";
const char* upgrade_url_esp       = "/local/poolmaster/PoolMaster.bin";
const char* upgrade_url_watchdog  = "/local/tft/WatchDog.bin";

// Nextion upgrade counter for feedback
int upgradeCounter = 0;
int contentLength = 0;

// Nextion PIN Numbers
#define NEXT_RX 33 // Nextion RX pin
#define NEXT_TX 32 // Nextion TX pin

// PoolMaster PIN Numbers
#define ENABLE_PIN  25
#define BOOT_PIN    26
// Enable and Boot pin numbers
const int ENPin = 25;
const int BOOTPin = 26;

const char *ssid = "CasaParigi";
const char *password = "Elsa2011Andrea2017Clara2019";

WiFiMulti wifiMulti;
WiFiServer Telnetserver(23);
WiFiClient serverClients[MAX_SRV_CLIENTS];
AsyncWebServer Webserver(80);
//AsyncWebSocket websock("/ws");

char sbuf[BUFFER_SIZE];
char local_sbuf[LOG_BUFFER_SIZE];

// OTA
unsigned long ota_progress_millis = 0;



//////////////////// HELPER FUNCTIONS ///////////////////
/////////////////////////////////////////////////////////
//Compute free RAM
//useful to check if it does not shrink over time
int freeRam () {
  int v = xPortGetFreeHeapSize();
  return v;
}

// Get current free stack 
unsigned stack_hwm(){
  return uxTaskGetStackHighWaterMark(nullptr);
}

// Monitor free stack (display smallest value)
void stack_mon(UBaseType_t &hwm)
{
  UBaseType_t temp = uxTaskGetStackHighWaterMark(nullptr);
  if(!hwm || temp < hwm)
  {
    hwm = temp;
    Serial.printf("[stack_mon] %s: %d bytes",pcTaskGetTaskName(NULL), hwm);
  }  
}

/*! Thread safe, memory safe non blocking read until delimiter is found.
 *
 * \param stream Stream.
 * \param buf Receiving buffer.
 * \param delim Delimeter.
 * \param i Buffer index.
 * \param j Delimiter index.
 *
 * \return true when delimiter is found or the buffer is full, false otherwise.
 */
template <size_t n> bool readUntil_r(
  Stream& stream, char (&buf)[n], char const* delim, size_t& i, size_t& j) {
for (; i < n and delim[j]; i++) {
  if (not stream.available()) {
    return false;
  }
  buf[i] = stream.read();

  if (buf[i] == delim[j]) {
    j++;
  }
  else {
    j = 0;
  }
}
for (; i < n; i++) {
  buf[i] = 0;
}
i = 0;
j = 0;
return true;
}

/*! Memory safe non blocking read until delimiter is found.
*
* \param stream Stream.
* \param buf Receiving buffer.
* \param delim Delimeter.
*
* \return true when delimiter is found or the buffer is full, false otherwise.
*/
template <size_t n> bool readUntil(Stream& stream, char (&buf)[n], char const* delim) {
  static size_t i = 0;
  static size_t j = 0;
  return readUntil_r(stream, buf, delim, i, j);
}

void Local_Logs_Dispatch(const char *_log_message, uint8_t _targets)
{

  // Telnet
  for (int i = 0; i < MAX_SRV_CLIENTS; i++) {
    if (serverClients[i] && serverClients[i].connected()) {
      serverClients[i].write(_log_message, strlen(_log_message));
      delay(1);
    }
  }

  // WebSerial
  WebSerial.printf("%s",_log_message);

  // Cloud PaperTrail
  logger.log(WD_LOG, 1, "%s", _log_message);
}


//////////////////////// UPGRADE NEXTION //////////////////////////
////////////////////////////////////////////////////////////
// ----------------------------------------------------------------------------
// NEXTION UPGRADE FUNCTION initialization
// ----------------------------------------------------------------------------
void TaskUpgradeNextion(void *pvParameters)
{
  for (;;) {
    if(mustUpgradeNextion)
    {
      mustUpgradeNextion = false;
      rtc_wdt_protect_off();
      rtc_wdt_disable();
      Local_Logs_Dispatch("Nextion Upgrade Requested");
      WebSerial.printf("Stopping PoolMaster...");
      pinMode(ENPin, OUTPUT);
      digitalWrite(ENPin, LOW);
      WebSerial.printf("Upgrading Nextion ...");
   
      HTTPClient http;
      
      // begin http client
        if(!http.begin(String("http://") + upgrade_host + upgrade_url_nextion)){
        WebSerial.printf("Connection failed");
        return;
      }
    
      WebSerial.printf("Requesting URL: %s",upgrade_url_nextion);
    
      // This will send the (get) request to the server
      int code          = http.GET();
      contentLength = http.getSize();
        
      // Update the nextion display
      if(code == 200){
        WebSerial.printf("File received. Update Nextion...");
        bool result;

        // initialize ESPNexUpload
        ESPNexUpload nextion(115200);
        // set callback: What to do / show during upload..... Optional! Called every 2048 bytes
        upgradeCounter=0;
        nextion.setUpdateProgressCallback([](){
          upgradeCounter++;
          snprintf(local_sbuf,sizeof(local_sbuf),"Nextion Upgrade Progress %f4.1\r\n",(float)((upgradeCounter*2048/contentLength)*100));
          Local_Logs_Dispatch(local_sbuf;3);
        });
        // prepare upload: setup serial connection, send update command and send the expected update size
        result = nextion.prepareUpload(contentLength);
        
        if(!result){
            WebSerial.printf("Error: %s",nextion.statusMessage.c_str());
            //Serial.println("Error: " + nextion.statusMessage);
        }else{
            WebSerial.printf("Start upload. File size is: %d bytes",contentLength);
            
            // Upload the received byte Stream to the nextion
            result = nextion.upload(*http.getStreamPtr());
            
            if(result){
              WebSerial.printf("Successfully updated Nextion");
            }else{
              WebSerial.printf("Error updating Nextion: %s",nextion.statusMessage.c_str());
            }

            // end: wait(delay) for the nextion to finish the update process, send nextion reset command and end the serial connection to the nextion
            nextion.end();
            pinMode(NEXT_RX,INPUT);
            pinMode(NEXT_TX,INPUT);
        }
        
      }else{
        // else print http error
        WebSerial.printf("HTTP error: %d",http.errorToString(code).c_str());
      }

      http.end();
      WebSerial.printf("Closing connection");
      WebSerial.printf("Starting PoolMaster ...");
      digitalWrite(ENPin, HIGH);
      pinMode(ENPin, INPUT);
      //rtc_wdt_enable();
      //rtc_wdt_protect_on();
    }
  }
}


//////////////////////// ELEGANT OTA //////////////////////////
////////////////////////////////////////////////////////////
void onOTAStart() {
  // Log when OTA has started
  Local_Logs_Dispatch("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    snprintf(local_sbuf,sizeof(local_sbuf),"OTA Progress Current: %u bytes, Final: %u bytes\n\r", current, final);
    Local_Logs_Dispatch(local_sbuf);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Local_Logs_Dispatch("OTA update finished successfully!");
  } else {
    Local_Logs_Dispatch("There was an error during OTA update!");
  }
  // <Add your own code here>
}

void initElegantOTA() {
  ElegantOTA.begin(&Webserver);    // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
}

//////////////////////// COMMANDS //////////////////////////
////////////////////////////////////////////////////////////
void cmdExecute(char _command) {
  //snprintf(local_sbuf,sizeof(local_sbuf),"Command Arrived %s",_command);
  //Local_Logs_Dispatch(local_sbuf);
  switch (_command) {
    case 'R': // WatchDog Reboot
      delay(100);
      ESP.restart();
    break;
    case 'P':  // PoolMaster Stop
      Local_Logs_Dispatch("Stopping PoolMaster ...");
      pinMode(ENPin, OUTPUT);
      digitalWrite(ENPin, LOW);
    break;
    case 'Q':  // PoolMaster Start
      Local_Logs_Dispatch("Starting PoolMaster ...");
      if(digitalRead(ENPin)==LOW)
      {
        pinMode(ENPin, OUTPUT);
        digitalWrite(ENPin, HIGH);
        pinMode(ENPin, INPUT);
      }
    break;

    case 'S':  // PoolMaster Upgrade

    break;
    case 'T':  // Nextion Upgrade
      WebSerial.print("Request Upgrade Nextion");
      mustUpgradeNextion = true;
    break;
  }
}

//////////////////////// WEBSERIAL //////////////////////////
////////////////////////////////////////////////////////////
// ----------------------------------------------------------------------------
// Message Callback WebSocket
// ----------------------------------------------------------------------------
void recvMsg(uint8_t *data, size_t len) {
  //WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  //cmdExecute(data[0]);
  //WebSerial.println(d);
}

//////////////////////// SETUP //////////////////////////
/////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);

  wifiMulti.addAP(ssid, password);
  //wifiMulti.addAP("ssid_from_AP_2", "your_password_for_AP_2");
  //wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");

  //Serial.println("Connecting Wifi ");
  for (int loops = 10; loops > 0; loops--) {
    if (wifiMulti.run() == WL_CONNECTED) {
      Local_Logs_Dispatch("");
      Local_Logs_Dispatch("WiFi connected ");
      //snprintf(local_sbuf,sizeof(local_sbuf),"OTA Progress Current: %u bytes, Final: %u bytes\n\r", current, final);

      Local_Logs_Dispatch("IP address: ");
      Serial.println(WiFi.localIP());
      break;
    } else {
      Serial.println(loops);
      delay(1000);
    }
  }
  if (wifiMulti.run() != WL_CONNECTED) {
    Serial.println("WiFi connect failed");
    delay(1000);
    ESP.restart();
  }

  // Config NTP
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  time_t now = time(nullptr);
  while (now < SECS_YR_2000) {
    delay(100);
    now = time(nullptr);
  }
  setTime(now);

  initElegantOTA();

  // Start WebSerial
  WebSerial.begin(&Webserver);
  WebSerial.onMessage(&recvMsg); /* Attach Message Callback */
  Webserver.onNotFound([](AsyncWebServerRequest* request) { request->redirect("/webserial"); });

  Webserver.begin();
  
  //Connect to serial receiving messages from PoolMaster
  Serial2.begin(115200);
  Serial2.setTimeout(100);
  
  //Start Telnet Server
  Telnetserver.begin();
  Telnetserver.setNoDelay(true);

  Serial.print("Ready! Use 'telnet ");
  Serial.print(WiFi.localIP());
  Serial.println(" 23' to connect");

  // Start PaperTrail Logging
  logger.configureSyslog(PAPERTRAIL_HOST, PAPERTRAIL_PORT, ""); // Syslog server IP, port and device name
  //logger.registerSerial(COUNTER, DEBUG, "COUNT", Serial); // Log both to serial...
  logger.registerSyslog(PL_LOG, DEBUG, FAC_LOCAL0, "poolmaster"); // ...and syslog. Set the facility to user
  logger.registerSyslog(WD_LOG, DEBUG, FAC_LOCAL0, "watchdog"); // ...and syslog. Set the facility to user
  logger.log(WD_LOG, INFO, "PoolMaster Logs Started");
  logger.log(WD_LOG, INFO, "WatchDog Logs Started");

  xTaskCreate(
    TaskUpgradeNextion, "Task Upgrade Nextion",
    7000 // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,
    NULL  // No parameter is used
    ,
    1  // Priority
    ,
    nullptr  // Task handle is not used here
  );
}

//////////////////////// MAIN LOOP //////////////////////////
/////////////////////////////////////////////////////////////
void loop() {
  uint8_t i;
  if (wifiMulti.run() == WL_CONNECTED) {
    //check if there are any new clients
    if (Telnetserver.hasClient()) {
      for (i = 0; i < MAX_SRV_CLIENTS; i++) {
        //find free/disconnected spot
        if (!serverClients[i] || !serverClients[i].connected()) {
          if (serverClients[i]) {
            serverClients[i].stop();
          }
          serverClients[i] = Telnetserver.accept();
          if (!serverClients[i]) {
            Serial.println("available broken");
          }
          Serial.print("New client: ");
          Serial.print(i);
          Serial.print(' ');
          Serial.println(serverClients[i].remoteIP());
          break;
        }
      }
      if (i >= MAX_SRV_CLIENTS) {
        //no free/disconnected spot so reject
        Telnetserver.accept().stop();
      }
    }
    //check clients for data
    for (i = 0; i < MAX_SRV_CLIENTS; i++) {
      if (serverClients[i] && serverClients[i].connected()) {
        if (serverClients[i].available()) {
          //get data from the telnet client and push it to the UART
          while (serverClients[i].available()) {
            //Serial.write(serverClients[i].read());
            cmdExecute(serverClients[i].read());
          }
        }
      } else {
        if (serverClients[i]) {
          serverClients[i].stop();
        }
      }
    }
    //check UART for data
    if (Serial2.available()) {
      if (readUntil(Serial2, sbuf, "\n")) {
        // `buf` contains the delimiter, it can now be used for parsing.      
        //push UART data to all connected telnet clients
        for (i = 0; i < MAX_SRV_CLIENTS; i++) {
          if (serverClients[i] && serverClients[i].connected()) {
            serverClients[i].write(sbuf, strlen(sbuf));
            delay(1);
          }
        }

        // Cleanup buffer and send to the web
        for (int src = 0, dst = 0; src < sizeof(sbuf); src++)
        if (sbuf[src] != '\r') sbuf[dst++] = sbuf[src];

        // WebSerial
        WebSerial.printf("%s",sbuf);

        // Cloud Logger PaperTrail
        int logLevel = 0;
        bool must_send_to_server = false;
        char *loglevel_position;
        // Parse line to get log level and convert to standard syslog levels
        loglevel_position = strstr(sbuf,"[DBG_ERROR  ]");
        if (loglevel_position != NULL) {
          logLevel = 3;
          must_send_to_server = true;
        }

        loglevel_position = strstr(sbuf,"[DBG_WARNING]");
        if (loglevel_position != NULL) {
          logLevel = 4;
          must_send_to_server = true;
        }

        loglevel_position = strstr(sbuf,"[DBG_INFO   ]");
        if (loglevel_position != NULL) {
          logLevel = 5;
          must_send_to_server = true;
        }

        loglevel_position = strstr(sbuf,"[DBG_DEBUG  ]");
        if (loglevel_position != NULL) {
          logLevel = 6;
          must_send_to_server = true;
        }

        // Do not send the verbose to the server
        loglevel_position = strstr(sbuf,"[DBG_VERBOSE]");
        if (loglevel_position != NULL)
          logLevel = 7;

        logger.log(PL_LOG, logLevel, "%s", sbuf);
      }
    }
  } else {
    Serial.println("WiFi not connected!");
    for (i = 0; i < MAX_SRV_CLIENTS; i++) {
      if (serverClients[i]) {
        serverClients[i].stop();
      }
    }
    delay(1000);
  }

  // Check for updates
  ElegantOTA.loop();
  // Update WebSerial
  WebSerial.loop();
}