#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <OneButton.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define BUTTON_PIN    0  // Flash button on NodeMCU (GPIO 0)
#define SDA_PIN       4  // D2 on NodeMCU
#define SCL_PIN       5  // D1 on NodeMCU
#define BUZZER_PIN    14 // D5 on NodeMCU

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 18000); // GMT +5:00
OneButton button(BUTTON_PIN, true);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String chipId = String(ESP.getChipId(), HEX);
String securityPin = "1234"; // Default PIN
const char* mqttServer = "broker.hivemq.com";
unsigned long lastMqttRetry = 0;
unsigned long lastStatusUpdate = 0;

// Alarm System
struct Alarm {
  int hour;
  int minute;
  bool active;
};
Alarm alarms[5];
bool isAlarmTriggered = false;

void saveAlarms() {
  File f = LittleFS.open("/alarms.bin", "w");
  if (f) {
    f.write((uint8_t*)alarms, sizeof(alarms));
    f.close();
  }
}

void loadAlarms() {
  if (LittleFS.exists("/alarms.bin")) {
    File f = LittleFS.open("/alarms.bin", "r");
    if (f) {
      f.read((uint8_t*)alarms, sizeof(alarms));
      f.close();
    }
  } else {
    for(int i=0; i<5; i++) alarms[i] = {0, 0, false};
  }
}

// Animation State
int pupil_x = 0, pupil_y = 0;
int target_x = 0, target_y = 0;
unsigned long next_action = 0;
bool is_blinking = false;
unsigned long blink_start = 0;

// Smart Watch State
enum DisplayState { EYES, WATCH, MESSAGE, CONNECTING };
DisplayState currentState = EYES;
unsigned long stateStartTime = 0;
String currentMsg = "";
String presets[5] = {"Miss you!", "Love you!", "Call me", "Where are you?", "Ummah!"};

void saveSettings() {
  File f = LittleFS.open("/settings.bin", "w");
  if (f) {
    f.println(securityPin);
    f.close();
  }
}

void savePresets() {
  File f = LittleFS.open("/presets.txt", "w");
  if (f) {
    for (int i = 0; i < 5; i++) f.println(presets[i]);
    f.close();
  }
}

void loadSettings() {
  if (LittleFS.exists("/settings.bin")) {
    File f = LittleFS.open("/settings.bin", "r");
    if (f) {
      securityPin = f.readStringUntil('\n');
      securityPin.trim();
      f.close();
    }
  }
}

void loadPresets() {
  if (LittleFS.exists("/presets.txt")) {
    File f = LittleFS.open("/presets.txt", "r");
    if (f) {
      for (int i = 0; i < 5; i++) {
        if (f.available()) presets[i] = f.readStringUntil('\n');
        presets[i].trim();
      }
      f.close();
    }
  }
}

