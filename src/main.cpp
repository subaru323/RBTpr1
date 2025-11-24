#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

// ------------------- WiFi & 天気設定 -------------------
const char* WIFI_SSID = "jikei-class-air";
const char* WIFI_PASS = "2tXDsAx4";
const char* API_KEY   = "1daa8d88352d231dd87dbd561d3c784a";
const char* NTP_SERVER = "ntp.nict.jp";
const long GMT_OFFSET_SEC = 9*3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// 都市情報構造体とリスト（★追加）
struct CityInfo {
    String name;
    String id;
};
CityInfo cities[] = {
    {"Osaka",   "1853909"},
    {"Tokyo",   "1850147"},
    {"Sapporo", "2128295"},
    {"Nagoya",  "1856057"},
    {"Fukuoka", "1863967"},
    {"Naha",    "1894616"} // Okinawa (Naha)
};
const int NUM_CITIES = sizeof(cities) / sizeof(cities[0]);
int cityIndex = 0; // デフォルト Osaka

// API URL（初期は Osaka、★動的に更新）
// String weatherUrl = "http://api.openweathermap.org/data/2.5/weather?id=" + String(CITY_ID) +
//                     "&units=metric&lang=en&appid=" + String(API_KEY);
String weatherUrl = "";

// 天気情報
String CurrentCity = "Osaka";
String CurrentDescription = "No Data";
String CurrentSymbol = "UNK";
float CurrentTempWeather = 0.0f;
unsigned long lastWeatherFetchMillis = 0;

// ------------------- センサー設定 -------------------
#define BME_SDA 22
#define BME_SCL 21
Adafruit_BME280 bme;

#define LIGHT_PIN 36
constexpr int LIGHT_THRESHOLD = 100;

// 出力ピン
constexpr int ledPin = 25;

// グラフ/統計用定数
constexpr float TEMP_GRAPH_MIN = 10.0;
constexpr float TEMP_GRAPH_MAX = 40.0;
constexpr float LUX_MAX_VALUE = 1000.0;
constexpr int MAX_DATA_POINTS = 60;

// データ配列
float tempData[MAX_DATA_POINTS];
float humData[MAX_DATA_POINTS];
float luxData[MAX_DATA_POINTS];
int dataIndex = 0;

// 画面モード
int screenMode = 0; // 0:通常 1:グラフ 2:統計 3:LED状態 4:天気ページ（★追加）

// 色定義
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

// ------------------- タイマー -------------------
unsigned long lastUpdate = 0;
constexpr unsigned long UPDATE_INTERVAL = 1000;
unsigned long lastWeatherUpdate = 0;
constexpr unsigned long UPDATE_WEATHER_INTERVAL = 1800000; // 30分
unsigned long lastResetTime = 0;
constexpr unsigned long RESET_INTERVAL = 24UL*60UL*60UL*1000UL;
unsigned long lastInteraction = 0;

// アイドル顔アニメ
unsigned long lastIdleUpdate = 0;
constexpr unsigned long IDLE_TIMEOUT = 60000;
bool eyesOpen = true;
unsigned long lastBlinkTime = 0;
const int BLINK_DURATION = 180;
bool idleModeActive = false;

// ==================== プロトタイプ ====================
void drawNormalScreen();
void drawGraphScreen();
void drawStatsScreen();
void drawDeviceScreen(bool ledOn);
void drawWeatherScreen(); // ★追加
void resetStats();
void drawIdleFaceAnimated(float temp,float hum,int lux,float tempWeather,String weatherSymbol);
bool initBME280();
void checkIdleFace(float temp,float hum,int lux,float tempWeather,String weatherSymbol);
String getWeatherSymbol(String description);
String getWeatherIcon(String symbol); // ★追加（アイコン取得）
void fetchAndUpdateWeather();
void connectWiFi();
void updateTimeDisplay();
void updateWeatherUrlForCurrentCity(); // ★追加（URL更新）

