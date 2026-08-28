/*////////////////////////////////////////////////////////////////////////////////////////////
   _____ _____ _____     __  __ _____ _____
  |  ___| ____|_   _|   |  \/  | ____|_   _|
  | |_  |  _|   | |_____| |\/| |  _|   | |  
  |  _| | |___  | |_____| |  | | |___  | |  
  |_|   |_____| |_|     |_|  |_|_____| |_|  Metostanice LoRa verze M
*/
/////////////////////////////////////////////////////////////////////////////////////////////
const char* station = "OK1FET-73>APRS:!5006.91N/01436.53E_";
#define VREF              3.6527f
#define ELEVATION 225
const char* rstv = "000/000g000r000_RESETm";

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

// --- Debug výpisy podle OLED_ON_PIN ---
#define DebugPrint(x)      do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.print(x); } while(0)
#define DebugPrintln(x)    do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.println(x); } while(0)
#define DebugPrintf(...)   do { if (digitalRead(OLED_ON_PIN)==HIGH) Serial.printf(__VA_ARGS__); } while(0)

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledActive = false;

// ==================== PINY ====================
#define LORA_SCK      5
#define LORA_MISO     19
#define LORA_MOSI     27
#define LORA_SS       18
#define LORA_RST      23
#define LORA_DIO0     26
#define BATTERY_PIN       35
#define OLED_ON_PIN        4
#define WIND_SPEED_PIN    14
#define WIND_DIR_PIN      34
#define RAIN_PIN          13
#define GREEN_LED         25
#define SRDCE             15
#define RAIN_CALIB 1.923f

volatile int SpeedPulseCount = 0;

// ==================== RTC DATA ====================
RTC_DATA_ATTR int rainBuf1h[6] = {0};
RTC_DATA_ATTR int rainBuf1hPoradi = 0;
RTC_DATA_ATTR int rainCount10min = 0;
RTC_DATA_ATTR int rainSum1h = 0;
RTC_DATA_ATTR long rainBuf24h[144] = {0};
RTC_DATA_ATTR int rainBuf24hPoradi = 0;
RTC_DATA_ATTR long rainSum24h = 0;
RTC_DATA_ATTR int cyklus20s = 0;
RTC_DATA_ATTR int cyklus1s = 0;
RTC_DATA_ATTR float rtc_windSpeedBuf[24];
RTC_DATA_ATTR float rtc_windDirBuf[24];
RTC_DATA_ATTR float rtc_maxWindBuf = 0;
RTC_DATA_ATTR float rtc_minWindBuf = 99.9f;
RTC_DATA_ATTR int VoltageCycle = 0;
RTC_DATA_ATTR bool wasResetMsgSent = false;
RTC_DATA_ATTR unsigned long nextSleepInterval = 19500;

// ==================== WIFI & MQTT ====================
#include <WiFi.h>
#include <PubSubClient.h>
const char* ssid = "TVOJEWiFI";
const char* password = "HESLO";
const char* mqtt_server = "10.0.0.230";
const int   mqtt_port = 1883;
const char* mqtt_user = "UZIVATEL";
const char* mqtt_pass = "HESLO";
WiFiClient espClient;
PubSubClient client(espClient);
String mqtt_topic = "weather/ok1fet-73";

