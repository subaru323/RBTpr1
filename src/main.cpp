#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <BH1750.h>

// ====== センサー設定 ======
#define DHTPIN 16
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

BH1750 lightMeter;

// ====== 出力ピン ======
const int ledPin = 25;

// ====== しきい値 ======
const int LIGHT_THRESHOLD = 100; // Lux
const float TEMP_GRAPH_MIN = 10.0; 
const float TEMP_GRAPH_MAX = 40.0; 
#define LUX_MAX_VALUE 1000.0

// ====== データ配列 ======
#define MAX_DATA_POINTS 60
float tempData[MAX_DATA_POINTS];
float humData[MAX_DATA_POINTS];
float luxData[MAX_DATA_POINTS];
int dataIndex = 0;

// ====== 画面モード ======
int screenMode = 0; // 0:通常 1:グラフ 2:統計 3:LED状態

// ====== 色定義 ======
#define GRAY M5.Lcd.color565(100, 100, 100)
#define ORANGE M5.Lcd.color565(255, 165, 0)
#define PURPLE M5.Lcd.color565(128, 0, 128)
#define NORMAL_BG M5.Lcd.color565(210, 180, 140)

// ====== タイマー ======
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000; // 1秒
unsigned long lastResetTime = 0;             // 24時間リセット用
const unsigned long RESET_INTERVAL = 24UL * 60UL * 60UL * 1000UL; // 24時間

// ====== 関数プロトタイプ ======
void drawNormalScreen();
void drawGraphScreen();
void drawStatsScreen();
void drawDeviceScreen(bool ledOn);
void resetStats();  // ← 追加（24時間ごとに配列を初期化）

bool bh1750Initialized = false;

// ================= 初期設定 =================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);

  Wire.begin();
  dht.begin();

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Initializing sensors...");
  delay(500);

  // BH1750 初期化（失敗してもループで再試行）
  bh1750Initialized = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  if (!bh1750Initialized) {
    M5.Lcd.fillScreen(RED);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("BH1750 Init Failed!");
  }

  for (int i = 0; i < MAX_DATA_POINTS; i++) {
    tempData[i] = 0.0;
    humData[i] = 0.0;
    luxData[i] = 0.0;
  }

  drawNormalScreen();
  resetStats();
  lastResetTime = millis(); // ← 起動時刻記録
}

// ================= データリセット（24時間ごと） =================
void resetStats() {
  for (int i = 0; i < MAX_DATA_POINTS; i++) {
    tempData[i] = humData[i] = luxData[i] = 0.0;
  }
  dataIndex = 0;
}

// ================= 通常画面 =================
void drawNormalScreen() {
  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.setTextColor(BLACK, NORMAL_BG);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(30, 30);  M5.Lcd.printf("Temp:");
  M5.Lcd.setCursor(30, 110); M5.Lcd.printf("Hum :");
  M5.Lcd.setCursor(30, 190); M5.Lcd.printf("Lux :");
}

// ================= グラフ画面 =================
void drawGraphScreen() {
  M5.Lcd.fillScreen(BLACK);
  int baseY = 220;
  int width = 300;
  int startX = 10;

  float tempScale = baseY / (TEMP_GRAPH_MAX - TEMP_GRAPH_MIN);
  float humScale = baseY / 100.0;
  float luxScale = baseY / LUX_MAX_VALUE;

  for (int i = 1; i < MAX_DATA_POINTS; i++) {
    int idx1 = (dataIndex + i - 1) % MAX_DATA_POINTS;
    int idx2 = (dataIndex + i) % MAX_DATA_POINTS;
    int x1 = startX + (i - 1) * (width / MAX_DATA_POINTS);
    int x2 = startX + i * (width / MAX_DATA_POINTS);

    int yT1 = baseY - (int)((tempData[idx1] - TEMP_GRAPH_MIN) * tempScale);
    int yT2 = baseY - (int)((tempData[idx2] - TEMP_GRAPH_MIN) * tempScale);
    M5.Lcd.drawLine(x1, yT1, x2, yT2, RED);

    int yH1 = baseY - (int)(humData[idx1] * humScale);
    int yH2 = baseY - (int)(humData[idx2] * humScale);
    M5.Lcd.drawLine(x1, yH1, x2, yH2, BLUE);

    int yL1 = baseY - (int)(luxData[idx1] * luxScale);
    int yL2 = baseY - (int)(luxData[idx2] * luxScale);
    M5.Lcd.drawLine(x1, yL1, x2, yL2, YELLOW);
  }

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(RED, BLACK);     M5.Lcd.setCursor(10, 10);  M5.Lcd.printf("Temp(40C)");
  M5.Lcd.setTextColor(BLUE, BLACK);    M5.Lcd.setCursor(120, 10); M5.Lcd.printf("Hum(100%%)");
  M5.Lcd.setTextColor(YELLOW, BLACK);  M5.Lcd.setCursor(240, 10); M5.Lcd.printf("Lux(1k)");
}

