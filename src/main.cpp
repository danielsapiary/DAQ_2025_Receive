#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include <Arduino_JSON.h>
#include <Wire.h>
#include <CAN.h>

// WiFi Credentials
const char* ssid = "CEV_GOOBER";
const char* password = "G0Ob3rCEV!";

// Static IP Configuration
IPAddress local_IP(192, 168, 1, 242);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

// WebSocket & Server Setup
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
JSONVar readings;

// Timing Variables
unsigned long lastTime = 0;
const unsigned long updateInterval = 100; // Send data every 100ms (adjust as needed)

// Initialize WiFi
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nConnected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

// WebSocket Broadcast
void notifyClients(const String& sensorReadings) {
  ws.textAll(sensorReadings);
}

// Get Sensor Readings from CAN Bus
String getSensorReadings() {
  readings = JSONVar();

  while (CAN.parsePacket()) {
    long id = CAN.packetId();
    Serial.printf("Received CAN Packet ID: 0x%X\n", id);

    switch (id) {
      case 0x12: { // Steering Angle
        uint8_t high = CAN.read();
        uint8_t low = CAN.read();
        int steeringAngle = (high << 8) | low;
        readings["steering"] = steeringAngle;
        Serial.printf("Steering Angle: %d\n", steeringAngle);
        break;
      }
      case 0x15: { // RPM
        uint8_t leftHigh = CAN.read(), leftLow = CAN.read();
        uint8_t rightHigh = CAN.read(), rightLow = CAN.read();
        int leftRPM = (leftHigh << 8) | leftLow;
        int rightRPM = (rightHigh << 8) | rightLow;
        readings["left_rpm"] = leftRPM;
        readings["right_rpm"] = rightRPM;
        Serial.printf("Left RPM: %d | Right RPM: %d\n", leftRPM, rightRPM);
        break;
      }
      case 0x18: { // Accelerometer Data
        uint8_t xHigh = CAN.read(), xLow = CAN.read();
        uint8_t yHigh = CAN.read(), yLow = CAN.read();
        uint8_t zHigh = CAN.read(), zLow = CAN.read();
        int x = (xHigh << 8) | xLow;
        int y = (yHigh << 8) | yLow;
        int z = (zHigh << 8) | zLow;
        readings["x_accel"] = x;
        readings["y_accel"] = y;
        readings["z_accel"] = z;
        Serial.printf("Accel X: %d | Y: %d | Z: %d\n", x, y, z);
        break;
      }
      default:
        Serial.printf("Unknown Packet ID: 0x%X\n", id);
        break;
    }
  }

  String jsonString = JSON.stringify(readings);
  return jsonString;
}

// WebSocket Event Handler
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->opcode == WS_TEXT) {
    String sensorReadings = getSensorReadings();
    notifyClients(sensorReadings);
  }
}

// WebSocket Event Management
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("Client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    default:
      break;
  }
}

// Initialize WebSocket
void initWebSocket() {
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
}

// Setup Function
void setup() {
  Serial.begin(115200);

  // Configure Static IP
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }
  
  initWiFi();
  initWebSocket();
  server.begin();

  // Initialize CAN Bus
  Serial.println("Initializing CAN Bus...");
  if (!CAN.begin(500E3)) {
    Serial.println("Starting CAN failed!");
    while (1);
  }
}

// Loop Function
void loop() {
  if ((millis() - lastTime) >= updateInterval) {
    lastTime = millis();
    String sensorReadings = getSensorReadings();
    notifyClients(sensorReadings);
  }

  // WebSocket Client Cleanup
  ws.cleanupClients();
}
