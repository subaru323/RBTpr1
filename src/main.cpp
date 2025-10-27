#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <BH1750.h>
#include <math.h>

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
#define NORMAL_BG M5.Lcd.color565(210,180,140) // 薄茶色

// ====== タイマー ======
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000; // 1秒
unsigned long lastResetTime = 0;
const unsigned long RESET_INTERVAL = 24UL * 60UL * 60UL * 1000UL; // 24時間

// ====== 顔アニメーション用 ======
unsigned long lastIdleUpdate = 0;
const unsigned long IDLE_FRAME_INTERVAL = 200; // アニメーション更新間隔
bool eyesOpen = true;
bool mouthSmile = true;
unsigned long lastInteraction = 0;
const unsigned long IDLE_TIMEOUT = 180000; // 3分

// ====== BH1750リトライ ======
bool bh1750Initialized = false;
unsigned long lastBH1750Retry=0;
const unsigned long BH1750_RETRY_INTERVAL=5000;

// ====== 関数プロトタイプ ======
void drawNormalScreen();
void drawGraphScreen();
void drawStatsScreen();
void drawDeviceScreen(bool ledOn);
void resetStats();
void drawIdleFaceAnimated();
void checkIdleFace();

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

  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.setCursor(10,10);
  M5.Lcd.println("Initializing sensors...");
  delay(500);

  bh1750Initialized = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  if(!bh1750Initialized){
    M5.Lcd.setCursor(10,30);
    M5.Lcd.println("BH1750 Init Failed!");
  }

  for(int i=0;i<MAX_DATA_POINTS;i++){
    tempData[i]=humData[i]=luxData[i]=0.0;
  }

  drawNormalScreen();
  resetStats();
  lastResetTime=millis();
  lastInteraction=millis();
}

// ================= データリセット =================
void resetStats(){
  for(int i=0;i<MAX_DATA_POINTS;i++){
    tempData[i]=humData[i]=luxData[i]=0.0;
  }
  dataIndex=0;
}

// ================= 通常画面 =================
void drawNormalScreen(){
  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.setTextColor(BLACK,NORMAL_BG);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(30,30);  M5.Lcd.printf("Temp:");
  M5.Lcd.setCursor(30,110); M5.Lcd.printf("Hum :");
  M5.Lcd.setCursor(30,190); M5.Lcd.printf("Lux :");
}

// ================= グラフ画面 =================
void drawGraphScreen(){
  static bool firstDraw = true;
  static int lastIndex = -1;
  if(!firstDraw && lastIndex == dataIndex) return;
  firstDraw = false;
  lastIndex = dataIndex;

  int baseY=220; int width=300; int startX=10;
  float tempScale=baseY/(TEMP_GRAPH_MAX-TEMP_GRAPH_MIN);
  float humScale=baseY/100.0;
  float luxScale=baseY/LUX_MAX_VALUE;

  M5.Lcd.fillRect(startX,0,width,baseY+20,NORMAL_BG);

  for(int i=1;i<MAX_DATA_POINTS;i++){
    int idx1=(dataIndex+i-1)%MAX_DATA_POINTS;
    int idx2=(dataIndex+i)%MAX_DATA_POINTS;
    int x1=startX+(i-1)*(width/MAX_DATA_POINTS);
    int x2=startX+i*(width/MAX_DATA_POINTS);

    int yT1=baseY-(int)((tempData[idx1]-TEMP_GRAPH_MIN)*tempScale);
    int yT2=baseY-(int)((tempData[idx2]-TEMP_GRAPH_MIN)*tempScale);
    M5.Lcd.drawLine(x1,yT1,x2,yT2,RED);

    int yH1=baseY-(int)(humData[idx1]*humScale);
    int yH2=baseY-(int)(humData[idx2]*humScale);
    M5.Lcd.drawLine(x1,yH1,x2,yH2,BLUE);

    int yL1=baseY-(int)(luxData[idx1]*luxScale);
    int yL2=baseY-(int)(luxData[idx2]*luxScale);
    M5.Lcd.drawLine(x1,yL1,x2,yL2,YELLOW);
  }
}

