#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
<<<<<<< Updated upstream
=======
#include <Update.h>
#include <TimeLib.h>
>>>>>>> Stashed changes
#include <TelnetStream.h>
#include <HTTPClient.h>
#include <ESPNexUpload.h>
#include <Elog.h>
#include <ElogMacros.h>
#include <WebSerial.h>
#include "esp32_flasher.h"
#include "soc/rtc_wdt.h"

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

const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

#define TABLE_SIZE 300

// Enable and Boot pin numbers
const int ENPin = 25;
const int BOOTPin = 26;

const char* WIFI_SSID     = "CasaParigi";
const char* WIFI_PASS = "Elsa2011Andrea2017Clara2019";

const char* upgrade_host          = "192.168.86.250:8123";
const char* upgrade_url_nextion   = "/local/poolmaster/Nextion.tft";
const char* upgrade_url_esp       = "/local/poolmaster/PoolMaster.bin";
const char* upgrade_url_watchdog  = "/local/poolmaster/WatchDog.bin";

bool mustUpgradeNextion = false;
bool mustUpgradePoolMaster = false;
//bool mustUpgradeWatchDog = false;

int upgradeCounter = 0;

bool line_ended;
char endMarker = '\n';
char carriageReturn = '\r';
char car;
int x;
char destination_message[TABLE_SIZE];

unsigned long ota_progress_millis = 0;

// Define two tasks for reading and writing from and to the serial port.
void TaskWriteToSerial(void *pvParameters);
void TaskReadFromSerial(void *pvParameters);
// Publishing tasks handles to notify them
static TaskHandle_t WriteToSerialHandle;
static TaskHandle_t ReadFromSerialHandle;


// Define Queue handle
QueueHandle_t QueueHandle;
const int QueueElementSize = 10;
typedef struct {
  char line[TABLE_SIZE];
  uint8_t line_length;
} message_t;

// Set web server port number
AsyncWebServer server(HTTP_PORT);
//AsyncWebSocket websock("/ws");

//Compute free RAM
//useful to check if it does not shrink over time
int freeRam () {
  int v = xPortGetFreeHeapSize();
  return v;
}

