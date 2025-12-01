// 高速化版フルコード（M5Stack Basic/Gray）
// - OpenWeatherMap (API key required)
// - 都市キャッシュ・差分描画・アニメ高速化対応
// - LED 機能は完全削除

#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

// ==================== 設定 ====================
const char* WIFI_SSID = "jikei-class-air";
const char* WIFI_PASS = "2tXDsAx4";
const char* API_KEY   = "1daa8d88352d231dd87dbd561d3c784a"; // 既に渡されたキーを使用
const char* NTP_SERVER = "ntp.nict.jp";
const long GMT_OFFSET_SEC = 9*3600;
const int DAYLIGHT_OFFSET_SEC = 0;

// 都市リスト（五大都市 + 沖縄）
struct CityInfo {
  String name;
  String id;
};
CityInfo cities[] = {
  {"Osaka",   "1853909"},
  {"Tokyo",   "1850147"},
  {"Nagoya",  "1856057"},
  {"Sapporo", "2128295"},
  {"Fukuoka", "1863967"},
  {"Naha",    "1894616"} // Okinawa (Naha)
};
const int NUM_CITIES = sizeof(cities)/sizeof(cities[0]);
int cityIndex = 0; // デフォルト Osaka

// ==================== 天気キャッシュ構造体 ====================
struct WeatherCache {
  bool valid = false;
  String cityName;
  String description;
  String symbol; // SUN/CLD/RAIN/SNOW/THTR/UNK
  float temp = 0.0f;
  unsigned long lastFetch = 0; // millis
};
WeatherCache weatherCache[NUM_CITIES];

// ==================== センサー ====================
#define BME_SDA 22
#define BME_SCL 21
Adafruit_BME280 bme;

#define LIGHT_PIN 36

// データ保管
constexpr int MAX_DATA_POINTS = 60;
float tempData[MAX_DATA_POINTS];
float humData[MAX_DATA_POINTS];
float luxData[MAX_DATA_POINTS];
int dataIndex = 0;

// UI / 色
#define NORMAL_BG M5.Lcd.color565(210,180,140)
#define BLACK   M5.Lcd.color565(0,0,0)
#define WHITE   M5.Lcd.color565(255,255,255)
#define RED     M5.Lcd.color565(255,0,0)
#define BLUE    M5.Lcd.color565(0,0,255)
#define GREEN   M5.Lcd.color565(0,255,0)
#define ORANGE  M5.Lcd.color565(255,165,0)
#define PURPLE  M5.Lcd.color565(128,0,128)
#define YELLOW  M5.Lcd.color565(255,255,0)
#define GRAY    M5.Lcd.color565(100,100,100)

// 画面モード
int screenMode = 0; // 0 normal,1 graph,2 stats,3 weather
const int NUM_SCREENS = 4;

// タイマー
unsigned long lastUpdate = 0;
constexpr unsigned long UPDATE_INTERVAL = 1000;
unsigned long lastWeatherUpdate = 0;
constexpr unsigned long UPDATE_WEATHER_INTERVAL = 1800000; // 30分

unsigned long lastInteraction = 0;

// アイドル顔
unsigned long lastIdleUpdate = 0;
constexpr unsigned long IDLE_TIMEOUT = 60000;
bool idleModeActive = false;

// メッセージ表示
unsigned long msgStartMillis = 0;
unsigned long msgDuration = 1200;
String msgText = "";
int msgX=200, msgY=10, msgW=120, msgH=20;
bool msgActive = false;

// 差分描画用に最後に表示した値を保持
int lastDisplayedCityIndex = -1;
String lastDisplayedSymbol = "";
String lastDisplayedDescription = "";
float lastDisplayedTemp = NAN; // compare with isnan

