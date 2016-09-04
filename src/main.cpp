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

// declare MCP3208 on PIN 16
MCP3208 adc(16);

// declare timer
SimpleTimer timer;

// declare temperature array
float temperature[8] = {0, 0, 0, 0, 0, 0, 0, 0};

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

// declare MQTT
char MQTT_Server[40] = "nibbler.fritz.box";
uint16_t MQTT_Port = 1883;
char topic[] = "/ESPThermo";

PubSubClient client(espclient);

/////////////////////////////////////////////////////////////////////////
///// Calculate Temperature value based on reading //////////////////////
/////////////////////////////////////////////////////////////////////////
float calcT(uint32_t r, uint32_t typ){

  float Rmess = 47;
  float a, b, c, Rn;

  switch (typ) {
  case 1: { // Maverik
    Rn = 1000; a = 0.003358; b = 0.0002242; c = 0.00000261;
    break; }
  case 2: { // Fantast-Neu
    Rn = 220; a = 0.00334519; b = 0.000243825; c = 0.00000261726;
    break; }
  case 3: { // NTC 100K6A1B (lila Kopf)
    Rn = 100; a = 0.00335639; b = 0.000241116; c = 0.00000243362;
    break; }
  case 4: { // NTC 100K (braun/schwarz/gelb/gold)
    Rn = 100; a = 0.003354016; b = 0.0002460380; c = 0.00000340538;
    break; }
  default: { // NTC 5K3A1B (orange Kopf)
    Rn = 5; a = 0.0033555; b = 0.0002570; c = 0.00000243; }
  }

  float Rt = Rmess*((4096.0/(4096-r)) - 1);
  float v = log(Rt/Rn);
  float erg = (1/(a + b*v + c*v*v)) - 273;
  return (erg>-10)?erg:0x00;
}

void readingTemperature() {
  for (int i = 0; i < 8; i++) {
    temperature[i] = calcT(adc.analogRead(i), 2);

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

void sendingTemperatureMQTT() {
  // Now we can publish stuff!
  if (client.connected()) {
    for (int i = 0; i < 8; i++) {
      sprintf(topic, "/ESPThermo/%i/%i", ESP.getChipId(), i);
      char value[] = "";
      dtostrf(temperature[i], 7, 2, value);
      client.publish(topic, value);
    }
  }
}

void sendingTemperatureDisplay() {
  display.clearDisplay();
  display.setCursor(0,0);

  for (int i = 0; i < 8; i++) {
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
    clientId += String(ESP.getChipId());
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
    display.println(MQTT_Server);
    display.println(MQTT_Port);
    display.display();
    delay(2000);
    display.clearDisplay();

    // initialize MQTT
    client.setServer(MQTT_Server, MQTT_Port);
    client.setCallback(callback);

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");

    // initialize MCP3208
    adc.begin();

    timer.setInterval(1000, readingTemperature);     // ReadTemperature
    timer.setInterval(1000, sendingTemperatureMQTT);     // MQTT
    timer.setInterval(1000, sendingTemperatureDisplay);     // Display
}

void loop() {
  timer.run();

  if (!client.connected()) {
    reconnect();
  }

  client.loop();
}