// ==================== BME280 初期化 ====================
bool initBME280() {
    byte possibleAddresses[] = {0x75,0x76,0x77};
    for(byte i=0;i<3;i++){
        if(bme.begin(possibleAddresses[i], &Wire)){
            M5.Lcd.printf("BME280 found at 0x%02X\n", possibleAddresses[i]);
            return true;
        }
    }
    M5.Lcd.println("BME280 NOT FOUND!");
    return false;
}

// ==================== 天気関数 ====================
String getWeatherSymbol(String description) {
  description.toLowerCase();
  if (description.indexOf("clear") != -1) return "SUN";
  else if (description.indexOf("cloud") != -1) return "CLD";
  else if (description.indexOf("rain") != -1 || description.indexOf("drizzle") != -1) return "RAIN";
  else if (description.indexOf("snow") != -1) return "SNOW";
  else if (description.indexOf("thunder") != -1) return "THTR";
  else return "UNK";
}

String getWeatherIcon(String symbol){
  // Bタイプ用のアイコン（★追加） — UTF-8 を利用
  if(symbol == "SUN") return "☀";
  if(symbol == "CLD") return "☁";
  if(symbol == "RAIN") return "🌧";
  if(symbol == "SNOW") return "❄";
  if(symbol == "THTR") return "⛈";
  return "❓";
}

void updateWeatherUrlForCurrentCity(){
  // cities[cityIndex] を使って weatherUrl を組み立てる（★追加）
  weatherUrl = "http://api.openweathermap.org/data/2.5/weather?id=" + cities[cityIndex].id +
               "&units=metric&lang=en&appid=" + String(API_KEY);
}

// JSON を取得して現在の天気情報を更新
void fetchAndUpdateWeather() {
  if(WiFi.status() != WL_CONNECTED){
    // WiFi 切れていたら再接続（軽く）
    connectWiFi();
  }
  HTTPClient http;
  http.begin(weatherUrl);
  int httpCode = http.GET();
  if (httpCode > 0){
    String payload = http.getString();
    // JSON の容量に備えて大きめに（★少し拡張）
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc,payload);
    if(!error){
      CurrentCity = doc["name"].as<String>();
      CurrentDescription = doc["weather"][0]["description"].as<String>();
      CurrentSymbol = getWeatherSymbol(CurrentDescription);
      CurrentTempWeather = doc["main"]["temp"].as<float>();
      lastWeatherFetchMillis = millis();
    } else {
      // パース失敗時は情報は更新しない
    }
  }
  http.end();
}

// ==================== WiFi ====================
void connectWiFi() {
  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(10,10);
  M5.Display.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    M5.Display.print(".");
    // 10秒経っても繋がらなければ抜けて再試行の余地を残す
    if(millis() - start > 10000) break;
  }
  if(WiFi.status() == WL_CONNECTED){
    M5.Display.println("\nConnected!");
  } else {
    M5.Display.println("\nWiFi Failed");
  }
  delay(800);
}

// ==================== 初期設定 ====================
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE);
    pinMode(ledPin, OUTPUT);
    pinMode(LIGHT_PIN, INPUT);

    Wire.begin(BME_SDA,BME_SCL);
    Wire.setClock(100000);

    M5.Lcd.fillScreen(NORMAL_BG);
    if(!initBME280()) M5.Lcd.println("BME280 Init Failed!");

    resetStats();
    drawNormalScreen();
    lastResetTime = millis();
    lastInteraction = millis();

    // WiFi & 時刻
    connectWiFi();
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    // 天気 URL 初期化（★追加）
    updateWeatherUrlForCurrentCity();
    fetchAndUpdateWeather();
    lastWeatherUpdate = millis();
}

// ==================== データリセット ====================
void resetStats(){
    for(int i=0;i<MAX_DATA_POINTS;i++) tempData[i]=humData[i]=luxData[i]=0.0;
    dataIndex=0;
}

// ==================== 通常画面 ====================
void drawNormalScreen(){
    M5.Lcd.fillScreen(NORMAL_BG);
    M5.Lcd.setTextColor(BLACK,NORMAL_BG);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(30,30);  M5.Lcd.printf("Temp:");
    M5.Lcd.setCursor(30,110); M5.Lcd.printf("Hum :");
    M5.Lcd.setCursor(30,190); M5.Lcd.printf("Lux :");
}

