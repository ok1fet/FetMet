 /*////////////////////////////////////////////////////////////////////////////////////////////
   _____ _____ _____     __  __ _____ _____
  |  ___| ____|_   _|   |  \/  | ____|_   _|
  | |_  |  _|   | |_____| |\/| |  _|   | |  
  |  _| | |___  | |_____| |  | | |___  | |  
  |_|   |_____| |_|     |_|  |_|_____| |_|  Metostanice LoRa verze 1.j
*/
/////////////////////////////////////////////////////////////////////////////////////////////
const char* station = "OK1FET-99>APRS:!5004.91N/01431.53E_";  // vypocet loc je v poznamkach
#define VREF              3.657f // kalibrace AD prevodniku VREF = 3.657 × (4.17 hw / 4.34 skutecna hodnoty ) ≈ 3.657 × 0.960 ≈ 3.513
#define ELEVATION 225            // výška sondy v metrech nad mořem
const char* rstv = "000/000g000r000_RESETj";     // identifikace resetu sondy + verse SW
/////////////////////////////////////////////////////////////////////////////////////////////
// const char* rstv = "000/000g000r000_RESETj";     // identifikace resetu sondy + verse SW
/////////////////////////////////////////////////////////////////////////////////////////////

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

#define LORA_SCK      5
#define LORA_MISO     19
#define LORA_MOSI     27
#define LORA_SS       18
#define LORA_RST      23
#define LORA_DIO0     26

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledActive = false;

#define BATTERY_PIN       35      // Integrovany predradnik
#define OLED_ON_PIN        4      // Oled ON / OFF
#define WIND_SPEED_PIN    14      // wind speed interapt
#define WIND_DIR_PIN      34      // 10K odpor direction
#define RAIN_PIN          13      // srazkomer RTC GPIO 12
#define GREEN_LED         25      // integrovana led
#define SRDCE             15	    // pin pro pripojeni externiho resetu	

// --- TESTOVACÍ BLOK PRO RYCHLÉ LADĚNÍ ---
#define TEST_CYKLUS20S 13
#define TEST_INTERVAL_MS 500  // 0.5 s místo 20s
// --- TESTOVACÍ BLOK PRO RYCHLÉ LADĚNÍ ---

volatile int SpeedPulseCount = 0;

RTC_DATA_ATTR int rainBuf60[12] = {0};  // kruhovy buffer definici od nuly
RTC_DATA_ATTR int rainBuf60Poradi = 0;  // index v bufferu
RTC_DATA_ATTR int rainCount5min = 0;    // sčítá pulsy během 5min intervalu
RTC_DATA_ATTR int rainSum60 = 0;        // index v bufferu
RTC_DATA_ATTR int rainBuf24h[24] = {0};  // kruhový buffer pro 24 hodin
RTC_DATA_ATTR int rainBuf24hPoradi = 0;  // index pro zápis hodinového úhrnu
RTC_DATA_ATTR int rainSum24 = 0;         // součet za 24h
RTC_DATA_ATTR int callCount = 0;
RTC_DATA_ATTR int rainCallCount = 0;
RTC_DATA_ATTR int cyklus20s = 0;
RTC_DATA_ATTR int cyklus1s = 0;
RTC_DATA_ATTR float rtc_windSpeedBuf[13];
RTC_DATA_ATTR float rtc_windDirBuf[13];
RTC_DATA_ATTR float rtc_maxWindBuf = 0;

RTC_DATA_ATTR bool wasResetMsgSent = false;// idetifikace reset nebo prvnimu spusteni

Adafruit_BME280 bme;

const unsigned long sleepInterval = 1000;//1000=1s
unsigned long lastPulseTime = 0;
unsigned long lastRainPulseTime = 0;
float wSpeed = 0.0;  // aktuální rychlost větru v mp/h
float wDir = 0.0;    // aktuální směr větru ve stupních

