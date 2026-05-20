#include <Arduino.h>
#include <ArduinoJson.h>
#include <math.h>

#define MONITOR_BAUD      115200
#define ECU_BAUD          57600
#define SIMULATE          false
#define KEEP_ALIVE_MS     6000
#define ECU_TIMEOUT_MS    3000
#define ECU_RETRY_MS      2000
#define SEND_INTERVAL     500
#define ARDUINO_INTERVAL  100

#define VSS_DEBOUNCE_DELAY  0.5

// ========== PIN ==========
const int IRSensorPin   = PB8;
const int ThermistorPin = PA5;   // [FIXED] dari PA6 → PA5 (sesuai kode 1)
const int sensorAFR     = PA1;

// ========== Serial ==========
HardwareSerial ecuSerial(PA3, PA2);
HardwareSerial SerialESP(PA10, PA9);
HardwareSerial SerialArduino(PB10);  // TX only ke Arduino Uno

// ========== Konstanta ==========
// [FIXED] Semua konstanta suhu diganti sesuai kode 1 yang sudah benar
const float R1 = 9800.0;
const float c1 = 0.001129148;
const float c2 = 0.000234125;
const float c3 = 0.0000000876741;

const int JUMLAH_MAGNET = 4;
const float DIAMETER_BAN = 0.508;
const float KELILING_BAN = DIAMETER_BAN * 3.14159;

// ========== Global ==========
int   g_rpm  = 0;
float g_tps  = 0.0;
float g_vbat = 0.0;
int   g_kecepatan = 0;
int   g_kecepatan_raw = 0;
float g_afr  = 0.0;
int   g_suhu = 0;

String currentLine = "";

bool ecuConnected = false;

unsigned long lastEcuData = 0;
unsigned long lastPing = 0;

// ========== VSS ==========
int inputStateVSS = LOW;
int lastInputStateVSS = LOW;

unsigned long lastDebounceTimeVSS = 0;
unsigned long endTimeVSS = 0;
unsigned long startTimeVSS = 0;

// ========== Buffer kecepatan untuk average ==========
int kecepatanBuffer[5] = {0};
int kecepatanBufferIndex = 0;
int kecepatanBufferFilled = 0;

// ========== Buffer suhu ==========
float suhuBuffer[10] = {0};
int bufferIndex = 0;
int bufferFilled = 0;

// ========== Timing ==========
unsigned long lastSend = 0;
unsigned long lastArduinoSend = 0;
unsigned long packetCounter = 0;
unsigned long startTime = 0;

// =====================================================
// ECU
// =====================================================

void sendCmd(const char* cmd) {
  ecuSerial.print(cmd);
  ecuSerial.println();
  ecuSerial.flush();
}

String getField(const String &s, int fieldNum) {
  int start = 0;

  for (int i = 0; i < fieldNum; i++) {
    start = s.indexOf(';', start);
    if (start == -1)
      return "";
    start++;
  }

  int end = s.indexOf(';', start);
  if (end == -1)
    end = s.length();

  String field = s.substring(start, end);
  field.trim();
  return field;
}

String cleanString(const String &s) {
  String out = "";
  out.reserve(s.length());

  for (int i = 0; i < (int)s.length(); i++) {
    char c = s.charAt(i);
    if (c >= 32 && c <= 126)
      out += c;
  }
  return out;
}

