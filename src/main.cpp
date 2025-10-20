#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define PHOTO_PIN 36
#define LOAD_RESISTANCE 10000.0
#define MAX_DATA_POINTS 60
#define GRAY M5.Lcd.color565(100, 100, 100)
#define ORANGE M5.Lcd.color565(255, 165, 0)
#define PURPLE M5.Lcd.color565(128, 0, 128)
#define LUX_MAX_VALUE 1000.0 // 光センサーの最大値4095を約1000 Luxにマッピングすると仮定

Adafruit_BME280 bme;

// データ配列
float tempData[MAX_DATA_POINTS];
float humData[MAX_DATA_POINTS];
float luxData[MAX_DATA_POINTS]; // Luxのデータ
int dataIndex = 0;

// 画面モード
int screenMode = 0; // 0:通常 1:グラフ 2:平均値 3:デバイス状態

// 通常画面の背景色を定数として定義
const uint16_t NORMAL_BG = M5.Lcd.color565(210, 180, 140);

// ================= 関数プロトタイプ =================
void drawNormalScreen();
void drawGraphScreen();
void drawStatsScreen();
void drawDeviceScreen(bool fanOn, bool ledOn);

// ================= 初期設定 =================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  pinMode(PHOTO_PIN, INPUT);
  Wire.begin();
  delay(100);  // I2Cバス安定化待ち
  
  // データ配列をゼロで初期化
  for (int i = 0; i < MAX_DATA_POINTS; i++) {
    tempData[i] = 0.0;
    humData[i] = 0.0;
    luxData[i] = 0.0;
  }
  
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Initializing BME280...");

  delay(500);

  bool status = bme.begin(0x76, &Wire);
  if (!status) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("BME280 Error!");
    M5.Lcd.printf("Check: 0x77");
    while(1) { M5.update(); } // エラー時は停止
  }

  drawNormalScreen();  // 初期画面
}

// ================= 通常画面 =================
void drawNormalScreen() {
  uint16_t bg = NORMAL_BG; // 定数を使用
  M5.Lcd.fillScreen(bg);
  M5.Lcd.setTextColor(BLACK, bg);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(30, 40);  M5.Lcd.printf("Temp:");
  M5.Lcd.setCursor(30, 120); M5.Lcd.printf("Hum :");
  M5.Lcd.setCursor(30, 200); M5.Lcd.printf("Lux :");
}

// ================= グラフ画面 =================
void drawGraphScreen() {
  uint16_t bg = BLACK;
  M5.Lcd.fillScreen(bg);

  int baseY = 220;
  int width = 300;
  int startX = 10;
  
  float luxScale = LUX_MAX_VALUE / (float)baseY;  // 1000 / 220 ≈ 4.5

  for (int i = 1; i < MAX_DATA_POINTS; i++) {
    int idx1 = (dataIndex + i - 1) % MAX_DATA_POINTS;
    int idx2 = (dataIndex + i) % MAX_DATA_POINTS;
    int x1 = startX + (i - 1) * (width / MAX_DATA_POINTS);
    int x2 = startX + i * (width / MAX_DATA_POINTS);

    // Temp: Y軸スケーリングなし (元のまま)
    M5.Lcd.drawLine(x1, baseY - (int)tempData[idx1], x2, baseY - (int)tempData[idx2], RED);
    // Hum: Y軸スケーリング (/2) (元のまま)
    M5.Lcd.drawLine(x1, baseY - (int)(humData[idx1] / 2), x2, baseY - (int)(humData[idx2] / 2), BLUE);
    // Lux: 新しいスケーリング
    M5.Lcd.drawLine(x1, baseY - (int)(luxData[idx1] / luxScale), x2, baseY - (int)(luxData[idx2] / luxScale), YELLOW);
  }

  // 凡例
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(RED, bg);    M5.Lcd.setCursor(10, 10);  M5.Lcd.printf("Temp");
  M5.Lcd.setTextColor(BLUE, bg);   M5.Lcd.setCursor(110, 10); M5.Lcd.printf("Hum");
  M5.Lcd.setTextColor(YELLOW, bg); M5.Lcd.setCursor(200, 10); M5.Lcd.printf("Lux");
}