//pocitani prerusen pro aneometr s 10ms SW osetrenim zakmitavani kontaktu 10ms
void IRAM_ATTR onWindPulse() {
  unsigned long t = millis();
  if (t - lastPulseTime > 10) {
    SpeedPulseCount++;
    lastPulseTime = t;
  }
}
// preruseni ze srazkomeru  
void IRAM_ATTR rainInterrupt() {
  unsigned long t = millis();
  if (t - lastRainPulseTime > 20) {// SW osetrenim zakmitavani kontaktu 20ms
    rainCount5min++;
    lastRainPulseTime = t;
  }
}
// vypocet prumerna hodnota rychlosti vetru
float avgSpeed(float *a, int n, float *maxSP) {
  float s = 0;
  float maxHodnota = a[0]; 
  DebugPrint("avgSpeed vstupni pole: ");
  for (int i = 0; i < n; i++) {
    DebugPrintf("%.2f ", a[i]);
    s += a[i];
    if (a[i] > maxHodnota) {
      maxHodnota = a[i];
    }
  }
  DebugPrintln("");
  float prumer = s / n;
  DebugPrintf("Speed průměr: %.2f | Speed max: %.2f\n", prumer, maxHodnota);
  *maxSP = maxHodnota;
  return prumer;
}

// prepocet ADC na °
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
// Výpočet průměrného směru větru pomocí Yamartino algoritmu
float avgDirYamartino(float* a, int n, float* stdDevOut = nullptr) {
  float sumSin = 0, sumCos = 0;

  DebugPrint("avgDirYamartino vstupni pole: ");
  for (int i = 0; i < n; i++) {
    DebugPrintf("%.2f ", a[i]);
  }
  DebugPrintln("");

  for (int i = 0; i < n; i++) {
    float rad = radians(a[i]);
    float s = sin(rad);
    float c = cos(rad);
    sumSin += s;
    sumCos += c;
    DebugPrintf("Mereni %d: deg=%.2f rad=%.2f cos=%.4f sin=%.4f | sumCos=%.4f sumSin=%.4f\n",
                i, a[i], rad, c, s, sumCos, sumSin);
  }

  float avgSin = sumSin / n;
  float avgCos = sumCos / n;

  float avgDirRad = atan2(avgSin, avgCos);
  float avgDirDeg = degrees(avgDirRad);
  if (avgDirDeg < 0) avgDirDeg += 360;

  float epsilon = sqrt(1.0 - (avgSin * avgSin + avgCos * avgCos));
  float stdDev = asin(epsilon);
  stdDev = degrees(stdDev);

  if (stdDevOut) *stdDevOut = stdDev;

  DebugPrintf("Souhrn: avgSin=%.4f avgCos=%.4f -> atan2=%.4f rad = %.2f deg\n", 
              avgSin, avgCos, avgDirRad, avgDirDeg);
  DebugPrintf("Yamartino odchylka: epsilon=%.4f asin=%.4f rad = %.2f deg\n", 
              epsilon, asin(epsilon), stdDev);
  DebugPrintf("Yamartino průměr: %.2f°, odchylka: %.2f°\n", avgDirDeg, stdDev);

  return avgDirDeg;
}
// mereni napeti baterie
float readBat() {
  int r = analogRead(BATTERY_PIN);
  return (r / 4095.0f * VREF) / 0.5f;
}
// Mereni srazek s hodinovym kruhovym buferem
// Mereni srazek s hodinovym a 24hodinovym kruhovym bufferem
// 💧 Měření srážek – 5min, 1h a 24h agregace
float rain() {
  // --- 5min slot ---
  rainBuf60[rainBuf60Poradi] = rainCount5min;
  DebugPrintf("💧 5min srážkový slot %d/12 = %d pulzů\n", rainBuf60Poradi, rainCount5min);

  rainBuf60Poradi = (rainBuf60Poradi + 1) % 12;
  rainCount5min = 0;

  // --- Hodinový součet ---
  int sumHour = 0;
  for (int i = 0; i < 12; i++) sumHour += rainBuf60[i];
  rainSum60 = sumHour;
  DebugPrintf("🌦️ Hodinový součet: %d pulzů = r%03d\n", rainSum60, rainSum60);

  // --- Každou hodinu ulož do 24h bufferu ---
  rainCallCount++;
  bool savedThisCall = false;
  if (rainCallCount >= 12) {
    // uložíme právě uzavřenou hodinu do bufferu
    rainBuf24h[rainBuf24hPoradi] = rainSum60;
    // DEBUG: vypis indexu kam zapisujeme (ukazujeme index před posunem)
    DebugPrintf("🕐 Uloženo do 24h bufferu [%d]: %d pulzů\n", rainBuf24hPoradi, rainSum60);

    rainBuf24hPoradi = (rainBuf24hPoradi + 1) % 24;
    rainCallCount = 0;
    savedThisCall = true;
  }

  // --- Výpočet 24h součtu (rolling) ---
  int sum24 = 0;
  for (int i = 0; i < 24; i++) sum24 += rainBuf24h[i];

  // přidej současnou (neuzavřenou) hodinu vždy — to dává "průběžný" 24h součet
  // Pozn.: i pokud jsme právě uložili hodinu do bufferu, ta hodina už je v bufferu,
  // takže přičtení rainSum60 zde bude znamenat, že se daná hodina započítá dvakrát.
  // Řešení: pokud jsme právě uložili (savedThisCall==true), **nesmíme** přičítat rainSum60 znovu.
  if (!savedThisCall) {
    sum24 += rainSum60;
  }

  rainSum24 = sum24;
  DebugPrintf("📆 24h součet (rolling): %d pulzů = p%03d\n", rainSum24, rainSum24);

  // Další debug — ukázat rainCallCount a poradi bufferu
  DebugPrintf("🔁 rainCallCount=%d, rainBuf24hPoradi=%d\n", rainCallCount, rainBuf24hPoradi);

  return rainSum60;
}

