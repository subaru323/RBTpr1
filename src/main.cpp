#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <math.h>

// ====== センサー設定 ======
#define BME_SDA 22
#define BME_SCL 21
Adafruit_BME280 bme;  // BME280

#define LIGHT_PIN 36  // NJL750L アナログ入力
constexpr int LIGHT_THRESHOLD = 100;

// ====== 出力ピン ======
constexpr int ledPin = 25;

// ====== グラフ/統計用定数 ======
constexpr float TEMP_GRAPH_MIN = 10.0;
constexpr float TEMP_GRAPH_MAX = 40.0;
constexpr float LUX_MAX_VALUE = 1000.0;
constexpr int MAX_DATA_POINTS = 60;

// ====== データ配列 ======
float tempData[MAX_DATA_POINTS];
float humData[MAX_DATA_POINTS];
float luxData[MAX_DATA_POINTS];
int dataIndex = 0;

// ====== 画面モード ======
int screenMode = 0; // 0:通常 1:グラフ 2:統計 3:LED状態

// ====== 色定義 ======
#define GRAY    M5.Lcd.color565(100,100,100)
#define ORANGE  M5.Lcd.color565(255,165,0)
#define PURPLE  M5.Lcd.color565(128,0,128)
#define RED     M5.Lcd.color565(255,0,0)
#define GREEN   M5.Lcd.color565(0,255,0)
#define BLUE    M5.Lcd.color565(0,0,255)
#define CYAN    M5.Lcd.color565(0,255,255)
#define YELLOW  M5.Lcd.color565(255,255,0)
#define WHITE   M5.Lcd.color565(255,255,255)
#define BLACK   M5.Lcd.color565(0,0,0)
#define NORMAL_BG M5.Lcd.color565(210,180,140)

// ====== タイマー ======
unsigned long lastUpdate = 0;
constexpr unsigned long UPDATE_INTERVAL = 1000;
unsigned long lastResetTime = 0;
constexpr unsigned long RESET_INTERVAL = 24UL * 60UL * 60UL * 1000UL;
unsigned long lastInteraction = 0;

// ====== アイドル顔アニメーション ======
unsigned long lastIdleUpdate = 0;
constexpr unsigned long IDLE_TIMEOUT = 180000; // 3分
float mouthAnimT = 0.0f;
bool blinking = false;
unsigned long blinkStartTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 4000;
const int BLINK_DURATION = 180;
bool eyesOpen = true;

// ====== プロトタイプ ======
void drawNormalScreen();
void drawGraphScreen();
void drawStatsScreen();
void drawDeviceScreen(bool ledOn);
void resetStats();
void drawIdleFaceAnimated();
void checkIdleFace();
bool initBME280();

// ================= BME280 初期化 =================
bool initBME280() {
    byte possibleAddresses[] = {0x75, 0x76, 0x77};
    for(byte i=0; i<3; i++){
        if(bme.begin(possibleAddresses[i])){
            M5.Lcd.printf("BME280 found at 0x%02X\n", possibleAddresses[i]);
            return true;
        }
    }
    M5.Lcd.println("BME280 NOT FOUND!");
    return false;
}

// ================= 初期設定 =================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE);
    pinMode(ledPin, OUTPUT);
    pinMode(LIGHT_PIN, INPUT);

    Wire.begin(BME_SDA, BME_SCL);
    Wire.setClock(100000); // 100kHz

    M5.Lcd.fillScreen(NORMAL_BG);
    M5.Lcd.setCursor(10,10);
    if(!initBME280()){
        M5.Lcd.println("BME280 Init Failed!");
    }

    resetStats();
    drawNormalScreen();
    lastResetTime = millis();
    lastInteraction = millis();
}

// ================= データリセット =================
void resetStats() {
    for(int i = 0; i < MAX_DATA_POINTS; i++)
        tempData[i] = humData[i] = luxData[i] = 0.0;
    dataIndex = 0;
}

// ================= 通常画面 =================
void drawNormalScreen() {
    M5.Lcd.fillScreen(NORMAL_BG);
    M5.Lcd.setTextColor(BLACK,NORMAL_BG);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(30,30);  M5.Lcd.printf("Temp:");
    M5.Lcd.setCursor(30,110); M5.Lcd.printf("Hum :");
    M5.Lcd.setCursor(30,190); M5.Lcd.printf("Lux :");
}

