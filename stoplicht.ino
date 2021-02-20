#include <ESP8266httpUpdate.h>

#include <ESP8266WiFi.h>             // Wifi library
#include <WiFiClientSecure.h>
#include <PubSubClient.h>            // MQTT library
#include <DNSServer.h>               // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>        // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>             // https://github.com/tzapu/WiFiManager WiFi Configuration Magic 
#include <Ticker.h>

#include <ESP8266httpUpdate.h>       // OTA update


// Only update to different versions of the firmware
const String CURRENT_VERSION = "1.0";

// Don't edit!
const String NAME = "stoplicht";     // name of the firmware
const String updateServer = "<update server>";

Ticker ticker;                       // Connection animation
WiFiClientSecure wifiClientSecure;   // WiFi
WiFiClient wifiClient;
PubSubClient client(wifiClient);     // MQTT

// PINS
const int PIN_RED = D0;
const int PIN_ORANGE = D1;
const int PIN_GREEN = D2;
const int PIN_BUTTON = D3;

// MODES
// Default: show the current rowing bans from mijn.orcaroeien.nl
const int DEFAULT_MODE = 1;
const int PARTY_MODE = 2;
const int RANDOM_MODE = 3;
const int OFF_MODE = 4;
const int ON_MODE = 5;
int currentMode = DEFAULT_MODE; 

// MQTT settings
const char* mqtt_server = "<server>";
const int mqtt_port = 1883;
const char* mqtt_username = "<username>";
const char* mqtt_password = "<password>";
const char* mqtt_client_name = "orca_stoplicht";

// TIMERS
unsigned long buttonTimer;

// FLAGS
bool buttonPressed = false;
bool buttonPressedLong = false;
bool isConnected = false;
bool showGreen = true;              // determines whether green (the default light) is being shown in default mode
bool updated = false;               // prevents device from repeatedly trying to update on failure

// INTERVALS
const int DEBOUNCE = 250;           // make sure the button is properly pressed
const int LONG_BUTTON_PRESS = 2000; // 2s for pressing long (in practise 3s)

// ANIMATIONS
enum animation {CONNECTING, ACCESS_POINT, PARTY, RANDOM};
int animationCycle = 0;
const float CONNECTING_INTERVAL = 0.5; // interval for the connection animation
const int AP_INTERVAL = 2;
const float PARTY_INTERVAL = 0.5;
const int RANDOM_INTERVAL = 2;

// Animation for Party Mode
bool PARTY_ANIMATION[][3] = {{true,true,true}, {false,false,false}, {true,true,true}, {false,false,false}, // knipper
                 {true, false, false}, {true, true, false}, {true,true,true},                              // heen lopen
                 {false, true, true}, {false, false, true}, {false,false,false},                           // teruglopen
                 {true, false, true}, {false, true, false}, {true, false, true}, {false, true, false},     // 2-1-2-1
                 {false,false,false}};                                                                     // uit

// Keep track of states while in other modes
bool redState = false; 
bool orangeState = false;   
bool greenState = false;           // The state of the green light regardless of "showGreen"

void setup() {
 Serial.begin(115200);  
 
 
  // Set pin modes
  pinMode(PIN_GREEN, OUTPUT);
  pinMode(PIN_ORANGE, OUTPUT);
  pinMode(PIN_RED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Setup animation while connecting
  StartAnimation(CONNECTING);
  
  // Wifi Manager
  WiFiManager wifiManager;
  WiFiManagerParameter custom_text("<p>(c) 2020 by <a href=\"maito:hoi@joszuijderwijk.nl\">Jos Zuijderwijk</a></p>");
  wifiManager.addParameter(&custom_text);
  wifiManager.setAPCallback(configModeCallback); 

  if (wifiManager.autoConnect("Orca Stoplicht", "haaldoor")){
    isConnected = true;
    StopAnimation();
  }

  wifiClientSecure.setInsecure();
  
  // MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // set randomizer
  randomSeed(analogRead(A0));

  // configure updater
  ESPhttpUpdate.closeConnectionsOnUpdate(false);
  ESPhttpUpdate.setAuthorization("orca","<password>");

  Serial.print("Firmware: ");
  Serial.println(NAME);

  
  Serial.print("Current version: ");
  Serial.println(CURRENT_VERSION);
}


// Start new animation when connection fails
void configModeCallback(WiFiManager * wf){
   StartAnimation(ACCESS_POINT);
}

// Try to reset MQTT connection
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    if (client.connect(mqtt_client_name, mqtt_username, mqtt_password, "connection/stoplicht", 0, 1, "0")) {
      
      // Send Hello World!
      client.publish("connection/stoplicht", "1", 1);
      client.publish("vvb/stoplicht/mode/out", "vaarverbod", 1);
      client.publish("vvb/stoplicht/showgreen/out", "1", 1);

      client.subscribe("vvb/stoplicht/showgreen/in");
      client.subscribe("vvb/status");
      client.subscribe("vvb/update");
      client.subscribe("vvb/stoplicht/mode/in");

    } else {
      // check for update in case of no MQTT connection
       if (!updated){
         updated = true;
         CheckUpdate();
       }
    }
  }
}