// Nastaveni LoRa protokolu
void setupLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.setTxPower(17);  // nebo 20 17 14 dBm
  // Aktivace PA_BOOST a High Power Mode (20 dBm)
  // LoRa.setTxPower(20, true);  // true = PA_BOOST 42dBm

  //  V EU platí limity dle ETSI EN300.220:
  //  Max EIRP 14 dBm bez omezení.
  //  LoRa.setTxPower(14);
  if (!LoRa.begin(433775000)) {  // nastaveni kmitoctu
    DebugPrintln("LoRa init selhalo!");
    while (1);
  }
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  DebugPrintln("LoRa init OK");
}
// naformatovani APRS zpravy
void sendMsg(float avgSp, float avgDr, float maxSp, float batv, bool shortFormat,
             float tF = 0, float pres = 0, float hum = 0, int rainSum60 = 0, int rainSum24 = 0) {
// Zkontroluj pin 13 - pokud je HIGH rozsvit LED na pinu 25
  if (digitalRead(OLED_ON_PIN) == HIGH) {
    digitalWrite(GREEN_LED, HIGH);
  }
  // Přepočet impulsy v Hz na mph a zaokrouhlení
  // rychlost MPh  1Hz = rychlost větru 2,4km/h 100 km/h * 0,621371 = 62,1371 mph
  int sp_mph   = round(avgSp * 0.621371f);// průměr na MP/h
  int max_mph  = round(maxSp * 0.621371f);// náraz na MP/h

  char msg[128];
  char dirStr[4], spStr[4], gustStr[4], batStr[10];
  snprintf(dirStr,  sizeof(dirStr),  "%03d", (int)avgDr);      // DDD
  snprintf(spStr,   sizeof(spStr),   "%03d", sp_mph);          // SSS
  snprintf(gustStr, sizeof(gustStr), "%03d", max_mph);         // GGG
  snprintf(batStr,  sizeof(batStr),  "_B%.2fV", batv);         //_B3.70V
  if (shortFormat) {
      // Krátká zpráva
    snprintf(msg, sizeof(msg), "%s%s/%sg%s%s", station, dirStr, spStr, gustStr, batStr);
  } else {
        // Doplňkové části
    char tempStr[5], humStr[4], presStr[8], rainStr[5] , rain24Str[5];
    snprintf(tempStr, sizeof(tempStr), "t%03d", (int)tF);// tNNN

    if ((int)hum >= 100)
      snprintf(humStr, sizeof(humStr), "h00"); // 100% → h00
    else
      snprintf(humStr, sizeof(humStr), "h%02d", (int)hum); // hNN

    snprintf(presStr, sizeof(presStr), "b%05d", (int)pres);// bNNNNN
    snprintf(rainStr, sizeof(rainStr), "r%03d", rainSum60); // rNNN
    snprintf(rain24Str, sizeof(rain24Str), "p%03d", rainSum24); // pNNN (24h srážky)
// Plná zpráva
    snprintf(msg, sizeof(msg), "%s%s/%sg%s%s%s%s%s%s%s",
         station, dirStr, spStr, gustStr, tempStr, humStr, presStr, rainStr, rain24Str, batStr);
  }
 // Odeslání přes LoRa
  DebugPrint("LoRa vysílá: ");
  DebugPrintln(msg);

  LoRa.beginPacket();
  LoRa.write('<');// tohle jsem Googlil nekolik hodin :(
  LoRa.write(0xFF);
  LoRa.write(0x01);
  LoRa.print(msg);
  LoRa.endPacket();
  LoRa.sleep();
// Po odeslání zhasni LED
  digitalWrite(GREEN_LED, LOW);
}

