// ==========================================
// 統合版（M5Stack Basic/Gray）
// - BME280センサー + 照度センサー + LED自動制御
// - OpenWeatherMap API + 6都市切替
// ==========================================

#include <M5Unified.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

const char* WIFI_SSID = "jikei-class-air";
const char* WIFI_PASS = "2tXDsAx4";
const char* API_KEY   = "1daa8d88352d231dd87dbd561d3c784a";
const char* NTP_SERVER = "ntp.nict.jp";
const long GMT_OFFSET_SEC = 9*3600;
const int DAYLIGHT_OFFSET_SEC = 0;

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
    {"Naha",    "1894616"}
};
const int NUM_CITIES = 6;
int cityIndex = 0;

struct WeatherCache {
    bool valid = false;
    String cityName;
    String description;
    String symbol;
    float temp = 0.0f;
    unsigned long lastFetch = 0;
};
WeatherCache weatherCache[6];

#define BME_SDA 22
#define BME_SCL 21
Adafruit_BME280 bme;
#define LIGHT_PIN 36
constexpr int LIGHT_THRESHOLD = 100;
constexpr int LUX_MAX = 2000;
constexpr int ledPin = 25;

constexpr int MAX_DATA_POINTS = 60;
float tempData[MAX_DATA_POINTS];
float humData[MAX_DATA_POINTS];
float luxData[MAX_DATA_POINTS];
int dataIndex = 0;

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
#define CYAN    M5.Lcd.color565(0,255,255)

int screenMode = 0;
const int NUM_SCREENS = 4;

unsigned long lastUpdate = 0;
constexpr unsigned long UPDATE_INTERVAL = 1000;
unsigned long lastWeatherUpdate = 0;
constexpr unsigned long UPDATE_WEATHER_INTERVAL = 1800000;
unsigned long lastInteraction = 0;

unsigned long lastIdleUpdate = 0;
constexpr unsigned long IDLE_TIMEOUT = 60000;
bool idleModeActive = false;
bool eyesOpen = true;
unsigned long lastBlinkTime = 0;
const int BLINK_DURATION = 180;

unsigned long msgStartMillis = 0;
unsigned long msgDuration = 1200;
int msgX=200, msgY=10, msgW=120, msgH=20;
bool msgActive = false;

int lastDisplayedCityIndex = -1;
String lastDisplayedSymbol = "";
String lastDisplayedDescription = "";
float lastDisplayedTemp = NAN;

void drawNormalScreen();
void drawGraphScreen();
void drawStatsScreen();
void drawWeatherScreenOptimized(int cityIdx, bool force);
void drawWeatherFullFromCache(int cityIdx);
void drawIdleFaceAnimated(float temp,float hum,int lux,float tempWeather,String weatherSymbol);
bool initBME280();
void checkIdleFace(float temp,float hum,int lux,float tempWeather,String weatherSymbol);
String getWeatherSymbol(String description);
void requestWeatherFetchNow(int cityIdx);
void scheduleWeatherFetchForCity(int cityIdx);
void connectWiFi();
void showTempMessage(const String &text, int duration=1200);
void checkTempMessage();
void resetStats();

void showTempMessage(const String &text, int duration){
    M5.Lcd.fillRect(msgX, msgY, msgW, msgH, NORMAL_BG);
    M5.Lcd.setCursor(msgX+2, msgY+2);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.print(text);
    msgStartMillis = millis();
    msgDuration = duration;
    msgActive = true;
}

void checkTempMessage(){
    if(msgActive && millis() - msgStartMillis >= msgDuration){
        M5.Lcd.fillRect(msgX, msgY, msgW, msgH, NORMAL_BG);
        msgActive = false;
    }
}

bool initBME280(){
    byte possibleAddresses[] = {0x76, 0x77, 0x75};
    for(byte i=0;i<3;i++){
        if(bme.begin(possibleAddresses[i], &Wire)){
            showTempMessage("BME OK", 700);
            return true;
        }
    }
    showTempMessage("BME Error", 900);
    return false;
}