// ================= 平均・最大・最小 =================
void drawStatsScreen() {
  auto calcStats = [](float data[], float &avg, float &maxVal, float &minVal) {
    avg = 0; maxVal = -9999; minVal = 9999;
    
    // データ収集が完了していない場合（初期値0）をスキップ
    int validCount = 0;
    for (int i = 0; i < MAX_DATA_POINTS; i++) {
        if (data[i] != 0.0) { 
            avg += data[i];
            if (data[i] < minVal) minVal = data[i];
            if (data[i] > maxVal) maxVal = data[i];
            validCount++;
        }
    }
    // 実際に読み取ったデータポイント数で割る
    if (validCount > 0) {
        avg /= validCount;
    } else {
        avg = 0; 
        maxVal = 0; 
        minVal = 0;
    }
  };

  float avgT, maxT, minT;
  float avgH, maxH, minH;
  float avgL, maxL, minL;
  calcStats(tempData, avgT, maxT, minT);
  calcStats(humData, avgH, maxH, minH);
  calcStats(luxData, avgL, maxL, minL);

  uint16_t bg = NORMAL_BG; // 初期画面と同じ背景色
  M5.Lcd.fillScreen(bg);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(BLACK, bg);
  
  // ヘッダー
  M5.Lcd.setCursor(100, 10); M5.Lcd.printf("AVG");
  M5.Lcd.setCursor(200, 10); M5.Lcd.printf("MAX");
  M5.Lcd.setCursor(280, 10); M5.Lcd.printf("MIN");
  M5.Lcd.drawFastHLine(5, 45, 310, GRAY);
  
  int y_offset = 60;
  
  // --- 温度 ---
  M5.Lcd.setCursor(10, y_offset); M5.Lcd.printf("T:");
  
  // 色分けロジック: temp >= 30: RED, temp <= 20: CYAN, else: GREEN
  uint16_t tempColorAvg = (avgT >= 30) ? RED : (avgT <= 20) ? CYAN : GREEN;
  uint16_t tempColorMax = (maxT >= 30) ? RED : (maxT <= 20) ? CYAN : GREEN;
  uint16_t tempColorMin = (minT >= 30) ? RED : (minT <= 20) ? CYAN : GREEN;

  M5.Lcd.setCursor(80, y_offset); M5.Lcd.setTextColor(tempColorAvg, bg); M5.Lcd.printf("%.1f", avgT);
  M5.Lcd.setCursor(170, y_offset); M5.Lcd.setTextColor(tempColorMax, bg); M5.Lcd.printf("%.1f", maxT);
  M5.Lcd.setCursor(250, y_offset); M5.Lcd.setTextColor(tempColorMin, bg); M5.Lcd.printf("%.1f", minT);
  
  y_offset += 60;
  
  // --- 湿度 ---
  M5.Lcd.setCursor(10, y_offset); M5.Lcd.setTextColor(BLACK, bg); M5.Lcd.printf("H:");
  
  // 色分けロジック: hum >= 80: PURPLE, hum <= 60: BLUE, else: GREEN
  uint16_t humColorAvg = (avgH >= 80) ? PURPLE : (avgH <= 60) ? BLUE : GREEN;
  uint16_t humColorMax = (maxH >= 80) ? PURPLE : (maxH <= 60) ? BLUE : GREEN;
  uint16_t humColorMin = (minH >= 80) ? PURPLE : (minH <= 60) ? BLUE : GREEN;

  M5.Lcd.setCursor(80, y_offset); M5.Lcd.setTextColor(humColorAvg, bg); M5.Lcd.printf("%.1f", avgH);
  M5.Lcd.setCursor(170, y_offset); M5.Lcd.setTextColor(humColorMax, bg); M5.Lcd.printf("%.1f", maxH);
  M5.Lcd.setCursor(250, y_offset); M5.Lcd.setTextColor(humColorMin, bg); M5.Lcd.printf("%.1f", minH);
  
  y_offset += 60;
  
  // --- 照度 ---
  M5.Lcd.setCursor(10, y_offset); M5.Lcd.setTextColor(BLACK, bg); M5.Lcd.printf("L:");

  // 色分けロジック: lux >= 800: WHITE, lux <= 100: GRAY, else: YELLOW (Lux値に合わせて調整)
  uint16_t luxColorAvg = (avgL >= 800) ? WHITE : (avgL <= 100) ? GRAY : YELLOW;
  uint16_t luxColorMax = (maxL >= 800) ? WHITE : (maxL <= 100) ? GRAY : YELLOW;
  uint16_t luxColorMin = (minL >= 800) ? WHITE : (minL <= 100) ? GRAY : YELLOW;

  M5.Lcd.setCursor(80, y_offset); M5.Lcd.setTextColor(luxColorAvg, bg); M5.Lcd.printf("%d", (int)avgL);
  M5.Lcd.setCursor(170, y_offset); M5.Lcd.setTextColor(luxColorMax, bg); M5.Lcd.printf("%d", (int)maxL);
  M5.Lcd.setCursor(250, y_offset); M5.Lcd.setTextColor(luxColorMin, bg); M5.Lcd.printf("%d", (int)minL);
}

