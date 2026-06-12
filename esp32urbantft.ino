/*
 * SEMERU TEAM - ESP32 URBAN GATEWAY
 * Menerima JSON dari STM32 via PA9 -> GPIO18
 * 
 * Koneksi:
 * STM32 PA9 (TX) ---> ESP32 GPIO18 (RX)
 * STM32 GND      ---> ESP32 GND
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ========== KONFIGURASI ==========
const char* ssid     = "VERITAS";
const char* password = "bundahara45";
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;

// ========== TOPIK MQTT ==========
const char* topic_data   = "semeru/urban/data";
const char* topic_status = "semeru/urban/status";

// ========== PIN ==========
#define RX_PIN 18
#define LED_WIFI   2
#define LED_DATA   4

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ========== VARIABEL ==========
String inputBuffer = "";
unsigned long lastStatusTime = 0;
unsigned long lastPacketTime = 0;
unsigned long lastMqttReconnect = 0;
int packetCount = 0;
bool stm32Connected = false;

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, -1);
  
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_DATA, OUTPUT);
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_DATA, HIGH);
  
  inputBuffer.reserve(512);
  
  Serial.println("\n");
  Serial.println("╔════════════════════════════════════════════════════════════╗");
  Serial.println("║     SEMERU URBAN GATEWAY - ESP32 (JSON from STM32)        ║");
  Serial.println("╠════════════════════════════════════════════════════════════╣");
  Serial.println("║ Koneksi: STM32 PA9 (TX) ---> ESP32 GPIO18 (RX)            ║");
  Serial.println("║ Baud Rate: 9600                                           ║");
  Serial.println("║ MQTT: broker.emqx.io:1883                                 ║");
  Serial.println("╚════════════════════════════════════════════════════════════╝");
  Serial.println();
  
  // WiFi
  Serial.print("[WiFi] Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH);
    Serial.println("\n[WiFi] ✅ CONNECTED!");
    Serial.print("       IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] ❌ FAILED!");
  }
  
  // MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  connectMQTT();
}

// ========== CONNECT MQTT ==========
void connectMQTT() {
  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED) {
    String clientId = "SEMERU_URBAN_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    
    Serial.print("[MQTT] Connecting...");
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" ✅");
      mqttClient.publish(topic_status, "online");
    } else {
      Serial.print(" ❌ rc=");
      Serial.println(mqttClient.state());
    }
  }
}

// ========== LOOP ==========
void loop() {
  // Maintain connections
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_WIFI, LOW);
    ESP.restart();
  }
  
  if (!mqttClient.connected()) {
    connectMQTT();
  } else {
    mqttClient.loop();
  }
  
  // Status every 30 seconds
  if (mqttClient.connected() && (millis() - lastStatusTime > 30000)) {
    StaticJsonDocument<64> status;
    status["packets"] = packetCount;
    status["stm32"] = stm32Connected ? 1 : 0;
    String msg;
    serializeJson(status, msg);
    mqttClient.publish(topic_status, msg.c_str());
    lastStatusTime = millis();
  }
  
  // ========== READ JSON FROM STM32 ==========
  while (Serial2.available()) {
    char c = Serial2.read();
    
    if (c == '\n') {
      if (inputBuffer.length() > 0) {
        inputBuffer.trim();
        
        if (inputBuffer.startsWith("{") && inputBuffer.endsWith("}")) {
          // Ini JSON, proses dan kirim ke MQTT
          processAndForward(inputBuffer);
          stm32Connected = true;
          lastPacketTime = millis();
          
          // LED kedip
          digitalWrite(LED_DATA, LOW);
          delay(5);
          digitalWrite(LED_DATA, HIGH);
        }
      }
      inputBuffer = "";
    } 
    else if (c != '\r') {
      inputBuffer += c;
      if (inputBuffer.length() > 1000) inputBuffer = "";
    }
  }
  
  // Check STM32 connection
  if (stm32Connected && (millis() - lastPacketTime > 5000)) {
    stm32Connected = false;
    Serial.println("[STM32] ⚠️ LOST CONNECTION");
  }
  
  // Warning jika tidak ada data
  static unsigned long lastWarn = 0;
  if (!stm32Connected && (millis() - lastWarn > 10000)) {
    Serial.println("[WAITING] Menunggu data dari STM32...");
    lastWarn = millis();
  }
  
  delay(1);
}

// ========== PROSES DAN FORWARD KE MQTT ==========
void processAndForward(String jsonString) {
  StaticJsonDocument<384> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.print("[ERROR] JSON: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Ekstrak data untuk log
  int kecepatan = doc["kecepatan"] | 0;
  int rpm = doc["rpm"] | 0;
  float afr = doc["afr"] | 0;
  int suhu = doc["suhu"] | 0;
  int tps = doc["tps"] | 0;
  int packet = doc["packet"] | 0;
  
  packetCount++;
  
  // Kirim ke MQTT (forward apa adanya, tambah altitude)
  doc["altitude"] = 125;
  
  String output;
  serializeJson(doc, output);
  
  if (mqttClient.connected()) {
    if (mqttClient.publish(topic_data, output.c_str())) {
      Serial.print("[OK] #");
      Serial.print(packet);
      Serial.print(" | SPD:");
      Serial.print(kecepatan);
      Serial.print(" | RPM:");
      Serial.print(rpm);
      Serial.print(" | AFR:");
      Serial.print(afr);
      Serial.print(" | TEMP:");
      Serial.print(suhu);
      Serial.print(" | TPS:");
      Serial.println(tps);
    } else {
      Serial.println("[FAILED] MQTT publish");
    }
  } else {
    Serial.println("[MQTT] Not connected");
  }
}