//=======================================================================================
void setup() {
//=======================================================================================
  setCpuFrequencyMhz(40);
  Serial.begin(115200);
  delay(200);
  while (!Serial);
// Serial.printf("CPU freq: %d MHz\n", getCpuFrequencyMhz()); // zobrazi na jakem kmitoctu bezi cpu
  float batv = readBat();
// Nastavení pinů
  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(WIND_SPEED_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(WIND_SPEED_PIN), onWindPulse, FALLING);
  pinMode(OLED_ON_PIN, INPUT_PULLUP);
  bool oledHold = (digitalRead(OLED_ON_PIN) == LOW);
  pinMode(GREEN_LED, OUTPUT);// GREEN LED na pin 25
  digitalWrite(GREEN_LED, LOW);// LED zhasnuta na začátku
  pinMode(SRDCE, OUTPUT);
  digitalWrite(SRDCE, LOW);

 // Nastavení OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // Inicializace
  display.setRotation(2);                     // Nastavení rotace displeje o 180°
  display.setTextSize(1);                     // Velikost textu
  display.setTextColor(SSD1306_WHITE);        // Barva textu
  
// Zjistíme důvod probuzení
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();

  switch (reason) {
//////////////////////////////////////////////////////////////////////////////////////////////
///--------------PROBUZENI PO RST NEBO PRIPOJENI NAPETI------------------------------------///
//////////////////////////////////////////////////////////////////////////////////////////////  
    case ESP_SLEEP_WAKEUP_UNDEFINED: {
      if (!wasResetMsgSent) {
        DebugPrint("Prvni spusteni po resetu – posilam ");
        DebugPrintln(rstv);

        if (oledHold) {
          display.clearDisplay();
          display.display();// zaručí, že je displej fyzicky smazaný
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

        digitalWrite(GREEN_LED, HIGH);// Zapni zelenou LED

        setupLoRa(); // Inicializuj LoRa (musí být před odesláním)
    // Odeslání zprávy
        LoRa.beginPacket();
        LoRa.write('<');
        LoRa.write(0xFF);
        LoRa.write(0x01);
        LoRa.print(station);
        LoRa.print(rstv);
        LoRa.endPacket();
        LoRa.sleep();

        digitalWrite(GREEN_LED, LOW);// Vypni LED
        wasResetMsgSent = true;// Uloží se do RTC a přežije deepsleep

        // 🧹 Reset všech RTC proměnných a bufferů
        rainCount5min   = 0;
        cyklus1s        = 0;
        cyklus20s       = 0;
        rainSum60       = 0;
        rainBuf60Poradi = 0;
        for (int i = 0; i < 12; ++i) rainBuf60[i] = 0;

        // Reset 24hodinového srážkového bufferu
        rainSum24        = 0;
        rainBuf24hPoradi = 0;
        for (int i = 0; i < 24; ++i) rainBuf24h[i] = 0;

        // Reset bufferů pro vítr
        rtc_maxWindBuf = 0;
        for (int i = 0; i < 13; ++i) {
          rtc_windSpeedBuf[i] = 0;
          rtc_windDirBuf[i]   = 0;
        }
  // Pokud chceš ručně nulovat buffery po tvrdém resetu:
  // memset(rainBuf24h, 0, sizeof(rainBuf24h));
  // memset(rainBuf60, 0, sizeof(rainBuf60));

        DebugPrintln("🧹 RTC paměť, srážkové a větrné buffery byly vymazány!");
      }
      break;
    }

//////////////////////////////////////////////////////////////////////////////////////////////
//---------------PROBUZENI OD DESTOVEHO SRAZKOMERU-----------------------------------------///
//////////////////////////////////////////////////////////////////////////////////////////////  
    case ESP_SLEEP_WAKEUP_EXT0: {
      
      rainCount5min++;
      DebugPrintf("💧 Doslo preruseni od srazkomeru v %d/19  %d/13 cyklu pocet je %d\n", cyklus1s ,cyklus20s, rainCount5min);
 
      if (oledHold) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        oledActive = false;
      } else {
        display.ssd1306_command(SSD1306_DISPLAYON);
        display.clearDisplay();
        display.setCursor(0, 0);
        display.printf("!!!RAIN+INTERRUPT!!!");
        display.setCursor(0, 10);
        display.printf("Cyklus20s: %d/13", cyklus20s);
        display.setCursor(0, 20);
        display.printf("RainCount : %.d pocet", rainCount5min);
        display.display();
        oledActive = true;
      }

      break;
    }
//////////////////////////////////////////////////////////////////////////////////////////////
///--------------PROBUZENI OD 20 SEKUNDOVEHO CASOVACE--------------------------------------///
//////////////////////////////////////////////////////////////////////////////////////////////
    case ESP_SLEEP_WAKEUP_TIMER: {
     cyklus1s++;
    
      DebugPrintf("⏱️ Probuzeni %d/16 v %d/13 cyklu\n", cyklus1s, cyklus20s);

//kdyz na pin13 je 1 display bude zobrazovat jinak ne. O 3mA se snizi spotreba      
if (oledHold) {
        display.ssd1306_command(SSD1306_DISPLAYOFF);
        oledActive = false;
      }
// každých 18s + 3s základní meteorologické měření = 20s
        if (cyklus1s % 15 == 0) {
        cyklus1s   = 0;
        cyklus20s++;
        DebugPrintf("📡 Doslo k preruseni od casovace 20s je %d/13 cyklus.📡\n", cyklus20s);
        DebugPrintln("😁 Provedese 3 sekundove mereni vetru!");
        attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainInterrupt, RISING);// kdyby prisla srazka v prubehu mereni z 1->0 50ms
// --- Měření větru (3 s) + 5 vzorků směru ---

        digitalWrite(SRDCE,HIGH);// reset externiho wathdog
        SpeedPulseCount = 0;
        float dir5x[5] = {0};
        int idx = 0;
        unsigned long start = millis();
        unsigned long lastSample = 0;
        while (millis() - start < 3000) {
          if (idx < 5 && millis() - lastSample >= 600) {
            int adcVal = analogRead(WIND_DIR_PIN);
            dir5x[idx++] = getAdcDir(adcVal).toFloat();
            lastSample = millis();
          }
        }
        digitalWrite(SRDCE,LOW);
        float wSpeed = (SpeedPulseCount * 2.1f) / 3.0f;// prepocet 3 sekund pulsu na MPh
        rtc_maxWindBuf = max(rtc_maxWindBuf, wSpeed);
        float stdDev = 0.0;// vypocet prumeru smeru vetru s Yamartino odchylkou
        float wDir = avgDirYamartino(dir5x, 5, &stdDev);

        // Pozor na index: zapisujeme do slotu cyklu; cyklus20s jde 1..11
        int writeIdx = cyklus20s-1; // pokud chceš 0..10, uprav na cyklus20s-1
        if (writeIdx < 0) writeIdx = 0;
        if (writeIdx >= 13) writeIdx = writeIdx % 13;
        rtc_windSpeedBuf[writeIdx] = wSpeed;
        rtc_windDirBuf[writeIdx]   = wDir;

//kdyz na pin13 jo 0 display bude zobrazovat jinak ne. O 3mA se snizi spotreba
        if (oledHold) {
          display.ssd1306_command(SSD1306_DISPLAYOFF);
          oledActive = false;
        } else {
          display.ssd1306_command(SSD1306_DISPLAYON);
          display.clearDisplay();
          display.setCursor(0, 0);
          display.printf("Cyklus20s: %d/13", cyklus20s);
          display.setCursor(0, 10);
          display.printf("Pulsy: %d", SpeedPulseCount);
          display.setCursor(0, 20);
          display.printf("Rychl: %.1f m/s", (wSpeed*0.44704));// zobrazi rychlost m/s ne v MPh
          display.setCursor(0, 30);
          display.printf("Smer: %.0f st", wDir);
          display.setCursor(0, 40);
          display.printf("Bat: %.2f V", batv);
          display.setCursor(0, 50);
          display.printf("RainCount: %d pocet", rainCount5min);
          display.display();
          oledActive = true;
        }
//konec 20 sekundoveho cyklu
        DebugPrintf("Cyklus %d/13 | %d speed pulse = %.1f m/s  smer %.0f° rain pulse %d bat %.2fV\n", cyklus20s, SpeedPulseCount, (wSpeed*0.44704), wDir, rainCount5min, readBat());

        // -----------------------------------------------------------------
        // Agregace a odeslání dat po 5 minutach = 11 cyklech
        // -----------------------------------------------------------------
        // 13 cyklu x 20 sekund + 80 tx msg = 300 sekund = 5 minut
        if (cyklus20s >= 13) {
          DebugPrintln("AGREGACE DAT PO 5MIN!");
          // kdyz je napeti pod 3,3V bude poslana kratka msg jen vitr a baterie a deepsleep bude nastaven na 30 minut!!!
          if (batv < 3.3f) {
            DebugPrintln("NAPETI JE PO 3,3V!");
            setupLoRa();
// vypocet prumernych hodnot a maximalni hodnoty vwreu
  //float maxSp;
            float maxSp  = rtc_maxWindBuf;
            float avgSp  = avgSpeed(rtc_windSpeedBuf, cyklus20s, &maxSp);
            float avgDr  = avgDirYamartino(rtc_windDirBuf, cyklus20s);
// smaze hodinovy srazkomer
            for (int i = 0; i < 11; i++) {
              rainBuf60[i] = 0;
            }
 // Odešleme kratka zprava jen vítr a napětí setri se baterie usne na 30 minut
            sendMsg(avgSp, avgDr, maxSp, batv, true);
  // Zde zrušíme probouzení od deště (EXT0), aby ESP šel jen timer!
  // preruseni od deste nefunguje protoze se musi aktivovat
  // DeepSleep 30 minut pouze na timer
            DebugPrintln("Sonda vypne preruseni od destoveho senzoru a usne na 30 minut!");
            esp_sleep_enable_timer_wakeup(1800000ULL * 1000ULL);//30 minut
  // esp_sleep_enable_timer_wakeup(60000000ULL); // 2minuty 2*60 testovani
            display.ssd1306_command(SSD1306_DISPLAYOFF);
            oledActive = false;
            cyklus20s   = 0;
            rtc_maxWindBuf = 0;
            rainCount5min = 0;
            detachInterrupt(digitalPinToInterrupt(RAIN_PIN));// vypne moznost vzbuzeni uP srazkomerem bude spat 30 + 5minut
            delay(50);
            esp_deep_sleep_start();
          }

          // normalni stav kdyz je napeti > 3.3V
          setupLoRa();
          if (!bme.begin(0x76)) {
            DebugPrintln("BME280 init selhalo!");
          }
          float maxSp = rtc_maxWindBuf;
          float avgSp = avgSpeed(rtc_windSpeedBuf, cyklus20s, &maxSp);
          float avgDr  = avgDirYamartino(rtc_windDirBuf, cyklus20s);

          float tC = bme.readTemperature();
          float tF = tC * 1.8f + 32.0f;//prepocet °C na °F
          float pres = (bme.readPressure())/pow((1-ELEVATION/44330.0), 5.255)/10;// prepocet tlaku na hladinu more
          float hum = bme.readHumidity();
          int rainSum60 = rain();

          DebugPrintf("avgSp je: %.0f\n", avgSp);
          DebugPrintf("maxSp je: %.0f\n", maxSp);
          DebugPrintf("avgDr je: %.0f\n", avgDr);
          DebugPrintf("TeplF je: %.0f\n", tF);
          DebugPrintf("TeplC je: %.0f\n", tC);
          DebugPrintf("pres  je: %.0f\n", pres);
          DebugPrintf("hum   je: %.0f\n", hum);
          DebugPrintf("rain  je: %.03d\n", rainSum60);

          if (oledHold) {
            display.clearDisplay();
            display.display();// zaručí, že je displej fyzicky smazaný
            display.ssd1306_command(SSD1306_DISPLAYOFF);
            oledActive = false;
          } else {
            display.ssd1306_command(SSD1306_DISPLAYON);
            display.clearDisplay();
            display.setCursor(0, 0);
            display.printf("!!!LORA TX!!!");
            display.setCursor(0, 10);
            display.printf("AvgDir: %.0f", avgDr);
            display.setCursor(0, 20);
            display.printf("AvgSp: %.0f Mh", avgSp);
            display.setCursor(0, 30);
            display.printf("maxSp: %.0f Mh", maxSp);
            display.setCursor(0, 40);
            display.printf("Rain: %.03d in", rainSum60);
            display.setCursor(0, 50);
            display.printf("Batt: %.2f V", batv);
            display.display();
            oledActive = true;
          }
    // Plná normalni zpráva:
          sendMsg(avgSp, avgDr, maxSp, batv, false, tF, pres, hum, rainSum60, rainSum24);

          cyklus20s = 0;
          rainCount5min = 0;
          rtc_maxWindBuf = 0;
          detachInterrupt(digitalPinToInterrupt(RAIN_PIN));
        } // end if (cyklus20s >= 13)

      } // end if (cyklus1s % 18 == 0)
      break;
    } // end case TIMER
  } // end switch