// ================= Fan / LED状態画面 =================
void drawDeviceScreen(bool fanOn, bool ledOn) {
  uint16_t bg = NORMAL_BG; // 初期画面と同じ背景色
  M5.Lcd.fillScreen(bg);
  M5.Lcd.setTextSize(4);
  
  // FAN: ON/OFF
  uint16_t fanColor = fanOn ? ORANGE : BLACK;
  // LED: ON/OFF 
  uint16_t ledColor = ledOn ? RED : BLACK;
  
  // Fan
  M5.Lcd.setCursor(40, 70);  M5.Lcd.setTextColor(BLACK, bg); M5.Lcd.printf("Fan : ");
  M5.Lcd.setTextColor(fanColor, bg);
  M5.Lcd.printf("%s", fanOn ? "ON" : "OFF");

  // LED
  M5.Lcd.setCursor(40, 150); M5.Lcd.setTextColor(BLACK, bg); M5.Lcd.printf("LED : ");
  M5.Lcd.setTextColor(ledColor, bg);
  M5.Lcd.printf("%s", ledOn ? "ON" : "OFF");
}

// ================= メインループ =================
void loop() {
  M5.update();

  // センサー読み取り
  int lightRaw = analogRead(PHOTO_PIN);
  // Luxに換算
  float lux = (lightRaw / 4095.0) * LUX_MAX_VALUE; 
  
  // BME280から実測値を読み取り
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();

  // ファン・LED判定 (Luxの判定基準を1000 Luxに合わせて調整)
  bool fanOn = temp > 25.0; // 25°C以上でON
  bool ledOn = lux < 200.0; // 200 Lux未満でON

  // データ配列に格納
  if (!isnan(temp) && !isnan(hum)) { // BME280のデータが有効かチェック
      tempData[dataIndex] = temp;
      humData[dataIndex] = hum;
      luxData[dataIndex] = lux;
      dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;
  }

  // ==== ボタン操作 ====
  if (M5.BtnA.wasPressed()) {
    screenMode = (screenMode == 1) ? 0 : 1;  // 通常↔グラフ
    if (screenMode == 0) drawNormalScreen();
    else drawGraphScreen();
  }
  if (M5.BtnB.wasPressed()) { screenMode = 2; drawStatsScreen(); }
  if (M5.BtnC.wasPressed()) { screenMode = 3; drawDeviceScreen(fanOn, ledOn); }

  // ==== 通常画面更新 ====
  if (screenMode == 0) {
    uint16_t bg = NORMAL_BG;
    M5.Lcd.setTextSize(5);

    // 温度
    uint16_t tempColor = (temp >= 30) ? RED : (temp <= 20) ? CYAN : GREEN;
    M5.Lcd.setTextColor(tempColor, bg);
    M5.Lcd.fillRect(140, 20, 160, 60, bg);
    M5.Lcd.setCursor(140, 20); M5.Lcd.printf("%.1f C", temp);

    // 湿度
    uint16_t humColor = (hum >= 80) ? PURPLE : (hum <= 60) ? BLUE : GREEN;
    M5.Lcd.setTextColor(humColor, bg);
    M5.Lcd.fillRect(140, 100, 160, 60, bg);
    M5.Lcd.setCursor(140, 100); M5.Lcd.printf("%.1f %%", hum);

    // 照度 
    uint16_t luxColor = (lux >= 800) ? WHITE : (lux <= 100) ? GRAY : YELLOW; // 1000 Lux基準で色分け
    M5.Lcd.setTextColor(luxColor, bg);
    M5.Lcd.fillRect(140, 180, 160, 60, bg);
    M5.Lcd.setCursor(140, 180); M5.Lcd.printf("%d Lx", (int)lux);
  }

  // ==== グラフ画面更新 ====
  if (screenMode == 1) drawGraphScreen();

  delay(1000);
}