// ================= 統計画面 =================
void drawStatsScreen(){
  M5.Lcd.fillScreen(NORMAL_BG);
  float avgT=0,maxT=-9999,minT=9999;
  float avgH=0,maxH=-9999,minH=9999;
  float avgL=0,maxL=-9999,minL=9999;
  int countT=0,countH=0,countL=0;

  for(int i=0;i<MAX_DATA_POINTS;i++){
    if(tempData[i]!=0.0){avgT+=tempData[i]; if(tempData[i]<minT)minT=tempData[i]; if(tempData[i]>maxT)maxT=tempData[i]; countT++;}
    if(humData[i]!=0.0){avgH+=humData[i]; if(humData[i]<minH)minH=humData[i]; if(humData[i]>maxH)maxH=humData[i]; countH++;}
    if(luxData[i]!=0.0){avgL+=luxData[i]; if(luxData[i]<minL)minL=luxData[i]; if(luxData[i]>maxL)maxL=luxData[i]; countL++;}
  }
  if(countT>0)avgT/=countT; else {avgT=maxT=minT=0;}
  if(countH>0)avgH/=countH; else {avgH=maxH=minH=0;}
  if(countL>0)avgL/=countL; else {avgL=maxL=minL=0;}

  M5.Lcd.setTextSize(3);
  M5.Lcd.setTextColor(BLACK,NORMAL_BG);
  M5.Lcd.setCursor(80,10); M5.Lcd.printf("AVG");
  M5.Lcd.setCursor(180,10); M5.Lcd.printf("MAX");
  M5.Lcd.setCursor(260,10); M5.Lcd.printf("MIN");
  M5.Lcd.drawFastHLine(5,45,310,BLACK);

  int y_offset=60;
  M5.Lcd.setCursor(10,y_offset); 
  M5.Lcd.printf("T:"); M5.Lcd.setCursor(70,y_offset); M5.Lcd.printf("%.1f",avgT);
  M5.Lcd.setCursor(160,y_offset); M5.Lcd.printf("%.1f",maxT);
  M5.Lcd.setCursor(240,y_offset); M5.Lcd.printf("%.1f",minT);

  y_offset+=60;
  M5.Lcd.setCursor(10,y_offset);
  M5.Lcd.printf("H:"); M5.Lcd.setCursor(70,y_offset); M5.Lcd.printf("%.1f",avgH);
  M5.Lcd.setCursor(160,y_offset); M5.Lcd.printf("%.1f",maxH);
  M5.Lcd.setCursor(240,y_offset); M5.Lcd.printf("%.1f",minH);

  y_offset+=60;
  M5.Lcd.setCursor(10,y_offset);
  M5.Lcd.printf("L:"); M5.Lcd.setCursor(70,y_offset); M5.Lcd.printf("%d",(int)avgL);
  M5.Lcd.setCursor(160,y_offset); M5.Lcd.printf("%d",(int)maxL);
  M5.Lcd.setCursor(240,y_offset); M5.Lcd.printf("%d",(int)minL);
}

// ================= LED状態画面 =================
void drawDeviceScreen(bool ledOn){
  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(40,100);
  M5.Lcd.setTextColor(ledOn?ORANGE:BLACK,NORMAL_BG);
  M5.Lcd.printf("LED : %s",ledOn?"ON":"OFF");
}

// ================= アイドル顔アニメーション =================
void drawIdleFaceAnimated() {
    M5.Lcd.fillScreen(NORMAL_BG);

    int centerX = 160;
    int centerY = 120;
    int eyeOffsetX = 50;
    int eyeOffsetY = 30;
    int eyeRadius = 20;
    int pupilRadius = 8;

    // 目
    if(eyesOpen){
        M5.Lcd.fillCircle(centerX - eyeOffsetX, centerY - eyeOffsetY, eyeRadius, WHITE);
        M5.Lcd.fillCircle(centerX + eyeOffsetX, centerY - eyeOffsetY, eyeRadius, WHITE);
        M5.Lcd.fillCircle(centerX - eyeOffsetX, centerY - eyeOffsetY, pupilRadius, BLACK);
        M5.Lcd.fillCircle(centerX + eyeOffsetX, centerY - eyeOffsetY, pupilRadius, BLACK);
    } else {
        // 自然な目パチ（半円ライン）
        M5.Lcd.drawLine(centerX - eyeOffsetX - eyeRadius, centerY - eyeOffsetY, centerX - eyeOffsetX + eyeRadius, centerY - eyeOffsetY, BLACK);
        M5.Lcd.drawLine(centerX + eyeOffsetX - eyeRadius, centerY - eyeOffsetY, centerX + eyeOffsetX + eyeRadius, centerY - eyeOffsetY, BLACK);
    }

    // 眉毛
    M5.Lcd.drawLine(centerX - eyeOffsetX - 15, centerY - eyeOffsetY - 20, centerX - eyeOffsetX + 15, centerY - eyeOffsetY - 20, BLACK);
    M5.Lcd.drawLine(centerX + eyeOffsetX - 15, centerY - eyeOffsetY - 20, centerX + eyeOffsetX + 15, centerY - eyeOffsetY - 20, BLACK);

    // 口（曲線）
    int mouthWidth = 80;
    int mouthHeight = mouthSmile ? 20 : 10;
    for(int i=0;i<=mouthHeight;i++){
        int y = centerY + 40 + i;
        float k = (float)i/mouthHeight;
        int startX = centerX - mouthWidth/2 + (int)(mouthWidth * 0.1 * sin(k*M_PI));
        int endX = centerX + mouthWidth/2 - (int)(mouthWidth * 0.1 * sin(k*M_PI));
        M5.Lcd.drawLine(startX, y, endX, y, RED);
    }
}

