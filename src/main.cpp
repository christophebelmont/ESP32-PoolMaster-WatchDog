/*
  PoolMaster WatchDog - Act as a watchdog and support ESP32 to PoolMaster's main CPU

*/
#define TARGET_TELNET
//#define TARGET_WEBSERIAL
//#define TARGET_PAPERTRAIL

#include <WiFi.h>
#include <WiFiMulti.h>
#ifdef TARGET_WEBSERIAL
  #include <WebSerial.h>
#endif
#include <ESPAsyncWebServer.h>
#include <TimeLib.h>
#include <ElegantOTA.h>
#ifdef TARGET_PAPERTRAIL
  #include <Elog.h>
  #include <ElogMacros.h>
#endif
#include <ESPNexUpload.h>
#include <HTTPClient.h>
#include "soc/rtc_wdt.h"
#include "esp32_flasher.h"

const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

// PaperTrail
#ifdef TARGET_PAPERTRAIL
  #define PAPERTRAIL_HOST "logs6.papertrailapp.com"
  #define PAPERTRAIL_PORT 21858
  #define PL_LOG 0  // PoolMaster Logs
  #define WD_LOG 1  // WatchDog Logs
#endif

#ifdef TARGET_TELNET
  //how many clients should be able to telnet to this ESP32
  #define MAX_SRV_CLIENTS 2
#endif

#define BUFFER_SIZE 400
#define LOG_BUFFER_SIZE 400

// Upgrade triggers
volatile bool mustUpgradeNextion = false;
volatile bool mustUpgradePoolMaster = false;

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

const char *ssid = "SSID";
const char *password = "PASSWORD";

WiFiMulti wifiMulti;
#ifdef TARGET_TELNET
  WiFiServer Telnetserver(23);
  WiFiClient serverClients[MAX_SRV_CLIENTS];
#endif
AsyncWebServer Webserver(80);

// Local logline buffers
char sbuf[BUFFER_SIZE];
char local_sbuf[LOG_BUFFER_SIZE];

// OTA
unsigned long ota_progress_millis = 0;

/*! Send Logs to the various facilities
 *
 * \param _log_message The message to be sent.
 * \param _targets The targets where message should be printed (1-Telnet 2-WebSerial 3-PaperTrail or any combination)
 * \param _telnet_separator The newline character to be used when printing with Telnet
 *
 * \return None
 */
void Local_Logs_Dispatch(const char *_log_message, uint8_t _targets = 7, const char* _telnet_separator = "\r\n")
{

#ifdef TARGET_TELNET
  if(_targets & 1) {  // First bit is for telnet
    // Telnet
    for (int i = 0; i < MAX_SRV_CLIENTS; i++) {
      if (serverClients[i] && serverClients[i].connected()) {
        serverClients[i].write(_log_message, strlen(_log_message));
        delay(1);
        serverClients[i].write(_telnet_separator, strlen(_telnet_separator));
        delay(1);
      }
    }
  }
#endif

#ifdef TARGET_WEBSERIAL
  if(_targets & 2) {  // Second bit is for WebSerial
    // WebSerial
    WebSerial.printf("%s",_log_message);
  }
#endif

#ifdef TARGET_PAPERTRAIL
  if(_targets & 4) {  // Third bit is for WebSerial
    // Cloud PaperTrail
    logger.log(WD_LOG, 1, "%s", _log_message);
  }
#endif
}