// ==================== OBJEKTY ====================
Adafruit_BME280 bme;
unsigned long lastPulseTime = 0;
unsigned long lastRainPulseTime = 0;
float wSpeed = 0.0;
float wDir = 0.0;
float batv;
bool LowBatt = true;

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
float avgSpeed(float *a, int n, float *maxSP) {
  float s = 0;
  float maxHodnota = a[0];
  DebugPrint("avgSpeed vstupni pole: ");
  for (int i = 0; i < n; i++) {
    DebugPrintf("%.2f ", a[i]);
    s += a[i];
    if (a[i] > maxHodnota) maxHodnota = a[i];
  }
  DebugPrintln("");
  float prumer = s / n;
  DebugPrintf("Speed průměr: %.2f | Speed max: %.2f\n", prumer, maxHodnota);
  *maxSP = maxHodnota;
  return prumer;
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
  float sumSin = 0, sumCos = 0;
  for (int i = 0; i < n; i++) {
    float rad = radians(a[i]);
    sumSin += sin(rad);
    sumCos += cos(rad);
  }
  float avgSin = sumSin / n;
  float avgCos = sumCos / n;
  float avgDirRad = atan2(avgSin, avgCos);
  float avgDirDeg = degrees(avgDirRad);
  if (avgDirDeg < 0) avgDirDeg += 360;

  float epsilon = sqrt(1.0 - (avgSin * avgSin + avgCos * avgCos));
  float stdDev = degrees(asin(epsilon));
  if (stdDevOut) *stdDevOut = stdDev;

  return avgDirDeg;
}

float readBat() {
  int r = analogRead(BATTERY_PIN);
  batv = (r / 4095.0f * VREF) / 0.5f;
  if (batv < 3.3f) LowBatt = true;
  return batv;
}

float rain() {
  DebugPrintf("10min slot %d/6 = %d pulzů\n", rainBuf1hPoradi, rainCount10min);
  rainBuf1hPoradi = (rainBuf1hPoradi + 1) % 6;
  rainBuf24hPoradi = (rainBuf24hPoradi + 1) % 144;
  rainBuf1h[rainBuf1hPoradi] = (rainCount10min * RAIN_CALIB + 0.5f);
  rainBuf24h[rainBuf24hPoradi] = (rainCount10min * RAIN_CALIB + 0.5f);

  rainSum1h = 0;
  for (int i = 0; i < 6; i++) rainSum1h += rainBuf1h[i];
  rainSum24h = 0;
  for (int i = 0; i < 144; i++) rainSum24h += rainBuf24h[i];

  DebugPrintf("1h: %d | 24h: %d\n", rainSum1h, rainSum24h);
  rainCount10min = 0;
  return rainSum1h;
}

void setupLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.setTxPower(17);
  if (!LoRa.begin(433775000)) {
    DebugPrintln("LoRa init selhalo!");
    while (1);
  }
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  DebugPrintln("LoRa init OK");
}

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
           station, dirStr, spStr, gustStr, tempStr, humStr, presStr,
           rainStr, rain24Str, batStr, minStr);

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
  DebugPrintln("Připojuji WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {  // max 8 sekund
    delay(200);
    DebugPrint(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    DebugPrintln("\nWiFi připojeno!");
    DebugPrintf("IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  } else {
    DebugPrintln("\nWiFi se nepodařilo připojit (timeout)");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }
}

// ====================== MQTT ======================
bool reconnectMQTT() {
  if (!client.connected()) {
    DebugPrint("Připojuji MQTT...");
    String clientId = "ESP32-Weather-" + String(random(0xffff), HEX);
    
    // Krátký timeout – nebudeme viset
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      DebugPrintln("OK");
      return true;
    } else {
      DebugPrintf("selhalo, rc=%d\n", client.state());
      return false;
    }
  }
  return true;
}