//------------------------------------------------------------------------------------------
//-----------------------------END SWITCH---------------------------------------------------
//------------------------------------------------------------------------------------------
// --- Nastavení intervalu spánku podle srážek ---
unsigned long nextSleepInterval;
int rainSumCheck = 0;

// Spočítáme, jestli za poslední hodinu něco spadlo
for (int i = 0; i < 12; i++) rainSumCheck += rainBuf60[i];

// Pokud prší nebo nedávno pršelo -> běžný režim
if (rainSumCheck > 0 || rainCount5min > 0) {
  nextSleepInterval = 1000;    // běžný režim, 1s cyklus
  cyklus1s = cyklus1s;         // zachovej běh
  DebugPrintln("🌧️ Aktivní déšť – rychlý 1s režim měření");
} else {
  // Jinak pomalý režim – jen jednou za 20 sekund
  nextSleepInterval = 19500;   // 20 sekund
  cyklus1s = 14;               // aby se 20s měření spouštělo každých 20 sekund
  DebugPrintln("☀️ Bez deště – zpomaluji měření na 20s interval");
}

// --- Aktivace probuzení ---
esp_sleep_enable_ext0_wakeup((gpio_num_t)RAIN_PIN, 0);
esp_sleep_enable_timer_wakeup(nextSleepInterval * 1000ULL);
DebugPrintf("😴 DeepSleep nastaven na %lu ms!\n", nextSleepInterval);
delay(50);
esp_deep_sleep_start();

} // end setup()

