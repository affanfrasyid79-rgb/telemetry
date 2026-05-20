// ======================================================
// RACING DASHBOARD v2 - FULL SCREEN LAYOUT
// TFT 2.4 MCUFRIEND + ARDUINO UNO
// ======================================================

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <math.h>

MCUFRIEND_kbv tft;

// ======================================================
// COLORS
// ======================================================

#define BLACK     0x0000
#define WHITE     0xFFFF
#define RED       0xF800
#define GREEN     0x07E0
#define CYAN      0x07FF
#define YELLOW    0xFFE0
#define ORANGE    0xFD20
#define DARK      0x2104
#define DARKGRAY  0x39E7
#define LIME      0x87E0
#define TEAL      0x0410

// ======================================================
// DATA
// ======================================================

int   rpmVal   = 0;
int   speedVal = 0;
int   tempVal  = 0;
int   tpsVal   = 0;
int   lapVal   = 0;
float voltVal  = 12.0;
float afrVal   = 0.0;

char   dataBuffer[80];
byte   dataIndex  = 0;
unsigned long startMillis;

// ======================================================
// RPM ARC CONFIG
// ======================================================

#define ARC_CX     30
#define ARC_CY     270
#define ARC_R_IN   135
#define ARC_R_OUT  165
#define ARC_TICKS  60

#define ARC_START  210.0
#define ARC_END    30.0

// ======================================================
// Helper
// ======================================================

void polarXY(int cx, int cy, int r, float deg, int &x, int &y)
{
  float rad = deg * PI / 180.0;
  x = cx + (int)(cos(rad) * r);
  y = cy - (int)(sin(rad) * r);
}

// ======================================================
// DRAW RPM BACKGROUND (tick abu-abu saja, tanpa arc aktif)
// ======================================================

void drawRPMBackground()
{
  for (int i = 0; i <= ARC_TICKS; i++)
  {
    float deg = ARC_START + (ARC_END - ARC_START) * i / (float)ARC_TICKS;
    int x1, y1, x2, y2;
    polarXY(ARC_CX, ARC_CY, ARC_R_IN, deg, x1, y1);
    polarXY(ARC_CX, ARC_CY, ARC_R_OUT, deg, x2, y2);
    if (x1 >= 0 && x1 < 320 && y1 >= 0 && y1 < 240 &&
        x2 >= 0 && x2 < 320 && y2 >= 0 && y2 < 240)
      tft.drawLine(x1, y1, x2, y2, DARK);
  }
  for (float deg = ARC_START; deg >= ARC_END; deg -= 1.5)
  {
    int x1, y1, x2, y2;
    polarXY(ARC_CX, ARC_CY, ARC_R_OUT + 2, deg, x1, y1);
    polarXY(ARC_CX, ARC_CY, ARC_R_OUT + 2, deg - 1.0, x2, y2);
    if (x1 >= 0 && x1 < 320 && y1 >= 0 && y1 < 240 &&
        x2 >= 0 && x2 < 320 && y2 >= 0 && y2 < 240)
      tft.drawLine(x1, y1, x2, y2, DARKGRAY);
  }
}

// ======================================================
// UPDATE RPM
// [FIX] Urutan: background dulu → baru arc aktif di atasnya
//       Sehingga arc aktif tidak tertimpa background
// ======================================================

void drawRPMTick(int i, uint16_t color)
{
  float deg = ARC_START + (ARC_END - ARC_START) * i / (float)ARC_TICKS;
  int x1, y1, x2, y2;
  polarXY(ARC_CX, ARC_CY, ARC_R_IN, deg, x1, y1);
  polarXY(ARC_CX, ARC_CY, ARC_R_OUT, deg, x2, y2);
  if (x1 >= 0 && x1 < 320 && y1 >= 0 && y1 < 240 &&
      x2 >= 0 && x2 < 320 && y2 >= 0 && y2 < 240)
    tft.drawLine(x1, y1, x2, y2, color);
}

void updateRPM(int rpm)
{
  static int lastRPM   = -1;
  static int lastActive = 0;
  if (rpm == lastRPM) return;
  lastRPM = rpm;

  // Update angka RPM saja
  tft.fillRect(5, 35, 110, 30, BLACK);
  tft.setTextColor(CYAN, BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 38);
  tft.print("RPM");
  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(2);
  tft.setCursor(5, 50);
  int rk = rpm / 100;
  tft.print(rk / 10);
  tft.print(".");
  tft.print(rk % 10);
  tft.print("k");

  int active = map(constrain(rpm, 0, 5500), 0, 5500, 0, ARC_TICKS);

  if (active > lastActive)
  {
    // RPM naik: gambar tick baru yang aktif saja
    for (int i = lastActive + 1; i <= active; i++)
    {
      uint16_t c;
      if (i < 30)      c = GREEN;
      else if (i < 48) c = YELLOW;
      else             c = RED;
      drawRPMTick(i, c);
    }
  }
  else if (active < lastActive)
  {
    // RPM turun: hapus tick yang tidak aktif lagi
    for (int i = active + 1; i <= lastActive; i++)
      drawRPMTick(i, DARK);
  }

  lastActive = active;
}