// ====================== ODESLÁNÍ DO HA ======================
void sendMQTT(float avgSp_mph, float avgDr, float maxSp_mph, float minSp_mph,
              float batv, float tC, float pres_hPa, float hum) {
  
  // Nejdřív zkusíme WiFi
  if (!connectWiFi()) {
    DebugPrintln("MQTT přeskočeno – není WiFi");
    return;   // ← důležité: nepokračujeme dál
  }

  if (!reconnectMQTT()) {
    DebugPrintln("MQTT se nepodařilo připojit");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  float avgSp_ms = avgSp_mph * 0.44704f;
  float maxSp_ms = maxSp_mph * 0.44704f;
  float minSp_ms = minSp_mph * 0.44704f;

  char payload[320];
  snprintf(payload, sizeof(payload),
    "{\"temperature\":%.1f,\"humidity\":%.1f,\"pressure\":%.1f,"
    "\"wind_speed\":%.1f,\"wind_gust\":%.1f,\"wind_min\":%.1f,"
    "\"wind_dir\":%.0f,\"battery\":%.2f,\"rssi\":%d}",
    tC, hum, pres_hPa, avgSp_ms, maxSp_ms, minSp_ms, avgDr, batv, WiFi.RSSI());

  bool success = client.publish(mqtt_topic.c_str(), payload, true);
  DebugPrintf("MQTT %s: %s\n", success ? "OK" : "CHYBA", payload);

  // Po odeslání WiFi vypneme (šetříme energii)
  client.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

//=======================================================================================
void setup() {
//=======================================================================================
  setCpuFrequencyMhz(40);

  // FIX nízké spotřeby pro RAIN_PIN
  gpio_deep_sleep_hold_dis();
  rtc_gpio_pullup_dis((gpio_num_t)RAIN_PIN);
  gpio_hold_dis((gpio_num_t)RAIN_PIN);

  Serial.begin(115200);
  client.setServer(mqtt_server, mqtt_port);
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

        if (oledHold) {
          display.clearDisplay();
          display.display();
          display.ssd1306_command(SSD1306_DISPLAYOFF);
          oledActive = false;
        } else {
          display.ssd1306_command(SSD1306_DISPLAYON);
          display.clearDisplay();
          display.setCursor(0, 0);
          display.printf("!!!LORA TX!!!");
          display.setCursor(0, 10);
          display.printf(station);
          display.setCursor(0, 30);
          display.printf(rstv);
          display.setCursor(0, 50);
          display.printf("Batt: %.2f V", batv);
          display.display();
          oledActive = true;
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

        // Reset všech bufferů
        rainCount10min = 0;
        cyklus1s = 0;
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
      rainCount10min++;
      DebugPrintf("RAIN INTERRUPT | rainCount10min = %d | cyklus20s = %d\n", 
                  rainCount10min, cyklus20s);

      // Při dešti jdeme spát jen na krátkou dobu (1s),
      // aby se co nejdřív vrátil normální 20s rytmus
      nextSleepInterval = 1000;

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
    //----------------------------- TIMER WAKEUP -----------------------------
    case ESP_SLEEP_WAKEUP_TIMER: {
      DebugPrintf("TIMER | cyklus20s=%d | rainCount=%d\n", cyklus20s, rainCount10min);

      if (oledHold) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        oledActive = false;
      }

      // ===== KAŽDÝ TIMER = jedno 20s měření větru =====
      cyklus20s++;

      DebugPrintf("=== 20s MERENI VETRU | cyklus %d/24 ===\n", cyklus20s);

      // Povolíme počítání deště i během měření větru
      attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainInterrupt, FALLING);

      digitalWrite(SRDCE, HIGH);          // heartbeat pro externí watchdog
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

      float wSpeed = (SpeedPulseCount * 2.1f) / 5.0f;
      rtc_maxWindBuf = max(rtc_maxWindBuf, wSpeed);
      rtc_minWindBuf = min(rtc_minWindBuf, wSpeed);

      float stdDev = 0.0;
      float wDir = avgDirYamartino(dir8x, idx, &stdDev);

      int writeIdx = (cyklus20s - 1) % 24;
      rtc_windSpeedBuf[writeIdx] = wSpeed;
      rtc_windDirBuf[writeIdx]   = wDir;

      if (!oledHold) {
        display.ssd1306_command(SSD1306_DISPLAYON);
        display.clearDisplay();
        display.setCursor(0, 0);
        display.printf("Cyklus: %d/24", cyklus20s);
        display.setCursor(0, 12);
        display.printf("Pulsy: %d", SpeedPulseCount);
        display.setCursor(0, 24);
        display.printf("Rychl: %.1f m/s", wSpeed * 0.44704f);
        display.setCursor(0, 36);
        display.printf("Smer: %.0f", wDir);
        display.setCursor(0, 48);
        display.printf("Rain: %d", rainCount10min);
        display.display();
      }

      DebugPrintf("Cyklus %d/24 | %.1f m/s | %.0f° | rain %d | bat %.2fV\n",
                  cyklus20s, wSpeed * 0.44704f, wDir, rainCount10min, readBat());

      // ========== AGREGACE + ODESLÁNÍ PO 24 CYKLECH (≈10 min) ==========
      if (cyklus20s >= 24) {
        DebugPrintln("=== AGREGACE + ODESLANI DAT ===");

        if (!bme.begin(0x76)) {
          DebugPrintln("BME280 init selhalo!");
        }

        float maxSp = rtc_maxWindBuf;
        float minSp = 99.9f;
        for (int i = 0; i < 24; i++) {
          if (rtc_windSpeedBuf[i] > 0.01f && rtc_windSpeedBuf[i] < minSp) {
            minSp = rtc_windSpeedBuf[i];
          }
        }
        if (minSp > 90.0f) minSp = 0.0f;

        float avgSp = avgSpeed(rtc_windSpeedBuf, 24, &maxSp);
        float avgDr = avgDirYamartino(rtc_windDirBuf, 24);

        float tC = bme.readTemperature();
        float tF = tC * 1.8f + 32.0f;
        float pressure_hPa = bme.readPressure() / 100.0f;
        float pres = pressure_hPa / pow((1.0 - ELEVATION / 44330.0), 5.255) * 10;
        float hum = bme.readHumidity();
        int rainSum60 = rain();

        DebugPrintf("avgSp=%.1f max=%.1f min=%.1f dir=%.0f\n", avgSp, maxSp, minSp, avgDr);
        DebugPrintf("tC=%.1f hum=%.0f pres=%.0f rain1h=%d\n", tC, hum, pres, rainSum60);

        if (!oledHold) {
          display.ssd1306_command(SSD1306_DISPLAYON);
          display.clearDisplay();
          display.setCursor(0, 0);
          display.printf("!!! LORA TX !!!");
          display.setCursor(0, 14);
          display.printf("Dir: %.0f", avgDr);
          display.setCursor(0, 28);
          display.printf("Sp: %.1f m/s", avgSp * 0.44704f);
          display.setCursor(0, 42);
          display.printf("Rain: %d", rainSum60);
          display.setCursor(0, 54);
          display.printf("Bat: %.2fV", batv);
          display.display();
        }

        if (batv < 3.3f) {
          VoltageCycle++;
          if (VoltageCycle >= 3) {
            
            sendMsg(avgSp, avgDr, maxSp, minSp, batv, tF, pres, hum, rainSum60, rainSum24h);
           /// sendMQTT(avgSp, avgDr, maxSp, minSp, batv, tC, pres, hum);
          }
        } else {
          VoltageCycle = 0;
          sendMsg(avgSp, avgDr, maxSp, minSp, batv, tF, pres, hum, rainSum60, rainSum24h);
          ///sendMQTT(avgSp, avgDr, maxSp, minSp, batv, tC, pres, hum);
        }

        // Reset po odeslání
        cyklus20s = 0;
        rainCount10min = 0;
        rtc_maxWindBuf = 0;
        rtc_minWindBuf = 99.9f;
        for (int i = 0; i < 24; i++) {
          rtc_windSpeedBuf[i] = 0;
          rtc_windDirBuf[i] = 0;
        }
      }

      detachInterrupt(digitalPinToInterrupt(RAIN_PIN));

      // Po normálním měření vždy dlouhý sleep
      nextSleepInterval = 19500;
      break;
    }

    default:
      DebugPrintf("Neznamy wakeup reason: %d\n", reason);
      break;
  }

  // ==================== NASTAVENÍ SLEEP ====================
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RAIN_PIN, 0);   // LOW = aktivní
  esp_sleep_enable_timer_wakeup(nextSleepInterval * 1000ULL);

  DebugPrintf("Jdu spat na %lu ms | rainCount=%d | cyklus20s=%d\n",
              nextSleepInterval, rainCount10min, cyklus20s);

  delay(30);
  esp_deep_sleep_start();
}

void loop() {}
