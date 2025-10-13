#include <M5Stack.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define PHOTO_PIN 36
#define LOAD_RESISTANCE 10000.0

Adafruit_BME280 bme;

void setup() {
  M5.begin();
  pinMode(PHOTO_PIN, INPUT);
  
  Wire.begin();
  delay(100);  // I2Cバス安定化待ち
  
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Initializing...");
  
  delay(500);
  
  bool status = bme.begin(0x76, &Wire);
  
  if (!status) {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("BME280 Error!");
    M5.Lcd.printf("Check: 0x77");
    while(1);
  }
  
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Sensors Ready");
  delay(2000);
}

void loop() {
  M5.update();
  
  int lightRaw = analogRead(PHOTO_PIN);
  int lightPercent = map(lightRaw, 0, 4095, 0, 100);
  
  float temp = bme.readTemperature();
  float humidity = bme.readHumidity();
  float pressure = bme.readPressure() / 100.0F;
  
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("=== Sensor Data ===");
  M5.Lcd.setCursor(10, 50);
  M5.Lcd.printf("Light: %d%%", lightPercent);
  M5.Lcd.setCursor(10, 80);
  M5.Lcd.printf("Temp: %.1fC", temp);
  M5.Lcd.setCursor(10, 110);
  M5.Lcd.printf("Humid: %.1f%%", humidity);
  M5.Lcd.setCursor(10, 140);
  M5.Lcd.printf("Press: %.1fhPa", pressure);
  
  delay(1000);
}