// Monitor free stack (display smallest value)
void stack_mon(UBaseType_t &hwm)
{
  UBaseType_t temp = uxTaskGetStackHighWaterMark(nullptr);
  if(!hwm || temp < hwm)
  {
    hwm = temp;
    Serial.printf("[stack_mon] %s: %d bytes\n",pcTaskGetTaskName(NULL), hwm);
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



//////////////////////// UPGRADE NEXTION //////////////////////////
////////////////////////////////////////////////////////////
void TaskUpgradeNextion(void *pvParameters)
{
  static UBaseType_t hwm=0;     // free stack size
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  for (;;) {
    if(mustUpgradeNextion)
    {
      mustUpgradeNextion = false;
      Local_Logs_Dispatch("Nextion Upgrade Requested");
      Local_Logs_Dispatch("Stopping PoolMaster...");
      pinMode(ENPin, OUTPUT);
      digitalWrite(ENPin, LOW);
      Local_Logs_Dispatch("Upgrading Nextion ...");
   
      HTTPClient http;
      
      // begin http client
        if(!http.begin(String("http://") + upgrade_host + upgrade_url_nextion)){
          Local_Logs_Dispatch("Connection failed");
        return;
      }
      snprintf(local_sbuf,sizeof(local_sbuf),"Requesting URL: %s",upgrade_url_nextion);
      Local_Logs_Dispatch(local_sbuf);
    
      // This will send the (get) request to the server
      int code          = http.GET();
      contentLength     = http.getSize();
        
      // Update the nextion display
      if(code == 200){
        Local_Logs_Dispatch("File received. Update Nextion...");
        bool result;

        // initialize ESPNexUpload
        ESPNexUpload nextion(115200);
        // set callback: What to do / show during upload..... Optional! Called every 2048 bytes
        upgradeCounter=0;
        nextion.setUpdateProgressCallback([](){
          upgradeCounter++;
          snprintf(local_sbuf,sizeof(local_sbuf),"Nextion Upgrade Progress %4.1f%%",((((float)upgradeCounter*2048)/contentLength)*100));
          Local_Logs_Dispatch(local_sbuf,1,"\r");
        });
        // prepare upload: setup serial connection, send update command and send the expected update size
        result = nextion.prepareUpload(contentLength);
        
        if(!result){
            snprintf(local_sbuf,sizeof(local_sbuf),"Error: %s",nextion.statusMessage.c_str());
            Local_Logs_Dispatch(local_sbuf);
            //Serial.println("Error: " + nextion.statusMessage);
        }else{
            snprintf(local_sbuf,sizeof(local_sbuf),"Start upload. File size is: %d bytes",contentLength);
            Local_Logs_Dispatch(local_sbuf);
            // Upload the received byte Stream to the nextion
            result = nextion.upload(*http.getStreamPtr());
            
            if(result){
              Local_Logs_Dispatch("Successfully updated Nextion");
            }else{
              snprintf(local_sbuf,sizeof(local_sbuf),"Error updating Nextion: %s",nextion.statusMessage.c_str());
              Local_Logs_Dispatch(local_sbuf);
            }

            // end: wait(delay) for the nextion to finish the update process, send nextion reset command and end the serial connection to the nextion
            nextion.end();
            pinMode(NEXT_RX,INPUT);
            pinMode(NEXT_TX,INPUT);
        }
        
      }else{
        // else print http error
        snprintf(local_sbuf,sizeof(local_sbuf),"HTTP error: %d",http.errorToString(code).c_str());
        Local_Logs_Dispatch(local_sbuf);
      }

      http.end();
      Local_Logs_Dispatch("Closing connection");
      Local_Logs_Dispatch("Starting PoolMaster ...");
      digitalWrite(ENPin, HIGH);
      pinMode(ENPin, INPUT);
      //rtc_wdt_enable();
      //rtc_wdt_protect_on();
    }
    stack_mon(hwm);
  }
}

//////////////////////// UPGRADE NEXTION //////////////////////////
////////////////////////////////////////////////////////////
//void TaskUpgradePoolMaster(void *pvParameters)
void TaskUpgradePoolMaster(void)
{
  //static UBaseType_t hwm=0;     // free stack size
  //rtc_wdt_protect_off();
  //rtc_wdt_disable();
  //for (;;) {
    if(mustUpgradePoolMaster) {
      mustUpgradePoolMaster = false;
      HTTPClient http;
      
      // begin http client
        if(!http.begin(String("http://") + upgrade_host + upgrade_url_esp)){
          Local_Logs_Dispatch("Connection failed");
        return;
      }
      snprintf(local_sbuf,sizeof(local_sbuf),"Requesting URL: %s",upgrade_url_esp);
      Local_Logs_Dispatch(local_sbuf);
    
      // This will send the (get) request to the server
      int code          = http.GET();
      contentLength     = http.getSize();
        
      // Update the nextion display
      if(code == 200){
        Local_Logs_Dispatch("File received. Update PoolMaster...");
        bool result;

        // Initialize ESP32Flasher
        ESP32Flasher espflasher;
        // set callback: What to do / show during upload..... Optional! Called every transfert integer %
        upgradeCounter=0;
        espflasher.setUpdateProgressCallback([](){
          upgradeCounter++;
          snprintf(local_sbuf,sizeof(local_sbuf),"PoolMaster Upgrade Progress %02d%%",upgradeCounter);
          Local_Logs_Dispatch(local_sbuf,1,"\r");
        });
        espflasher.espFlasherInit();//sets up Serial communication to another esp32

        int connect_status = espflasher.espConnect();

        if (connect_status != SUCCESS) {
          Local_Logs_Dispatch("Cannot connect to target");
        }else{
          Local_Logs_Dispatch("Connected to target");

          espflasher.espFlashBinStream(*http.getStreamPtr(),contentLength);
        }

      }else{
        // else print http error
        snprintf(local_sbuf,sizeof(local_sbuf),"HTTP error: %d",http.errorToString(code).c_str());
        Local_Logs_Dispatch(local_sbuf);
      }

      http.end();
      Local_Logs_Dispatch("Closing connection");
    }
    //stack_mon(hwm);
  //}
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
    snprintf(local_sbuf,sizeof(local_sbuf),"OTA Progress Current: %u bytes, Final: %u bytes", current, final);
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
      mustUpgradePoolMaster = true;
      TaskUpgradePoolMaster();
    break;
    case 'T':  // Nextion Upgrade
      mustUpgradeNextion = true;
    break;
    case 'H':  // Help
      Local_Logs_Dispatch("***********************");  
      Local_Logs_Dispatch("Help Message:");
      Local_Logs_Dispatch("R: Reboot WatchDog");
      Local_Logs_Dispatch("P: Stop PoolMaster");
      Local_Logs_Dispatch("Q: Start PoolMaster");
      Local_Logs_Dispatch("S: Upgrade PoolMaster");
      Local_Logs_Dispatch("T: Upgrade Nextion");
      Local_Logs_Dispatch("***********************");  
    break;
  }
}

//////////////////////// WEBSERIAL //////////////////////////
////////////////////////////////////////////////////////////
// ----------------------------------------------------------------------------
// Message Callback WebSocket
// ----------------------------------------------------------------------------
#ifdef TARGET_WEBSERIAL
void recvMsg(uint8_t *data, size_t len) {
  //WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  //cmdExecute(data[0]);
  //WebSerial.println(d);
}
#endif
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

  #ifdef TARGET_WEBSERIAL
  // Start WebSerial
  WebSerial.begin(&Webserver);
  WebSerial.onMessage(&recvMsg); /* Attach Message Callback */
  Webserver.onNotFound([](AsyncWebServerRequest* request) { request->redirect("/webserial"); });
  #endif

  Webserver.begin();

  //Connect to serial receiving messages from PoolMaster
  Serial2.begin(115200);
  Serial2.setTimeout(100);
  Serial2.setRxBufferSize(1024);

  //Start Telnet Server
  #ifdef TARGET_TELNET
  Telnetserver.begin();
  Telnetserver.setNoDelay(true);
  #endif

  Local_Logs_Dispatch("Ready! Use 'telnet ");
  Local_Logs_Dispatch(" 23' to connect");

  // Start PaperTrail Logging
  #ifdef TARGET_PAPERTRAIL
  logger.configureSyslog(PAPERTRAIL_HOST, PAPERTRAIL_PORT, ""); // Syslog server IP, port and device name
  //logger.registerSerial(COUNTER, DEBUG, "COUNT", Serial); // Log both to serial...
  logger.registerSyslog(PL_LOG, DEBUG, FAC_LOCAL0, "poolmaster"); // ...and syslog. Set the facility to user
  logger.registerSyslog(WD_LOG, DEBUG, FAC_LOCAL0, "watchdog"); // ...and syslog. Set the facility to user
  logger.log(WD_LOG, INFO, "PoolMaster Logs Started");
  logger.log(WD_LOG, INFO, "WatchDog Logs Started");
  #endif

  Local_Logs_Dispatch("Creating Tasks");
  // Create loop tasks in the scheduler.
  //------------------------------------
  int app_cpu = xPortGetCoreID();

  xTaskCreatePinnedToCore(
    TaskUpgradeNextion, 
    "Nextion Upgrade",
    4500, // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    NULL,  // No parameter is used
    1,  // Priority
    nullptr,  // Task handle is not used here
    app_cpu
  );

  /*xTaskCreatePinnedToCore(
    TaskUpgradePoolMaster, 
    "PoolMaster Upgrade",
    2048, // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    NULL,  // No parameter is used
    1,  // Priority
    nullptr,  // Task handle is not used here
    app_cpu
  );*/
}