bool isNumeric(const String &s) {
  if (s.length() == 0)
    return false;

  int start = 0;
  if (s.charAt(0) == '-')
    start = 1;

  bool hasDot = false;

  for (int i = start; i < (int)s.length(); i++) {
    char c = s.charAt(i);
    if (c == '.') {
      if (hasDot)
        return false;
      hasDot = true;
    } else if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

float parseFloat(const String &s, float defaultVal, float minVal, float maxVal) {
  if (!isNumeric(s))
    return defaultVal;

  float val = s.toFloat();
  if (val < minVal || val > maxVal)
    return defaultVal;

  return val;
}

int parseInt(const String &s, int defaultVal, int minVal, int maxVal) {
  if (!isNumeric(s))
    return defaultVal;

  int val = s.toInt();
  if (val < minVal || val > maxVal)
    return defaultVal;

  return val;
}

void parseECU(const String &rawLine) {
  String line = cleanString(rawLine);

  if (line.length() < 10)
    return;

  if (line.indexOf(';') == -1)
    return;

  int headerIdx = -1;
  String lineLower = line;
  lineLower.toLowerCase();
  headerIdx = lineLower.indexOf("a603;");

  if (headerIdx == -1)
    return;

  String data = line.substring(headerIdx);

  int fieldCount = 1;
  for (int i = 0; i < (int)data.length(); i++) {
    if (data.charAt(i) == ';')
      fieldCount++;
  }

  if (fieldCount < 5)
    return;

  String fTPS  = getField(data, 1);
  String fVBAT = getField(data, 2);
  String fRPM  = getField(data, 4);

  float newTPS  = parseFloat(fTPS, g_tps, 0.0, 100.0);
  float newVBAT = parseFloat(fVBAT, g_vbat, 6.0, 20.0);
  int newRPM    = parseInt(fRPM, g_rpm, 0, 20000);

  if (ecuConnected && abs(newRPM - g_rpm) > 8000)
    return;

  g_tps  = newTPS;
  g_vbat = newVBAT;
  g_rpm  = newRPM;

  ecuConnected = true;
  lastEcuData = millis();
}

void handshakeECU() {
  sendCmd("1B00");
  delay(150);

  while (ecuSerial.available())
    ecuSerial.read();

  sendCmd("1617");
  delay(200);

  while (ecuSerial.available()) {
    char c = ecuSerial.read();
    if (c == '\n') {
      currentLine = "";
      break;
    } else if (c != '\r') {
      currentLine += c;
    }
  }

  currentLine = "";
  sendCmd("160A");
  delay(50);

  lastPing = millis();
  lastEcuData = millis();
}

// =====================================================
// Sensor
// =====================================================

void updateKecepatanAverage(int kecepatanBaru) {
  g_kecepatan_raw = kecepatanBaru;

  kecepatanBuffer[kecepatanBufferIndex++] = kecepatanBaru;

  if (kecepatanBufferIndex >= 5) {
    kecepatanBufferIndex = 0;
    kecepatanBufferFilled = 1;
  }

  int samples = kecepatanBufferFilled ? 5 : (kecepatanBufferIndex == 0 ? 1 : kecepatanBufferIndex);

  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += kecepatanBuffer[i];
  }

  g_kecepatan = sum / samples;
}

void bacaSuhu() {
  // [FIXED] Seluruh fungsi ini diganti sesuai kode 1 yang sudah benar

  // Langkah 1: Rata-rata 20 sampel ADC (noise reduction)
  const int N = 20;
  long sum = 0;
  for (int i = 0; i < N; i++) {
    sum += analogRead(ThermistorPin);
    delay(2);
  }
  float Vo = sum / (float)N;

  // Langkah 2: Hitung resistansi thermistor (voltage divider)
  // [FIXED] Faktor 1.07 dihapus → pakai 1.0 yang benar
  float R2 = R1 * (4095.0 / Vo - 1.0);

  // Langkah 3: Steinhart-Hart → Kelvin
  float logR2 = log(R2);
  float T = 1.0 / (c1 + c2 * logR2 + c3 * logR2 * logR2 * logR2);

  // Langkah 4: Konversi ke Celsius
  float mentah = T - 273.15;

  // Langkah 5: Masukkan ke circular buffer
  suhuBuffer[bufferIndex++] = mentah;
  if (bufferIndex >= 10) {
    bufferIndex = 0;
    bufferFilled = 1;
  }

  // Langkah 6: Moving average 10 sampel
  int samples = bufferFilled ? 10 : (bufferIndex == 0 ? 1 : bufferIndex);
  float sum2 = 0;
  for (int i = 0; i < samples; i++)
    sum2 += suhuBuffer[i];

  // Langkah 7: Simpan hasil akhir
  g_suhu = (int)round(sum2 / samples);
}

void bacaAFR() {
  const int N = 10;
  long sum = 0;

  for (int i = 0; i < N; i++) {
    sum += analogRead(sensorAFR);
    delay(1);
  }

  float volt = (sum / (float)N) * (3.3 / 4095.0);
  float afr_bensin = 2.375 * volt + 7.3125;
  float lamda = afr_bensin / 14.7;
  g_afr = round(lamda * 9.12 * 100) / 100.0;
}

// =====================================================
// KIRIM KE ARDUINO UNO
// =====================================================

void kirimKeArduino() {
  // Format: kecepatan;rpm;tps;suhu;afr
  SerialArduino.print(g_kecepatan);   // Field 0: km/h
  SerialArduino.print(";");
  SerialArduino.print(g_rpm);         // Field 1: RPM
  SerialArduino.print(";");
  SerialArduino.print((int)g_tps);    // Field 2: TPS %
  SerialArduino.print(";");
  SerialArduino.print(g_vbat, 1);     // Field 3: vbat (misal: 12.6)
  SerialArduino.print(";");
  SerialArduino.print(g_suhu);        // Field 4: suhu Celsius
  SerialArduino.print(";");
  SerialArduino.println(g_afr, 2);    // Field 5: afr + \n
}

// =====================================================
// JSON ke ESP32
// =====================================================

void kirimJSON() {
  unsigned long detik_total = millis() / 1000;
  int menit = detik_total / 60;
  int detik = detik_total % 60;

  StaticJsonDocument<256> doc;

  doc["rpm"]           = g_rpm;
  doc["tps"]           = (int)g_tps;
  doc["vbat"]          = g_vbat;
  doc["kecepatan"]     = g_kecepatan;
  doc["kecepatan_raw"] = g_kecepatan_raw;
  doc["afr"]           = g_afr;
  doc["suhu"]          = g_suhu;
  doc["ecu_connected"] = ecuConnected;
  doc["packet"]        = packetCounter++;

  JsonObject waktu = doc.createNestedObject("waktu");
  waktu["menit"] = menit;
  waktu["detik"] = detik;

  String output;
  serializeJson(doc, output);
  SerialESP.println(output);
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(MONITOR_BAUD);
  SerialESP.begin(9600);
  SerialArduino.begin(9600);

  pinMode(IRSensorPin, INPUT);
  pinMode(sensorAFR, INPUT);
  analogReadResolution(12);
  pinMode(LED_BUILTIN, OUTPUT);

  startTime = millis();

  if (SIMULATE) {
    randomSeed(analogRead(PA4));
    lastSend = millis();
    lastArduinoSend = millis();
    return;
  }

  ecuSerial.begin(ECU_BAUD);
  delay(800);
  handshakeECU();

  lastSend = millis();
  lastArduinoSend = millis();
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  unsigned long now = millis();

  // ================= SIMULASI =================
  if (SIMULATE) {
    if (now - lastSend >= SEND_INTERVAL) {
      g_rpm = random(1200, 9000);
      g_tps = random(0, 100);
      g_vbat = random(120, 145) / 10.0;
      int kecepatanBaru = random(0, 60);
      updateKecepatanAverage(kecepatanBaru);
      g_afr = random(800, 1500) / 100.0;
      g_suhu = random(60, 110);
      ecuConnected = true;

      kirimKeArduino();
      kirimJSON();

      lastSend = now;
      lastArduinoSend = now;
    }
    delay(10);
    return;
  }

  // ================= VSS =================
  int currentSwitchState = digitalRead(IRSensorPin);

  if (currentSwitchState != lastInputStateVSS) {
    lastDebounceTimeVSS = now;
  }

  if ((now - lastDebounceTimeVSS) > VSS_DEBOUNCE_DELAY) {
    if (currentSwitchState != inputStateVSS) {
      inputStateVSS = currentSwitchState;

      if (inputStateVSS == HIGH) {
        startTimeVSS = lastDebounceTimeVSS;

        if ((startTimeVSS - endTimeVSS) > 0) {
          float RPM_VSS = 60000.0 / (startTimeVSS - endTimeVSS);
          float RPM_roda = RPM_VSS / JUMLAH_MAGNET;
          float kecepatanBaru = (RPM_roda * KELILING_BAN * 60.0) / 1000.0;
          updateKecepatanAverage((int)kecepatanBaru);
        }
        endTimeVSS = startTimeVSS;
      }
    }
  }
  lastInputStateVSS = currentSwitchState;

  // ================= ECU TIMEOUT =================
  if (ecuConnected && (now - lastEcuData > ECU_TIMEOUT_MS)) {
    ecuConnected = false;
    g_rpm = 0;
    g_tps = 0;
    g_vbat = 0;
  }

  // ================= ECU RECONNECT =================
  if (!ecuConnected && (now - lastPing >= ECU_RETRY_MS)) {
    handshakeECU();
  }

  // ================= KEEP ALIVE =================
  if (ecuConnected && (now - lastPing >= KEEP_ALIVE_MS)) {
    sendCmd("1B00");
    lastPing = now;
  }

  // ================= ECU STREAM =================
  while (ecuSerial.available()) {
    char c = ecuSerial.read();

    if (c == '\n' || c == '\r') {
      if (currentLine.length() > 5) {
        parseECU(currentLine);
        currentLine = "";
      }
    } else {
      currentLine += c;
      if (currentLine.length() > 80) {
        parseECU(currentLine);
        currentLine = "";
      }
    }
  }

  // ================= BACA SENSOR & KIRIM KE ESP32 =================
  if (now - lastSend >= SEND_INTERVAL) {
    bacaSuhu();
    bacaAFR();
    kirimJSON();
    lastSend = now;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // ================= KIRIM KE ARDUINO UNO =================
  if (now - lastArduinoSend >= ARDUINO_INTERVAL) {
    kirimKeArduino();
    lastArduinoSend = now;
  }

  delay(1);
}