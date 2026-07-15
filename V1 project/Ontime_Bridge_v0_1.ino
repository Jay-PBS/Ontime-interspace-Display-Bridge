/*
  Ontime Display Bridge
  
  MIT License

  Copyright (c) 2025 technicaleventservices.com

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <WebSocketsClient.h>     // https://github.com/Links2004/arduinoWebSockets
#include <Arduino_JSON.h>         // Arduino_JSON by Arduino
#include <Wire.h>                 // Required for I2C
#include <LiquidCrystal_I2C.h>    // Install "LiquidCrystal I2C" library

// ===================================================================================
// USER CONFIGURATION
// ===================================================================================

// Default Ontime Instance Settings (Can be changed via WiFi Portal later)
// Note: Buffer size is 60 to handle long domain names
char ontime_host[60] = "example.com"; 
int  ontime_port = 443; 

// ===================================================================================
// GLOBAL VARIABLES & OBJECTS
// ===================================================================================

bool shouldSaveConfig = false;
WebSocketsClient webSocket;

// Initialize LCD: Address 0x27 is standard, 16 chars, 2 lines.
// If 0x27 doesn't work, try 0x3F.
LiquidCrystal_I2C lcd(0x27, 16, 2);

// GLOBAL: Store Event Title persistently
String currentEventTitle = "Ontime";

// SCROLLING VARIABLES
unsigned long lastScrollTime = 0;
const int scrollInterval = 400; // Speed in ms (lower is faster)
int scrollIndex = 0;

// ===================================================================================
// HELPER FUNCTIONS
// ===================================================================================

// Callback notifying us of the need to save config
void saveConfigCallback () {
  shouldSaveConfig = true;
}

// Convert integer to BCD with Swapped Nibbles (Required by Display Protocol)
// Example: 12 -> 0x21 (High nibble = 2, Low nibble = 1)
byte formatByte(int value) {
  int tens = value / 10;
  int ones = value % 10;
  return (ones << 4) | tens;
}

// Helper to pad numbers on LCD (e.g. print "05" instead of "5")
void printTwoDigits(int number) {
  if (number < 10) lcd.print("0");
  lcd.print(number);
}

// Function to handle Title Scrolling on Row 0
void updateLcdTitle() {
  // Only update if enough time has passed
  if (millis() - lastScrollTime >= scrollInterval) {
    lastScrollTime = millis();

    lcd.setCursor(0, 0);

    // If title is too long, scroll it
    if (currentEventTitle.length() > 16) {
      // Create a buffer: "Title   Title" to allow smooth looping
      String scrollBuffer = currentEventTitle + "   " + currentEventTitle;
      
      // Get the 16-char window
      String toPrint = scrollBuffer.substring(scrollIndex, scrollIndex + 16);
      lcd.print(toPrint);

      // Increment position
      scrollIndex++;

      // Reset if we have scrolled past the end of the original title + gap
      if (scrollIndex > currentEventTitle.length() + 3) {
        scrollIndex = 0;
      }
    } 
    else {
      // If title fits, just print it padded with spaces
      String line0 = currentEventTitle;
      while (line0.length() < 16) line0 += " "; 
      lcd.print(line0);
    }
  }
}

// ===================================================================================
// WEBSOCKET EVENT HANDLER
// ===================================================================================

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WSc] Disconnected! Attempting to reconnect...");
      lcd.clear();
      lcd.setCursor(0, 0); 
      lcd.print("Disconnected!");
      break;
      
    case WStype_CONNECTED:
      Serial.println("[WSc] >>> CONNECTED TO ONTIME! <<<");
      Serial.printf("[WSc] Connected to: wss://%s:%d/ws\n", ontime_host, ontime_port);
      lcd.clear();
      lcd.setCursor(0, 0); 
      lcd.print("Connected!");
      lcd.setCursor(0, 1);
      lcd.print("Waiting data...");
      break;
      
    case WStype_TEXT: { // Scoped block for JSONVar
      // 1. Parse JSON Payload
      JSONVar doc = JSON.parse((char*)payload);

      // Check for parsing errors
      if (JSON.typeof(doc) == "undefined") return;

      // 2. Extract Data from Payload
      if (doc.hasOwnProperty("payload")) {
         JSONVar corePayload = doc["payload"];
         
         String newTitle = currentEventTitle; // Default to keep existing

         // --- CHECK FOR EVENT TITLE UPDATE ---
         // Ontime V2 often sends title in "eventNow" object
         if (corePayload.hasOwnProperty("eventNow")) {
            JSONVar eventNow = corePayload["eventNow"];
            if (eventNow.hasOwnProperty("title")) {
               newTitle = (const char*)eventNow["title"];
            }
         }

         // --- CHECK FOR TIMER UPDATE ---
         if (corePayload.hasOwnProperty("timer")) {
           JSONVar timer = corePayload["timer"];
           
           // Check for title inside timer object (legacy/simple views)
           if (timer.hasOwnProperty("label")) {
              newTitle = (const char*)timer["label"];
           } else if (timer.hasOwnProperty("title")) {
              newTitle = (const char*)timer["title"];
           }

           // Check if title actually changed to reset the scroll
           if (newTitle != currentEventTitle) {
              currentEventTitle = newTitle;
              scrollIndex = 0; // Reset scroll to start
           }
           
           // 3. Extract Time (Milliseconds)
           long currentMs = 0;
           if (timer.hasOwnProperty("current")) {
             currentMs = (long)timer["current"];
           }

           // 4. Convert MS to Minutes/Seconds
           long absMs = abs(currentMs);
           int totalSeconds = absMs / 1000;

           // FIX: Sync with Ontime Browser Display
           // Countdown (Positive): Ontime rounds UP (Ceil), e.g., 0.1s -> 1s.
           // Integer math rounds DOWN (Floor), e.g., 0.1s -> 0s.
           // We add 1 second to positive partial seconds to match the browser.
           // We do NOT change negative (overtime) logic as that is already correct.
           if (currentMs > 0 && (absMs % 1000 != 0)) {
             totalSeconds++;
           }
           
           int minutes = totalSeconds / 60;
           int seconds = totalSeconds % 60;
           
           // Cap minutes at 99 because the numeric display uses 2 hex digits
           if (minutes > 99) minutes = 99;

           // 5. Determine Color
           String cStr = "green";
           
           if (timer.hasOwnProperty("color")) {
             cStr = (const char*)timer["color"];
           } else if (timer.hasOwnProperty("colour")) {
             cStr = (const char*)timer["colour"];
           } else {
             // Fallback
             if (currentMs < 0) {
               cStr = "red";
             } else if (timer.hasOwnProperty("phase")) {
               String phase = (const char*)timer["phase"];
               if (phase == "overtime") cStr = "red";
               if (phase == "warn") cStr = "amber";
               if (phase == "warning") cStr = "amber";
             }
           }

           // 6. Map Color to Hex Byte & LCD Status Text
           byte colorByte = 0x01; 
           String lcdStatus = "Status";

           if (cStr == "red") {
             colorByte = 0x02;
             lcdStatus = "Over";
           } else if (cStr == "amber") {
             colorByte = 0x03;
             lcdStatus = "Warn";
           } else {
             lcdStatus = "Running";
           }

           // --- OVERRIDE STATUS IF STOPPED ---
           if (timer.hasOwnProperty("playback")) {
             String playback = (const char*)timer["playback"];
             if (playback != "play" && playback != "start") {
                lcdStatus = "Stopped";
             }
           }

           // 7. Format Digits for RS232
           byte minByte = formatByte(minutes);
           byte secByte = formatByte(seconds);

           // 9. HARDWARE OUTPUT (To Numeric Display via Pin D4 / GPIO2)
           Serial1.write(minByte);
           Serial1.write(secByte);
           Serial1.write(colorByte);

           // 10. LCD OUTPUT - ROW 1 (Time/Status)
           // (Row 0 is handled in the main loop now)
           String timeStr = "";
           if (currentMs < 0) timeStr += "-";
           if (minutes < 10) timeStr += "0";
           timeStr += String(minutes) + ":";
           if (seconds < 10) timeStr += "0";
           timeStr += String(seconds);

           String line1 = lcdStatus + " " + timeStr;
           
           // Pad Line 1 to 16 chars
           while (line1.length() < 16) line1 += " ";
           if (line1.length() > 16) line1 = line1.substring(0, 16);

           lcd.setCursor(0, 1);
           lcd.print(line1);
         }
      }
      break;
    } 
      
    case WStype_ERROR:
      Serial.println("[WSc] WebSocket Error Occurred!");
      lcd.setCursor(0, 1);
      lcd.print("WS Error!");
      break;
  }
}

// ===================================================================================
// MAIN SETUP
// ===================================================================================

void setup() {
  // 1. Initialize Serial Ports
  Serial.begin(115200); 

  delay(2000); 

  Serial.println("\n\n--- ESP8266 Ontime Bridge Starting ---");

  // --- LCD SETUP ---
  Wire.begin(D2, D1); 
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0, 0);
  lcd.print("Booting...");
  lcd.setCursor(0, 1);
  lcd.print("Ontime Bridge");
  delay(1000);

  // --- STABILITY FIX: Explicit Mode & No Sleep ---
  WiFi.mode(WIFI_STA); 
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // 2. Load Configuration from Flash
  if (SPIFFS.begin()) {
    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        JSONVar json = JSON.parse(buf.get());
        if (JSON.typeof(json) != "undefined") {
          if (json.hasOwnProperty("host")) strcpy(ontime_host, (const char*)json["host"]);
          if (json.hasOwnProperty("port")) ontime_port = (int)json["port"];
        }
        configFile.close();
      }
    }
  }

  // 3. Configure WiFiManager
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  char portStr[6];
  sprintf(portStr, "%d", ontime_port);
  WiFiManagerParameter custom_ontime_host("host", "Ontime Domain", ontime_host, 60);
  WiFiManagerParameter custom_ontime_port("port", "Ontime Port", portStr, 6);

  wifiManager.addParameter(&custom_ontime_host);
  wifiManager.addParameter(&custom_ontime_port);

  wifiManager.setConnectTimeout(30);

  Serial.println("[WiFi] Starting WiFi Manager...");
  lcd.clear();
  lcd.print("Connecting WiFi");
  
  if (!wifiManager.autoConnect("Ontime_Setup")) {
    Serial.println("[WiFi] Failed to connect to any network. Resetting...");
    lcd.setCursor(0, 1);
    lcd.print("Failed! Reset.");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  Serial.println("[WiFi] Connected!");
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.localIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP Address:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(3000); 

  // 4. Save New Configuration
  strcpy(ontime_host, custom_ontime_host.getValue());
  ontime_port = atoi(custom_ontime_port.getValue());

  if (shouldSaveConfig) {
    Serial.println("[Cfg] Saving new configuration...");
    JSONVar json;
    json["host"] = ontime_host;
    json["port"] = ontime_port;
    File configFile = SPIFFS.open("/config.json", "w");
    if (configFile) {
      configFile.print(JSON.stringify(json));
      configFile.close();
    }
  }

  // --- STABILITY FIX: Initialize Display Serial Later ---
  Serial1.begin(9600); 

  // 5. Start Secure WebSocket Connection
  Serial.printf("[WSc] Connecting to %s : %d ...\n", ontime_host, ontime_port);
  lcd.clear();
  lcd.print("Connecting WS...");

  // CRITICAL: Set Origin Header for NGINX Compatibility
  // Uses the configured host dynamically to ensure it works regardless of the domain
  String originHeader = "Origin: https://" + String(ontime_host);
  webSocket.setExtraHeaders(originHeader.c_str());

  webSocket.beginSSL(ontime_host, ontime_port, "/ws");
  webSocket.onEvent(webSocketEvent);
  
  webSocket.setReconnectInterval(5000);
  webSocket.enableHeartbeat(15000, 3000, 2);
}

// ===================================================================================
// MAIN LOOP
// ===================================================================================

void loop() {
  webSocket.loop();
  
  // Handle Title Scrolling independently of Websocket logic
  updateLcdTitle();
}