/*
 * SEMERU TEAM - ESP32 URBAN GATEWAY (FINAL)
 * Menerima data dari STM32 via Serial2 (GPIO18) dengan baud rate 9600
 * Mengirim ke MQTT broker EMQX
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ========== KONFIGURASI WiFi ==========
const char* ssid     = "  PROTO";      // dua spasi di depan
const char* password = "2500kmll";

// ========== KONFIGURASI MQTT ==========
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;
const char* mqtt_client_id = "SEMERU_PROTO_GATEWAY_001";

// Topik MQTT
const char* topic_data   = "semeru/proto/data";
const char* topic_speed  = "semeru/proto/speed";
const char* topic_rpm    = "semeru/proto/rpm";
const char* topic_afr    = "semeru/proto/afr";
const char* topic_tps    = "semeru/proto/tps";
const char* topic_temp   = "semeru/proto/temp";
const char* topic_status = "semeru/proto/status";

// ========== PIN ==========
#define RX_PIN 18        // Menerima dari STM32 PA9 (TX)
#define TX_PIN 19        // Tidak digunakan (opsional)
#define LED_WIFI 2
#define LED_DATA 4

WiFiClient espClient;
PubSubClient mqttClient(espClient);

String inputBuffer = "";
unsigned long lastStatusTime = 0;
const unsigned long statusInterval = 30000;
unsigned long lastReconnectAttempt = 0;
int packetCount = 0;
bool stm32Connected = false;
unsigned long lastPacketTime = 0;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_DATA, OUTPUT);
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_DATA, HIGH);
  
  inputBuffer.reserve(512);
  delay(500);
  
  Serial.println("\n╔══════════════════════════════════════════════════════════╗");
  Serial.println("║   SEMERU PROTO GATEWAY - ESP32 (STM32 -> MQTT)         ║");
  Serial.println("╠══════════════════════════════════════════════════════════╣");
  Serial.println("║ WiFi SSID :   PROTO                                    ║");
  Serial.println("║ MQTT      : broker.emqx.io:1883                        ║");
  Serial.println("║ Topik     : semeru/proto/*                             ║");
  Serial.println("║ RX Pin    : GPIO18 (from STM32 PA9)                    ║");
  Serial.println("╚══════════════════════════════════════════════════════════╝\n");
  
  setupWiFi();
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);
  connectMQTT();
}

void loop() {
  // Jaga koneksi WiFi
  if (WiFi.status() != WL_CONNECTED) {
    setupWiFi();
  }
  
  // Jaga koneksi MQTT
  if (!mqttClient.connected()) {
    if (millis() - lastReconnectAttempt > 5000) {
      connectMQTT();
      lastReconnectAttempt = millis();
    }
  } else {
    mqttClient.loop();
    
    // Kirim status "online" setiap 30 detik
    if (millis() - lastStatusTime > statusInterval) {
      if (mqttClient.publish(topic_status, "online")) {
        Serial.println("[MQTT] Status: online");
      } else {
        Serial.println("[MQTT] Gagal publish status");
      }
      lastStatusTime = millis();
    }
  }
  
  // ---------- Baca data dari STM32 ----------
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      if (inputBuffer.length() > 0) {
        inputBuffer.trim();
        
        if (inputBuffer.startsWith("{") && inputBuffer.endsWith("}")) {
          processAndForwardJSON(inputBuffer);
          stm32Connected = true;
          lastPacketTime = millis();
          
          // Blink LED data
          digitalWrite(LED_DATA, LOW);
          delay(5);
          digitalWrite(LED_DATA, HIGH);
        }
        else if (inputBuffer == "#ESP_CONNECTED#") {
          Serial.println("[STM32] ✅ Siap mengirim data");
        }
        else if (inputBuffer.length() < 100) {
          Serial.print("[DEBUG] Non-JSON: ");
          Serial.println(inputBuffer);
        }
      }
      inputBuffer = "";
    } else if (c != '\r') {
      inputBuffer += c;
      if (inputBuffer.length() > 500) {
        Serial.println("[WARN] Buffer overflow");
        inputBuffer = "";
      }
    }
  }
  
  // Deteksi koneksi STM32 putus
  if (stm32Connected && (millis() - lastPacketTime > 5000)) {
    stm32Connected = false;
    Serial.println("[STM32] ⚠️ Tidak ada data selama 5 detik");
  }
  
  // Perintah dari Serial Monitor (debug)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    handleCommand(cmd);
  }
}

// ========== WiFi ==========
void setupWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  
  Serial.print("[WiFi] Menghubungkan ke '");
  Serial.print(ssid);
  Serial.print("' ...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH);
    Serial.println(" ✅");
    Serial.print("       IP: ");
    Serial.println(WiFi.localIP());
  } else {
    digitalWrite(LED_WIFI, LOW);
    Serial.println(" ❌ Gagal");
  }
}

// ========== MQTT ==========
void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT] WiFi belum tersedia");
    return;
  }
  
  Serial.print("[MQTT] Menghubungkan ...");
  
  // Generate client ID unik
  String clientId = String(mqtt_client_id) + "_" + String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str())) {
    Serial.println(" ✅ Terhubung ke EMQX");
    mqttClient.publish(topic_status, "online");
    mqttClient.subscribe("semeru/proto/command");
  } else {
    Serial.print(" ❌ Gagal, rc=");
    Serial.println(mqttClient.state());
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Perintah dari ");
  Serial.print(topic);
  Serial.print(": ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  // Bisa ditambahkan aksi sesuai perintah
}

// ========== Proses JSON dari STM32 & Kirim ke MQTT ==========
void processAndForwardJSON(String jsonString) {
  // Parse JSON untuk mengambil nilai individual
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.print("[JSON] Error: ");
    Serial.println(error.c_str());
    return;
  }
  
  packetCount++;
  
  // Kirim JSON lengkap ke topik data
  if (mqttClient.connected()) {
    mqttClient.publish(topic_data, jsonString.c_str());
  }
  
  // Kirim ke topik individual (sinkron dengan web)
  if (mqttClient.connected()) {
    if (doc.containsKey("kecepatan")) {
      int val = doc["kecepatan"];
      mqttClient.publish(topic_speed, String(val).c_str());
    }
    if (doc.containsKey("rpm_coil")) {
      int val = doc["rpm_coil"];
      mqttClient.publish(topic_rpm, String(val).c_str());
    }
    if (doc.containsKey("afr_etanol")) {
      float val = doc["afr_etanol"];
      char buf[10];
      dtostrf(val, 1, 2, buf);
      mqttClient.publish(topic_afr, buf);
    }
    if (doc.containsKey("throttle")) {
      int val = doc["throttle"];
      mqttClient.publish(topic_tps, String(val).c_str());
    }
    if (doc.containsKey("suhu")) {
      int val = doc["suhu"];
      mqttClient.publish(topic_temp, String(val).c_str());
    }
  }
  
  // Log ke Serial Monitor
  Serial.print("Packet #");
  Serial.print(packetCount);
  Serial.print(" | Speed: ");
  Serial.print(doc["kecepatan"].as<int>());
  Serial.print(" | RPM: ");
  Serial.print(doc["rpm_coil"].as<int>());
  Serial.print(" | AFR: ");
  Serial.print(doc["afr_etanol"].as<float>(), 1);
  Serial.print(" | TPS: ");
  Serial.print(doc["throttle"].as<int>());
  Serial.print("% | Temp: ");
  Serial.print(doc["suhu"].as<int>());
  Serial.println("C");
}

// ========== Perintah Serial Monitor ==========
void handleCommand(String cmd) {
  cmd.toLowerCase();
  if (cmd == "status" || cmd == "s") {
    Serial.println("\n--- STATUS ---");
    Serial.printf("Uptime    : %lu detik\n", millis()/1000);
    Serial.printf("WiFi      : %s (%s)\n", 
                  WiFi.status()==WL_CONNECTED?"ON":"OFF",
                  WiFi.localIP().toString().c_str());
    Serial.printf("MQTT      : %s\n", mqttClient.connected()?"ON":"OFF");
    Serial.printf("STM32     : %s\n", stm32Connected?"ONLINE":"OFFLINE");
    Serial.printf("Packet    : %d\n", packetCount);
    Serial.println("-------------");
  } else if (cmd == "reset") {
    Serial.println("Restart ESP32...");
    if (mqttClient.connected()) mqttClient.publish(topic_status, "offline");
    delay(100);
    ESP.restart();
  } else if (cmd == "help") {
    Serial.println("Perintah:");
    Serial.println("  status / s : Tampilkan status");
    Serial.println("  reset      : Restart ESP32");
    Serial.println("  help       : Menu ini");
  } else {
    Serial.println("Perintah tidak dikenal. Ketik 'help'.");
  }
}