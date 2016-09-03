#include <Arduino.h>
#include <ESP8266WiFi.h>        // https://github.com/esp8266/Arduino
#include <DNSServer.h>          // https://github.com/esp8266/Arduino
#include <ESP8266WebServer.h>   // https://github.com/esp8266/Arduino
#include <SPI.h>                // https://github.com/esp8266/Arduino
#include <Wire.h>               // https://github.com/esp8266/Arduino

#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <MCP3208.h>            // https://github.com/MajenkoLibraries/MCP3208
#include <SimpleTimer.h>        // https://github.com/jfturcot/SimpleTimer

#include <Adafruit_GFX.h>       // https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_SSD1306.h>   // https://github.com/adafruit/Adafruit_SSD1306

#include <PubSubClient.h>       // https://github.com/knolleary/pubsubclient
//#include "Adafruit_MQTT.h"      // https://github.com/adafruit/Adafruit_MQTT_Library
//#include "Adafruit_MQTT_Client.h" // https://github.com/adafruit/Adafruit_MQTT_Library

// declare MCP3208 on PIN 16
MCP3208 adc(16);

// declare timer
SimpleTimer timer;

// declare temperature array
float temperature[8] = {0,0,0,0,0,0,0,0};

// declare display
#define OLED_RESET -1
Adafruit_SSD1306 display(OLED_RESET);

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espclient;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

//char mqtt_server[40] = "192.168.178.41";
//uint16_t mqtt_port = 1883;
IPAddress server(192, 168, 178, 41);
char topic[] = "/ESPThermo";

PubSubClient client(espclient);
//PubSubClient client(server, 1883, callback, espclient);

long lastMsg = 0;
char msg[50];
int value = 0;


/////////////////////////////////////////////////////////////////////////
///// Calculate Temperature value based on reading //////////////////////
/////////////////////////////////////////////////////////////////////////
float calcT(uint16_t r, uint16_t typ){
  float Rt = 0;
  float Rmess = 47;
  float v = 0;
  float erg = 0;
  float a = 0.00334519;
  float b = 0.000243825;
  float c = 0.00000261726;
  float Rn = 47;

  if (typ==1){ // Maverik
    Rn = 1000;
    a = 0.003358;
    b = 0.0002242;
    c = 0.00000261;
  }else if (typ==2){ // Fantast-Neu
    Rn = 47;
    a = 0.00334519;
    b = 0.000243825;
    c = 0.00000261726;
  }else if (typ==4){ // NTC 5k
    Rn = 5;
    a = 0.00335389;
    b = 0.00025756;
    c = 0.0000025024;
  }

  Rt = Rmess*((4096.0/(4096-r)) - 1);
  v = log(Rt/Rn);
  erg = (1/(a + b*v + c*v*v)) - 273;
  return (erg>-10)?erg:0x00;
}

void shortReading() {
  display.clearDisplay();
  display.setCursor(0,0);
  for (int i = 0; i < 8; i++) {
    uint16_t reading = adc.analogRead(i);

    temperature[i] = calcT(reading, 2);

    //Serial.print(temp);
    Serial.print(temperature[i]);
    Serial.print(";");

    // text display tests
    display.print("Temperature ");
    display.print(i);
    display.print(": ");
    display.print(temperature[i]);
    display.println("C");
  }
  display.display();
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

void sendingPhotocell() {
  // Now we can publish stuff!
  Serial.println(client.state());
  if (client.connected()) {
    //char* topic = "";
    for (int i = 0; i < 8; i++) {
      sprintf(topic, "/ESPThermo/%i", i);
      char value[] = "";
      dtostrf(temperature[i], 7, 2, value);
      client.publish(topic, value);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESPThermo-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("/ESPThermo/hello", "hello world");
      // ... and resubscribe
      client.subscribe("/ESPThermo/reset");
      client.subscribe("/ESPThermo/reconfigure");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(74880);

    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)

    // Clear Adafruit logo
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

    display.clearDisplay();
    display.println("Connected!");
    //display.println(mqtt_server);
    //display.println(mqtt_port);
    display.display();
    delay(2000);
    display.clearDisplay();

    client.setServer(server, 1883);
    client.setCallback(callback);

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");

    // initialize MCP3208
    adc.begin();

    timer.setInterval(1000, shortReading);     // Temperaturen
    timer.setInterval(1000, sendingPhotocell);     // MQTT
}

void loop() {
  timer.run();

  if (!client.connected()) {
    reconnect();
  }

  client.loop();
}