// ==================== グラフ画面 ====================
void drawGraphScreen(){
    static int lastIndex=-1;
    if(lastIndex==dataIndex) return;
    lastIndex=dataIndex;

    constexpr int baseY=220,width=300,startX=10;
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

// ==================== 統計画面 ====================
void drawStatsScreen(){
    M5.Lcd.fillScreen(NORMAL_BG);
    float sumT=0,sumH=0,sumL=0,maxT=-9999,minT=9999,maxH=-9999,minH=9999,maxL=-9999,minL=9999;
    int countT=0,countH=0,countL=0;

    for(int i=0;i<MAX_DATA_POINTS;i++){
        if(tempData[i]!=0){sumT+=tempData[i]; maxT=max(maxT,tempData[i]); minT=min(minT,tempData[i]); countT++;}
        if(humData[i]!=0){sumH+=humData[i]; maxH=max(maxH,humData[i]); minH=min(minH,humData[i]); countH++;}
        if(luxData[i]!=0){sumL+=luxData[i]; maxL=max(maxL,luxData[i]); minL=min(minL,luxData[i]); countL++;}
    }

    float avgT=(countT>0)?sumT/countT:0;
    float avgH=(countH>0)?sumH/countH:0;
    float avgL=(countL>0)?sumL/countL:0;
    if(countT==0){maxT=minT=0;} if(countH==0){maxH=minH=0;} if(countL==0){maxL=minL=0;}

    M5.Lcd.setTextSize(3);
    M5.Lcd.setTextColor(BLACK,NORMAL_BG);
    M5.Lcd.setCursor(80,10); M5.Lcd.printf("AVG");
    M5.Lcd.setCursor(180,10); M5.Lcd.printf("MAX");
    M5.Lcd.setCursor(260,10); M5.Lcd.printf("MIN");
    M5.Lcd.drawFastHLine(5,45,310,BLACK);

    int y=60;
    M5.Lcd.setCursor(10,y); M5.Lcd.printf("T:"); M5.Lcd.setCursor(70,y); M5.Lcd.printf("%.1f",avgT);
    M5.Lcd.setCursor(160,y); M5.Lcd.printf("%.1f",maxT); M5.Lcd.setCursor(240,y); M5.Lcd.printf("%.1f",minT);

    y+=60;
    M5.Lcd.setCursor(10,y); M5.Lcd.printf("H:"); M5.Lcd.setCursor(70,y); M5.Lcd.printf("%.1f",avgH);
    M5.Lcd.setCursor(160,y); M5.Lcd.printf("%.1f",maxH); M5.Lcd.setCursor(240,y); M5.Lcd.printf("%.1f",minH);

    y+=60;
    M5.Lcd.setCursor(10,y); M5.Lcd.printf("L:"); M5.Lcd.setCursor(70,y); M5.Lcd.printf("%d",(int)avgL);
    M5.Lcd.setCursor(160,y); M5.Lcd.printf("%d",(int)maxL); M5.Lcd.setCursor(240,y); M5.Lcd.printf("%d",(int)minL);
}

// ==================== LED画面 ====================
void drawDeviceScreen(bool ledOn){
    M5.Lcd.fillScreen(NORMAL_BG);
    M5.Lcd.setCursor(40,100);
    M5.Lcd.setTextSize(4);
    M5.Lcd.setTextColor(ledOn?ORANGE:BLACK,NORMAL_BG);
    M5.Lcd.printf("LED : %s",ledOn?"ON":"OFF");
}

// ==================== 天気画面（Bタイプ：大きな文字＋アイコン風） ====================  // ★追加
void drawWeatherScreen(){
    M5.Lcd.fillScreen(M5.Lcd.color565(200,230,255)); // 淡い水色背景
    M5.Lcd.setTextColor(BLACK, M5.Lcd.color565(200,230,255));

    // 大きな都市名
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(20,20);
    M5.Lcd.printf("[%s]", CurrentCity.c_str());

    // 天気アイコン（大きく）と説明
    String icon = getWeatherIcon(CurrentSymbol);
    M5.Lcd.setTextSize(8);
    M5.Lcd.setCursor(20,70);
    M5.Lcd.print(icon);

    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(120,90);
    M5.Lcd.printf("%s", CurrentDescription.c_str());

    // 温度表示（大きく）
    M5.Lcd.setTextSize(6);
    M5.Lcd.setCursor(120,150);
    M5.Lcd.printf("%.1f C", CurrentTempWeather);

    // 情報（都市切替案内）
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10,230);
    M5.Lcd.printf("A: Next city   C: Prev city   B: Home");

    // 最終更新時刻（簡易）
    M5.Lcd.setCursor(10,260);
    if(lastWeatherFetchMillis>0){
        unsigned long secsAgo = (millis()-lastWeatherFetchMillis)/1000;
        M5.Lcd.printf("Updated %lus ago", secsAgo);
    } else {
        M5.Lcd.printf("Updated: --");
    }
}

