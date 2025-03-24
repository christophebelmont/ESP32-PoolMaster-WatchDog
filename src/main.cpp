#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <TimeLib.h>
#include <TelnetStream.h>
#include <HTTPClient.h>
#include <ESPNexUpload.h>
#include <Elog.h>
#include <ElogMacros.h>
#include <WebSerial.h>
#include "esp32_flasher.h"

#define PL_LOG 0
#define WD_LOG 1


#define PAPERTRAIL_HOST "logs6.papertrailapp.com"
#define PAPERTRAIL_PORT 21858
#define MAX_SRV_CLIENTS 10

#define NEXT_RX 33 // Nextion RX pin
#define NEXT_TX 32 // Nextion TX pin

// ----------------------------------------------------------------------------
// Definition of macros
// ----------------------------------------------------------------------------

#define HTTP_PORT 80
#define LED_BUILTIN 2

const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

#define TABLE_SIZE 400

#define ENABLE_PIN  25
#define BOOT_PIN    26

// Enable and Boot pin numbers
const int ENPin = 25;
const int BOOTPin = 26;

// Stores LED state
String ENState;

const char* WIFI_SSID     = "CasaParigi";
const char* WIFI_PASS = "Elsa2011Andrea2017Clara2019";

const char* upgrade_host      = "192.168.86.250:8123";
const char* upgrade_url_nextion       = "/local/poolmaster/Nextion.tft";
const char* upgrade_url_esp           = "/local/poolmaster/PoolMaster.bin";
const char* upgrade_url_watchdog           = "/local/tft/WatchDog.bin";

bool mustUpgradeNextion = false;
bool mustUpgradePoolMaster = false;
bool mustUpgradeWatchDog = false;

int upgradeCounter = 0;

const int numChars = 399;

int j;
const char* endMarker = "\n";
char car;
char source_data[TABLE_SIZE];
char destination_message[TABLE_SIZE];

unsigned long ota_progress_millis = 0;

// Set web server port number
AsyncWebServer server(HTTP_PORT);
//AsyncWebSocket websock("/ws");

// ----------------------------------------------------------------------------
// Connecting to the WiFi network
// ----------------------------------------------------------------------------

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Trying to connect [%s] ", WiFi.macAddress().c_str());
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
  }
  Serial.printf(" %s\n", WiFi.localIP().toString().c_str());

  // Config NTP
  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");
  time_t now = time(nullptr);
  while (now < SECS_YR_2000) {
    delay(100);
    now = time(nullptr);
  }
  setTime(now);
}

// ----------------------------------------------------------------------------
// NEXTION UPGRADE FUNCTION initialization
// ----------------------------------------------------------------------------
void UpgradeNextion(void)
{
   WebSerial.printf("Connection to %s",upgrade_host);
  
    HTTPClient http;
    
    // begin http client
      if(!http.begin(String("http://") + upgrade_host + upgrade_url_nextion)){
      WebSerial.printf("Connection failed");
      return;
    }
  
    WebSerial.printf("Requesting URL: %s",upgrade_url_nextion);
  
    // This will send the (get) request to the server
    int code          = http.GET();
    int contentLength = http.getSize();
      
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
}

// ----------------------------------------------------------------------------
// POOLMASTER UPGRADE FUNCTION initialization
// ----------------------------------------------------------------------------
void UpgradePoolMaster(void)
{
   WebSerial.printf("Connection to %s",upgrade_host);
  
    HTTPClient http;
    
    // begin http client
      if(!http.begin(String("http://") + upgrade_host + upgrade_url_esp)){
      WebSerial.printf("Connection failed");
      return;
    }
  
    WebSerial.printf("Requesting URL: %s",upgrade_url_esp);
   
    // This will send the (get) request to the server
    int code          = http.GET();
    int contentLength = http.getSize();
    
    // Update the nextion display
    if(code == 200){
      WebSerial.printf("File received. Update PoolMaster...");
      bool result;

      // Initialize ESP32Flasher
      ESP32Flasher espflasher;
      // set callback: What to do / show during upload..... Optional! Called every 2048 bytes
      upgradeCounter=0;
      espflasher.setUpdateProgressCallback([](){
        upgradeCounter++;
        WebSerial.printf("PoolMaster Programming progress: %d%%",upgradeCounter);   
      });
      espflasher.espFlasherInit();//sets up Serial communication to another esp32

      int connect_status = espflasher.espConnect();

      if (connect_status != SUCCESS) {
        WebSerial.printf("Cannot connect to target");
      }else{
        WebSerial.printf("Connected to target");

        espflasher.espFlashBinStream(*http.getStreamPtr(),contentLength);
      }

    }else{
      // else print http error
      WebSerial.printf("HTTP error: %d",http.errorToString(code).c_str());
    }

    http.end();
    WebSerial.printf("Closing connection");
}

// ----------------------------------------------------------------------------
// Message Callback WebSocket
// ----------------------------------------------------------------------------
void recvMsg(uint8_t *data, size_t len){
  //WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  WebSerial.println(d);
  if (d == "Start PoolMaster"){
    WebSerial.printf("Starting PoolMaster ...");
    if(digitalRead(ENPin)==LOW)
    {
      pinMode(ENPin, OUTPUT);
      digitalWrite(ENPin, HIGH);
      pinMode(ENPin, INPUT);
    }
  }
  if (d=="Stop PoolMaster"){
    WebSerial.printf("Stopping PoolMaster ...");
    pinMode(ENPin, OUTPUT);
    digitalWrite(ENPin, LOW);
  }
  if (d=="Reboot PoolMaster"){
    WebSerial.printf("Stopping PoolMaster ...");
    pinMode(ENPin, OUTPUT);
    digitalWrite(ENPin, LOW);
    delay(500);
    WebSerial.printf("Starting PoolMaster ...");
    digitalWrite(ENPin, HIGH);
    pinMode(ENPin, INPUT);
  }
  if (d =="Upgrade Nextion"){
    mustUpgradeNextion=true;
  }
  if (d=="Upgrade PoolMaster"){
    mustUpgradePoolMaster=true;
  }
  if (d=="Upgrade WatchDog"){
    mustUpgradeWatchDog=true;
  }
}