// Web Server Handlers
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Smatu Dashboard</title>";
  html += "<meta name='theme-color' content='#0f172a'>";
  html += "<link rel='manifest' href='/manifest.json'>";
  html += "<link rel='apple-touch-icon' href='https://cdn-icons-png.flaticon.com/512/2592/2592201.png'>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;600&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += ":root{--primary:#8b5cf6; --secondary:#3b82f6; --bg:#0f172a; --card:rgba(255,255,255,0.05); --text:#f8fafc;}";
  html += "*{margin:0; padding:0; box-sizing:border-box; font-family:'Poppins', sans-serif;}";
  html += "body{background:linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%); color:var(--text); min-height:100vh; display:flex; flex-direction:column;}";
  
  // Header
  html += "header{background:rgba(15,23,42,0.8); backdrop-filter:blur(10px); padding:15px 25px; border-bottom:1px solid rgba(255,255,255,0.1); display:flex; justify-content:space-between; align-items:center; position:sticky; top:0; z-index:100;}";
  html += ".logo{font-size:1.5rem; font-weight:600; background:linear-gradient(to right, #8b5cf6, #3b82f6); -webkit-background-clip:text; -webkit-text-fill-color:transparent;}";
  html += ".status-chip{background:rgba(16,185,129,0.2); color:#10b981; padding:5px 12px; border-radius:20px; font-size:0.8rem; font-weight:600; border:1px solid rgba(16,185,129,0.3);}";

  // Layout
  html += ".main-container{display:flex; flex:1; overflow:hidden;}";
  html += "nav{width:260px; background:rgba(255,255,255,0.02); border-right:1px solid rgba(255,255,255,0.05); padding:20px; display:flex; flex-direction:column; gap:10px;}";
  html += ".nav-item{padding:12px 15px; border-radius:12px; cursor:pointer; transition:0.3s; display:flex; align-items:center; gap:12px; color:rgba(255,255,255,0.6);}";
  html += ".nav-item:hover, .nav-item.active{background:rgba(139,92,246,0.1); color:white;}";
  html += ".content-area{flex:1; padding:30px; overflow-y:auto; scroll-behavior:smooth;}";

  // Components
  html += ".card{background:var(--card); backdrop-filter:blur(20px); padding:30px; border-radius:24px; border:1px solid rgba(255,255,255,0.1); box-shadow:0 20px 40px rgba(0,0,0,0.3); max-width:600px; margin:0 auto; animation:fadeIn 0.5s ease-out;}";
  html += "@keyframes fadeIn{from{opacity:0; transform:translateY(20px);} to{opacity:1; transform:translateY(0);}}";
  html += "h2{margin-bottom:20px; font-weight:600;}";
  html += "input[type='text'], input[type='number'], input[type='password']{width:100%; padding:14px; background:rgba(255,255,255,0.05); border:1px solid rgba(255,255,255,0.1); border-radius:12px; color:white; margin-bottom:15px; outline:none; transition:0.3s;}";
  html += "input:focus{border-color:var(--primary); box-shadow:0 0 0 2px rgba(139,92,246,0.2);}";
  html += "button{width:100%; padding:14px; background:linear-gradient(to right, #8b5cf6, #3b82f6); border:none; border-radius:12px; color:white; font-weight:600; cursor:pointer; transition:0.3s;}";
  html += "button:hover{transform:translateY(-2px); box-shadow:0 10px 20px rgba(139,92,246,0.3);}";
  html += ".danger-btn{background:rgba(239,68,68,0.1); color:#ef4444; border:1px solid rgba(239,68,68,0.2);}";
  html += ".danger-btn:hover{background:#ef4444; color:white;}";

  // Responsive
  html += "@media (max-width:768px){ .main-container{flex-direction:column;} nav{width:100%; border-right:none; border-bottom:1px solid rgba(255,255,255,0.05); flex-direction:row; overflow-x:auto; padding:10px;} .nav-item{padding:8px 15px; white-space:nowrap;} .content-area{padding:20px;} }";
  html += "</style></head><body>";

  html += "<header><div class='logo'>SMATU</div><div class='status-chip' id='active-status'>Connecting...</div></header>";

  html += "<div class='main-container'>";
  html += "<nav>";
  html += "<div class='nav-item active' onclick='showTab(\"chat\")'>💬 Chat</div>";
  html += "<div class='nav-item' onclick='showTab(\"alarms\")'>⏰ Alarms</div>";
  html += "<div class='nav-item' onclick='showTab(\"presets\")'>⭐ Presets</div>";
  html += "<div class='nav-item' onclick='showTab(\"wifi\")'>📶 Network</div>";
  html += "<div class='nav-item' onclick='showTab(\"settings\")'>⚙️ System</div>";
  html += "</nav>";

  html += "<div class='content-area'>";
  
  // Chat Section
  html += "<div id='tab-chat' class='card'><h2>Send Message</h2><form action='/msg'><input name='m' maxlength='20' placeholder='Type a message...' autocomplete='off'><button>Send to Eyes</button></form></div>";

  // Alarms Section
  html += "<div id='tab-alarms' class='card' style='display:none;'><h2>Set Alarms</h2><form action='/set-alarms'>";
  for (int i = 0; i < 5; i++) {
    html += "<div style='display:flex; align-items:center; gap:10px; margin-bottom:15px; background:rgba(255,255,255,0.02); padding:10px; border-radius:12px;'>";
    html += "<span>#"+String(i+1)+"</span>";
    html += "<input type='number' name='h"+String(i)+"' min='0' max='23' value='"+String(alarms[i].hour)+"' style='margin:0; flex:1;'>";
    html += "<span>:</span>";
    html += "<input type='number' name='m"+String(i)+"' min='0' max='59' value='"+String(alarms[i].minute)+"' style='margin:0; flex:1;'>";
    html += "<input type='checkbox' name='a"+String(i)+"' "+(alarms[i].active?"checked":"")+"></div>";
  }
  html += "<button>Update Alarms</button></form></div>";

  // Presets Section
  html += "<div id='tab-presets' class='card' style='display:none;'><h2>Edit Presets</h2><form action='/save'>";
  for (int i = 0; i < 5; i++) {
    html += "<input name='p"+String(i)+"' value='"+presets[i]+"' maxlength='20' placeholder='Preset "+String(i+1)+"'>";
  }
  html += "<button>Save Presets</button></form></div>";

  // WiFi Section
  html += "<div id='tab-wifi' class='card' style='display:none;'><h2>WiFi Scanner</h2><div id='wifi-list'><button onclick='scanWiFi()'>Scan Nearby Networks</button></div>";
  html += "<div id='wifi-form' style='display:none; margin-top:20px;'><input id='ssid' placeholder='SSID'><input id='pw' type='password' placeholder='Password'><button onclick='connectWiFi()'>Connect Now</button></div>";
  html += "<div style='margin-top:30px; border-top:1px solid rgba(255,255,255,0.1); padding-top:20px;'>";
  html += "<p style='font-size:0.8rem; opacity:0.5; margin-bottom:10px;'>Want to change WiFi? This will restart the setup hotspot.</p>";
  html += "<form action='/reset-wifi'><button class='danger-btn'>Disconnect & Forget WiFi</button></form></div></div>";

  // Settings Section
  html += "<div id='tab-settings' class='card' style='display:none;'><h2>System Settings</h2>";
  html += "<div style='background:rgba(255,255,255,0.05); padding:15px; border-radius:12px; margin-bottom:20px;'>";
  html += "<small style='opacity:0.6; display:block;'>Unique Device ID (Global Access)</small>";
  html += "<code style='font-size:1.2rem; color:var(--primary); font-family:monospace;'>" + chipId + "</code>";
  html += "<p style='font-size:0.8rem; margin-top:5px; opacity:0.5;'>Use this ID in your Smatu Global App to send messages from anywhere.</p></div>";
  html += "<div style='margin-bottom:20px;'>";
  html += "<label style='font-size:0.8rem; opacity:0.6;'>Security PIN</label>";
  html += "<form action='/set-pin' style='display:flex; gap:10px;'>";
  html += "<input name='pin' value='"+securityPin+"' maxlength='4' style='margin:0; width:100px;'>";
  html += "<button style='width:auto; padding:10px 20px;'>Save PIN</button></form></div>";
  html += "<p style='margin-bottom:20px; opacity:0.6;'>Resetting WiFi will erase saved credentials and restart the setup hotspot.</p><form action='/reset-wifi'><button class='danger-btn'>Full System Reset</button></form></div>";

  html += "</div></div>"; // end content-area & main-container

  html += "<footer style='text-align:center; padding:20px; opacity:0.3; font-size:0.8rem;'>&copy; 2026 SMATU PRODUCTS | MADE WITH LOVE</footer>";

  // JavaScript
  html += "<script>";
  html += "function showTab(id){ document.querySelectorAll('.card').forEach(c=>c.style.display='none'); document.querySelectorAll('.nav-item').forEach(n=>n.classList.remove('active'));";
  html += "document.getElementById('tab-'+id).style.display='block'; event.currentTarget.classList.add('active'); }";
  
  html += "function scanWiFi(){ document.getElementById('wifi-list').innerHTML='<p>Scanning...</p>'; fetch('/api/scan').then(r=>r.json()).then(data=>{";
  html += "let h=''; data.forEach(net=>{ h+='<div style=\"padding:12px; cursor:pointer; background:rgba(255,255,255,0.03); margin-bottom:5px; border-radius:10px; display:flex; justify-content:space-between;\" onclick=\"selectSSID(\\''+net.s+'\\')\"><span>'+net.s+'</span><small style=\"opacity:0.5\">'+net.r+' dBm</small></div>'; });";
  html += "document.getElementById('wifi-list').innerHTML=h; }); }";
  
  html += "function selectSSID(s){ document.getElementById('ssid').value=s; document.getElementById('wifi-form').style.display='block'; }";
  html += "function connectWiFi(){ let s=document.getElementById('ssid').value; let p=document.getElementById('pw').value;";
  html += "fetch('/api/connect?s='+encodeURIComponent(s)+'&p='+encodeURIComponent(p)).then(r=>alert('Connecting... ESP will restart.')); }";

  html += "setInterval(()=>{ fetch('/api/status').then(r=>r.json()).then(d=>{ document.getElementById('active-status').innerText=d.status; }); }, 3000);";
  html += "if ('serviceWorker' in navigator) { window.addEventListener('load', function() { navigator.serviceWorker.register('/sw.js'); }); }";
  html += "</script></body></html>";

  server.send(200, "text/html", html);
}