//////////////////////// MAIN LOOP //////////////////////////
/////////////////////////////////////////////////////////////
void loop() {
  uint8_t i;
  if (wifiMulti.run() == WL_CONNECTED) {
    #ifdef TARGET_TELNET
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
            Local_Logs_Dispatch("available broken");
          }
          IPAddress ip_temp=serverClients[i].remoteIP();
          snprintf(local_sbuf,sizeof(local_sbuf),"New Client %d (%d.%d.%d.%d)",i,ip_temp[0],ip_temp[1],ip_temp[2],ip_temp[3]);
          Local_Logs_Dispatch(local_sbuf);
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
    #endif
    //check UART for data
    if (Serial2.available()) {
      if (readUntil(Serial2, sbuf, "\n")) {
        // `buf` contains the delimiter, it can now be used for parsing.     
        #ifdef TARGET_TELNET 
        //push UART data to all connected telnet clients
        for (i = 0; i < MAX_SRV_CLIENTS; i++) {
          if (serverClients[i] && serverClients[i].connected()) {
            serverClients[i].write(sbuf, strlen(sbuf));
            delay(1);
          }
        }
        #endif

        // Cleanup buffer and send to the web
        for (int src = 0, dst = 0; src < sizeof(sbuf); src++)
        if (sbuf[src] != '\r') sbuf[dst++] = sbuf[src];

        #ifdef TARGET_WEBSERIAL
        // WebSerial
        WebSerial.printf("%s",sbuf);
        #endif

        #ifdef TARGET_PAPERTRAIL
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
        #endif
      }
    }
  } else {
    Local_Logs_Dispatch("WiFi not connected!");
    #ifdef TARGET_TELNET
    for (i = 0; i < MAX_SRV_CLIENTS; i++) {
      if (serverClients[i]) {
        serverClients[i].stop();
      }
    }
    #endif
    delay(1000);
  }

  // Check for updates
  ElegantOTA.loop();
  #ifdef TARGET_WEBSERIAL
  // Update WebSerial
  WebSerial.loop();
  #endif
}