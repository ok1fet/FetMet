/*////////////////////////////////////////////////////////////////////////////////////////////
   _____ _____ _____     __  __ _____ _____
  |  ___| ____|_   _|   |  \/  | ____|_   _|
  | |_  |  _|   | |_____| |\/| |  _|   | |  
  |  _| | |___  | |_____| |  | | |___  | |  
  |_|   |_____| |_|     |_|  |_|_____| |_|    Metostanice LoRa + MQTT v.M
 
*//////////////////////////////////////////////////////////////////////////////////////////////

const char* station = "OK1FET-6>APRS:!5004.91N/01431.53E_";
#define VREF              3.6527f   //nové VREF = 3.657 × (napeti 4.17 hw / napeti 4.34 mp ) ≈ 3.657 × 0.960 ≈ 3.513
#define ELEVATION         225       // výška sondy v metrech nad mořem 
const char* rstv = "000/000g000r000_RESETm";

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Debug jen když je OLED jumper otevřený (pin HIGH)
#define DebugPrint(x)      do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.print(x); } while(0)
#define DebugPrintln(x)    do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.println(x); } while(0)
#define DebugPrintf(...)   do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.printf(__VA_ARGS__); } while(0)

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== PINY ====================
#define LORA_SCK          5
#define LORA_MISO         19
#define LORA_MOSI         27
#define LORA_SS           18
#define LORA_RST          23
#define LORA_DIO0         26
#define BATTERY_PIN       35
#define OLED_ON_PIN       4   //OLED ovládání podle jumperu na pinu
#define WIND_SPEED_PIN    14
#define WIND_DIR_PIN      34
#define RAIN_PIN          13
#define GREEN_LED         25    // integrovana na LiLYGO
#define SRDCE             15

// Kalibrace srážek (do APRS setin palce)
#define RAIN_CALIB        1.323f //original hodnota 1.923f

// Převody
#define MPH_TO_MS         0.44704f
#define MS_TO_MPH         2.23694f

volatile int SpeedPulseCount = 0;

// ==================== RTC DATA ====================
RTC_DATA_ATTR int  rainBuf1h[6] = {0};
RTC_DATA_ATTR int  rainBuf1hPoradi = 0;
RTC_DATA_ATTR int  rainCount10min = 0;
RTC_DATA_ATTR int  rainSum1h = 0;
RTC_DATA_ATTR long rainBuf24h[144] = {0};
RTC_DATA_ATTR int  rainBuf24hPoradi = 0;
RTC_DATA_ATTR long rainSum24h = 0;
RTC_DATA_ATTR int  cyklus20s = 0;
RTC_DATA_ATTR float rtc_windSpeedBuf[24];   // mph
RTC_DATA_ATTR float rtc_windDirBuf[24];     // deg
RTC_DATA_ATTR float rtc_maxWindBuf = 0;     // mph
RTC_DATA_ATTR float rtc_minWindBuf = 99.9f; // mph
RTC_DATA_ATTR int  VoltageCycle = 0;
RTC_DATA_ATTR bool wasResetMsgSent = false;
RTC_DATA_ATTR unsigned long nextSleepInterval = 19500;

// ==================== WIFI & MQTT ====================
const char* ssid        = "BehalWiFi";
const char* password    = "a1a1a1a1a1";
const char* mqtt_server = "10.0.0.230";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "mqtt";
const char* mqtt_pass   = "b2b2b2b2b2";
const char* mqtt_topic  = "weather/ok1fet-6";

WiFiClient espClient;
PubSubClient client(espClient);

// ==================== OBJEKTY / STAV ====================
Adafruit_BME280 bme;
unsigned long lastPulseTime = 0;
unsigned long lastRainPulseTime = 0;
float batv = 0;

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

// ==================== POMOCNÉ FUNKCE ====================
float avgSpeedMph(float *a, int n, float *maxOut) {
  if (n <= 0) {
    if (maxOut) *maxOut = 0;
    return 0;
  }
  float s = 0;
  float mx = a[0];
  DebugPrint("avgSpeed vstupni pole (mph): ");
  for (int i = 0; i < n; i++) {
    DebugPrintf("%.2f ", a[i]);
    s += a[i];
    if (a[i] > mx) mx = a[i];
  }
  DebugPrintln("");
  float avg = s / n;
  DebugPrintf("Speed průměr: %.2f mph | max: %.2f mph\n", avg, mx);
  if (maxOut) *maxOut = mx;
  return avg;
}

