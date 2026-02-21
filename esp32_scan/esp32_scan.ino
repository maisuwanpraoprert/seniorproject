#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "maipromax";
const char* password = "17sep2003";

WebServer server(80);

void setup() {
  Serial.begin(115200);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("ESP32 IP address: ");
  Serial.println(WiFi.localIP());

  // ----- Register API routes -----
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.on("/data", handleData);

  server.begin();
  Serial.println("HTTP server started");
}


/* -------- Scan Parameters -------- */
bool scanning = false;
int currentHeight = 0;
const int maxHeight = 100;
const int stepHeight = 2;

/* -------- Simulate Detector -------- */
int simulateCounts(int height) {
  float base = 800;

  // profile หลัก
  float profile = base + 200 * sin(height * 0.15);

  // tray effects
  if (height > 20 && height < 30) profile -= 150;   // weeping
  if (height > 40 && height < 50) profile += 250;   // flooding
  if (height > 65 && height < 70) profile -= 200;   // collapsed tray

  int noise = random(-30, 30);

  return max(0, (int)(profile + noise));
}


void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}


/* -------- API HANDLERS -------- */
void handleStart() {
  sendCORS();
  scanning = true;
  currentHeight = 0;
  server.send(200, "text/plain", "START");
}


void handleStop() {
  sendCORS();
  scanning = false;
  server.send(200, "text/plain", "STOP");
}


void handleData() {
  sendCORS();

  if (!scanning) {
    server.send(200, "application/json", "{\"status\":\"idle\"}");
    return;
  }

  int counts = simulateCounts(currentHeight);

  String json = "{";
  json += "\"height\":" + String(currentHeight) + ",";
  json += "\"counts\":" + String(counts);
  json += "}";

  server.send(200, "application/json", json);

  currentHeight += stepHeight;
  if (currentHeight > maxHeight) scanning = false;
}


/* -------- LOOP -------- */
void loop() {
  server.handleClient();
}