// ================= 平均・最大・最小 =================
void drawStatsScreen() {
  auto calcStats = [](float data[], float &avg, float &maxVal, float &minVal) {
    avg = 0; maxVal = -9999; minVal = 9999;
    int validCount = 0;
    for (int i = 0; i < MAX_DATA_POINTS; i++) {
      if (data[i] != 0.0) {
        avg += data[i];
        if (data[i] < minVal) minVal = data[i];
        if (data[i] > maxVal) maxVal = data[i];
        validCount++;
      }
    }
    if (validCount > 0) avg /= validCount;
    else { avg = 0; maxVal = 0; minVal = 0; }
  };

  float avgT, maxT, minT;
  float avgH, maxH, minH;
  float avgL, maxL, minL;
  calcStats(tempData, avgT, maxT, minT);
  calcStats(humData, avgH, maxH, minH);
  calcStats(luxData, avgL, maxL, minL);

  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(BLACK, NORMAL_BG);
  M5.Lcd.setCursor(80, 10); M5.Lcd.printf("AVG");
  M5.Lcd.setCursor(180, 10); M5.Lcd.printf("MAX");
  M5.Lcd.setCursor(260, 10); M5.Lcd.printf("MIN");
  M5.Lcd.drawFastHLine(5, 45, 310, GRAY);

  int y_offset = 60;
  M5.Lcd.setCursor(10, y_offset); 
  M5.Lcd.printf("T:");
  M5.Lcd.setTextColor((avgT>=30)?RED:(avgT<=20)?CYAN:GREEN, NORMAL_BG); M5.Lcd.setCursor(70, y_offset); M5.Lcd.printf("%.1f C", avgT);
  M5.Lcd.setTextColor((maxT>=30)?RED:(maxT<=20)?CYAN:GREEN, NORMAL_BG); M5.Lcd.setCursor(160, y_offset); M5.Lcd.printf("%.1f C", maxT);
  M5.Lcd.setTextColor((minT>=30)?RED:(minT<=20)?CYAN:GREEN, NORMAL_BG); M5.Lcd.setCursor(240, y_offset); M5.Lcd.printf("%.1f C", minT);

  y_offset += 60;
  M5.Lcd.setCursor(10, y_offset);
  M5.Lcd.printf("H:");
  M5.Lcd.setTextColor((avgH>=80)?PURPLE:(avgH<=60)?BLUE:GREEN, NORMAL_BG); M5.Lcd.setCursor(70, y_offset); M5.Lcd.printf("%.1f %%", avgH);
  M5.Lcd.setTextColor((maxH>=80)?PURPLE:(maxH<=60)?BLUE:GREEN, NORMAL_BG); M5.Lcd.setCursor(160, y_offset); M5.Lcd.printf("%.1f %%", maxH);
  M5.Lcd.setTextColor((minH>=80)?PURPLE:(minH<=60)?BLUE:GREEN, NORMAL_BG); M5.Lcd.setCursor(240, y_offset); M5.Lcd.printf("%.1f %%", minH);

  y_offset += 60;
  M5.Lcd.setCursor(10, y_offset);
  M5.Lcd.printf("L:");
  M5.Lcd.setTextColor((avgL>=800)?WHITE:(avgL<=100)?GRAY:YELLOW, NORMAL_BG); M5.Lcd.setCursor(70, y_offset); M5.Lcd.printf("%d Lx", (int)avgL);
  M5.Lcd.setTextColor((maxL>=800)?WHITE:(maxL<=100)?GRAY:YELLOW, NORMAL_BG); M5.Lcd.setCursor(160, y_offset); M5.Lcd.printf("%d Lx", (int)maxL);
  M5.Lcd.setTextColor((minL>=800)?WHITE:(minL<=100)?GRAY:YELLOW, NORMAL_BG); M5.Lcd.setCursor(240, y_offset); M5.Lcd.printf("%d Lx", (int)minL);
}