// ----------------------------------------------------------------------------
// AlegantOTA functions
// ----------------------------------------------------------------------------
void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n\r", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

void initElegantOTA() {
  ElegantOTA.begin(&server);    // Start ElegantOTA
  // ElegantOTA callbacks
  ElegantOTA.onStart(onOTAStart);
  ElegantOTA.onProgress(onOTAProgress);
  ElegantOTA.onEnd(onOTAEnd);
}

// ----------------------------------------------------------------------------
// SETUP
// ----------------------------------------------------------------------------
void setup() { 
  Serial.begin(115200);delay(100);
  Serial2.begin(115200);delay(100);

  initWiFi();

  WebSerial.begin(&server);
    /* Attach Message Callback */
  WebSerial.onMessage(&recvMsg);
  server.onNotFound([](AsyncWebServerRequest* request) { request->redirect("/webserial"); });
  server.begin();

  //initWebSocket();
  //initWebServer();
  initElegantOTA();

  // Clear the buffer.
  for(j=0; j<TABLE_SIZE;j++) source_data[j] = 0;
  
  // Configure Syslog Loger (ELog)
  logger.configureSyslog(PAPERTRAIL_HOST, PAPERTRAIL_PORT, ""); // Syslog server IP, port and device name
  //logger.registerSerial(COUNTER, DEBUG, "COUNT", Serial); // Log both to serial...
  logger.registerSyslog(PL_LOG, DEBUG, FAC_LOCAL0, "poolmaster"); // ...and syslog. Set the facility to user
  logger.registerSyslog(WD_LOG, DEBUG, FAC_LOCAL0, "watchdog"); // ...and syslog. Set the facility to user
  logger.log(WD_LOG, INFO, "PoolMaster Logs Started");
  logger.log(WD_LOG, INFO, "WatchDog Logs Started");
  // Configure Telnet Streaming
  TelnetStream.begin(23);
}

void loop(){
  //websock.cleanupClients();

  static int ndx = 0;
  uint8_t logLevel = 0;
  char *loglevel_position;

  switch (TelnetStream.read()) {
    case 'R':
    TelnetStream.stop();
    delay(100);
    ESP.restart();
      break;
    case 'N':
      mustUpgradeNextion=true;
    case 'P':
      mustUpgradePoolMaster=true;
    break;
  
  }

  //Check if upgrade requested
  if (mustUpgradeNextion) {
    mustUpgradeNextion=false;
    WebSerial.printf("Nextion Upgrade Requested\n");
    WebSerial.printf("Stopping PoolMaster...");
    pinMode(ENPin, OUTPUT);
    digitalWrite(ENPin, LOW);
    WebSerial.printf("Upgrading Nextion ...");
    UpgradeNextion();
    WebSerial.printf("Starting PoolMaster ...");
    digitalWrite(ENPin, HIGH);
    pinMode(ENPin, INPUT);
  }
  if(mustUpgradePoolMaster) {
    mustUpgradePoolMaster=false;
    WebSerial.printf("PoolMaster Upgrade Requested");
    UpgradePoolMaster();
  }

  while (Serial2.available()) {
    car = Serial2.read();
    //delay(10);
    // Split lines
    if (strcmp(&car,endMarker) != 0) {         // New caracter in line
        source_data[ndx] = car;
        ndx++;
        if (ndx >= numChars) {    // We reached line max size
            ndx = numChars - 1;
        }
    }
    else {                          // End of line
        source_data[ndx] = '\0';    // Terminate the string
        TelnetStream.println(source_data);
        // Error by default
        logLevel = 0;
        bool must_send_to_server = false;

        // Parse line to get log level
        loglevel_position = strstr(source_data,"[DBG_ERROR  ]");
        if (loglevel_position != NULL) {
          logLevel = 3;
          must_send_to_server = true;
        }

        loglevel_position = strstr(source_data,"[DBG_WARNING]");
        if (loglevel_position != NULL) {
          logLevel = 4;
          must_send_to_server = true;
        }

        loglevel_position = strstr(source_data,"[DBG_INFO   ]");
        if (loglevel_position != NULL) {
          logLevel = 5;
          must_send_to_server = true;
        }

        loglevel_position = strstr(source_data,"[DBG_DEBUG  ]");
        if (loglevel_position != NULL) {
          logLevel = 6;
          must_send_to_server = true;
        }

        // Do not send the verbose to the server
        loglevel_position = strstr(source_data,"[DBG_VERBOSE]");
        if (loglevel_position != NULL)
          logLevel = 7;

        if (logLevel == 0) {
          logLevel = 2;
          memcpy(destination_message, source_data,sizeof(source_data));
          must_send_to_server = true;
        } else {
          memcpy(destination_message, source_data+14,sizeof(source_data));
        }

        // Send the logs if message contains something
        if ((strlen(destination_message) >= 1) && must_send_to_server) {
          WebSerial.printf("%s",destination_message);
          logger.log(PL_LOG, logLevel, "%s", destination_message);
        }

        // Reset number of char read from serial
        ndx = 0;
    }
  }  
  // Check for updates
  ElegantOTA.loop();
  // Update WebSerial
  WebSerial.loop();
} 