String getAdcDir(int v) {
  if (v < 250)  return "270";
  if (v < 550)  return "315";
  if (v < 900)  return "360";
  if (v < 1600) return "225";
  if (v < 2200) return "045";
  if (v < 2900) return "180";
  if (v < 3400) return "135";
  if (v < 3900) return "090";
  return "000";
}

float avgDirYamartino(float* a, int n, float* stdDevOut = nullptr) {
  if (n <= 0) {
    if (stdDevOut) *stdDevOut = 0;
    return 0;
  }

  float sumSin = 0, sumCos = 0;
  for (int i = 0; i < n; i++) {
    float rad = radians(a[i]);
    sumSin += sin(rad);
    sumCos += cos(rad);
  }

  float avgSin = sumSin / n;
  float avgCos = sumCos / n;
  float avgDirDeg = degrees(atan2(avgSin, avgCos));
  if (avgDirDeg < 0) avgDirDeg += 360.0f;

  float r2 = avgSin * avgSin + avgCos * avgCos;
  if (r2 > 1.0f) r2 = 1.0f;
  float eps = sqrtf(1.0f - r2);
  if (eps > 1.0f) eps = 1.0f;
  float stdDev = degrees(asinf(eps));
  if (stdDevOut) *stdDevOut = stdDev;
  return avgDirDeg;
}

float readBat() {
  int r = analogRead(BATTERY_PIN);
  batv = (r / 4095.0f * VREF) / 0.5f;
  return batv;
}

// vrací 1h úhrn v APRS jednotkách (setiny palce)
int rainUpdateAndGet1h() {
  DebugPrintf("10min slot %d/6 = %d pulzů\n", rainBuf1hPoradi, rainCount10min);

  rainBuf1hPoradi  = (rainBuf1hPoradi + 1) % 6;
  rainBuf24hPoradi = (rainBuf24hPoradi + 1) % 144;

  int tips = (int)(rainCount10min * RAIN_CALIB + 0.5f);
  rainBuf1h[rainBuf1hPoradi] = tips;
  rainBuf24h[rainBuf24hPoradi] = tips;

  rainSum1h = 0;
  for (int i = 0; i < 6; i++) rainSum1h += rainBuf1h[i];

  rainSum24h = 0;
  for (int i = 0; i < 144; i++) rainSum24h += rainBuf24h[i];

  DebugPrintf("1h: %d | 24h: %d (APRS setiny in)\n", rainSum1h, (int)rainSum24h);
  rainCount10min = 0;
  return rainSum1h;
}

void setupLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.setTxPower(17);
  if (!LoRa.begin(433775000)) {
    DebugPrintln("LoRa init selhalo!");
    while (1) delay(1000);
  }
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  DebugPrintln("LoRa init OK");
}

// ====================== LORA TX ======================
// avgSp/maxSp/minSp_mph = mph
// _M je v m/s, _D ve stupních
void sendMsg(float avgSp_mph, float avgDr, float maxSp_mph, float minSp_mph,
             float batv, float tF, float pres_aprs, float hum,
             int rain1h_aprs, int rain24h_aprs, float dirStd) {

  int sp_mph   = constrain((int)round(avgSp_mph), 0, 999);
  int gust_mph = constrain((int)round(maxSp_mph), 0, 999);
  float min_ms = minSp_mph * MPH_TO_MS;

  char msg[180];
  char dirStr[4], spStr[4], gustStr[4];
  char tempStr[6], humStr[4], presStr[8], rainStr[5], rain24Str[5];
  char batStr[12], minStr[12], dirStdStr[12];

  snprintf(dirStr,    sizeof(dirStr),    "%03d", (int)avgDr);
  snprintf(spStr,     sizeof(spStr),     "%03d", sp_mph);
  snprintf(gustStr,   sizeof(gustStr),   "%03d", gust_mph);
  snprintf(tempStr,   sizeof(tempStr),   "t%03d", (int)tF);
  snprintf(humStr,    sizeof(humStr),    "h%02d", ((int)hum >= 100) ? 0 : (int)hum);
  snprintf(presStr,   sizeof(presStr),   "b%05d", (int)pres_aprs);
  snprintf(rainStr,   sizeof(rainStr),   "r%03d", rain1h_aprs);
  snprintf(rain24Str, sizeof(rain24Str), "p%03d", rain24h_aprs);
  snprintf(batStr,    sizeof(batStr),    "_B%.2f", batv);
  snprintf(minStr,    sizeof(minStr),    "_M%.1f", min_ms);

  int d = (int)round(dirStd);
  if (d < 0) d = 0;
  if (d > 180) d = 180;
  snprintf(dirStdStr, sizeof(dirStdStr), "_D%d", d);

  snprintf(msg, sizeof(msg),
           "%s%s/%sg%s%s%s%s%s%s%s%s%s",
           station, dirStr, spStr, gustStr,
           tempStr, humStr, presStr,
           rainStr, rain24Str,
           batStr, minStr, dirStdStr);

  DebugPrint("LoRa TX: ");
  DebugPrintln(msg);

  digitalWrite(GREEN_LED, HIGH);
  setupLoRa();
  LoRa.beginPacket();
  LoRa.write('<');
  LoRa.write(0xFF);
  LoRa.write(0x01);
  LoRa.print(msg);
  LoRa.endPacket();
  LoRa.sleep();
  digitalWrite(GREEN_LED, LOW);
}

