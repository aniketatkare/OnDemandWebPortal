// OnDemandWebPortal.ino
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <DNSServer.h>          // Add mDNS support For ESP8266 & ESP32
#include "ssid.h"

// Platform-specific headers
#ifdef ESP8266
  #include <ESPAsyncTCP.h>
  #include <ESP8266WiFi.h>
  #include <ESP8266mDNS.h>
  #include <FS.h>
  #define RELAY_ON HIGH
  #define MDNS_UPDATE() MDNS.update()
#elif defined(ESP32)
  #include <AsyncTCP.h>
  #include <WiFi.h>
  #include <ESPmDNS.h>
  #include <SPIFFS.h>
  #define RELAY_ON LOW
  #define MDNS_UPDATE()
#endif

// Hardware pins (adjust according to your ESP32 pinout)
#ifdef ESP8266
  const int relayPin1 = 0;  // D3
  const int relayPin2 = 2;  // D4
  const int resetPin = 3;   // RX
#elif defined(ESP32)
  const int relayPin1 = 15;
  const int relayPin2 = 2;
  const int resetPin = 4;
#endif


AsyncWebServer server(80);
WebSocketsServer webSocket(81);
DNSServer dnsServer;

bool relayState1 = false;
bool relayState2 = false;
char ssid[32];
char password[32];
bool wifiMode = true;

const char* webAddress = "tecknowinventors";
unsigned long buttonPressStart = 0;
bool resetInitiated = false;

// Function Prototypes
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void updateButtonState(const char* buttonId, const char* state);
void loadCredentials();
void saveCredentials();
void saveRelayState();
void clearCredentials();
void resetSettings();
void IRAM_ATTR handleButtonInterrupt();
void toggleRelay1();
void toggleRelay2();