// ======================================================
// DRAW STATIC UI
// ======================================================

void drawStaticUI()
{
  tft.fillScreen(BLACK);

  tft.drawLine(0, 28, 320, 28, DARKGRAY);

  tft.setTextColor(YELLOW, BLACK);
  tft.setTextSize(1);
  tft.setCursor(140, 6);
  tft.print("TIME");

  tft.setTextColor(ORANGE, BLACK);
  tft.setTextSize(1);
  tft.setCursor(265, 6);
  tft.print("LAP");

  tft.drawLine(100, 0, 100, 28, DARKGRAY);
  tft.drawLine(215, 0, 215, 28, DARKGRAY);
  tft.drawLine(258, 0, 258, 28, DARKGRAY);

  tft.setTextColor(TEAL, BLACK);
  tft.setTextSize(1);
  tft.setCursor(5, 210);
  tft.print("VOLT");

  tft.drawRect(175, 100, 140, 135, CYAN);
  tft.drawLine(175, 165, 315, 165, CYAN);
  tft.drawLine(175, 197, 315, 197, CYAN);

  tft.setTextColor(ORANGE, BLACK);
  tft.setTextSize(1);
  tft.setCursor(182, 172);
  tft.print("TEMP");

  tft.setTextColor(LIME, BLACK);
  tft.setCursor(182, 204);
  tft.print("TPS");

  tft.drawLine(0, 235, 320, 235, DARKGRAY);

  tft.drawLine(0, 0, 10, 0, CYAN);
  tft.drawLine(0, 0, 0, 10, CYAN);
  tft.drawLine(310, 0, 319, 0, CYAN);
  tft.drawLine(319, 0, 319, 10, CYAN);
  tft.drawLine(319, 229, 319, 239, CYAN);
  tft.drawLine(310, 239, 319, 239, CYAN);
  tft.drawLine(0, 229, 0, 239, CYAN);
  tft.drawLine(0, 239, 10, 239, CYAN);

  drawRPMBackground();
}

// ======================================================
// UPDATE SPEED
// ======================================================

void updateSpeed(int val)
{
  static int last = -1;
  if (val == last) return;
  last = val;

  tft.fillRect(175, 100, 140, 65, BLACK);
  tft.drawRect(175, 100, 140, 65, CYAN);

  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(4);

  if (val < 10)       tft.setCursor(235, 120);
  else if (val < 100) tft.setCursor(227, 120);
  else                tft.setCursor(219, 120);

  tft.print(val);

  tft.setTextColor(DARKGRAY, BLACK);
  tft.setTextSize(1);
  tft.setCursor(270, 150);
  tft.print("km/h");
}

// ======================================================
// UPDATE TEMP
// [FIX] fillRect diperlebar agar 3 digit + "C" tidak terpotong
//       Sebelumnya lebar 80 tidak cukup untuk "100C" di size 2
// ======================================================

void updateTemp(int val)
{
  static int last = -1;
  if (val == last) return;
  last = val;

  // [FIX] lebar dari 80 → 85, hapus area yang cukup untuk "100C"
  tft.fillRect(228, 167, 85, 22, BLACK);

  uint16_t c;
  if (val > 100)     c = RED;
  else if (val > 85) c = ORANGE;
  else               c = WHITE;

  tft.setTextColor(c, BLACK);
  tft.setTextSize(2);
  // [FIX] geser cursor agar rata kiri di panel, tidak terpotong kanan
  tft.setCursor(232, 170);
  tft.print(val);
  tft.print("C");
}

// ======================================================
// UPDATE TPS
// ======================================================

void updateTPS(int val)
{
  static int last = -1;
  if (val == last) return;
  last = val;

  tft.fillRect(230, 200, 80, 18, BLACK);
  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(2);
  tft.setCursor(232, 202);
  tft.print(val);
  tft.print("%");
}

// ======================================================
// UPDATE VOLTAGE
// [FIX] Tambah validasi range 6–20V sebelum assign
//       Cegah nilai ganda atau noise dari parsing
// ======================================================

void updateVolt(float val)
{
  // [FIX] Clamp: tolak nilai di luar range wajar aki motor/mobil
  if (val < 6.0 || val > 20.0) return;

  static float last = -1;
  if (abs(val - last) < 0.05) return;
  last = val;

  tft.fillRect(5, 220, 80, 20, BLACK);

  uint16_t c;
  if (val < 11.5)      c = RED;
  else if (val < 12.5) c = YELLOW;
  else                 c = WHITE;

  tft.setTextColor(c, BLACK);
  tft.setTextSize(2);
  tft.setCursor(5, 220);
  tft.print(val, 1);
  tft.print("V");
}