// ====================== WIFI ======================
bool connectWiFi() {
  setCpuFrequencyMhz(80);

  DebugPrintln("Připojuji WiFi...");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
    DebugPrint(".");
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    DebugPrintln("\nWiFi OK");
    DebugPrintf("IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  DebugPrintln("\nWiFi timeout");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  setCpuFrequencyMhz(40);
  return false;
}

// ====================== MQTT ======================
// pressure_hPa = skutečný tlak v hPa (ne APRS *10)
void sendMQTT(float avgSp_mph, float avgDr, float maxSp_mph, float minSp_mph,
              float batv, float tC, float pressure_hPa, float hum,
              int rain1h_aprs, int rain24h_aprs, float dirStd) {

  if (!connectWiFi()) {
    DebugPrintln("MQTT přeskočeno – není WiFi");
    return;
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setSocketTimeout(4);

  String clientId = "FETMET-" + String(random(0xffff), HEX);
  if (!client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    DebugPrintf("MQTT connect fail, rc=%d\n", client.state());
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    setCpuFrequencyMhz(40);
    return;
  }

  DebugPrintln("MQTT připojeno");

  float avg_ms = avgSp_mph * MPH_TO_MS;
  float max_ms = maxSp_mph * MPH_TO_MS;
  float min_ms = minSp_mph * MPH_TO_MS;

  // APRS setiny palce -> mm
  float rain1h_mm  = rain1h_aprs  * 0.254f;
  float rain24h_mm = rain24h_aprs * 0.254f;

  char payload[420];
  snprintf(payload, sizeof(payload),
    "{"
    "\"temperature\":%.1f,"
    "\"humidity\":%.1f,"
    "\"pressure\":%.1f,"
    "\"wind_speed\":%.1f,"
    "\"wind_gust\":%.1f,"
    "\"wind_min\":%.1f,"
    "\"wind_dir\":%.0f,"
    "\"wind_dir_std\":%.0f,"
    "\"rain_1h\":%.1f,"
    "\"rain_24h\":%.1f,"
    "\"battery\":%.2f,"
    "\"rssi\":%d"
    "}",
    tC, hum, pressure_hPa,
    avg_ms, max_ms, min_ms,
    avgDr, dirStd,
    rain1h_mm, rain24h_mm,
    batv, WiFi.RSSI()
  );

  bool ok = client.publish(mqtt_topic, payload, true);
  DebugPrintf("MQTT %s: %s\n", ok ? "OK" : "FAIL", payload);

  client.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
  setCpuFrequencyMhz(40);
}

//=======================================================================================
void setup() {
//=======================================================================================
  setCpuFrequencyMhz(40);

  gpio_deep_sleep_hold_dis();
  rtc_gpio_pullup_dis((gpio_num_t)RAIN_PIN);
  gpio_hold_dis((gpio_num_t)RAIN_PIN);

  Serial.begin(115200);
  delay(200);

  batv = readBat();

  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(WIND_SPEED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WIND_SPEED_PIN), onWindPulse, FALLING);

  pinMode(OLED_ON_PIN, INPUT_PULLUP);
  bool oledHold = (digitalRead(OLED_ON_PIN) == LOW);

  pinMode(GREEN_LED, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  pinMode(SRDCE, OUTPUT);
  digitalWrite(SRDCE, LOW);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  switch (reason) {
    //----------------------------- POWER ON / RESET -----------------------------
    case ESP_SLEEP_WAKEUP_UNDEFINED: {
      if (!wasResetMsgSent) {
        DebugPrint("Prvni spusteni po resetu – posilam ");
        DebugPrintln(rstv);

        if (!oledHold) {
          display.ssd1306_command(SSD1306_DISPLAYON);
          display.clearDisplay();
          display.setCursor(0, 0);
          display.printf("!!!LORA TX!!!");
          display.setCursor(0, 10);
          display.printf("%s", station);
          display.setCursor(0, 30);
          display.printf("%s", rstv);
          display.setCursor(0, 50);
          display.printf("Batt: %.2f V", batv);
          display.display();
        } else {
          display.ssd1306_command(SSD1306_DISPLAYOFF);
        }

        digitalWrite(GREEN_LED, HIGH);
        setupLoRa();
        LoRa.beginPacket();
        LoRa.write('<');
        LoRa.write(0xFF);
        LoRa.write(0x01);
        LoRa.print(station);
        LoRa.print(rstv);
        LoRa.endPacket();
        LoRa.sleep();
        digitalWrite(GREEN_LED, LOW);

        wasResetMsgSent = true;

        rainCount10min = 0;
        cyklus20s = 0;
        rainSum1h = 0;
        rainBuf1hPoradi = 0;
        VoltageCycle = 0;
        rtc_maxWindBuf = 0;
        rtc_minWindBuf = 99.9f;
        for (int i = 0; i < 6; ++i) rainBuf1h[i] = 0;
        rainSum24h = 0;
        rainBuf24hPoradi = 0;
        for (int i = 0; i < 144; ++i) rainBuf24h[i] = 0;
        for (int i = 0; i < 24; ++i) {
          rtc_windSpeedBuf[i] = 0;
          rtc_windDirBuf[i] = 0;
        }
        DebugPrintln("RTC paměť a buffery byly vymazány!");
      }
      break;
    }

    //----------------------------- RAIN INTERRUPT -----------------------------
    case ESP_SLEEP_WAKEUP_EXT0: {
      // 1 tip na EXT0 wakeup (bez attachInterrupt v této větvi)
      rainCount10min++;
      DebugPrintf("RAIN INTERRUPT | rainCount10min=%d | cyklus20s=%d\n",
                  rainCount10min, cyklus20s);

      nextSleepInterval = 1000; // rychlý návrat do 20s rytmu

      if (!oledHold) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        display.clearDisplay();
        display.setCursor(0, 0);
        display.printf("!!! RAIN !!!");
        display.setCursor(0, 16);
        display.printf("Count: %d", rainCount10min);
        display.setCursor(0, 32);
        display.printf("Cyklus: %d/24", cyklus20s);
        display.display();
      }
      break;
    }

    //----------------------------- TIMER WAKEUP -----------------------------
    case ESP_SLEEP_WAKEUP_TIMER: {
      DebugPrintf("TIMER | cyklus20s=%d | rainCount=%d\n", cyklus20s, rainCount10min);

      if (oledHold) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
      }

      cyklus20s++;
      DebugPrintf("=== 20s MERENI VETRU | cyklus %d/24 ===\n", cyklus20s);

      // během měření větru počítej i déšť
      attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainInterrupt, FALLING);

      digitalWrite(SRDCE, HIGH);
      SpeedPulseCount = 0;
      float dir8x[8] = {0};
      int idx = 0;
      unsigned long start = millis();
      unsigned long lastSample = 0;

      while (millis() - start < 5000) {
        if (idx < 8 && millis() - lastSample >= 600) {
          int adcVal = analogRead(WIND_DIR_PIN);
          dir8x[idx++] = getAdcDir(adcVal).toFloat();
          lastSample = millis();
        }
      }
      digitalWrite(SRDCE, LOW);

      // impulsy za 5 s -> mph (kalibrace 2.1)
      float speed_mph = (SpeedPulseCount * 2.1f) / 5.0f;

      rtc_maxWindBuf = max(rtc_maxWindBuf, speed_mph);
      rtc_minWindBuf = min(rtc_minWindBuf, speed_mph);

      float stdDev = 0.0f;
      float dir = avgDirYamartino(dir8x, idx, &stdDev);

      int writeIdx = (cyklus20s - 1) % 24;
      rtc_windSpeedBuf[writeIdx] = speed_mph;
      rtc_windDirBuf[writeIdx]   = dir;

      if (!oledHold) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        display.clearDisplay();
        display.setCursor(0, 0);
        display.printf("Cyklus: %d/24", cyklus20s);
        display.setCursor(0, 12);
        display.printf("Pulsy: %d", SpeedPulseCount);
        display.setCursor(0, 24);
        display.printf("Rychl: %.1f m/s", speed_mph * MPH_TO_MS);
        display.setCursor(0, 36);
        display.printf("Smer: %.0f", dir);
        display.setCursor(0, 48);
        display.printf("Rain: %d", rainCount10min);
        display.display();
      }

      DebugPrintf("Cyklus %d/24 | %.1f mph (%.1f m/s) | %.0f deg | rain %d | bat %.2fV\n",
                  cyklus20s, speed_mph, speed_mph * MPH_TO_MS, dir, rainCount10min, readBat());

      // ========== AGREGACE + ODESLÁNÍ (≈10 min) ==========
      if (cyklus20s >= 24) {
        DebugPrintln("=== AGREGACE + ODESLANI DAT ===");

        bool bmeOk = bme.begin(0x76);
        if (!bmeOk) DebugPrintln("BME280 init selhalo!");

        float maxSp = rtc_maxWindBuf;
        float minSp = rtc_minWindBuf;
        if (minSp > 90.0f) minSp = 0.0f;

        float avgSp = avgSpeedMph(rtc_windSpeedBuf, 24, &maxSp);

        float dirStd = 0.0f;
        float avgDr  = avgDirYamartino(rtc_windDirBuf, 24, &dirStd);

        float tC = 0, tF = 0, pressure_hPa = 0, pres_aprs = 0, hum = 0;
        if (bmeOk) {
          tC = bme.readTemperature();
          tF = tC * 1.8f + 32.0f;
          float p_station = bme.readPressure() / 100.0f; // hPa
          pressure_hPa = p_station / pow((1.0 - ELEVATION / 44330.0), 5.255); // moře hPa
          pres_aprs = pressure_hPa * 10.0f; // APRS desetiny hPa
          hum = bme.readHumidity();
        }

        int rain1h = rainUpdateAndGet1h();
        int rain24 = (int)rainSum24h;

        DebugPrintf("avg=%.1f max=%.1f min=%.1f mph | dir=%.0f std=%.0f\n",
                    avgSp, maxSp, minSp, avgDr, dirStd);
        DebugPrintf("tC=%.1f hum=%.0f p=%.1f hPa rain1h=%d rain24h=%d\n",
                    tC, hum, pressure_hPa, rain1h, rain24);

        if (!oledHold) {
          display.ssd1306_command(SSD1306_DISPLAYON);
          display.clearDisplay();
          display.setCursor(0, 0);
          display.printf("!!! LORA TX !!!");
          display.setCursor(0, 14);
          display.printf("Dir:%.0f D:%.0f", avgDr, dirStd);
          display.setCursor(0, 28);
          display.printf("Sp: %.1f m/s", avgSp * MPH_TO_MS);
          display.setCursor(0, 42);
          display.printf("Rain: %d", rain1h);
          display.setCursor(0, 54);
          display.printf("Bat: %.2fV", batv);
          display.display();
        }

        bool doTx = true;
        if (batv < 3.3f) {
          VoltageCycle++;
          doTx = (VoltageCycle >= 3); // při nízké baterii řidčeji
        } else {
          VoltageCycle = 0;
        }

        if (doTx) {
          sendMsg(avgSp, avgDr, maxSp, minSp, batv, tF, pres_aprs, hum,
                  rain1h, rain24, dirStd);
          sendMQTT(avgSp, avgDr, maxSp, minSp, batv, tC, pressure_hPa, hum,
                   rain1h, rain24, dirStd);
        }

        // reset 10min agregace
        cyklus20s = 0;
        rtc_maxWindBuf = 0;
        rtc_minWindBuf = 99.9f;
        for (int i = 0; i < 24; i++) {
          rtc_windSpeedBuf[i] = 0;
          rtc_windDirBuf[i] = 0;
        }
      }

      detachInterrupt(digitalPinToInterrupt(RAIN_PIN));
      nextSleepInterval = 19500;
      break;
    }

    default:
      DebugPrintf("Neznamy wakeup reason: %d\n", reason);
      break;
  }

  // ==================== SLEEP ====================
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RAIN_PIN, 0);
  esp_sleep_enable_timer_wakeup(nextSleepInterval * 1000ULL);

  DebugPrintf("Jdu spat na %lu ms | rainCount=%d | cyklus20s=%d\n",
              nextSleepInterval, rainCount10min, cyklus20s);

  delay(30);
  esp_deep_sleep_start();
}

void loop() {}