// プロトタイプ
void drawNormalScreen();
void drawGraphScreen();
void drawStatsScreen();
void drawWeatherScreenOptimized(int cityIdx, bool force);
void drawWeatherFullFromCache(int cityIdx); // full draw (used on first show)
void drawIdleFaceAnimated(float temp,float hum,int lux,float tempWeather,String weatherSymbol);
bool initBME280();
void checkIdleFace(float temp,float hum,int lux,float tempWeather,String weatherSymbol);
String getWeatherSymbol(String description);
String getWeatherIcon(String symbol);
void requestWeatherFetchNow(int cityIdx); // fetch and update cache (blocking)
void scheduleWeatherFetchForCity(int cityIdx);
void connectWiFi();
void updateWeatherUrlForCity(int idx, String &outUrl);
void showTempMessage(const String &text, int duration=1200);
void checkTempMessage();
void resetStats();

// ==================== ヘルパー ====================
void showTempMessage(const String &text, int duration){
  // 短く右上に表示（差分で消す）
  M5.Lcd.fillRect(msgX, msgY, msgW, msgH, NORMAL_BG);
  M5.Lcd.setCursor(msgX+2, msgY+2);
  M5.Lcd.setTextColor(BLACK, NORMAL_BG);
  M5.Lcd.setTextSize(2);
  M5.Lcd.print(text);
  msgStartMillis = millis();
  msgDuration = duration;
  msgActive = true;
  msgText = text;
}

void checkTempMessage(){
  if(msgActive && millis() - msgStartMillis >= msgDuration){
    M5.Lcd.fillRect(msgX, msgY, msgW, msgH, NORMAL_BG);
    msgActive = false;
  }
}

// ==================== BME280 初期化 ====================
bool initBME280(){
  byte possibleAddresses[] = {0x76, 0x77, 0x75};
  for(byte i=0;i<3;i++){
    if(bme.begin(possibleAddresses[i], &Wire)){
      showTempMessage("BME OK", 700);
      return true;
    }
  }
  showTempMessage("BME not found", 900);
  return false;
}

// ==================== 天気アイコン / シンボル ====================
String getWeatherSymbol(String description){
  String d = description;
  d.toLowerCase();
  if(d.indexOf("clear") != -1) return "SUN";
  if(d.indexOf("cloud") != -1) return "CLD";
  if(d.indexOf("rain") != -1 || d.indexOf("drizzle") != -1) return "RAIN";
  if(d.indexOf("snow") != -1) return "SNOW";
  if(d.indexOf("thun") != -1 || d.indexOf("tstorm") != -1) return "THTR";
  return "UNK";
}
String getWeatherIcon(String symbol){
  if(symbol=="SUN") return "☀";
  if(symbol=="CLD") return "☁";
  if(symbol=="RAIN") return "☂";
  if(symbol=="SNOW") return "❄";
  if(symbol=="THTR") return "⚡";
  return "❓";
}

// ==================== URL作成 ====================
void updateWeatherUrlForCity(int idx, String &outUrl){
  outUrl = "http://api.openweathermap.org/data/2.5/weather?id=" + cities[idx].id +
           "&units=metric&lang=en&appid=" + String(API_KEY);
}

// ==================== 都市切替処理（即時表示 + 背後フェッチ） ====================
void scheduleWeatherFetchForCity(int cityIdx){
  // 即時表示：キャッシュがあればそれを表示（瞬時）
  if(weatherCache[cityIdx].valid){
    drawWeatherScreenOptimized(cityIdx, false);
  } else {
    // キャッシュ無しなら空画面 -> フェッチ結果で完全描画
    M5.Lcd.fillScreen(NORMAL_BG);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(20, 20);
    M5.Lcd.printf("[%s]", cities[cityIdx].name.c_str());
    M5.Lcd.setCursor(20, 110);
    M5.Lcd.printf("Fetching...");
  }
  // バックグラウンド的に（ループで呼び出し）fetchする — 実際はブロッキングなので注意
  // ここでは直接呼ぶ（即ち同期）だが、ユーザー感覚はキャッシュを先に見せているため高速
  requestWeatherFetchNow(cityIdx);
}