// Get current free stack 
unsigned stack_hwm(){
  return uxTaskGetStackHighWaterMark(nullptr);
<<<<<<< Updated upstream
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
=======
>>>>>>> Stashed changes
}


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
void TaskUpgradeNextion(void *pvParameters)
{
  for (;;) {
    if(mustUpgradeNextion)
    {
    mustUpgradeNextion = false;
    rtc_wdt_protect_off();
    rtc_wdt_disable();
    WebSerial.printf("Nextion Upgrade Requested\n");
    WebSerial.printf("Stopping PoolMaster...");
    pinMode(ENPin, OUTPUT);
    digitalWrite(ENPin, LOW);
    WebSerial.printf("Upgrading Nextion ...");
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
      WebSerial.printf("Starting PoolMaster ...");
      digitalWrite(ENPin, HIGH);
      pinMode(ENPin, INPUT);
      xTaskResumeAll();
      rtc_wdt_enable();
      rtc_wdt_protect_on();
    }
  }
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

<<<<<<< Updated upstream
      // Initialize ESP32Flasher
      ESP32Flasher espflasher;
      // set callback: What to do / show during upload..... Optional! Called every 2048 bytes
      upgradeCounter=0;
      espflasher.setUpdateProgressCallback([](){
        upgradeCounter++;
        WebSerial.printf("PoolMaster Programming progress: %d%%",upgradeCounter);   
        //display remaining RAM/Heap space.
        WebSerial.printf("[memCheck] Stack: %d bytes - Heap: %d bytes",stack_hwm(),freeRam()); 
      });
      espflasher.espFlasherInit();//sets up Serial communication to another esp32
=======
  WebSerial.printf("Requesting URL: %s",upgrade_url_esp);
 
  // This will send the (get) request to the server
  int code          = http.GET();
  int contentLength = http.getSize();
  
  // Update the nextion display
  if(code == 200){
    WebSerial.printf("File received. Update PoolMaster...");
    bool result;
>>>>>>> Stashed changes

    // Initialize ESP32Flasher
    ESP32Flasher espflasher;
    // set callback: What to do / show during upload..... Optional! Called every 2048 bytes
    upgradeCounter=0;
    espflasher.setUpdateProgressCallback([](){
      upgradeCounter++;
      WebSerial.printf("PoolMaster Programming progress: %d%%",upgradeCounter);   
      //display remaining RAM/Heap space.
      WebSerial.printf("[memCheck] Stack: %d bytes - Heap: %d bytes",stack_hwm(),freeRam()); 
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
  if (d=="Reboot WatchDog"){
    WebSerial.printf("Rebooting WatchDog ...");
    ESP.restart();
  }
  if ((d =="Upgrade Nextion")||(d =="Update Nextion")){
    mustUpgradeNextion=true;
  }
  if ((d=="Upgrade PoolMaster")||(d=="Update PoolMaster")){
    mustUpgradePoolMaster=true;
  }
<<<<<<< Updated upstream
  if ((d=="Upgrade WatchDog")||(d=="Update WatchDog")){
    mustUpgradeWatchDog=true;
  }

=======
>>>>>>> Stashed changes
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
  Serial2.setRxBufferSize(1024);
  Serial2.begin(115200);delay(100);
  

<<<<<<< Updated upstream
  //initSPIFFS();
=======
>>>>>>> Stashed changes
  initWiFi();

  WebSerial.begin(&server);
    /* Attach Message Callback */
  WebSerial.onMessage(&recvMsg);
  server.onNotFound([](AsyncWebServerRequest* request) { request->redirect("/webserial"); });
  server.begin();

  initElegantOTA();

  // Configure Syslog Loger (ELog)
  logger.configureSyslog(PAPERTRAIL_HOST, PAPERTRAIL_PORT, ""); // Syslog server IP, port and device name
  //logger.registerSerial(COUNTER, DEBUG, "COUNT", Serial); // Log both to serial...
  logger.registerSyslog(PL_LOG, DEBUG, FAC_LOCAL0, "poolmaster"); // ...and syslog. Set the facility to user
  logger.registerSyslog(WD_LOG, DEBUG, FAC_LOCAL0, "watchdog"); // ...and syslog. Set the facility to user
  logger.log(PL_LOG, INFO, "PoolMaster Logs Started");
  logger.log(WD_LOG, INFO, "WatchDog Logs Started");
  // Configure Telnet Streaming
  TelnetStream.begin(23);

  // Create the queue which will have <QueueElementSize> number of elements, each of size `message_t` and pass the address to <QueueHandle>.
  QueueHandle = xQueueCreate(QueueElementSize, sizeof(message_t));
  // Check if the queue was successfully created
  if (QueueHandle == NULL) {
    logger.log(WD_LOG, INFO, "Queue could not be created. Halt.");
    while (1) {
      delay(1000);  // Halt at this point as is not possible to continue
    }
  }

  // Set up two tasks to run independently.
  xTaskCreate(
    TaskWriteToSerial, "Task Write To Logs"  // A name just for humans
    ,
    2048  // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,
    NULL  // No parameter is used
    ,
    2  // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    ,
    &WriteToSerialHandle  // Task handle is not used here
  );

  xTaskCreate(
    TaskReadFromSerial, "Task Read From Serial"
    ,
    2048  // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,
    NULL  // No parameter is used
    ,
    1  // Priority
    ,
    &ReadFromSerialHandle  // Task handle is not used here
  );

  xTaskCreate(
    TaskUpgradeNextion, "Task Upgrade Nextion",
    2048 // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,
    NULL  // No parameter is used
    ,
    1  // Priority
    ,
    nullptr  // Task handle is not used here
  );
}

void loop()
{
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
 /*if (mustUpgradeNextion) {
    mustUpgradeNextion=false;
    rtc_wdt_protect_off();
    rtc_wdt_disable();
    vTaskSuspend(WriteToSerialHandle);
    vTaskSuspend(ReadFromSerialHandle);
    
    WebSerial.printf("Nextion Upgrade Requested\n");
    WebSerial.printf("Stopping PoolMaster...");
    pinMode(ENPin, OUTPUT);
    digitalWrite(ENPin, LOW);
    WebSerial.printf("Upgrading Nextion ...");
    UpgradeNextion();
    WebSerial.printf("Starting PoolMaster ...");
    digitalWrite(ENPin, HIGH);
    pinMode(ENPin, INPUT);
    xTaskResumeAll();
    rtc_wdt_enable();
    rtc_wdt_protect_on();
  }*/
  if(mustUpgradePoolMaster) {
    mustUpgradePoolMaster=false;
    WebSerial.printf("PoolMaster Upgrade Requested");
    rtc_wdt_protect_off();
    rtc_wdt_disable();
    vTaskSuspend(WriteToSerialHandle);
    vTaskSuspend(ReadFromSerialHandle);
    UpgradePoolMaster();
<<<<<<< Updated upstream
    xTaskResumeAll();
    rtc_wdt_enable();
    rtc_wdt_protect_on();
=======
>>>>>>> Stashed changes
  }

  // Check for updates
  ElegantOTA.loop();
  // Update WebSerial
  WebSerial.loop();
}

/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

void TaskWriteToSerial(void *pvParameters) {  // This is a task.
  message_t message;
  uint8_t logLevel = 0;
  char *loglevel_position;

  for (;;) {  // A Task shall never return or exit.
    // One approach would be to poll the function (uxQueueMessagesWaiting(QueueHandle) and call delay if nothing is waiting.
    // The other approach is to use infinite time to wait defined by constant `portMAX_DELAY`:
    if (QueueHandle != NULL) {  // Sanity check just to make sure the queue actually exists
      int ret = xQueueReceive(QueueHandle, &message, portMAX_DELAY);
      if (ret == pdPASS) {
        // The message was successfully received - send it back to Serial port and "Echo: "
          logLevel = 0;
          bool must_send_to_server = false;

          // Parse line to get log level
          loglevel_position = strstr(message.line,"[DBG_ERROR  ]");
          if (loglevel_position != NULL) {
            logLevel = 3;
            must_send_to_server = true;
          }

          loglevel_position = strstr(message.line,"[DBG_WARNING]");
          if (loglevel_position != NULL) {
            logLevel = 4;
            must_send_to_server = true;
          }

          loglevel_position = strstr(message.line,"[DBG_INFO   ]");
          if (loglevel_position != NULL) {
            logLevel = 5;
            must_send_to_server = true;
          }

          loglevel_position = strstr(message.line,"[DBG_DEBUG  ]");
          if (loglevel_position != NULL) {
            logLevel = 6;
            must_send_to_server = true;
          }

          // Do not send the verbose to the server
          loglevel_position = strstr(message.line,"[DBG_VERBOSE]");
          if (loglevel_position != NULL)
            logLevel = 7;

          if (logLevel == 0) {
            logLevel = 2;
            memcpy(destination_message, message.line,sizeof(message.line));
            must_send_to_server = true;
          } else {
            memcpy(destination_message, message.line+14,sizeof(message.line)-14);
          }

          // Send the logs if message contains something
          if ((strlen(destination_message) >= 1) && must_send_to_server) {
            WebSerial.printf("%s",destination_message);
            TelnetStream.println(destination_message);
            logger.log(PL_LOG, logLevel, "%s", destination_message);
          }

        //Serial.printf("Echo line of size %d: \"%s\"\n", message.line_length, message.line);
        // The item is queued by copy, not by reference, so lets free the buffer after use.
      } else if (ret == pdFALSE) {
        logger.log(WD_LOG, INFO, "The `TaskWriteToSerial` was unable to receive data from the Queue");
      }
    }  // Sanity check
  }  // Infinite loop
}

void TaskReadFromSerial(void *pvParameters) {  // This is a task.
  message_t message;
  for (;;) {
    // Check if any data are waiting in the Serial buffer
    message.line_length = Serial2.available();
    if (message.line_length > 0) {
      // Check if the queue exists AND if there is any free space in the queue
      if (QueueHandle != NULL && uxQueueSpacesAvailable(QueueHandle) > 0) {
        line_ended = false;
        x=0;
        while(line_ended == false) {
          car = Serial2.read();
          message.line[x] = car;
          if ((car == endMarker) || (car == carriageReturn)) {         // New caracter in line
              message.line[x] = 0;    // Terminate the string
              line_ended = true;
              break;
          }
          x++;
        }

        // The line needs to be passed as pointer to void.
        // The last parameter states how many milliseconds should wait (keep trying to send) if is not possible to send right away.
        // When the wait parameter is 0 it will not wait and if the send is not possible the function will return errQUEUE_FULL
        int ret = xQueueSend(QueueHandle, (void *)&message, 0);
        if (ret == pdTRUE) {
          // The message was successfully sent.
        } else if (ret == errQUEUE_FULL) {
          // Since we are checking uxQueueSpacesAvailable this should not occur, however if more than one task should
          //   write into the same queue it can fill-up between the test and actual send attempt
          logger.log(WD_LOG, INFO, "The `TaskReadFromSerial` was unable to send data into the Queue");
          
        }  // Queue send check
      }  // Queue sanity check
    } else {
      delay(100);  // Allow other tasks to run when there is nothing to read
    }  // Serial buffer check
  }  // Infinite loop
}

//////////////////////////////////////
// OLD STUFF
//////////////////////////////////////

// ----------------------------------------------------------------------------
// SPIFFS initialization
// ----------------------------------------------------------------------------
/*
void initSPIFFS() {
  if (!SPIFFS.begin()) {
    Serial.println("Cannot mount SPIFFS volume...");
  }
}

void listDir(const char * dir){
  WebSerial.printf("Files in %s: ", dir); 
  File root = SPIFFS.open(dir);
  File file = root.openNextFile();

  while(file){
      WebSerial.printf("File %s\n",file.name());
      file = root.openNextFile();
  }
  WebSerial.printf("***** End *****"); 
}
*/

// ----------------------------------------------------------------------------
// Upgrade WatchDog from SPIFFS
// ----------------------------------------------------------------------------
/*void UpgradeWatchDog(void)
{
  File file = SPIFFS.open("/firmware.bin");
  
  if(!file){
      Serial.println("Failed to open file for reading");
      return;
  }
      
  Serial.println("Starting update..");
  size_t fileSize = file.size();
  if(!Update.begin(fileSize)){
      Serial.println("Cannot do the update");
      return;
  };

  Update.writeStream(file);
  if(Update.end()){
      
    Serial.println("Successful update");  
  }else {
      
    Serial.println("Error Occurred: " + String(Update.getError()));
    return;
  }
    
  file.close();
  Serial.println("Reset in 4 seconds...");
  delay(4000);
  ESP.restart();  
}
*/
// ----------------------------------------------------------------------------
// DOWNLOAD Firmware to SPIFFS
// ----------------------------------------------------------------------------
/*void DownloadtoSPIFFS(const char* upgrade_url)
{
   logger.log(WD_LOG, WARNING, "Connection to %s",upgrade_host);
  
    HTTPClient http;
    
    // begin http client
      if(!http.begin(String("http://") + upgrade_host + upgrade_url)){
      logger.log(WD_LOG, WARNING, "Connection Failed");
      WebSerial.printf("Connection failed");
      return;
    }
  
    logger.log(WD_LOG, WARNING, "Requesting URL: %s",upgrade_url);
   
  
    // This will send the (get) request to the server
    int code          = http.GET();
    int contentLength = http.getSize();
      
    // Update the nextion display
    if(code == 200){
      logger.log(WD_LOG, WARNING, "Connected to download server");

      bool result;

      File file = SPIFFS.open("/firmware.bin", "w");
      if (file) {
        http.writeToStream(&file);
        file.close();
      }
      else {
        logger.log(WD_LOG, WARNING, "Failed to create binary file");
      }
    }else{
      // else print http error
      logger.log(WD_LOG, WARNING, "HTTP Error %s",http.errorToString(code).c_str());
    }
    http.end();
    Serial.println("Closing connection\n");
}



*/

  /*if (d=="Download PoolMaster"){
    mustDownloadPoolMaster=true;
  }
  if (d=="Download WatchDog"){
    mustDownloadWatchDog=true;
  }*/

  /*if (d=="List Files"){
    listDir("/");
  }*/

  /*if (d=="Delete Firmware"){
    SPIFFS.remove("/firmware.bin");
  }*/