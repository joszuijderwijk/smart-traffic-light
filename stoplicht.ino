#include <ESP8266WiFi.h>        // Wifi library
#include <PubSubClient.h>       // MQTT library

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic 
#include <Ticker.h>

Ticker ticker;
WiFiClient wifiClient;               // WiFi
PubSubClient client(wifiClient);     // MQTT

// PINS
const int PIN_RED = 3;
const int PIN_ORANGE = 0;
const int PIN_GREEN = 2;

// MQTT settings
const char* mqtt_server = "mqtt.orcaroeien.nl";
const int mqtt_port = 1883;
const char* mqtt_username = "orca";
const char* mqtt_password = "?";
const char* mqtt_client_name = "mini_stoplicht";

// animation intervals
unsigned long animationTimer;
const float startupInterval = 0.5;
const float apInterval = 1;

bool isConnected = false;

void setup() {

  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_ORANGE, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  
  updateLights(false, false, false);
  
  ticker.attach(startupInterval, startupAnimation);
  
  WiFiManager wifiManager;
  WiFiManagerParameter custom_text("<p>(c) 2019 by <a href=\"maito:hoi@joszuijderwijk.nl\">Jos Zuijderwijk</a></p>");
  wifiManager.addParameter(&custom_text);
  wifiManager.setAPCallback(configModeCallback); 

  if (wifiManager.autoConnect("Stoplicht", "haaldoor")){
    isConnected = true;
    ticker.detach();
  }
  
  // MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void configModeCallback(WiFiManager * wf){
   ticker.detach();
   ticker.attach(apInterval, apAnimation);
}

// Try reconnecting to MQTT broker
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    if (client.connect(mqtt_client_name, mqtt_username, mqtt_password, "connection/mini-stoplicht", 0, 1, "0")) {
      // Send Hello World!
      client.publish("connection/mini-stoplicht", "1", 1);
      client.subscribe("vvb/status"); 
    }
  }
}

// Handle incoming messages
void callback(char* topic, byte* payload, unsigned int len) {
    
    String msg = ""; // payload
    for (int i = 0; i < len; i++) {
      msg += ((char)payload[i]);
    }

  if ( strcmp(topic, "vvb/status") == 0 ){
    if (msg == "0"){
      updateLights(true, false, false);
    } else if (msg == "1"){
      updateLights(false, true, false);
    } else if (msg == "2"){
      updateLights(false, false, true);
    }  
  }
}

int animationCycle = 0;

// connecting to wifi animation
void startupAnimation(){
    if (animationCycle == 0){
      updateLights(false, false, true);
      animationCycle++;
    } else if (animationCycle == 1){
      updateLights(false, true, false);
      animationCycle++;
    } else if (animationCycle == 2){
      updateLights(true, false, false);
      animationCycle++;
    } else if (animationCycle == 3){
      updateLights(false, false, false);
      animationCycle = 0;
    }
}

// access point animation
void apAnimation(){
  if (animationCycle == 0){
    updateLights(true, false, false);
    animationCycle++;
  }else{
    updateLights(false, false, false);
    animationCycle = 0;
  }
}

void updateLights(boolean red, boolean orange, boolean green){
  digitalWrite(PIN_RED, red ? LOW : HIGH);
  digitalWrite(PIN_ORANGE, orange ? LOW : HIGH);
  digitalWrite(PIN_GREEN, green ? LOW : HIGH);
}

void loop() {
    
    if (!client.connected() && isConnected){
     reconnect();
    }
    
    client.loop();
}