// ================= グラフ画面 =================
void drawGraphScreen() {
    static int lastIndex = -1;
    if(lastIndex == dataIndex) return;
    lastIndex = dataIndex;

    constexpr int baseY = 220;
    constexpr int width = 300;
    constexpr int startX = 10;

    float tempScale = baseY / (TEMP_GRAPH_MAX - TEMP_GRAPH_MIN);
    float humScale  = baseY / 100.0;
    float luxScale  = baseY / LUX_MAX_VALUE;

    M5.Lcd.fillRect(startX,0,width,baseY+20,NORMAL_BG);

    for(int i = 1; i < MAX_DATA_POINTS; i++){
        int idx1 = (dataIndex + i - 1) % MAX_DATA_POINTS;
        int idx2 = (dataIndex + i) % MAX_DATA_POINTS;

        int x1 = startX + (i - 1) * (width / MAX_DATA_POINTS);
        int x2 = startX + i * (width / MAX_DATA_POINTS);

        int yT1 = baseY - (int)((tempData[idx1] - TEMP_GRAPH_MIN) * tempScale);
        int yT2 = baseY - (int)((tempData[idx2] - TEMP_GRAPH_MIN) * tempScale);
        M5.Lcd.drawLine(x1,yT1,x2,yT2,RED);

        int yH1 = baseY - (int)(humData[idx1] * humScale);
        int yH2 = baseY - (int)(humData[idx2] * humScale);
        M5.Lcd.drawLine(x1,yH1,x2,yH2,BLUE);

        int yL1 = baseY - (int)(luxData[idx1] * luxScale);
        int yL2 = baseY - (int)(luxData[idx2] * luxScale);
        M5.Lcd.drawLine(x1,yL1,x2,yL2,YELLOW);
    }
}

// ================= 統計画面 =================
void drawStatsScreen() {
    M5.Lcd.fillScreen(NORMAL_BG);
    float sumT=0,sumH=0,sumL=0;
    float maxT=-9999,minT=9999;
    float maxH=-9999,minH=9999;
    float maxL=-9999,minL=9999;
    int countT=0,countH=0,countL=0;

    for(int i=0;i<MAX_DATA_POINTS;i++){
        if(tempData[i]!=0){sumT+=tempData[i]; maxT=max(maxT,tempData[i]); minT=min(minT,tempData[i]); countT++;}
        if(humData[i]!=0){sumH+=humData[i]; maxH=max(maxH,humData[i]); minH=min(minH,humData[i]); countH++;}
        if(luxData[i]!=0){sumL+=luxData[i]; maxL=max(maxL,luxData[i]); minL=min(minL,luxData[i]); countL++;}
    }

    float avgT = (countT>0)?sumT/countT:0;
    float avgH = (countH>0)?sumH/countH:0;
    float avgL = (countL>0)?sumL/countL:0;
    if(countT==0){maxT=minT=0;}
    if(countH==0){maxH=minH=0;}
    if(countL==0){maxL=minL=0;}

    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(BLACK,NORMAL_BG);
    M5.Lcd.setCursor(80,10); M5.Lcd.printf("AVG");
    M5.Lcd.setCursor(180,10); M5.Lcd.printf("MAX");
    M5.Lcd.setCursor(260,10); M5.Lcd.printf("MIN");
    M5.Lcd.drawFastHLine(5,45,310,BLACK);

    int y=60;
    M5.Lcd.setCursor(10,y); M5.Lcd.printf("T:");
    M5.Lcd.setCursor(70,y); M5.Lcd.printf("%.1f", avgT);
    M5.Lcd.setCursor(160,y);M5.Lcd.printf("%.1f", maxT);
    M5.Lcd.setCursor(240,y);M5.Lcd.printf("%.1f", minT);

    y+=60;
    M5.Lcd.setCursor(10,y); M5.Lcd.printf("H:");
    M5.Lcd.setCursor(70,y); M5.Lcd.printf("%.1f", avgH);
    M5.Lcd.setCursor(160,y);M5.Lcd.printf("%.1f", maxH);
    M5.Lcd.setCursor(240,y);M5.Lcd.printf("%.1f", minH);

    y+=60;
    M5.Lcd.setCursor(10,y); M5.Lcd.printf("L:");
    M5.Lcd.setCursor(70,y); M5.Lcd.printf("%d",(int)avgL);
    M5.Lcd.setCursor(160,y);M5.Lcd.printf("%d",(int)maxL);
    M5.Lcd.setCursor(240,y);M5.Lcd.printf("%d",(int)minL);
}

