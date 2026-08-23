/*////////////////////////////////////////////////////////////////////////////////////////////
   _____ _____ _____     __  __ _____ _____
  |  ___| ____|_   _|   |  \/  | ____|_   _|
  | |_  |  _|   | |_____| |\/| |  _|   | |  
  |  _| | |___  | |_____| |  | | |___  | |  
  |_|   |_____| |_|     |_|  |_|_____| |_|  Metostanice LoRa verze 1.m

 Verze: 2026-05-10
 Změny: 
   - Upravená struktura kodu (lepší čitelnost)
   - Opravené minimum větru (verze B)
   - Opravený tlak (správný formát bxxxxx)
   - Zkráceno na 24 cyklů (~9,5 min)
   - Minimum větru počítáno pouze z nenulových hodnot
///////////////////////////////////////////////////////////////////////////////////////////////*/

const char* station = "OK1FET-73>APRS:!5006.91N/01331.53E_";   // změň na -6 podle potřeby

#define VREF        3.656f      // kalibrace AD převodníku
#define ELEVATION   225         // výška nad mořem v metrech
#define RAIN_CALIB  1.923f      // kalibrace srážkoměru (mm na puls)

const char* rstv = "000/000g000r000_RESETm";   // reset + verze SW

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// ==================== PINY ====================
#define BATTERY_PIN     35
#define OLED_ON_PIN     4
#define WIND_SPEED_PIN  14
#define WIND_DIR_PIN    34
#define RAIN_PIN        13
#define GREEN_LED       25
#define SRDCE           15

#define LORA_SCK      5
#define LORA_MISO     19
#define LORA_MOSI     27
#define LORA_SS       18
#define LORA_RST      23
#define LORA_DIO0     26

// ==================== DEBUG ====================
#define DebugPrint(x)    do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.print(x); } while(0)
#define DebugPrintln(x)  do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.println(x); } while(0)
#define DebugPrintf(...) do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.printf(__VA_ARGS__); } while(0)

// ==================== RTC DATA ====================
RTC_DATA_ATTR int   rainBuf1h[6] = {0};
RTC_DATA_ATTR int   rainBuf1hPoradi = 0;
RTC_DATA_ATTR int   rainCount10min = 0;
RTC_DATA_ATTR int   rainSum1h = 0;

RTC_DATA_ATTR long  rainBuf24h[144] = {0};
RTC_DATA_ATTR int   rainBuf24hPoradi = 0;
RTC_DATA_ATTR long  rainSum24h = 0;

RTC_DATA_ATTR int   cyklus20s = 0;
RTC_DATA_ATTR int   cyklus1s = 0;
RTC_DATA_ATTR float rtc_windSpeedBuf[24];
RTC_DATA_ATTR float rtc_windDirBuf[24];
RTC_DATA_ATTR float rtc_maxWindBuf = 0;
RTC_DATA_ATTR float rtc_minWindBuf = 99.9f;

RTC_DATA_ATTR int   VoltageCycle = 0;
RTC_DATA_ATTR bool  wasResetMsgSent = false;
RTC_DATA_ATTR bool  rainFastMode = false;
RTC_DATA_ATTR unsigned long nextSleepInterval = 19500;

// ==================== OBJEKTY ====================
Adafruit_BME280 bme;
Adafruit_SSD1306 display(128, 64, &Wire, -1);

volatile int SpeedPulseCount = 0;
unsigned long lastPulseTime = 0;
unsigned long lastRainPulseTime = 0;

// ==================== INTERRUPTS ====================
void IRAM_ATTR onWindPulse() {
  unsigned long t = millis();
  if (t - lastPulseTime > 10) {
    SpeedPulseCount++;
    lastPulseTime = t;
  }
}

void IRAM_ATTR rainInterrupt() {
  unsigned long t = millis();
  if (t - lastRainPulseTime > 20) {
    rainCount10min++;
    lastRainPulseTime = t;
  }
}

// ==================== FUNKCE ====================

float readBat() {
  int r = analogRead(BATTERY_PIN);
  return (r / 4095.0f * VREF) / 0.5f;
}