// ================= LED状態画面 =================
void drawDeviceScreen(bool ledOn) {
  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(40, 100);
  M5.Lcd.setTextColor(ledOn ? ORANGE : BLACK, NORMAL_BG);
  M5.Lcd.printf("LED : %s", ledOn ? "ON" : "OFF");
}

// ====== タイマー追加 ======
unsigned long lastBH1750Retry = 0;        // BH1750再試行タイマー
const unsigned long BH1750_RETRY_INTERVAL = 5000; // 5秒

// ================= メインループ =================
void loop() {
  M5.update();

  // ====== ボタン処理 ======
  if (M5.BtnA.wasPressed()) {
    screenMode = (screenMode == 1) ? 0 : 1;
    if (screenMode == 0) drawNormalScreen();
    else drawGraphScreen();
  }
  if (M5.BtnB.wasPressed()) { screenMode = 2; drawStatsScreen(); }
  if (M5.BtnC.wasPressed()) { screenMode = 3; drawDeviceScreen(digitalRead(ledPin)); }

  // ====== BH1750 初期化再試行 ======
  if (!bh1750Initialized) {
    if ((unsigned long)(millis() - lastBH1750Retry) >= BH1750_RETRY_INTERVAL) {
        lastBH1750Retry = millis();
        bh1750Initialized = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
        if (!bh1750Initialized) {
            M5.Lcd.setCursor(10, 50);
            M5.Lcd.println("Retrying BH1750...");
        } else {
            M5.Lcd.fillScreen(NORMAL_BG);
            drawNormalScreen();
        }
    }
    return;
}

  // ====== 1秒ごとのセンサー更新 ======
  if ((unsigned long)(millis() - lastUpdate) >= UPDATE_INTERVAL) {
    lastUpdate = millis();
  
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    if (isnan(temp) || isnan(hum)) {
      M5.Lcd.fillScreen(RED); 
      M5.Lcd.setTextSize(3);
      M5.Lcd.setTextColor(WHITE, RED);
      M5.Lcd.setCursor(10, 100); M5.Lcd.println("DHT Read Error!");
      digitalWrite(ledPin, LOW);
      return; // 次回ループで再試行
    }

    float lux = lightMeter.readLightLevel();
    if (lux < 0) lux = 0;

    bool ledOn = lux < LIGHT_THRESHOLD;
    digitalWrite(ledPin, ledOn ? HIGH : LOW);

    tempData[dataIndex] = temp;
    humData[dataIndex] = hum;
    luxData[dataIndex] = lux;
    dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;

    // 通常画面更新
    if (screenMode == 0) {
      M5.Lcd.setTextSize(5);

      uint16_t tempColor = (temp >= 30) ? RED : (temp <= 20) ? CYAN : GREEN;
      M5.Lcd.setTextColor(tempColor, NORMAL_BG);
      M5.Lcd.fillRect(140, 20, 160, 60, NORMAL_BG);
      M5.Lcd.setCursor(140, 20); M5.Lcd.printf("%.1f C", temp);

      uint16_t humColor = (hum >= 80) ? PURPLE : (hum <= 60) ? BLUE : GREEN;
      M5.Lcd.setTextColor(humColor, NORMAL_BG);
      M5.Lcd.fillRect(140, 100, 160, 60, NORMAL_BG);
      M5.Lcd.setCursor(140, 100); M5.Lcd.printf("%.1f %%", hum);

      uint16_t luxColor = (lux >= 800) ? WHITE : (lux <= 100) ? GRAY : YELLOW;
      M5.Lcd.setTextColor(luxColor, NORMAL_BG);
      M5.Lcd.fillRect(140, 180, 160, 60, NORMAL_BG);
      M5.Lcd.setCursor(140, 180); M5.Lcd.printf("%d Lx", (int)lux);
    }

    // グラフ画面のみ毎秒更新
    if (screenMode == 1) drawGraphScreen();
  }

  // ====== 24時間経過で統計リセット ======
  if ((unsigned long)(millis() - lastResetTime) >= RESET_INTERVAL) {
    resetStats();
    lastResetTime = millis();

    // 現在のモードを再描画
    if (screenMode == 0) drawNormalScreen();
    if (screenMode == 1) drawGraphScreen();
    if (screenMode == 2) drawStatsScreen();
  }
}