// ================= LED状態画面 =================
void drawDeviceScreen(bool ledOn) {
    M5.Lcd.fillScreen(NORMAL_BG);
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(40,100);
    M5.Lcd.setTextColor(ledOn ? ORANGE : BLACK, NORMAL_BG);
    M5.Lcd.printf("LED : %s", ledOn ? "ON" : "OFF");
}

// ================= アイドル顔描画 =================
void drawIdleFaceAnimated() {
    // 必要に応じてアイドル顔描画
}

// ================= アイドル制御 =================
void checkIdleFace() {
    // 必要に応じてアイドル顔制御
}

// ================= メインループ =================
void loop() {
    M5.update();
    unsigned long now = millis();

    // ボタン操作
    if(M5.BtnA.wasPressed()){
        lastInteraction = now;
        screenMode = (screenMode==1)?0:1;
        if(screenMode==0) drawNormalScreen();
        else drawGraphScreen();
    }
    if(M5.BtnB.wasPressed()){
        lastInteraction = now;
        screenMode = 2;
        drawStatsScreen();
    }
    if(M5.BtnC.wasPressed()){
        lastInteraction = now;
        screenMode = 3;
        drawDeviceScreen(digitalRead(ledPin));
    }

    // 1秒ごとデータ更新
    if(now - lastUpdate >= UPDATE_INTERVAL){
        lastUpdate = now;

        int prevIndex = (dataIndex + MAX_DATA_POINTS - 1) % MAX_DATA_POINTS;
        float prevTemp = tempData[prevIndex];
        float prevHum  = humData[prevIndex];

        float tempRead = bme.readTemperature();
        float humRead  = bme.readHumidity();
        float temp = isnan(tempRead) ? prevTemp : tempRead;
        float hum  = isnan(humRead)  ? prevHum  : humRead;

        int lux = analogRead(LIGHT_PIN);
        if(lux < 0) lux = 0;

        bool ledOn = (lux < LIGHT_THRESHOLD);
        digitalWrite(ledPin, ledOn ? HIGH : LOW);

        tempData[dataIndex] = temp;
        humData[dataIndex]  = hum;
        luxData[dataIndex]  = lux;
        dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;

        if(screenMode == 0){
            M5.Lcd.setTextSize(5);

            M5.Lcd.fillRect(140,20,180,60,NORMAL_BG);
            M5.Lcd.setCursor(140,20);
            M5.Lcd.setTextColor((temp>=30)?RED:(temp<=20)?CYAN:GREEN,NORMAL_BG);
            M5.Lcd.printf("%.1f C", temp);

            M5.Lcd.fillRect(140,100,180,60,NORMAL_BG);
            M5.Lcd.setCursor(140,100);
            M5.Lcd.setTextColor((hum>=80)?PURPLE:(hum<=60)?BLUE:GREEN,NORMAL_BG);
            M5.Lcd.printf("%.1f %%", hum);

            M5.Lcd.fillRect(140,180,180,60,NORMAL_BG);
            M5.Lcd.setCursor(140,180);
            M5.Lcd.setTextColor((lux>=800)?WHITE:(lux<=100)?GRAY:YELLOW,NORMAL_BG);
            M5.Lcd.printf("%d Lx",(int)lux);
        }

        if(screenMode == 1){
            drawGraphScreen();
        }
    }

    // 24時間ごとのリセット
    if(now - lastResetTime >= RESET_INTERVAL){
        resetStats();
        lastResetTime = now;
        if(screenMode==0) drawNormalScreen();
        else if(screenMode==1) drawGraphScreen();
        else if(screenMode==2) drawStatsScreen();
    }

    checkIdleFace();
}