// ==================== 天気取得（ブロッキング）: 必要最低限のフィールドだけ読む ====================
void requestWeatherFetchNow(int cityIdx){
  String url;
  updateWeatherUrlForCity(cityIdx, url);

  if(WiFi.status() != WL_CONNECTED){
    connectWiFi();
    if(WiFi.status() != WL_CONNECTED){
      showTempMessage("No WiFi", 900);
      return;
    }
  }

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  if(httpCode > 0){
    String payload = http.getString();
    // 小さめのStaticJsonDocumentでパース（必要なフィールドだけ）
    StaticJsonDocument<1024> doc; // 1KBで十分なはず
    DeserializationError err = deserializeJson(doc, payload);
    if(!err){
      WeatherCache c;
      c.valid = true;
      c.cityName = doc["name"].as<String>();
      // safe guard: weather[0].description maybe present
      const char* desc = doc["weather"][0]["description"] | "NoDesc";
      c.description = String(desc);
      c.symbol = getWeatherSymbol(c.description);
      c.temp = doc["main"]["temp"] | 0.0f;
      c.lastFetch = millis();
      weatherCache[cityIdx] = c;
      // 更新表示（差分）
      drawWeatherScreenOptimized(cityIdx, false);
    } else {
      showTempMessage("JSON err", 800);
    }
  } else {
    showTempMessage("HTTP err", 800);
  }
  http.end();
}

// ==================== WiFi (短めの接続待ち) ====================
void connectWiFi(){
  showTempMessage("WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while(WiFi.status() != WL_CONNECTED && millis()-start < 8000){
    delay(150);
  }
  if(WiFi.status() == WL_CONNECTED) showTempMessage("WiFi OK", 700);
  else showTempMessage("WiFi Fail", 800);
}

// ==================== 画面描画 (差分で素早く描画) ====================
void drawWeatherFullFromCache(int cityIdx){
  // フル描画（初回など）
  WeatherCache &c = weatherCache[cityIdx];
  M5.Lcd.fillScreen(NORMAL_BG);

  // City
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor(BLACK, NORMAL_BG);
  M5.Lcd.setCursor(20, 20);
  M5.Lcd.printf("[%s]", c.cityName.c_str());

  // Icon
  String icon = getWeatherIcon(c.symbol);
  M5.Lcd.setTextSize(6);
  M5.Lcd.setCursor(20, 70);
  M5.Lcd.print(icon);

  // Description
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(130, 85);
  M5.Lcd.printf("%s", c.description.c_str());

  // Temp
  M5.Lcd.setTextSize(6);
  M5.Lcd.setCursor(70, 160);
  M5.Lcd.printf("%.1f C", c.temp);

  // Updated age
  unsigned long secago = (c.lastFetch==0)?0:((millis()-c.lastFetch)/1000);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20, 230);
  if(c.lastFetch==0) M5.Lcd.printf("No data");
  else M5.Lcd.printf("Updated %lus ago", secago);

  // save lastDisplayed values
  lastDisplayedCityIndex = cityIdx;
  lastDisplayedSymbol = c.symbol;
  lastDisplayedDescription = c.description;
  lastDisplayedTemp = c.temp;
}

