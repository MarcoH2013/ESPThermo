#include <Arduino.h>
#include <ESP8266WiFi.h>        // https://github.com/esp8266/Arduino
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <FS.h>

#include <DNSServer.h>          // https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>   // https://github.com/esp8266/Arduino
#include <SPI.h>                // https://github.com/esp8266/Arduino
#include <Wire.h>               // https://github.com/esp8266/Arduino

#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <MCP3208.h>            // https://github.com/MajenkoLibraries/MCP3208
#include <SimpleTimer.h>        // https://github.com/jfturcot/SimpleTimer

#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson

#include <Adafruit_GFX.h>       // https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_SSD1306.h>   // https://github.com/adafruit/Adafruit_SSD1306

// declare MCP3208 on PIN 16
MCP3208 adc(16);

// declare timer
SimpleTimer timer;

// declare temperature array
float temperature[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// declare display
#define OLED_RESET -1
Adafruit_SSD1306 display(OLED_RESET);

// Height must be defined in header file to match with display.
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espclient;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

// declare MDNS name for OTA
const char* host = "espthermo";

// declare webserver to listen on port 80
ESP8266WebServer server(80);
File fsUploadFile;

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}

  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "text/json", output);
}

void handleData() {
  String output = "[{";

  for (int i = 0; i < 4; i++) {
    if (output != "[{")
      output += ",";
    output += "\"" + String(i) + "\":";
    output += String(temperature[i]);
  }

  output += "}]";
  server.send(200, "text/json", output);
}

/////////////////////////////////////////////////////////////////////////
///// Calculate Temperature value based on reading //////////////////////
/////////////////////////////////////////////////////////////////////////
float calcT(uint32_t r, uint32_t typ){

  float Rmess = 47;
  float a, b, c, Rn;

  switch (typ) {
  case 1: { // Maverick ET-732,733,735 992.4
    //Rn = 992.4; a = 0.0033562438; b = 0.00022411373; c = 0.0000026102506;
    Rn = 1004.0; a = 3.3561580e-03; b = 2.2237925e-04; c = 2.6520160e-06;
    break; }
  case 2: { // Fantast-Neu
    Rn = 47; a = 0.00334519; b = 0.000243825; c = 0.00000261726;
    break; }
  case 3: { // Fantast-Alt
      Rn = 47; a = 0.0033558340; b = 0.00025698192; c = 0.0000016391056;
      break; }
  default: { // NTC 5K3A1B (orange Kopf)
    Rn = 5; a = 0.0033555; b = 0.0002570; c = 0.00000243; }
  }

  float Rt = Rmess*((4096.0/(4096-r)) - 1);
  float v = log(Rt/Rn);
  float T = (1/(a + b*v + c*v*v)) - 273;
  return T;
  //return (erg>-10)?erg:0x00;
}

void readingTemperature() {
  //temperature[0] = calcT(adc.analogRead(0), 1);
  //temperature[1] = calcT(adc.analogRead(1), 1);
  //temperature[2] = calcT(adc.analogRead(2), 1);
  //temperature[3] = calcT(adc.analogRead(3), 1);

    for (int i = 0; i < 4; i++) {
    temperature[i] = calcT(adc.analogRead(i), 1);

    Serial.print(temperature[i]);
    Serial.print(";");

  }
  Serial.println();

}

/////////////////////////////////////////////////////////////////////////
void configModeCallback (WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());

    display.println("Entered config mode!");
    display.println("Connect client to:");
    display.print(myWiFiManager->getConfigPortalSSID());
    display.println(" Wifi to");
    display.println("setup device.");

    display.println(WiFi.softAPIP());

    display.display();
}

void sendingTemperatureDisplay() {
    display.clearDisplay();
    display.setCursor(0,0);

    for (int i = 0; i < 4; i++) {
        // text display tests
        char line[] = "";
        //sprintf(line, "Temperature %i: %.2f C", i, temperature[i]);
        //display.println(line);
        display.print("Temperature ");
        display.print(i);
        display.print(": ");
        display.print(temperature[i]);
        display.println("C");
    }
    display.display();
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(74880);

    Serial.println("Booting up...");

    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)

    // Adafruit logo
    display.display();
    delay(2000);
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(WHITE);

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //reset saved settings
    //wifiManager.resetSettings();
    wifiManager.setDebugOutput(true);
    wifiManager.setAPCallback(configModeCallback);

    wifiManager.autoConnect("ESPThermo");

    //if you get here you have connected to the WiFi
    display.clearDisplay();
    display.println("Connected!");
    display.display();
    delay(2000);
    display.clearDisplay();

    Serial.println("connected!");


    // Set up mDNS responder:
    // - first argument is the domain name, in this example
    //   the fully-qualified domain name is "esp8266.local"
    // - second argument is the IP address to advertise
    //   we send our IP address on the WiFi network
    if (!MDNS.begin("espthermo")) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");

    // Initialize ArduinoOTA
    // Set password for Arduino OTA upload
    //ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();

    // Initialize MCP3208 ADC
    adc.begin();

    // Initialize SPIFFS
    SPIFFS.begin();
    {
        Dir dir = SPIFFS.openDir("/");
        while (dir.next()) {
            String fileName = dir.fileName();
            size_t fileSize = dir.fileSize();
            Serial.printf("FS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
        }
    }

    Serial.print("Open http://");
    Serial.print(host);
    Serial.println(".fritz.local/edit to see the file browser");


    //SERVER INIT
    //list directory
    server.on("/list", HTTP_GET, handleFileList);

    //load editor
    server.on("/edit", HTTP_GET, [](){
        if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
    });

    //create file
    server.on("/edit", HTTP_PUT, handleFileCreate);

    //delete file
    server.on("/edit", HTTP_DELETE, handleFileDelete);

    //first callback is called after the request has ended with all parsed arguments
    //second callback handles file uploads at that location
    server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

    server.on("/config", HTTP_GET, []() {
        server.send(200, "text/json", "config");
    });

    //called when the url is not defined here
    //use it to load content from SPIFFS
    server.onNotFound([](){
        if(!handleFileRead(server.uri()))
            server.send(404, "text/plain", "FileNotFound");
    });

    //get heap status, analog input value and all GPIO statuses in one json call
    server.on("/all", HTTP_GET, [](){
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();

        root["heap"] = ESP.getFreeHeap();
        root["analog"] = analogRead(A0);
        root["gpio"] = (uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16));

        size_t size = root.measureLength() + 1;
        char json[size];
        root.printTo(json, size);

        server.send(200, "text/json", json);
    });

    //list directory
    server.on("/data", HTTP_GET, handleData);

    server.begin();
    Serial.println("HTTP server started");
    MDNS.addService("http", "tcp", 80);

    // Starting timer jobs
    timer.setInterval(1000, readingTemperature);          // Read temperature
    timer.setInterval(5000, sendingTemperatureDisplay);   // Update display
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
    timer.run();
}