String getWeatherSymbol(String description){
    String d = description; d.toLowerCase();
    if(d.indexOf("clear") != -1) return "SUN";
    if(d.indexOf("cloud") != -1) return "CLD";
    if(d.indexOf("rain") != -1 || d.indexOf("drizzle") != -1) return "RAIN";
    if(d.indexOf("snow") != -1) return "SNOW";
    if(d.indexOf("thun") != -1 || d.indexOf("tstorm") != -1) return "THTR";
    return "UNK";
}

void connectWiFi(){
    showTempMessage("WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while(WiFi.status() != WL_CONNECTED && millis()-start < 8000) delay(150);
    if(WiFi.status() == WL_CONNECTED) showTempMessage("WiFi OK", 700);
    else showTempMessage("WiFi Fail", 800);
}

void requestWeatherFetchNow(int cityIdx){
    String url = "http://api.openweathermap.org/data/2.5/weather?id=" + cities[cityIdx].id +
                 "&units=metric&lang=en&appid=" + String(API_KEY);

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
        StaticJsonDocument<1024> doc;
        if(!deserializeJson(doc, payload)){
            weatherCache[cityIdx].valid = true;
            weatherCache[cityIdx].cityName = cities[cityIdx].name;
            weatherCache[cityIdx].description = String(doc["weather"][0]["description"] | "NoDesc");
            weatherCache[cityIdx].symbol = getWeatherSymbol(weatherCache[cityIdx].description);
            weatherCache[cityIdx].temp = doc["main"]["temp"] | 0.0f;
            weatherCache[cityIdx].lastFetch = millis();
            drawWeatherScreenOptimized(cityIdx, false);
        } else showTempMessage("JSON err", 800);
    } else showTempMessage("HTTP err", 800);
    http.end();
}

void scheduleWeatherFetchForCity(int cityIdx){
    if(weatherCache[cityIdx].valid){
        drawWeatherScreenOptimized(cityIdx, false);
    } else {
        M5.Lcd.fillScreen(NORMAL_BG);
        M5.Lcd.setTextColor(BLACK, NORMAL_BG);
        M5.Lcd.setTextSize(4);
        M5.Lcd.setCursor(20, 20);
        M5.Lcd.printf("[%s]", cities[cityIdx].name.c_str());
        M5.Lcd.setCursor(20, 110);
        M5.Lcd.printf("Fetching...");
    }
    requestWeatherFetchNow(cityIdx);
}

void drawWeatherFullFromCache(int cityIdx){
    WeatherCache &c = weatherCache[cityIdx];
    M5.Lcd.fillScreen(NORMAL_BG);

    M5.Lcd.setTextSize(4);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.setCursor(20, 20);
    M5.Lcd.printf("[%s]", c.cityName.c_str());

    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(20, 85);
    M5.Lcd.printf("%s", c.description.c_str());

    M5.Lcd.setTextSize(6);
    M5.Lcd.setCursor(70, 140);
    M5.Lcd.printf("%.1f C", c.temp);

    lastDisplayedCityIndex = cityIdx;
    lastDisplayedSymbol = c.symbol;
    lastDisplayedDescription = c.description;
    lastDisplayedTemp = c.temp;
}

void drawWeatherScreenOptimized(int cityIdx, bool force){
    WeatherCache &c = weatherCache[cityIdx];
    if(!c.valid){
        M5.Lcd.fillRect(20,70,280,120,NORMAL_BG);
        M5.Lcd.setTextSize(3);
        M5.Lcd.setCursor(20, 110);
        M5.Lcd.setTextColor(BLACK, NORMAL_BG);
        M5.Lcd.printf("No Data");
        lastDisplayedCityIndex = -1;
        return;
    }

    if(force || lastDisplayedCityIndex != cityIdx){
        drawWeatherFullFromCache(cityIdx);
        return;
    }

    if(c.description != lastDisplayedDescription){
        M5.Lcd.fillRect(20,80,280,40,NORMAL_BG);
        M5.Lcd.setTextSize(3);
        M5.Lcd.setCursor(20,85);
        M5.Lcd.setTextColor(BLACK, NORMAL_BG);
        M5.Lcd.printf("%s", c.description.c_str());
        lastDisplayedDescription = c.description;
    }

    if(isnan(lastDisplayedTemp) || fabs(c.temp - lastDisplayedTemp) >= 0.1f){
        M5.Lcd.fillRect(70,140,200,80,NORMAL_BG);
        M5.Lcd.setTextSize(6);
        M5.Lcd.setCursor(70,140);
        M5.Lcd.setTextColor(BLACK, NORMAL_BG);
        M5.Lcd.printf("%.1f C", c.temp);
        lastDisplayedTemp = c.temp;
    }
}