void handleManifest() {
  String json = "{\"name\":\"Smatu Controller\",\"short_name\":\"Smatu\",\"start_url\":\"/\",\"display\":\"standalone\",\"background_color\":\"#0f172a\",\"theme_color\":\"#8b5cf6\",\"icons\":[{\"src\":\"https://cdn-icons-png.flaticon.com/512/2592/2592201.png\",\"sizes\":\"512x512\",\"type\":\"image/png\"}]}";
  server.send(200, "application/json", json);
}

void handleSW() {
  String sw = "self.addEventListener('install', (e) => { }); self.addEventListener('fetch', (f) => { f.respondWith(fetch(f.request)); });";
  server.send(200, "application/javascript", sw);
}

void handleApiStatus() {
  String statusText = "👀 Robotic Eyes";
  if (currentState == MESSAGE) statusText = "💬 " + currentMsg;
  else if (currentState == WATCH) statusText = "⌚ Digital Watch";
  String json = "{\"status\":\"" + statusText + "\"}";
  server.send(200, "application/json", json);
}

void handleApiScan() {
  int n = WiFi.scanNetworks(false, true); // false = synchronous, true = show hidden
  String json = "[";
  for (int i = 0; i < n; ++i) {
    String ssid = WiFi.SSID(i);
    if (ssid == "") ssid = "(Hidden Network)";
    json += "{\"s\":\"" + ssid + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
    if (i < n - 1) json += ",";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleApiConnect() {
  String ssid = server.arg("s");
  String pass = server.arg("p");
  server.send(200, "text/plain", "OK");
  delay(1000);
  WiFi.begin(ssid.c_str(), pass.c_str());
}

void notifyMessage() {
  digitalWrite(LED_BUILTIN, LOW); // LED ON
  tone(BUZZER_PIN, 2000, 100);    // Beep 1
  delay(150);
  tone(BUZZER_PIN, 2000, 100);    // Beep 2
  digitalWrite(LED_BUILTIN, HIGH); // LED OFF
}

void sendToCloud(String msg) {
    if (mqttClient.connected()) {
        String topic = "smatu/device/" + chipId;
        mqttClient.publish(topic.c_str(), msg.c_str());
        Serial.print("Sent to Cloud: ");
        Serial.println(msg);
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String rawMsg = "";
  for (int i = 0; i < length; i++) rawMsg += (char)payload[i];
  Serial.print("MQTT Received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(rawMsg);
  
  if (rawMsg.startsWith("{")) {
      // Security Check: Message must contain the correct PIN
      if (rawMsg.indexOf("\"pin\":\"" + securityPin + "\"") < 0) {
          Serial.println("Unauthorized JSON attempt! PIN mismatch.");
          return;
      }
      Serial.println("Secure JSON command accepted.");

      if (rawMsg.indexOf("\"type\":\"alarm\"") > 0) {
          int idx = rawMsg.substring(rawMsg.indexOf("\"i\":")+4).toInt();
          alarms[idx].hour = rawMsg.substring(rawMsg.indexOf("\"h\":")+4).toInt();
          alarms[idx].minute = rawMsg.substring(rawMsg.indexOf("\"m\":")+4).toInt();
          alarms[idx].active = (rawMsg.indexOf("\"a\":true") > 0);
          saveAlarms();
      } else if (rawMsg.indexOf("\"type\":\"preset\"") > 0) {
          int idx = rawMsg.substring(rawMsg.indexOf("\"i\":")+4).toInt();
          int start = rawMsg.indexOf("\"v\":\"") + 5;
          int end = rawMsg.indexOf("\"", start);
          presets[idx] = rawMsg.substring(start, end);
          savePresets();
      }
  } else {
      // Regular Message
      rawMsg.trim(); // Clean up the message
      currentMsg = rawMsg;
      currentState = MESSAGE;
      stateStartTime = millis();
      Serial.print("Displaying Message: ");
      Serial.println(currentMsg);
      notifyMessage();
  }
}

void reconnectMqtt() {
  if (millis() - lastMqttRetry > 5000) {
    lastMqttRetry = millis();
    String clientId = "Smatu-" + chipId;
    if (mqttClient.connect(clientId.c_str())) {
      String topic = "smatu/app/" + chipId;
      mqttClient.subscribe(topic.c_str());
      Serial.println("MQTT Connected to: " + topic);
    }
  }
}

void handleMsg() {
  currentMsg = server.arg("m");
  if (currentMsg.length() > 20) currentMsg = currentMsg.substring(0, 20);
  Serial.print("New Message: ");
  Serial.println(currentMsg);
  currentState = MESSAGE;
  stateStartTime = millis();
  notifyMessage();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSave() {
  for (int i = 0; i < 5; i++) {
    String p = "p" + String(i);
    if (server.hasArg(p)) presets[i] = server.arg(p);
  }
  savePresets();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSetAlarms() {
  for (int i = 0; i < 5; i++) {
    alarms[i].hour = server.arg("h" + String(i)).toInt();
    alarms[i].minute = server.arg("m" + String(i)).toInt();
    alarms[i].active = server.hasArg("a" + String(i));
  }
  saveAlarms();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleResetWiFi() {
  server.send(200, "text/plain", "WiFi Settings Reset. ESP8266 will restart and create 'Smatu-Setup' AP.");
  delay(1000);
  WiFiManager wm;
  wm.resetSettings();
  ESP.restart();
}

// Button Events
void handleClick() { 
    if (isAlarmTriggered) {
        isAlarmTriggered = false;
        noTone(BUZZER_PIN);
    } else {
        currentMsg = presets[0]; 
        currentState = MESSAGE; 
        stateStartTime = millis(); 
        sendToCloud(currentMsg);
    }
}
void handleDoubleClick() { 
    currentMsg = presets[1]; 
    currentState = MESSAGE; 
    stateStartTime = millis(); 
    sendToCloud(currentMsg);
}

void handleLongPressStart() { currentState = WATCH; stateStartTime = millis(); }
void handleLongPressStop() { currentState = EYES; }

void drawEye(int x, int y, int pX, int pY, bool blink) {
    if (blink) {
        display.fillRoundRect(x - 20, y - 2, 40, 4, 2, SSD1306_WHITE);
    } else {
        display.drawRoundRect(x - 25, y - 20, 50, 40, 15, SSD1306_WHITE);
        display.fillCircle(x + pX, y + pY, 10, SSD1306_WHITE);
        display.fillCircle(x + pX - 3, y + pY - 3, 3, SSD1306_BLACK);
    }
}

void showWatch() {
    timeClient.update();
    unsigned long elapsed = millis() - stateStartTime;
    display.setTextColor(SSD1306_WHITE);
    
    if (elapsed < 3000) {
        display.setTextSize(3);
        String timeStr = timeClient.getFormattedTime().substring(0, 5);
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(timeStr, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 20);
        display.print(timeStr);
        
        display.setTextSize(1);
        String secStr = ":" + timeClient.getFormattedTime().substring(6);
        display.setCursor((128 - w) / 2 + w + 2, 34);
        display.print(secStr);
    } else if (elapsed < 6000) {
        // Show Day & Actual Date
        time_t rawTime = timeClient.getEpochTime();
        struct tm * ti;
        ti = localtime(&rawTime);
        
        const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        String dayName = days[ti->tm_wday];
        
        display.setTextSize(2);
        int16_t x1, y1; uint16_t w, h;
        display.getTextBounds(dayName, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 15);
        display.print(dayName);
        
        // Date: DD-MM-YYYY
        char dateBuf[12];
        sprintf(dateBuf, "%02d-%02d-%04d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
        display.setTextSize(1);
        display.getTextBounds(dateBuf, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 40);
        display.print(dateBuf);
    } else {
        // Show Device ID
        display.setTextSize(1);
        display.setCursor(25, 15);
        display.print("DEVICE INFO");
        display.setTextSize(2);
        display.setCursor(15, 30);
        display.print(chipId);
        display.setTextSize(1);
        display.setCursor(20, 50);
        display.print("Global ID Mode");
    }
}

void showMessage() {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(currentMsg, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, (64 - h) / 2);
    display.print(currentMsg);
    
    if (millis() - stateStartTime > 10000) {
        currentState = EYES;
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) for(;;);
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 25);
    display.println("Smatu: Connect to");
    display.setCursor(20, 35);
    display.println("'Smatu-Setup' WiFi");
    display.setCursor(20, 50);
    display.print("ID: ");
    display.println(chipId);
    display.display();

    LittleFS.begin();
    loadPresets();

    WiFiManager wm;
    // Connects to last WiFi or starts "Smatu-Setup" Access Point
    if (!wm.autoConnect("Smatu-Setup")) {
        Serial.println("Failed to connect and hit timeout");
        ESP.restart();
    }
    
    if (MDNS.begin("smatu")) {
        Serial.println("MDNS responder started: http://smatu.local");
    }
    
    timeClient.begin();
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // OFF (Active Low)
    loadAlarms();
    
    button.attachClick(handleClick);
    button.attachDoubleClick(handleDoubleClick);
    button.attachMultiClick([]() {
        int n = button.getNumberClicks();
        if (n == 3) {
            currentMsg = presets[2];
            currentState = MESSAGE;
            stateStartTime = millis();
            sendToCloud(currentMsg);
        }
    });
    button.attachLongPressStart(handleLongPressStart);
    button.attachLongPressStop(handleLongPressStop);

    server.on("/", handleRoot);
    server.on("/msg", handleMsg);
    server.on("/save", handleSave);
    server.on("/set-alarms", handleSetAlarms);
    server.on("/reset-wifi", handleResetWiFi);
    server.on("/manifest.json", handleManifest);
    server.on("/sw.js", handleSW);
    server.on("/api/status", handleApiStatus);
    server.on("/api/scan", handleApiScan);
    server.on("/api/connect", handleApiConnect);
    server.on("/set-pin", []() {
        securityPin = server.arg("pin");
        saveSettings();
        server.sendHeader("Location", "/");
        server.send(303);
    });
    server.begin();

    ArduinoOTA.setHostname("Smatu-Device");
    ArduinoOTA.begin();

    mqttClient.setServer(mqttServer, 1883);
    mqttClient.setCallback(mqttCallback);
}

void loop() {
    button.tick();
    
    if (WiFi.status() == WL_CONNECTED) {
        server.handleClient();
        timeClient.update();
        ArduinoOTA.handle();
        if (!mqttClient.connected()) reconnectMqtt();
        mqttClient.loop();

        // Publish Status to Cloud every 5 seconds
        if (millis() - lastStatusUpdate > 5000) {
            lastStatusUpdate = millis();
            String status = "{\"state\":";
            status += String(currentState);
            status += ",\"msg\":\"" + currentMsg + "\"}";
            String topic = "smatu/status/" + chipId;
            mqttClient.publish(topic.c_str(), status.c_str());
        }
    }
    
    display.clearDisplay();

    if (currentState == WATCH) {
        showWatch();
    } else if (currentState == MESSAGE) {
        showMessage();
    } else {
        unsigned long now = millis();
        if (now > next_action) {
            int choice = random(0, 10);
            if (choice < 2) { 
                is_blinking = true; 
                blink_start = now; 
                next_action = now + 200; 
            } else { 
                target_x = random(-12, 12); 
                target_y = random(-8, 8); 
                next_action = now + random(1000, 3000); 
            }
        }
        if (pupil_x < target_x) { pupil_x++; }
        if (pupil_x > target_x) { pupil_x--; }
        if (pupil_y < target_y) { pupil_y++; }
        if (pupil_y > target_y) { pupil_y--; }
        if (is_blinking && (now - blink_start > 150)) is_blinking = false;
        drawEye(35, 32, pupil_x, pupil_y, is_blinking);
        drawEye(93, 32, pupil_x, pupil_y, is_blinking);
    }

    // Alarm Logic
    if (isAlarmTriggered) {
        // High-pitched Persistent Alarm
        if (millis() % 1000 < 700) {
            tone(BUZZER_PIN, 2500); // 2.5kHz Tone
        } else {
            noTone(BUZZER_PIN);
        }
    } else {
        int h = timeClient.getHours();
        int m = timeClient.getMinutes();
        int s = timeClient.getSeconds();
        
        if (s == 0) { // Check only at the start of every minute
            for (int i = 0; i < 5; i++) {
                if (alarms[i].active && alarms[i].hour == h && alarms[i].minute == m) {
                    isAlarmTriggered = true;
                    break;
                }
            }
        }
    }

    display.display();
    delay(10);
}