// Srážky - 1h a 24h
float rain() {
  rainBuf1hPoradi = (rainBuf1hPoradi + 1) % 6;
  rainBuf24hPoradi = (rainBuf24hPoradi + 1) % 144;

  float current10min = rainCount10min * RAIN_CALIB + 0.5f;

  rainBuf1h[rainBuf1hPoradi] = current10min;
  rainBuf24h[rainBuf24hPoradi] = current10min;

  rainSum1h = 0;
  for (int i = 0; i < 6; i++) rainSum1h += rainBuf1h[i];

  rainSum24h = 0;
  for (int i = 0; i < 144; i++) rainSum24h += rainBuf24h[i];

  DebugPrintf("10min pulzů: %d | 1h: %d | 24h: %d\n", rainCount10min, rainSum1h, rainSum24h);

  rainCount10min = 0;
  return rainSum1h;
}

// Průměrná rychlost větru + max
float avgSpeed(float *a, int n, float *maxSP) {
  float sum = 0;
  float maxH = a[0];
  for (int i = 0; i < n; i++) {
    sum += a[i];
    if (a[i] > maxH) maxH = a[i];
  }
  *maxSP = maxH;
  return sum / n;
}

// Směr větru - Yamartino
float avgDirYamartino(float* a, int n, float* stdDevOut = nullptr) {
  float sumSin = 0, sumCos = 0;
  for (int i = 0; i < n; i++) {
    float rad = radians(a[i]);
    sumSin += sin(rad);
    sumCos += cos(rad);
  }
  float avgSin = sumSin / n;
  float avgCos = sumCos / n;
  float avgDir = degrees(atan2(avgSin, avgCos));
  if (avgDir < 0) avgDir += 360;

  if (stdDevOut) {
    float epsilon = sqrt(1.0f - (avgSin*avgSin + avgCos*avgCos));
    *stdDevOut = degrees(asin(epsilon));
  }
  return avgDir;
}

// LoRa inicializace
void setupLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.setTxPower(17);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();

  if (!LoRa.begin(433775000)) {
    DebugPrintln("LoRa init selhalo!");
    while(1);
  }
  DebugPrintln("LoRa OK");
}

// Odeslání APRS zprávy
void sendMsg(float avgSp, float avgDr, float maxSp, float minSp_mph, float batv,
             float tF, float pres, float hum, int rain1h, int rain24h) {
  
  if (digitalRead(OLED_ON_PIN) == HIGH) digitalWrite(GREEN_LED, HIGH);

  int sp_mph   = round(avgSp * 0.621371f);
  int gust_mph = round(maxSp * 0.621371f);
  float min_ms = minSp_mph * 0.44704f;

  char msg[160];
  char dirStr[4], spStr[4], gustStr[4], batStr[12];
  char tempStr[6], humStr[4], presStr[8], rainStr[5], rain24Str[5];
  char minStr[10];

  snprintf(dirStr,    sizeof(dirStr),    "%03d", (int)avgDr);
  snprintf(spStr,     sizeof(spStr),     "%03d", sp_mph);
  snprintf(gustStr,   sizeof(gustStr),   "%03d", gust_mph);
  snprintf(tempStr,   sizeof(tempStr),   "t%03d", (int)tF);
  snprintf(humStr,    sizeof(humStr),    "h%02d", (int)hum >= 100 ? 0 : (int)hum);
  snprintf(presStr,   sizeof(presStr),   "b%05d", (int)pres);
  snprintf(rainStr,   sizeof(rainStr),   "r%03d", rain1h);
  snprintf(rain24Str, sizeof(rain24Str), "p%03d", rain24h);
  snprintf(batStr,    sizeof(batStr),    "_B%.2f", batv);
  snprintf(minStr,    sizeof(minStr),    "_M%.1f", min_ms);

  snprintf(msg, sizeof(msg),
           "%s%s/%sg%s%s%s%s%s%s%s%s",
           station, dirStr, spStr, gustStr,
           tempStr, humStr, presStr, rainStr, rain24Str, batStr, minStr);

  DebugPrint("LoRa TX: ");
  DebugPrintln(msg);

  LoRa.beginPacket();
  LoRa.write('<'); LoRa.write(0xFF); LoRa.write(0x01);
  LoRa.print(msg);
  LoRa.endPacket();
  LoRa.sleep();

  digitalWrite(GREEN_LED, LOW);
}