// 差分描画: 前回表示と違う部分のみ更新する（高速）
void drawWeatherScreenOptimized(int cityIdx, bool force){
  WeatherCache &c = weatherCache[cityIdx];
  if(!c.valid){
    // no cache yet -> full fetch will fill it. show placeholder
    M5.Lcd.fillRect(20,70,280,120,NORMAL_BG); // clear main area
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(20, 110);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.printf("No cached data");
    lastDisplayedCityIndex = -1;
    return;
  }

  // If forced full redraw or city changed -> full redraw
  if(force || lastDisplayedCityIndex != cityIdx){
    drawWeatherFullFromCache(cityIdx);
    return;
  }

  // Otherwise update only changed parts
  // City name rarely changes but check
  if(lastDisplayedCityIndex != cityIdx){
    M5.Lcd.fillRect(20,20,280,40,NORMAL_BG);
    M5.Lcd.setTextSize(4);
    M5.Lcd.setCursor(20,20);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.printf("[%s]", c.cityName.c_str());
    lastDisplayedCityIndex = cityIdx;
  }
  // Icon (symbol)
  if(c.symbol != lastDisplayedSymbol){
    M5.Lcd.fillRect(20,70,80,100,NORMAL_BG); // clear icon area
    M5.Lcd.setTextSize(6);
    M5.Lcd.setCursor(20,70);
    M5.Lcd.print(getWeatherIcon(c.symbol));
    lastDisplayedSymbol = c.symbol;
  }
  // Description
  if(c.description != lastDisplayedDescription){
    M5.Lcd.fillRect(120,80,180,40,NORMAL_BG);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(130,85);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.printf("%s", c.description.c_str());
    lastDisplayedDescription = c.description;
  }
  // Temp (small delta threshold to avoid constant update)
  if(isnan(lastDisplayedTemp) || fabs(c.temp - lastDisplayedTemp) >= 0.1f){
    M5.Lcd.fillRect(70,150,200,80,NORMAL_BG);
    M5.Lcd.setTextSize(6);
    M5.Lcd.setCursor(70,160);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.printf("%.1f C", c.temp);
    lastDisplayedTemp = c.temp;
  }
  // Updated age (always refresh small area)
  unsigned long secago = (c.lastFetch==0)?0:((millis()-c.lastFetch)/1000);
  M5.Lcd.fillRect(20,225,200,16,NORMAL_BG);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20,230);
  if(c.lastFetch==0) M5.Lcd.printf("No data");
  else M5.Lcd.printf("Updated %lus ago", secago);

  // After updating weather screen, update face if currently on weather screen or idle
  // (higher-level loop will call checkIdleFace)
}

// ==================== 通常 / グラフ / 統計 画面 ====================
void drawNormalScreen(){
  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.setTextColor(BLACK, NORMAL_BG);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(30, 30);  M5.Lcd.printf("Temp:");
  M5.Lcd.setCursor(30,110);  M5.Lcd.printf("Hum :");
  M5.Lcd.setCursor(30,190);  M5.Lcd.printf("Lux :");

  M5.Lcd.fillRect(140, 20, 180, 60, NORMAL_BG);  // Temp
  M5.Lcd.fillRect(140,100, 180, 60, NORMAL_BG);  // Hum
  M5.Lcd.fillRect(140,180, 180, 60, NORMAL_BG);  // Lux
}

void drawGraphScreen(){
  static int lastIndex = -1;
  if(lastIndex == dataIndex) return;
  lastIndex = dataIndex;

  constexpr int baseY=220,width=300,startX=10;
  float tempScale = baseY / 30.0; // 10..40
  float humScale = baseY / 100.0;
  float luxScale = baseY / 2000.0;

  M5.Lcd.fillRect(startX,0,width,baseY+20,NORMAL_BG);

  for(int i=1;i<MAX_DATA_POINTS;i++){
    int idx1 = (dataIndex + i - 1) % MAX_DATA_POINTS;
    int idx2 = (dataIndex + i) % MAX_DATA_POINTS;
    int x1 = startX + (i-1)*(width/MAX_DATA_POINTS);
    int x2 = startX + i*(width/MAX_DATA_POINTS);

    int yT1 = baseY - (int)((tempData[idx1]-10.0f)*tempScale);
    int yT2 = baseY - (int)((tempData[idx2]-10.0f)*tempScale);
    M5.Lcd.drawLine(x1, yT1, x2, yT2, RED);

    int yH1 = baseY - (int)(humData[idx1]*humScale);
    int yH2 = baseY - (int)(humData[idx2]*humScale);
    M5.Lcd.drawLine(x1, yH1, x2, yH2, BLUE);

    int yL1 = baseY - (int)(luxData[idx1]*luxScale);
    int yL2 = baseY - (int)(luxData[idx2]*luxScale);
    M5.Lcd.drawLine(x1, yL1, x2, yL2, YELLOW);
  }
}