// ==================== アイドル顔 ====================

void drawIdleFaceAnimated(float temp,float hum,int lux,float tempWeather,String weatherSymbol){
    int cx=160,cy=120;
    int eyeW=30,eyeH=20;
    int mouthW=80,mouthH=25;

    M5.Lcd.fillScreen(NORMAL_BG);

    // ほっぺ
    if(temp>30 || hum>70) M5.Lcd.fillCircle(cx-50,cy+30,12,CYAN);
    M5.Lcd.fillCircle(cx-50,cy+20,15,M5.Lcd.color565(255,182,193));
    M5.Lcd.fillCircle(cx+50,cy+20,15,M5.Lcd.color565(255,182,193));

    // 目
    eyesOpen = (millis()/500)%2;
    int eyeActualH = eyesOpen?eyeH:5;
    M5.Lcd.fillEllipse(cx-50,cy-20,eyeW,eyeActualH,BLACK);
    M5.Lcd.fillEllipse(cx+50,cy-20,eyeW,eyeActualH,BLACK);
    M5.Lcd.fillCircle(cx-50,cy-22,5,WHITE);
    M5.Lcd.fillCircle(cx+50,cy-22,5,WHITE);

    // 口
    float mouthT = sin(millis()/200.0)*8.0;
    if(temp>30 || weatherSymbol=="SUN"){
        M5.Lcd.fillEllipse(cx,cy+40+mouthT, mouthW/2, mouthH, RED);
    } else if(hum>70 || weatherSymbol=="RAIN"){
        M5.Lcd.drawLine(cx-mouthW/2, cy+40+mouthT, cx+mouthW/2, cy+40+mouthT, RED);
    } else if(weatherSymbol=="SNOW" || weatherSymbol=="CLD"){
        M5.Lcd.fillRect(cx-mouthW/2, cy+40+mouthT, mouthW, mouthH/2, PURPLE);
    } else {
        M5.Lcd.fillRect(cx-mouthW/2, cy+40+mouthT, mouthW, mouthH/2, RED);
    }

    // 眉毛
    M5.Lcd.drawLine(cx-70,cy-40,cx-30,cy-50,BLACK);
    M5.Lcd.drawLine(cx+30,cy-50,cx+70,cy-40,BLACK);
}

// ==================== アイドル制御 ====================

void checkIdleFace(float temp,float hum,int lux,float tempWeather,String weatherSymbol){
    unsigned long now=millis();
    if(now-lastInteraction>IDLE_TIMEOUT){
        idleModeActive=true;
        if(now-lastIdleUpdate>100){
            lastIdleUpdate=now;
            if(now-lastBlinkTime>4000){eyesOpen=false; lastBlinkTime=now;}
            if(!eyesOpen && now-lastBlinkTime>BLINK_DURATION) eyesOpen=true;
            drawIdleFaceAnimated(temp,hum,lux,tempWeather,weatherSymbol);
        }
    } else {
        if(idleModeActive){
            idleModeActive=false;
            switch(screenMode){
                case 0: drawNormalScreen(); break;
                case 1: drawGraphScreen(); break;
                case 2: drawStatsScreen(); break;
                case 3: drawDeviceScreen(digitalRead(ledPin)); break;
                case 4: drawWeatherScreen(); break; // ★追加
            }
        }
    }
}