void loop() {}
/*
  ____   ___ ______   _    _    __  __ _  ____   __
 |  _ \ / _ \__  / \ | |  / \  |  \/  | |/ /\ \ / /
 | |_) | | | |/ /|  \| | / _ \ | |\/| | ' /  \ V /
 |  __/| |_| / /_| |\  |/ ___ \| |  | | . \   | |  
 |_|    \___/____|_| \_/_/   \_\_|  |_|_|\_\  |_|  

   dalsi verze bude vyuzivat externí pin (heartbeat) :)
  verze J dodelan 24h uhrn srazek + kombinovyny DeepSleep 1s i 20s 
  verze I pri off display pin 4 vypnese i serial
  verze H z duvodu prutrze mracen tak aby se neprodluzovani cas agregace po 5 minutach predelan cyklus deep sleep 20s-->1s
  verze G kompenzace prutrze mracen slepa ulicka
  verze F zmena pin 12 srazkomer vyvojova deska nesla nahrad pokud byla v PCB
  verze E otocena logika on/off OLED
  verze D implementovana Yamartino odchylka
  verze C pri prvnim spusteni bude v poznamce reset verse
  verze B reset srazkomeroveho bufer pri napetim 3,3V

  Díky tomu, že meteostanice je postavena na desce ESP32 LILYGO T3 v1.6.1, která má již integrovaný LoRa modul na 
  frekvenci 433 MHz a displej, je její konstrukce značně zjednodušená.
  Aby sonda mohla měřit směr a rychlost větru, dešťové srážky, tlak, teplotu a vlhkost, potřebuje pouze pět součástek: 
  dva kondenzátory, jeden odpor a senzor BME280.
  Data jsou odesílána na server aprs.fi.
  Pro měření směru a rychlosti větru a srážek využívá čidel z meteorologické stanice WH1080-90, která lze pořídit jako náhradní díly za rozumnou cenu.
  
  
  Měření větru + déšť + LoRa + OLED + BME280 + hodinový úhrn srážek + DeepSleep 2mA
  Rain interrupt na pin 12 (EXT0)
  Agregace po 11 cyklech ktere trvaji 20s = ~5min odeslání
  Otočení OLED o 180°
  Správná inicializace BME280 vždy před měřením nekontroluje pripojeni
  Přidání počítání srážek za uplynulou hodinu a odeslání v APRS poli r###
  Pocet rain impulsu = 0,3 mm, 10 impuls = 3 mm = 0,118 in ≈ 12 setin → r012
  Reseno pomoci kruhoveho bufferu za posledni hodinu (12×5min)
  Ovladani OLED pomoci pinu 04
  mereni srazek bezi i pri mereni vetru i
  posle msg po resetu
  pridelan pin 15 pro srdce externi wathdog reset pri necinosti

konfigurace lokality
  prevest WGS84  N 50°10.84313', E 13°53.28152'zapsat jako  5010.84N/01353.28E
  prevest WGS84  N 49°20.94990', E 14°20.40583'             4920.94N/01420.40E
  N 49°43.12770', E 13°8.73093'
  N 50°3.72740', E 14°24.78170'

  OK1FET-11:!5004.91N/01431.53E_338/000g000_BAT=4.17V SNR=11.25 RSSI=-33
  OK1FET-11:!5004.91N/01431.53E_270/000g000t083h59b10211r000_BAT=4.17V SNR=9.50 RSSI=-32

Field  Meaning
CW0003  Your CW number
>APRS,TCPIP*: Boilerplate
/241505z  The ddhhmm in UTC of the time that you generate the report. However, the timestamp is pretty much ignored by everybody as it is assumed that your clock is not set correctly! If you want to omit this field, then just send an exclamation mark '!' instead.
4220.45N/07128.59W  Your location. This is ddmm.hh -- i.e. degrees, minutes and hundreths of minutes. The Longitude has three digits of degrees and leading zero digits cannot be omitted.
_032  The direction of the wind from true north (in degrees).
/005  The average windspeed in mph
g008  The maximum gust windspeed in mph (over the last five minutes)
t054  The temperature in degrees Farenheit -- if not available, then use '...' Temperatures below zero are expressed as -01 to -99.
r001  The rain in the last 1 hour (in hundreths of an inch) -- this can be omitted
p078  Rain in the last 24 hours (in hundreths of an inch) -- this can be omitted
P044  The rain since the local midnight (in hundreths of an inch) -- this can be omitted
h50 The humidity in percent. '00' => 100%. -- this can be omitted.
b10245  The barometric pressure in tenths of millbars -- this can be omitted. This is a corrected pressure and not the actual (station) pressure as measured at your weatherstation. The pressure is adjusted according to altimeter rules -- i.e. the adjustment is purely based on station elevation and does not include temperature compensation.
 
ANT 433MHz
https://quadmeup.com/3d-printed-433mhz-moxon-antenna-with-arm-and-snap-mount/
https://www.thingiverse.com/thing:2068392/files
 
bme280 how AMSL
https://www.meteocercal.info/forum/Thread-How-to-get-the-sea-level-pressure-with-BMP280

inspirace
https://www.instructables.com/Solar-Powered-WiFi-Weather-
-V30/?utm_source=newsletter&utm_medium=email

VBUS - Solar
https://github.com/Xinyuan-LilyGO/T-SIM7600X/issues/112
*/