void drawStatsScreen(){
  M5.Lcd.fillScreen(NORMAL_BG);
  float sumT=0,sumH=0,sumL=0;
  float maxT=-9999,minT=9999,maxH=-9999,minH=9999,maxL=-9999,minL=9999;
  int countT=0,countH=0,countL=0;

  for(int i=0;i<MAX_DATA_POINTS;i++){
    if(tempData[i]!=0){ sumT += tempData[i]; maxT = max(maxT,tempData[i]); minT=min(minT,tempData[i]); countT++; }
    if(humData[i]!=0){ sumH += humData[i]; maxH = max(maxH,humData[i]); minH=min(minH,humData[i]); countH++; }
    if(luxData[i]!=0){ sumL += luxData[i]; maxL = max(maxL,luxData[i]); minL=min(minL,luxData[i]); countL++; }
  }

  float avgT = (countT>0)? sumT/countT : 0;
  float avgH = (countH>0)? sumH/countH : 0;
  float avgL = (countL>0)? sumL/countL : 0;
  if(countT==0){ maxT=minT=0; } if(countH==0){ maxH=minH=0;} if(countL==0){ maxL=minL=0; }

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

// ==================== アイドル顔描画 (簡潔で高速) ====================
void drawIdleFaceAnimated(float temp,float hum,int lux,float tempWeather,String weatherSymbol){
  int cx=160, cy=120;
  int faceR=80;
  // full redraw for face (acceptable in idle)
  M5.Lcd.fillScreen(NORMAL_BG);
  M5.Lcd.fillCircle(cx, cy, faceR, M5.Lcd.color565(255,224,189));

  // cheeks
  if(temp >= 30){
    M5.Lcd.fillCircle(cx-40, cy+30, 15, RED);
    M5.Lcd.fillCircle(cx+40, cy+30, 15, RED);
  } else if(temp <= 15){
    M5.Lcd.fillCircle(cx-40, cy+30, 15, BLUE);
    M5.Lcd.fillCircle(cx+40, cy+30, 15, BLUE);
  }

  unsigned long now = millis();
  static unsigned long lastBlink = 0;
  static bool blinkState = false;
  if(now - lastBlink > 4000){
    blinkState = true; lastBlink = now;
  }
  if(blinkState && now - lastBlink > 200) blinkState = false;

  int eyeW = 18;
  int eyeH = blinkState ? 3 : 12;
  if(temp >= 30) eyeH -= 3;
  if(hum >= 70) eyeH -= 3;
  if(lux <= 200) eyeH -= 2;
  if(eyeH < 3) eyeH = 3;

  M5.Lcd.fillEllipse(cx-40, cy-20, eyeW, eyeH, BLACK);
  M5.Lcd.fillCircle(cx-40-3, cy-22, 3, WHITE);
  M5.Lcd.fillEllipse(cx+40, cy-20, eyeW, eyeH, BLACK);
  M5.Lcd.fillCircle(cx+40-3, cy-22, 3, WHITE);

  int browOffset = 0;
  if(weatherSymbol == "SUN") browOffset = -5;
  else if(weatherSymbol == "RAIN") browOffset = 6;
  M5.Lcd.drawLine(cx-50, cy-40+browOffset, cx-30, cy-50+browOffset, BLACK);
  M5.Lcd.drawLine(cx+30, cy-50+browOffset, cx+50, cy-40+browOffset, BLACK);

  int mouthY = cy + 40;
  int mouthW = 50;
  int mouthH = 10;
  float mouthAnim = sin(now / 300.0f) * 3.0f;

  // weather-priority expression
  if(weatherSymbol == "SUN"){
    for(int i=0;i<mouthW;i++){
      int x = cx - mouthW/2 + i;
      int y = mouthY + (int)(- (mouthH) * sin(PI * i / mouthW)) + (int)mouthAnim;
      M5.Lcd.drawPixel(x, y, ORANGE);
    }
  } else if(weatherSymbol == "RAIN"){
    for(int i=0;i<mouthW;i++){
      int x = cx - mouthW/2 + i;
      int y = mouthY + (int)((mouthH) * sin(PI * i / mouthW)) + (int)mouthAnim;
      M5.Lcd.drawPixel(x, y, BLUE);
    }
  } else if(weatherSymbol == "SNOW"){
    M5.Lcd.drawLine(cx-mouthW/2, mouthY, cx+mouthW/2, mouthY, BLACK);
  } else if(weatherSymbol == "THTR"){
    M5.Lcd.fillCircle(cx, mouthY, 8, BLACK);
  } else {
    // CLD or UNK -> apply temp-based variants
    if(temp >= 30){
      for(int i=0;i<mouthW;i++){
        int x = cx - mouthW/2 + i;
        int y = mouthY + (int)((mouthH/1.5) * sin(PI * i / mouthW)) + (int)mouthAnim;
        M5.Lcd.drawPixel(x, y, RED);
      }
    } else if(temp <= 10){
      for(int i=0;i<mouthW;i++){
        int x = cx - mouthW/2 + i;
        int y = mouthY + (int)((mouthH/2) * sin(PI * i / mouthW) + sin(now/80.0f)*2);
        M5.Lcd.drawPixel(x, y, BLUE);
      }
    } else {
      for(int i=0;i<mouthW;i++){
        int x = cx - mouthW/2 + i;
        int y = mouthY + (int)(- (mouthH/2.0) * sin(PI * i / mouthW)) + (int)mouthAnim;
        M5.Lcd.drawPixel(x, y, PURPLE);
      }
    }
  }
}

// ==================== アイドル制御 ====================
void checkIdleFace(float temp,float hum,int lux,float tempWeather,String weatherSymbol){
  unsigned long now = millis();
  if(now - lastInteraction > IDLE_TIMEOUT){
    if(!idleModeActive){
      idleModeActive = true;
      lastIdleUpdate = now;
    }
    if(now - lastIdleUpdate > 120){
      lastIdleUpdate = now;
      drawIdleFaceAnimated(temp, hum, lux, tempWeather, weatherSymbol);
    }
  } else {
    if(idleModeActive){
      idleModeActive = false;
      // re-draw current screen
      switch(screenMode){
        case 0: drawNormalScreen(); break;
        case 1: drawGraphScreen(); break;
        case 2: drawStatsScreen(); break;
        case 3: drawWeatherScreenOptimized(cityIndex, true); break;
        default: drawNormalScreen(); break;
      }
    }
  }
}

// ==================== 初期化 ====================
void setup(){
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);

  // BME init
  Wire.begin(BME_SDA,BME_SCL);
  Wire.setClock(100000);
  initBME280();

  resetStats();
  lastInteraction = millis();

  connectWiFi();
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

  // pre-fill cache with invalid entries
  for(int i=0;i<NUM_CITIES;i++) weatherCache[i].valid = false;

  // 起動時は通常画面のみ描画
  drawNormalScreen();

  // 天気データは裏で取得（画面はまだ表示しない）
  requestWeatherFetchNow(cityIndex);
  lastWeatherUpdate = millis();
}