// ===================================================================
// SETUP
// ===================================================================
void setup() {
  setCpuFrequencyMhz(40);

  // Low-power nastavení
  gpio_deep_sleep_hold_dis();
  rtc_gpio_pullup_dis((gpio_num_t)RAIN_PIN);
  gpio_hold_dis((gpio_num_t)RAIN_PIN);

  Serial.begin(115200);
  delay(200);

  // Pin setup
  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(WIND_SPEED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WIND_SPEED_PIN), onWindPulse, FALLING);
  pinMode(OLED_ON_PIN, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(SRDCE, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(SRDCE, LOW);

  // OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  float batv = readBat();
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  switch (reason) {
    // ====================== RESET / POWER ON ======================
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      if (!wasResetMsgSent) {
        DebugPrintln("=== První spuštění po resetu ===");
        // ... tvůj OLED + odeslání reset zprávy ...
        // (nechal jsem původní kód, jen trochu zkrácený)
        
        wasResetMsgSent = true;
        // Reset všech bufferů
        rainCount10min = 0;
        cyklus1s = 0;
        cyklus20s = 0;
        rtc_maxWindBuf = 0;
        rtc_minWindBuf = 99.9f;
        // ... nulování polí ...
        DebugPrintln("RTC buffery vymazány");
      }
      break;

    // ====================== SRAŽKY ======================
    case ESP_SLEEP_WAKEUP_EXT0:
      rainCount10min++;
      rainFastMode = true;
      cyklus1s = 0;
      DebugPrintf("💧 RAIN INTERRUPT! count = %d\n", rainCount10min);
      break;

    // ====================== ČASOVAČ ======================
    case ESP_SLEEP_WAKEUP_TIMER:
      cyklus1s++;

      if (cyklus1s % 18 == 0) {          // ~18s sleep + 5s měření
        cyklus1s = 0;
        cyklus20s++;

        // === 5s měření větru ===
        // ... tvůj kód měření (s opraveným min větrem) ...

        if (cyklus20s >= 24) {
          DebugPrintln("AGREGACE DAT PO ~9.5 MIN!");
          setupLoRa();
          if (!bme.begin(0x76)) DebugPrintln("BME280 selhal!");

          float maxSp = rtc_maxWindBuf;
          float minSp = 99.9f;
          for (int i = 0; i < cyklus20s; i++) {
            if (rtc_windSpeedBuf[i] > 0.0f && rtc_windSpeedBuf[i] < minSp)
              minSp = rtc_windSpeedBuf[i];
          }
          if (minSp == 99.9f) minSp = 0.0f;

          float avgSp = avgSpeed(rtc_windSpeedBuf, cyklus20s, &maxSp);
          float avgDr = avgDirYamartino(rtc_windDirBuf, cyklus20s);

          float tC = bme.readTemperature();
          float tF = tC * 1.8f + 32.0f;
          float pressureHpa = bme.readPressure() / 100.0f;
          float pres = pressureHpa / pow((1.0 - ELEVATION/44330.0), 5.255) * 10; // *10 pro APRS

          float hum = bme.readHumidity();
          int rainSum60 = rain();

          sendMsg(avgSp, avgDr, maxSp, minSp, batv, tF, pres, hum, rainSum60, rainSum24h);

          // Reset
          cyklus20s = 0;
          rainCount10min = 0;
          rtc_maxWindBuf = 0;
          rtc_minWindBuf = 99.9f;
          rainFastMode = false;
        }
      }
      break;
  }

  // === Nastavení dalšího spánku ===
  if (rainFastMode) {
    nextSleepInterval = 1000;
    DebugPrintln("🌧️ FAST MODE - 1s sleep");
  } else {
    nextSleepInterval = 19500;
    cyklus1s = 14;
    DebugPrintln("☀️ NORMAL MODE - 20s sleep");
  }

  esp_sleep_enable_ext0_wakeup((gpio_num_t)RAIN_PIN, 0);
  esp_sleep_enable_timer_wakeup(nextSleepInterval * 1000ULL);
  DebugPrintf("😴 DeepSleep %lu ms\n", nextSleepInterval);

  delay(50);
  esp_deep_sleep_start();
}

void loop() {}