// ================= アイドルチェック =================
void checkIdleFace() {
    if(millis() - lastInteraction >= IDLE_TIMEOUT){
        if(millis() - lastIdleUpdate >= IDLE_FRAME_INTERVAL){
            lastIdleUpdate = millis();
            eyesOpen = !eyesOpen;
            mouthSmile = !mouthSmile;
            drawIdleFaceAnimated();
        }
    }
}

// ================= メインループ =================
void loop(){
  M5.update();

  // ボタン判定（操作時にアイドルタイマーリセット）
  if(M5.BtnA.wasPressed()){
    lastInteraction = millis();
    screenMode=(screenMode==1)?0:1;
    if(screenMode==0) drawNormalScreen();
    else drawGraphScreen();
  }
  if(M5.BtnB.wasPressed()){
    lastInteraction = millis();
    screenMode=2; drawStatsScreen();
  }
  if(M5.BtnC.wasPressed()){
    lastInteraction = millis();
    screenMode=3; drawDeviceScreen(digitalRead(ledPin));
  }

  // BH1750再試行
  if(!bh1750Initialized){
    if((millis()-lastBH1750Retry)>=BH1750_RETRY_INTERVAL){
      lastBH1750Retry=millis();
      bh1750Initialized=lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
      if(!bh1750Initialized){
        M5.Lcd.setCursor(10,50);
        M5.Lcd.println("Retrying BH1750...");
      } else drawNormalScreen();
    }
    checkIdleFace();
    return;
  }

  // 1秒ごとの更新
  if((millis()-lastUpdate)>=UPDATE_INTERVAL){
    lastUpdate=millis();

    float prevTemp = tempData[(dataIndex+MAX_DATA_POINTS-1)%MAX_DATA_POINTS];
    float prevHum  = humData[(dataIndex+MAX_DATA_POINTS-1)%MAX_DATA_POINTS];

    float tempRead = dht.readTemperature();
    float humRead  = dht.readHumidity();

    float temp = isnan(tempRead) ? prevTemp : tempRead;
    float hum  = isnan(humRead) ? prevHum : humRead;

    float lux=lightMeter.readLightLevel();
    if(lux<0) lux=0;

    bool ledOn = lux<LIGHT_THRESHOLD;
    digitalWrite(ledPin,ledOn?HIGH:LOW);

    tempData[dataIndex]=temp;
    humData[dataIndex]=hum;
    luxData[dataIndex]=lux;
    dataIndex=(dataIndex+1)%MAX_DATA_POINTS;

    if(screenMode==0){
      M5.Lcd.setTextSize(5);
      M5.Lcd.setTextColor((temp>=30)?RED:(temp<=20)?CYAN:GREEN,NORMAL_BG);
      M5.Lcd.fillRect(140,20,160,60,NORMAL_BG); M5.Lcd.setCursor(140,20); M5.Lcd.printf("%.1f C",temp);
      M5.Lcd.setTextColor((hum>=80)?PURPLE:(hum<=60)?BLUE:GREEN,NORMAL_BG);
      M5.Lcd.fillRect(140,100,160,60,NORMAL_BG); M5.Lcd.setCursor(140,100); M5.Lcd.printf("%.1f %%",hum);
      M5.Lcd.setTextColor((lux>=800)?WHITE:(lux<=100)?GRAY:YELLOW,NORMAL_BG);
      M5.Lcd.fillRect(140,180,160,60,NORMAL_BG); M5.Lcd.setCursor(140,180); M5.Lcd.printf("%d Lx",(int)lux);
    }

    if(screenMode==1) drawGraphScreen();
  }

  // 24時間リセット
  if((millis()-lastResetTime)>=RESET_INTERVAL){
    resetStats();
    lastResetTime=millis();
    if(screenMode==0) drawNormalScreen();
    else if(screenMode==1) drawGraphScreen();
    else if(screenMode==2) drawStatsScreen();
  }

  // アイドル顔チェック
  checkIdleFace();
}