// Handel inkomenden berichten af
void callback(char* topic, byte* payload, unsigned int len) {

  
    String msg = ""; // payload
    for (int i = 0; i < len; i++) {
      msg += ((char)payload[i]);
    }

  
  if ( strcmp(topic, "vvb/status") == 0 ){
    redState = (msg == "0");
    orangeState = (msg == "1");
    greenState = (msg == "2");

    if (currentMode == DEFAULT_MODE){
      if (showGreen || !greenState)
        setLights(redState, orangeState, greenState);
      else
        setLights(false,false,false);
    }
  } else if (strcmp(topic, "vvb/update") ==0){    // Trigger update
    
    if (updated){
      updated = false;
    } else if (msg != CURRENT_VERSION && !updated){
      CheckUpdate();
      updated = true;
    }      
    // switch mode party, random, ON, default
  } else if(strcmp(topic, "vvb/stoplicht/mode/in") ==0){ // change  mode

    if (msg == "party"){
      switchMode(PARTY_MODE);
    }else if (msg == "random"){
      switchMode(RANDOM_MODE);
    }else if (msg == "on"){
      switchMode(ON_MODE); 
    }else if (msg =="off"){
      switchMode(OFF_MODE);
    }else if (msg == "vaarverbod"){
      switchMode(DEFAULT_MODE);
    }
  } else if (strcmp(topic, "vvb/stoplicht/showgreen/in")==0){
    showGreen = msg == "1" ? true : false;
    client.publish("vvb/stoplicht/showgreen/out", showGreen ? "1" : "0");
     if (currentMode == DEFAULT_MODE && greenState){
      digitalWrite(PIN_GREEN, showGreen ? HIGH : LOW);
     }
  }
   
}


void loop() {

  // Check whether button has been pressed (long)
  if (!digitalRead(PIN_BUTTON)){

    // Button has been pressed
    if (!buttonPressed && millis() - buttonTimer > DEBOUNCE){
      buttonPressed = true;
      buttonTimer = millis();
    }

    // Button has been pressed long
    if (millis() - buttonTimer > LONG_BUTTON_PRESS && !buttonPressedLong){
      buttonPressedLong = true;
      PressButtonLong();
    }
  }
  // Has button been pressed before?
  else if (buttonPressed){
   if (buttonPressedLong)
   {
     // Reset flag
     buttonPressedLong = false;
   }else{
    // Button has been pressed normally
    PressButton();
   }
    // Reset flag
    buttonPressed = false;    
  }

  // MQTT
   if (!client.connected() && isConnected){
     reconnect();
    }
    
    client.loop();
 
}

// MODES

void switchMode(int nextMode){
  currentMode = nextMode;
 
  switch(nextMode){
    case DEFAULT_MODE:
      StopAnimation();
      setLights(redState, orangeState, greenState && showGreen);
      client.publish("vvb/stoplicht/mode/out", "vaarverbod", 1);
      break;
 
    case PARTY_MODE:
     StartAnimation(PARTY);
     client.publish("vvb/stoplicht/mode/out", "party", 1);
     break;
     
      case RANDOM_MODE:
      StartAnimation(RANDOM);
      client.publish("vvb/stoplicht/mode/out", "random", 1);
      break;
      
     case ON_MODE:
     StopAnimation();
     setLights(true, true, true);
      client.publish("vvb/stoplicht/mode/out", "on", 1);
     break;
     
      case OFF_MODE:
      StopAnimation();
       client.publish("vvb/stoplicht/mode/out", "off", 1);
  }
}