// ==================== メインループ ====================
void loop(){
    M5.update();
    unsigned long now=millis();

    // --- ボタン操作 ---
    if(M5.BtnA.wasPressed()){
        lastInteraction=now;
        if(screenMode==4){
            // 天気ページ上なら次の都市へ（★追加: A = 次）
            cityIndex = (cityIndex + 1) % NUM_CITIES;
            updateWeatherUrlForCurrentCity();
            fetchAndUpdateWeather();
            drawWeatherScreen();
        } else {
            // 元の挙動（画面切り替え）を維持（戻る方向）
            screenMode=(screenMode+4)%5; // -1 mod 5
            if(!idleModeActive){
                switch(screenMode){
                    case 0: drawNormalScreen(); break;
                    case 1: drawGraphScreen(); break;
                    case 2: drawStatsScreen(); break;
                    case 3: drawDeviceScreen(digitalRead(ledPin)); break;
                    case 4: drawWeatherScreen(); break;
                }
            }
        }
    }
    if(M5.BtnB.wasPressed()){
        lastInteraction=now;
        screenMode=0;
        if(!idleModeActive) drawNormalScreen();
    }
    if(M5.BtnC.wasPressed()){
        lastInteraction=now;
        if(screenMode==4){
            // 天気ページ上なら前の都市へ（★追加: C = 前）
            cityIndex = (cityIndex - 1 + NUM_CITIES) % NUM_CITIES;
            updateWeatherUrlForCurrentCity();
            fetchAndUpdateWeather();
            drawWeatherScreen();
        } else {
            screenMode=(screenMode+1)%5;
            if(!idleModeActive){
                switch(screenMode){
                    case 0: drawNormalScreen(); break;
                    case 1: drawGraphScreen(); break;
                    case 2: drawStatsScreen(); break;
                    case 3: drawDeviceScreen(digitalRead(ledPin)); break;
                    case 4: drawWeatherScreen(); break;
                }
            }
        }
    }

    // 1秒ごとデータ更新
    if(now-lastUpdate>=UPDATE_INTERVAL){
        lastUpdate=now;

        int prevIndex=(dataIndex+MAX_DATA_POINTS-1)%MAX_DATA_POINTS;
        float prevTemp=tempData[prevIndex];
        float prevHum=humData[prevIndex];

        float tempRead=bme.readTemperature();
        float humRead=bme.readHumidity();
        float temp=isnan(tempRead)?prevTemp:tempRead;
        float hum=isnan(humRead)?prevHum:humRead;
        int lux=analogRead(LIGHT_PIN); if(lux<0) lux=0;
        bool ledOn=(lux<LIGHT_THRESHOLD); digitalWrite(ledPin,ledOn?HIGH:LOW);

        tempData[dataIndex]=temp;
        humData[dataIndex]=hum;
        luxData[dataIndex]=lux;
        dataIndex=(dataIndex+1)%MAX_DATA_POINTS;

        // 天気更新（※ここでは定期更新だが、都市切替時は即時fetchする）
        if(now-lastWeatherUpdate>=UPDATE_WEATHER_INTERVAL){
            fetchAndUpdateWeather();
            lastWeatherUpdate=now;
        }

        // 通常画面更新
        if(!idleModeActive){
            switch(screenMode){
                case 0:
                    M5.Lcd.fillRect(120,30,100,40,NORMAL_BG);
                    M5.Lcd.setCursor(120,30); M5.Lcd.printf("%.1f C",temp);
                    M5.Lcd.fillRect(120,110,100,40,NORMAL_BG);
                    M5.Lcd.setCursor(120,110); M5.Lcd.printf("%.1f %%",hum);
                    M5.Lcd.fillRect(120,190,100,40,NORMAL_BG);
                    M5.Lcd.setCursor(120,190); M5.Lcd.printf("%d",lux);
                    break;
                case 1: drawGraphScreen(); break;
                case 2: drawStatsScreen(); break;
                case 3: drawDeviceScreen(ledOn); break;
                case 4: drawWeatherScreen(); break; // ★追加
            }
        }

        checkIdleFace(temp,hum,lux,CurrentTempWeather,CurrentSymbol);
    }
}