// ==================== reset stats ====================
void resetStats(){
  for(int i=0;i<MAX_DATA_POINTS;i++) tempData[i]=0.0;
  for(int i=0;i<MAX_DATA_POINTS;i++) humData[i]=0.0;
  for(int i=0;i<MAX_DATA_POINTS;i++) luxData[i]=0.0;
  dataIndex=0;
}

// ==================== メインループ ====================
void loop(){
  M5.update();
  unsigned long now = millis();

  // Buttons: A previous / B home / C next
  if(M5.BtnA.wasPressed()){
    lastInteraction = now;
    // if on weather screen, next city; else cycle backward screen
    if(screenMode == 3){
      cityIndex = (cityIndex + NUM_CITIES - 1) % NUM_CITIES;
      scheduleWeatherFetchForCity(cityIndex);
    } else {
      screenMode = (screenMode + NUM_SCREENS - 1) % NUM_SCREENS;
      if(!idleModeActive){
        switch(screenMode){
          case 0: drawNormalScreen(); break;
          case 1: drawGraphScreen(); break;
          case 2: drawStatsScreen(); break;
          case 3: drawWeatherScreenOptimized(cityIndex, true); break;
        }
      }
    }
  }

  if(M5.BtnB.wasPressed()){
    lastInteraction = now;
    screenMode = 0;
    if(!idleModeActive) drawNormalScreen();
  }

  if(M5.BtnC.wasPressed()){
    lastInteraction = now;
    if(screenMode == 3){
      cityIndex = (cityIndex + 1) % NUM_CITIES;
      scheduleWeatherFetchForCity(cityIndex);
    } else {
      screenMode = (screenMode + 1) % NUM_SCREENS;
      if(!idleModeActive){
        switch(screenMode){
          case 0: drawNormalScreen(); break;
          case 1: drawGraphScreen(); break;
          case 2: drawStatsScreen(); break;
          case 3: drawWeatherScreenOptimized(cityIndex, true); break;
        }
      }
    }
  }

  // 1s data update
  if(now - lastUpdate >= UPDATE_INTERVAL){
    lastUpdate = now;
    int prevIndex = (dataIndex + MAX_DATA_POINTS - 1) % MAX_DATA_POINTS;
    float prevTemp = (tempData[prevIndex] != 0.0f) ? tempData[prevIndex] : 20.0f;
    float prevHum  = (humData[prevIndex] != 0.0f) ? humData[prevIndex] : 50.0f;

    float temp = prevTemp;
    float hum = prevHum;
    // try BME reading safely
    // If bme is initialized, read; otherwise keep previous
    if(bme.readTemperature() == bme.readTemperature()){ // forces a read; cheap check
      float t = bme.readTemperature();
      float h = bme.readHumidity();
      if(!isnan(t)) temp = t;
      if(!isnan(h)) hum = h;
    }

    int lux = analogRead(LIGHT_PIN);
    if(lux < 0) lux = 0;
    if(lux > 2000) lux = 2000;

    tempData[dataIndex] = temp;
    humData[dataIndex] = hum;
    luxData[dataIndex] = lux;
    dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;

    // periodic weather update (in background of loop)
    if(now - lastWeatherUpdate >= UPDATE_WEATHER_INTERVAL){
      // update cache for current city
      requestWeatherFetchNow(cityIndex);
      lastWeatherUpdate = now;
    }

    // Update screen quickly if not idle
    if(!idleModeActive){
      switch(screenMode){
        case 0:
          // partial update of numbers only
          M5.Lcd.fillRect(140,20,180,60,NORMAL_BG);
          M5.Lcd.setCursor(150,30);
          M5.Lcd.setTextColor((temp>=30)?RED:(temp<=20)?GRAY:GREEN,NORMAL_BG);
          M5.Lcd.setTextSize(4);
          M5.Lcd.printf("%.1f C", temp);

          M5.Lcd.fillRect(140,100,180,60,NORMAL_BG);
          M5.Lcd.setCursor(150,110);
          M5.Lcd.setTextColor((hum>=80)?PURPLE:(hum<=60)?BLUE:GREEN,NORMAL_BG);
          M5.Lcd.setTextSize(4);
          M5.Lcd.printf("%.1f %%", hum);

          M5.Lcd.fillRect(140,180,180,60,NORMAL_BG);
          M5.Lcd.setCursor(150,190);
          M5.Lcd.setTextColor((lux>=800)?WHITE:(lux<=100)?GRAY:YELLOW,NORMAL_BG);
          M5.Lcd.setTextSize(4);
          M5.Lcd.printf("%d Lx", lux);
          break;

        case 1: drawGraphScreen(); break;
        case 2: drawStatsScreen(); break;
        case 3: drawWeatherScreenOptimized(cityIndex, false); break;
      }
    }

    checkTempMessage();
    // Idle face update uses cached weather symbol
    String sym = weatherCache[cityIndex].valid ? weatherCache[cityIndex].symbol : String("UNK");
    float tmpw = weatherCache[cityIndex].valid ? weatherCache[cityIndex].temp : 0.0f;
    checkIdleFace(temp, hum, lux, tmpw, sym);
  }
}