// HTML content
const char index_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>TecKnow-Inventors Fence Control</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        html {
          font-family: Arial, sans-serif;
          display: inline-block;
          text-align: center;
          background-color: #1e1e1e;
          color: white;
        }
        h1 {
          color: #f5f5f5;
          margin-bottom: 50px;
        }
        h3 {
          color: #f5f5f5;
          margin-top: 50px;
        }
        .settings {
          position: absolute;
          bottom: 10px;
          right: 10px;
          font-size: 50px;
          text-decoration: none;
          color: white;
        }
        .settings:hover, .back:hover {
          color: red;
        }
        .button {
          background-color: #555555;
          border: none;
          color: white;
          padding: 20px;
          text-align: center;
          text-decoration: none;
          display: inline-block;
          font-size: 30px;
          margin: 20px;
          cursor: pointer;
          border-radius: 15px;
          transition: background-color 0.3s;
          width: 300px; /* Fixed width */
          height: 80px; /* Fixed height */
        }
        .button.off {
          background-color: #FF0000;
        }
        .button.on {
          background-color: #4CAF50;
        }
        .button:hover {
          background-color: #777777;
        }
        .button.off:hover {
          background-color: #FF6666;
        }
        .button.on:hover {
          background-color: #66BB6A;
        }
      </style>
    </head>
    <body>
    <h3>TecKnow-Inventors</h3>
      <h1>Fence Control</h1>
      <p><button id="relayButton1" class="button">Toggle Fence</button></p>
      <p><button id="relayButton2" class="button">Toggle Siren</button></p>
      <p class="settings"><a href="/settings" style="text-decoration: none; color: inherit;">&#9881;</a></p>
      <script>
        var gateway = `ws://${window.location.hostname}:81/`;
        var websocket;
        window.addEventListener('load', onLoad);
        function onLoad(event) {
          initWebSocket();
        }
        function initWebSocket() {
          websocket = new WebSocket(gateway);
          websocket.onopen = onOpen;
          websocket.onclose = onClose;
          websocket.onmessage = onMessage;
        }
        function onOpen(event) {
          console.log('Connection opened');
          websocket.send('getStatus');
        }
        function onClose(event) {
          console.log('Connection closed');
          setTimeout(initWebSocket, 2000);
        }
        function onMessage(event) {
        var message = event.data.split(',');
        var state1 = message[0];
        var state2 = message[1];
        updateButtonState('relayButton1', state1);
        updateButtonState('relayButton2', state2);
      }
        function updateButtonState(buttonId, state) {
          var button = document.getElementById(buttonId);
          if (state === 'OFF') {
            button.innerHTML = buttonId === 'relayButton1' ? 'Fence OFF' : 'Siren OFF';
            button.classList.remove('on');
            button.classList.add('off');
          } else {
            button.innerHTML = buttonId === 'relayButton1' ? 'Fence ON' : 'Siren ON';
            button.classList.remove('off');
            button.classList.add('on');
          }
        }
        function toggleRelay1() {
          websocket.send('toggle1');
        }
        function toggleRelay2() {
          websocket.send('toggle2');
        }
        document.getElementById('relayButton1').addEventListener('click', toggleRelay1);
        document.getElementById('relayButton2').addEventListener('click', toggleRelay2);
      </script>
    </body>
    </html>
    )rawliteral";
    
    const char settings_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>Settings</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        html {
          font-family: Arial, sans-serif;
          display: inline-block;
          text-align: center;
          background-color: #1e1e1e;
          color: white;
        }
        h1 {
          color: #f5f5f5;
          margin-top: 50px;
        }
        .settings, .back {
          position: absolute;
          bottom: 10px;
          font-size: 40px;
          text-decoration: none;
          color: white;
        }
        .settings:hover, .back:hover {
          color: red;
        }
        .settings {
          right: 10px;
        }
        .back {
          left: 10px;
        }
        .button {
          background-color: #555555;
          border: none;
          color: white;
          padding: 20px 50px;
          text-align: center;
          text-decoration: none;
          display: inline-block;
          font-size: 30px;
          margin: 20px;
          cursor: pointer;
          border-radius: 15px;
          transition: background-color 0.3s;
        }
        .button:hover {
          background-color: #45a049;
        }
        .button2 {
          background-color: #555555;
        }
        .button2:hover {
          background-color: #FF0000;
        }
        input[type=text], input[type=password], select {
          width: 60%;
          padding: 12px;
          margin: 8px 0;
          font-size: 20px;
          display: inline-block;
          border: 1px solid #ccc;
          border-radius: 4px;
          box-sizing: border-box;
        }
        label {
          font-size: 25px;
        }
      </style>
    </head>
    <body>
      <h1>Settings</h1>
      <form action="/save" method="post">
        <label for="mode">Mode:</label><br>
        <select id="mode" name="mode">
          <option value="host">WiFi Host</option>
          <option value="client">WiFi Client</option>
        </select><br><br>
        <label for="ssid">SSID:</label><br>
        <input type="text" id="ssid" name="ssid" value="Tecaknow_Inventors"><br><br>
        <label for="password">Password:</label><br>
        <input type="password" id="password" name="password"><br>
        <input type="checkbox" onclick="togglePassword()"> Show Password<br><br>
        <input type="submit" value="Save" class="button">
      </form>
      <p><button onclick="clearSettings()" class="button button2">Clear Settings</button></p>
      <p class="back"><a href="/" style="text-decoration: none; color: inherit;">&#9664;</a></p>
      <script>
        function togglePassword() {
          var x = document.getElementById("password");
          if (x.type === "password") {
            x.type = "text";
          } else {
            x.type = "password";
          }
        }
        function clearSettings() {
          fetch('/clear');
        }
      </script>
    </body>
    </html>
    )rawliteral";
    
    void setup() {
      Serial.begin(115200);
      Serial.println("Setup started");
    
      pinMode(relayPin1, OUTPUT);
      pinMode(relayPin2, OUTPUT);
      pinMode(resetPin, INPUT_PULLUP);
      
      // Initialize SPIFFS
      if(!SPIFFS.begin()){
        Serial.println("An error has occurred while mounting SPIFFS");
        return;
      }
    
      loadCredentials();
          
      // Start WiFi
      if (wifiMode) {
        Serial.println("Starting WiFi in AP mode...");
        WiFi.softAP(ssid, password);
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
        // Setup DNS server for AP mode only
        dnsServer.start(53, "*", WiFi.softAPIP());
        Serial.println("DNS Server started");
      } else {
        // Set mDNS hostname
        #ifdef ESP32
          WiFi.setHostname(webAddress); // Set hostname BEFORE connecting
        #endif
        Serial.print("Connecting to WiFi ");
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
          delay(1000);
          Serial.print(".");
        }
        Serial.println(".");
        Serial.print("Connected to WiFi, IP address: ");
        Serial.println(WiFi.localIP());
        // Setup DNS server for Client mode only
        dnsServer.start(53, "*", WiFi.localIP());
        Serial.println("DNS Server started");
      }
    
      // Start Web Server and WebSocket
      Serial.println("Starting Web Server...");
      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        #ifdef ESP8266
          request->send_P(200, "text/html", index_html);
        #elif defined(ESP32)
          request->send(200, "text/html", index_html);
        #endif
      });
    
      server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        #ifdef ESP8266
          request->send_P(200, "text/html", settings_html);
        #elif defined(ESP32)
          request->send(200, "text/html", settings_html);
        #endif
      });
    
      server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        int params = request->params();
        for(int i=0; i<params; i++){
          const AsyncWebParameter* p = request->getParam(i);
          if (p->name() == "ssid") {
            strcpy(ssid, p->value().c_str());
            Serial.print("SSID set to: ");
            Serial.println(ssid);
          }
          if (p->name() == "password") {
            strcpy(password, p->value().c_str());
            Serial.print("Password set to: ");
            Serial.println(password);
          }
          if (p->name() == "mode") {
            wifiMode = p->value() == "host";
            Serial.print("WiFi mode set to: ");
            Serial.println(wifiMode ? "Host" : "Client");
          }
        }
        saveCredentials();
        request->send(200, "text/plain", "Settings Saved! Rebooting...");
        delay(2000);
        ESP.restart();
      });
    
      server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request){
        clearCredentials();
        request->send(200, "text/plain", "Credentials Cleared! Rebooting...");
        delay(2000);
        ESP.restart();
      });
      if (!MDNS.begin(webAddress)) {
        Serial.println("Error setting up MDNS responder!");
            while(1) {
                delay(1000);
            }
      } else if (MDNS.begin(webAddress)) {
        // Add service to MDNS-SD
        #ifdef ESP8266
          MDNS.addService("http", "tcp", 80);
          MDNS.addService("ws", "tcp", 81);
        #elif defined(ESP32)
          MDNS.addService("http", "tcp", 80);
          //MDNS.addService("ws", "tcp", 81);
          MDNS.addService("websocket", "tcp", 81);
        #endif
        for (int i = 0; i < 5; i++) { // Run MDNS.update() 5 times
          //MDNS.update();
          dnsServer.processNextRequest();
          delay(300); // Optional: add delay if needed
        }
        Serial.print("mDNS responder started: http://");
        Serial.print(webAddress);
        Serial.println(".local");
      }
      server.begin();
      Serial.println("Web Server started");
      
      Serial.println("Starting WebSocket...");
      webSocket.begin();
      webSocket.onEvent(webSocketEvent);
      Serial.println("WebSocket started");
    
      // Attach interrupt for reset button
      Serial.println("Attaching interrupt for reset button...");
      attachInterrupt(digitalPinToInterrupt(resetPin), handleButtonInterrupt, CHANGE);
    }
    
    void loop() {
      webSocket.loop();
      #ifdef ESP8266
        MDNS.update(); // Refresh mDNS continuously
      #elif defined(ESP32)
        //MDNS.update(); // Refresh mDNS continuously
      #endif
      if (resetInitiated && (millis() - buttonPressStart > 5000)) {
        resetSettings();
      }
    }
    
    // WebSocket event handler
    void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
      if (type == WStype_TEXT) {
        if (strcmp((char *)payload, "toggle1") == 0) {
          relayState1 = !relayState1;
          digitalWrite(relayPin1, relayState1 ? HIGH : LOW);
          saveRelayState(); // Save the updated state
          Serial.print("Relay 1 toggled to: ");
          Serial.println(relayState1 ? "ON" : "OFF");
        } else if (strcmp((char *)payload, "toggle2") == 0) {
          relayState2 = !relayState2;
          digitalWrite(relayPin2, relayState2 ? HIGH : LOW);
          saveRelayState(); // Save the updated state
          Serial.print("Relay 2 toggled to: ");
          Serial.println(relayState2 ? "ON" : "OFF");
        }   
        String relay1State = relayState1 ? "ON" : "OFF";
        String relay2State = relayState2 ? "ON" : "OFF";
        String message = relay1State + "," + relay2State;
        webSocket.broadcastTXT(message); // Broadcast the updated relay states to all clients   
        webSocket.sendTXT(num, message); // Send the updated relay states to the client that triggered the event
      }
    }
    
    // Function to toggle relay 1
    void toggleRelay1() {
      relayState1 = !relayState1;
      digitalWrite(relayPin1, relayState1 ? HIGH : LOW);
      saveRelayState(); // Save the updated state
      updateButtonState("relayButton1", relayState1 ? "ON" : "OFF"); // Update button state on the client side
      String message = relayState1 ? "ON,OFF" : "OFF,OFF";
      webSocket.broadcastTXT(message); // Broadcast the updated relay states to all clients   
    }
    
    // Function to toggle relay 2
    void toggleRelay2() {
      relayState2 = !relayState2;
      digitalWrite(relayPin2, relayState2 ? HIGH : LOW);
      saveRelayState(); // Save the updated state
      updateButtonState("relayButton2", relayState2 ? "ON" : "OFF"); // Update button state on the client side
      String message = relayState2 ? "OFF,ON" : "OFF,OFF";
      webSocket.broadcastTXT(message); // Broadcast the updated relay states to all clients   
    }
    
    // Function to update button state on the client side
    void updateButtonState(const char* buttonId, const char* state) {
      // This function should contain the logic to update the button state on the client side
      // You can implement this function based on your requirements
    }
    
    // Load credentials from SPIFFS
    void loadCredentials() {
      Serial.println("Loading credentials from SPIFFS...");
      if(SPIFFS.exists("/credentials.txt")){
        File file = SPIFFS.open("/credentials.txt", "r");
        if(file){
          file.readBytes(ssid, 32);
          file.readBytes(password, 32);
          wifiMode = file.read() == '1';
          file.close();
          Serial.print("Loaded SSID: ");
          Serial.println(ssid);
          Serial.print("Loaded Password: ");
          Serial.println(password);
          Serial.print("Loaded WiFi Mode: ");
          Serial.println(wifiMode ? "Host" : "Client");
        }
      } else {
        strcpy(ssid, default_ssid);
        strcpy(password, default_password);
        wifiMode = true;
        Serial.println("No credentials found, using default credentials.");
      }
    
      if(SPIFFS.exists("/relays.txt")){
        File file = SPIFFS.open("/relays.txt", "r");
        if(file){
          relayState1 = file.read() == '1';
          relayState2 = file.read() == '1';
          file.close();
          Serial.print("Loaded Relay State 1: ");
          Serial.println(relayState1 ? "ON" : "OFF");
          Serial.print("Loaded Relay State 2: ");
          Serial.println(relayState2 ? "ON" : "OFF");
        }
      } else {
        relayState1 = false;
        relayState2 = true;
        Serial.println("No relay state found, using default states.");
      }
      digitalWrite(relayPin1, relayState1 ? HIGH : LOW);
      digitalWrite(relayPin2, relayState2 ? HIGH : LOW);
    }
      
    // Save credentials to SPIFFS
    void saveCredentials() {
      Serial.println("Saving credentials to SPIFFS...");
      File file = SPIFFS.open("/credentials.txt", "w");
      if(file){
        file.write((const uint8_t*)ssid, 32);
        file.write((const uint8_t*)password, 32);
        file.write(wifiMode ? '1' : '0');
        file.close();
        Serial.println("Credentials saved");
      } else {
        Serial.println("Failed to open file for writing");
      }
    }
    
    // Save relay states to SPIFFS
    void saveRelayState() {
      Serial.println("Saving relay state to SPIFFS...");
      File file = SPIFFS.open("/relays.txt", "w");
      if(file){
        file.write(relayState1 ? '1' : '0');
        file.write(relayState2 ? '1' : '0');
        file.close();
        Serial.println("Relay state saved");
      } else {
        Serial.println("Failed to open file for writing");
      }
    }
    
    // Clear credentials in SPIFFS
    void clearCredentials() {
      Serial.println("Clearing credentials in SPIFFS...");
      SPIFFS.remove("/credentials.txt");
      SPIFFS.remove("/relays.txt");
      Serial.println("Credentials cleared");
    }
    // handle Button Interrupt function
    void IRAM_ATTR handleButtonInterrupt() {
      if (digitalRead(resetPin) == LOW) {
        buttonPressStart = millis();
        resetInitiated = true;
      } else {
        resetInitiated = false;
      }
    }
    
    // Reset settings function
    void ICACHE_RAM_ATTR resetSettings() {
      Serial.println("Reset button pressed, clearing credentials...");
      clearCredentials();
      delay(2000);
      ESP.restart();
    }
   