void drawNormalScreen(){
    M5.Lcd.fillScreen(NORMAL_BG);
    M5.Lcd.setTextColor(BLACK, NORMAL_BG);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(30, 30);  M5.Lcd.printf("Temp:");
    M5.Lcd.setCursor(30,110);  M5.Lcd.printf("Hum :");
    M5.Lcd.setCursor(30,190);  M5.Lcd.printf("Light:");
}

void drawGraphScreen(){
    static int lastIndex = -1;
    if(lastIndex == dataIndex) return;
    lastIndex = dataIndex;

    constexpr int baseY=220,width=300,startX=10;
    float tempScale = baseY / 30.0;
    float humScale = baseY / 100.0;
    float luxScale = baseY / 100.0;

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

        float lux1Pct = (luxData[idx1] / LUX_MAX) * 100.0f;
        float lux2Pct = (luxData[idx2] / LUX_MAX) * 100.0f;
        int yL1 = baseY - (int)(lux1Pct*luxScale);
        int yL2 = baseY - (int)(lux2Pct*luxScale);
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
        if(luxData[i]!=0){ 
            float luxPct = (luxData[i] / LUX_MAX) * 100.0f;
            sumL += luxPct; 
            maxL = max(maxL, luxPct); 
            minL = min(minL, luxPct); 
            countL++; 
        }
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
    M5.Lcd.setCursor(10,y); M5.Lcd.printf("L:"); M5.Lcd.setCursor(70,y); M5.Lcd.printf("%d%%",(int)avgL);
    M5.Lcd.setCursor(160,y); M5.Lcd.printf("%d%%",(int)maxL); M5.Lcd.setCursor(240,y); M5.Lcd.printf("%d%%",(int)minL);
}

void drawIdleFaceAnimated(float temp,float hum,int lux,float tempWeather,String weatherSymbol){
    int cx=160,cy=120;
    int eyeW=20,eyeH=15;
    int mouthW=60,mouthH=20;

    M5.Lcd.fillScreen(NORMAL_BG);
    if(temp>30 || hum>70) M5.Lcd.fillCircle(cx-50,cy+30,12,CYAN);
    M5.Lcd.fillCircle(cx-50,cy+20,15,M5.Lcd.color565(255,182,193));
    M5.Lcd.fillCircle(cx+50,cy+20,15,M5.Lcd.color565(255,182,193));
    M5.Lcd.fillEllipse(cx-40,cy-20,eyeW, eyesOpen ? eyeH : 2, BLACK);
    M5.Lcd.fillEllipse(cx+40,cy-20,eyeW, eyesOpen ? eyeH : 2, BLACK);
    if(eyesOpen){
        M5.Lcd.fillCircle(cx-40,cy-22,5,WHITE);
        M5.Lcd.fillCircle(cx+40,cy-22,5,WHITE);
    }

    float mouthT = sin(millis()/200.0)*8.0;
    if(temp>30 || weatherSymbol=="SUN"){
        M5.Lcd.fillEllipse(cx,cy+40+mouthT, mouthW/2, mouthH, RED);
    } else if(hum>70 || weatherSymbol=="RAIN"){
        M5.Lcd.drawLine(cx-mouthW/2, cy+40+mouthT, cx+mouthW/2, cy+40+mouthT, RED);
    } else {
        M5.Lcd.fillRect(cx-mouthW/2, cy+40+mouthT, mouthW, mouthH/2, RED);
    }

    unsigned long now = millis();
    if(now - lastBlinkTime > 4000){
        eyesOpen = false;
        lastBlinkTime = now;
    }
    if(!eyesOpen && now - lastBlinkTime > BLINK_DURATION){
        eyesOpen = true;
    }
}

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
            switch(screenMode){
                case 0: drawNormalScreen(); break;
                case 1: drawGraphScreen(); break;
                case 2: drawStatsScreen(); break;
                case 3: drawWeatherScreenOptimized(cityIndex, true); break;
            }
        }
    }
}