// ANIMATIONS

// Start animation
 void StartAnimation(animation a){
  StopAnimation();
  animationCycle = 0;
  switch(a){
    case CONNECTING:
    ticker.attach(CONNECTING_INTERVAL, connectingAnimation);
    break;

    case PARTY:
    ticker.attach(PARTY_INTERVAL, partyAnimation);
    break;

    case RANDOM:
    ticker.attach(RANDOM_INTERVAL, randomAnimation);
    break;

    case ACCESS_POINT:
    ticker.attach(AP_INTERVAL, apAnimation);
    break;
    
  }
  
 }

// Stop animation
void StopAnimation(){
  ticker.detach();
  setLights(false,false,false);
}

// Connecting to wifi animation
void connectingAnimation(){
  if (animationCycle == 0){
    setLights(false, false, true);
    animationCycle++;
    } else if (animationCycle == 1){
        setLights(false, true, false);
        animationCycle++;
    } else if (animationCycle == 2){
        setLights(true, false, false);
        animationCycle++;
    } else if (animationCycle == 3){
        setLights(false, false, false);
        animationCycle = 0;
        }
    }

// Access point animation
void apAnimation(){
  if (animationCycle == 0){
    setLights(true, false, false);
    animationCycle++;
  }else{
    setLights(false, false, false);
    animationCycle = 0;
  }
}    

// Party mode
void partyAnimation(){

  if (animationCycle == 15)
    animationCycle = 0;
     
  setLights(PARTY_ANIMATION[animationCycle]);
  
  animationCycle++;
    
}

// Random mode
void randomAnimation(){
  int r = random(8) + 1;
  setLights(r % 2, r % 3, r % 4);
}

// BUTTONS

// Button has been pressed normally
void PressButtonLong(){
   // Go to second mode
   if (currentMode == DEFAULT_MODE)
    switchMode(PARTY_MODE);
   else
    // go back to default
    switchMode(DEFAULT_MODE);
}

// Button has been pressed normally
void PressButton(){
    if (currentMode == DEFAULT_MODE && greenState){
      // toggle setting
      showGreen = !showGreen; 
      digitalWrite(PIN_GREEN, greenState && showGreen ? HIGH : LOW);
      // update settings
      client.publish("vvb/stoplicht/showgreen/out", showGreen ? "1" : "0", 1);
    } else if (currentMode != DEFAULT_MODE){
         // Last mode? Back to default
        if (currentMode == ON_MODE){ 
          switchMode(DEFAULT_MODE);
        }
        else{
          // Go to next mode
          switchMode(currentMode + 1);
        }
    }
}

// Set light states; true = on, false = off
void setLights(bool red, bool orange, bool green){
    digitalWrite(PIN_RED, red ? HIGH : LOW);
    digitalWrite(PIN_ORANGE, orange ? HIGH : LOW);
    digitalWrite(PIN_GREEN, green ? HIGH : LOW);
}

void setLights(bool state[]){
    digitalWrite(PIN_RED, state[0] ? HIGH : LOW);
    digitalWrite(PIN_ORANGE, state[1] ? HIGH : LOW);
    digitalWrite(PIN_GREEN, state[2] ? HIGH : LOW);
}

// UPDATE
// asks for update
void CheckUpdate() {
  
  
    // do NOT change this
    String fwVersion = "{ \"name\":\"" + NAME + "\", \"version\":\"" + CURRENT_VERSION + "\"}"; // JSON format
    t_httpUpdate_return ret = ESPhttpUpdate.update(wifiClientSecure, updateServer, fwVersion); // compiled firmware file
    switch(ret) {
            case HTTP_UPDATE_FAILED:
                Serial.print("HTTP_UPDATE_FAILD Error (");
                Serial.print(ESPhttpUpdate.getLastError());
                Serial.print("):");
                Serial.println(ESPhttpUpdate.getLastErrorString());
                break;
 
            case HTTP_UPDATE_NO_UPDATES:
                Serial.println("HTTP_UPDATE_NO_UPDATES");
                
                break;
 
            case HTTP_UPDATE_OK:
                Serial.println("HTTP_UPDATE_OK");
                break;
        }

       

}