// ======================================================
// UPDATE TIMER
// ======================================================

void updateTimer()
{
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate < 1000) return;
  lastUpdate = millis();

  unsigned long sec = (millis() - startMillis) / 1000;
  int m = sec / 60;
  int s = sec % 60;

  char buf[10];
  sprintf(buf, "%02d:%02d", m, s);

  tft.fillRect(101, 0, 114, 28, BLACK);
  tft.setTextColor(YELLOW, BLACK);
  tft.setTextSize(2);
  tft.setCursor(120, 6);
  tft.print(buf);
}

// ======================================================
// UPDATE LAP
// ======================================================

void updateLap(int val)
{
  static int last = -1;
  if (val == last) return;
  last = val;

  tft.fillRect(259, 0, 60, 28, BLACK);
  tft.setTextColor(ORANGE, BLACK);
  tft.setTextSize(2);
  tft.setCursor(275, 6);
  tft.print(val);
}

// ======================================================
// PARSE DATA DARI STM32
// FORMAT: kecepatan;rpm;tps;vbat;suhu;afr\n
// Index:       0     1   2   3    4    5
// ======================================================

// Buang semua karakter di depan sampai ketemu angka atau '-'
char* trimToken(char* s)
{
  if (!s) return s;
  while (*s && !isdigit((unsigned char)*s) && *s != '-' && *s != '.')
    s++;
  // Potong dari belakang: buang CR, LF, spasi
  int len = strlen(s);
  while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' '))
  {
    s[len-1] = '\0';
    len--;
  }
  return s;
}

void parseData(char *data)
{
  // Buang CR/LF di akhir string dulu
  int dlen = strlen(data);
  while (dlen > 0 && (data[dlen-1] == '\r' || data[dlen-1] == '\n' || data[dlen-1] == ' '))
  {
    data[dlen-1] = '\0';
    dlen--;
  }

  char *tok;

  // Field 0: kecepatan
  tok = strtok(data, ";");
  if (tok) speedVal = atoi(trimToken(tok));

  // Field 1: rpm
  tok = strtok(NULL, ";");
  if (tok) rpmVal = constrain(atoi(trimToken(tok)), 0, 20000);

  // Field 2: tps
  tok = strtok(NULL, ";");
  if (tok) tpsVal = constrain(atoi(trimToken(tok)), 0, 100);

  // Field 3: vbat (Volt)
  tok = strtok(NULL, ";");
  if (tok)
  {
    float v = atof(trimToken(tok));
    if (v >= 6.0 && v <= 16.0) voltVal = v;
  }

  // Field 4: suhu (Celsius)
  tok = strtok(NULL, ";");
  if (tok)
  {
    int t = atoi(trimToken(tok));
    if (t >= -20 && t <= 150) tempVal = t;
  }

  // Field 5: afr
  tok = strtok(NULL, ";");
  if (tok) afrVal = atof(trimToken(tok));
  updateRPM(rpmVal);
  updateSpeed(speedVal);
  updateTemp(tempVal);
  updateTPS(tpsVal);
  updateVolt(voltVal);
}

// ======================================================
// SETUP
// ======================================================

void setup()
{
  Serial.begin(9600);

  uint16_t ID = tft.readID();
  if (ID == 0xD3D3) ID = 0x9486;
  tft.begin(ID);
  tft.setRotation(3);

  drawStaticUI();

  updateSpeed(0);
  updateTemp(0);
  updateTPS(0);
  updateVolt(12.0);
  updateLap(0);
  updateRPM(0);

  startMillis = millis();
}

// ======================================================
// LOOP
// ======================================================

// Tampilkan raw string di pojok kiri atas LCD untuk debug
// Matikan dengan set DEBUG_RAW 0 setelah data sudah benar
#define DEBUG_RAW 1

/*void showRawDebug(const char* raw)
{
  #if DEBUG_RAW
  tft.fillRect(0, 0, 100, 14, BLACK);
  tft.setTextColor(LIME, BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 2);
  tft.print(raw);
  #endif
}*/

void loop()
{
  updateTimer();

  while (Serial.available())
  {
    char c = Serial.read();

    if (c == '\n')
    {
      dataBuffer[dataIndex] = '\0';
      if (dataIndex > 5)
      {
        //showRawDebug(dataBuffer);   // tampil raw di LCD
        parseData(dataBuffer);
      }
      dataIndex = 0;
    }
    else if (c != '\r')
    {
      if (dataIndex < sizeof(dataBuffer) - 1)
        dataBuffer[dataIndex++] = c;
      else
        dataIndex = 0;
    }
  }
}