void resetStats(){
    for(int i=0;i<MAX_DATA_POINTS;i++){ 
        tempData[i]=0.0; 
        humData[i]=0.0; 
        luxData[i]=0.0; 
    }
    dataIndex = 0;
}

void setup(){
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(WHITE);
    
    pinMode(ledPin, OUTPUT);
    pinMode(LIGHT_PIN, INPUT);

    Wire.begin(BME_SDA,BME_SCL);
    Wire.setClock(100000);
    initBME280();

    resetStats();
    lastInteraction = millis();

    connectWiFi();
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    for(int i=0;i<NUM_CITIES;i++) weatherCache[i].valid = false;

    drawNormalScreen();
    requestWeatherFetchNow(cityIndex);
    lastWeatherUpdate = millis();
}

void loop(){
    M5.update();
    unsigned long now = millis();

    if(M5.BtnA.wasPressed()){
        lastInteraction = now;
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

    if(now - lastUpdate >= UPDATE_INTERVAL){
        lastUpdate = now;

        int prevIndex = (dataIndex + MAX_DATA_POINTS - 1) % MAX_DATA_POINTS;
        float temp = tempData[prevIndex] != 0.0f ? tempData[prevIndex] : 20.0f;
        float hum  = humData[prevIndex] != 0.0f ? humData[prevIndex] : 50.0f;

        if(bme.readTemperature() == bme.readTemperature()){
            float t = bme.readTemperature();
            float h = bme.readHumidity();
            if(!isnan(t)) temp = t;
            if(!isnan(h)) hum = h;
        }
        
        int lux = analogRead(LIGHT_PIN); 
        if(lux<0) lux=0; 
        if(lux>LUX_MAX) lux=LUX_MAX;

        bool ledOn = (lux < LIGHT_THRESHOLD);
        digitalWrite(ledPin, ledOn ? HIGH : LOW);

        tempData[dataIndex] = temp;
        humData[dataIndex] = hum;
        luxData[dataIndex] = lux;
        dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;

        if(now - lastWeatherUpdate >= UPDATE_WEATHER_INTERVAL){
            requestWeatherFetchNow(cityIndex);
            lastWeatherUpdate = now;
        }

        if(!idleModeActive){
            switch(screenMode){
                case 0: {
                    M5.Lcd.setTextSize(5);
                    M5.Lcd.fillRect(140,20,180,60,NORMAL_BG);
                    M5.Lcd.setCursor(140,20);
                    M5.Lcd.setTextColor((temp>=30)?RED:(temp<=20)?CYAN:GREEN,NORMAL_BG);
                    M5.Lcd.printf("%.1f C", temp);

                    M5.Lcd.fillRect(140,100,180,60,NORMAL_BG);
                    M5.Lcd.setCursor(140,100);
                    M5.Lcd.setTextColor((hum>=80)?PURPLE:(hum<=60)?BLUE:GREEN,NORMAL_BG);
                    M5.Lcd.printf("%.1f %%", hum);

                    int luxPercent = (int)((lux / (float)LUX_MAX) * 100.0f);
                    if(luxPercent > 100) luxPercent = 100;
                    M5.Lcd.fillRect(140,180,180,60,NORMAL_BG);
                    M5.Lcd.setCursor(140,180);
                    M5.Lcd.setTextColor((luxPercent>=80)?WHITE:(luxPercent<=10)?GRAY:YELLOW,NORMAL_BG);
                    M5.Lcd.printf("%d %%", luxPercent);
                    break;
                }
                case 1: drawGraphScreen(); break;
                case 2: drawStatsScreen(); break;
                case 3: drawWeatherScreenOptimized(cityIndex, false); break;
            }
        }

        checkTempMessage();
        String sym = weatherCache[cityIndex].valid ? weatherCache[cityIndex].symbol : String("UNK");
        float tmpw = weatherCache[cityIndex].valid ? weatherCache[cityIndex].temp : 0.0f;
        checkIdleFace(temp, hum, lux, tmpw, sym);
    }
}
