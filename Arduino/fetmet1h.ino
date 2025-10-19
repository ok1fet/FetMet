 /*////////////////////////////////////////////////////////////////////////////////////////////
   _____ _____ _____     __  __ _____ _____
  |  ___| ____|_   _|   |  \/  | ____|_   _|
  | |_  |  _|   | |_____| |\/| |  _|   | |  
  |  _| | |___  | |_____| |  | | |___  | |  
  |_|   |_____| |_|     |_|  |_|_____| |_|  Metostanice LoRa verze 1.h1

*/
/////////////////////////////////////////////////////////////////////////////////////////////
const char* station = "OK1FET-99>APRS:!5004.91N/01431.53E_";  // vypocet loc je v poznamkach
#define VREF              3.657f // kalibrace AD prevodniku
#define ELEVATION 225            // výška sondy v metrech nad mořem
const char* rstv = "000/000g000r000_RESETh";     // idetifikace ze doslo k reset sondy + verse sw pro orientaci
/////////////////////////////////////////////////////////////////////////////////////////////

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>

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

volatile int SpeedPulseCount = 0;

RTC_DATA_ATTR int rainBuf60[13] = {0};  // kruhovy buffer definici od nuly
RTC_DATA_ATTR int rainBuf60Poradi = 0;  // index v bufferu
RTC_DATA_ATTR int rainCount5min = 0;    // sčítá pulsy během 5min intervalu
RTC_DATA_ATTR int rainSum60 = 0;        // index v bufferu

RTC_DATA_ATTR int cyklus20s = 0;
RTC_DATA_ATTR int cyklus1s = 0;
RTC_DATA_ATTR float rtc_windSpeedBuf[13];
RTC_DATA_ATTR float rtc_windDirBuf[13];
RTC_DATA_ATTR float rtc_maxWindBuf = 0;

RTC_DATA_ATTR bool wasResetMsgSent = false;// idetifikace reset nebo prvnimu spusteni

Adafruit_BME280 bme;

const unsigned long sleepInterval = 1000; // 20000 ms = (20s) 1000=1s
unsigned long lastPulseTime = 0;
unsigned long lastRainPulseTime = 0;
float wSpeed = 0.0;  // aktuální rychlost větru v km/h
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
  if (t - lastRainPulseTime > 20) {  // SW osetrenim zakmitavani kontaktu 20ms
    rainCount5min++;
    lastRainPulseTime = t;
  }
}
// vypocet prumerna hodnota rychlosti vetru
float avgSpeed(float *a, int n, float *maxSP) {
  float s = 0;
  float maxHodnota = a[0]; 
  Serial.print("avgSpeed vstupni pole: ");
  for (int i = 0; i < n; i++) {
    Serial.print(a[i], 2);  // 2 desetinná místa
    Serial.print(" ");
    s += a[i];
    if (a[i] > maxHodnota) {
      maxHodnota = a[i];
    }
  }
  Serial.println();
  float prumer = s / n;
  Serial.print("Speed průměr: ");
  Serial.print(prumer, 2);
  Serial.print(" | Speed max: ");
  Serial.println(maxHodnota, 2);
  *maxSP = maxHodnota;  // nastavíme výstupní hodnotu
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

  Serial.print("avgDirYamartino vstupni pole: ");
  for (int i = 0; i < n; i++) {
    Serial.printf("%.2f ", a[i]);
  }

  Serial.println();

  for (int i = 0; i < n; i++) {
    float rad = radians(a[i]);
    float s = sin(rad);
    float c = cos(rad);
    sumSin += s;
    sumCos += c;

    Serial.printf("Mereni %d: deg=%.2f rad=%.2f cos=%.4f sin=%.4f | sumCos=%.4f sumSin=%.4f\n",
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

  if (stdDevOut) {
    *stdDevOut = stdDev;
  }

  Serial.printf("Souhrn: avgSin=%.4f avgCos=%.4f -> atan2=%.4f rad = %.2f deg\n", 
                avgSin, avgCos, avgDirRad, avgDirDeg);
  Serial.printf("Yamartino odchylka: epsilon=%.4f asin=%.4f rad = %.2f deg\n", 
                epsilon, asin(epsilon), stdDev);

  Serial.printf("Yamartino průměr: %.2f°, odchylka: %.2f°\n", avgDirDeg, stdDev);

  return avgDirDeg;
}
// mereni napeti baterie
float readBat() {
  int r = analogRead(BATTERY_PIN); // predradnik je integrovan LilyGO na pinu 35
  return (r / 4095.0f * VREF) / 0.5f; // R1 a R2 100K
}
// Mereni srazek s hodinovym kruhovym buferem
float rain(){
    rainBuf60[rainBuf60Poradi] = rainCount5min; // Srážkový buffer počet pulsu za 5 min
    Serial.printf("Srazky 60 minutovy kruhovy buffer poradi %d/12 hodnota %d  5min x12 cyklu = 60 minut\n", rainBuf60Poradi, rainCount5min);
    rainBuf60Poradi = (rainBuf60Poradi + 1) % 12;
    rainCount5min = 0; // Reset 5-min počítadla pro další interval
    int sumHour = 0;
    // Výpis celého bufferu
    Serial.print("Obsah rainBuf60: ");
    for (int i = 0; i < 12; i++) {
    Serial.printf("%d ", rainBuf60[i]);
     }
    Serial.println();
    // Vypočteme hodinový součet jako součet všech 12 položek v kruhovem bufferu
    for (int i = 0; i < 12; i++) sumHour += rainBuf60[i];
    // Součet pulsu za hodinu -> setiny palců (každý puls ~0.3mm = 0.0118 in → v setinách ~1.18
    // Pro zjednodušení posíláme přímo rainSum60 jako setiny palců.
    rainSum60 = sumHour;  // 1 puls = 1/100 inch tady je prostor pro kalibraci
    Serial.printf("60 minutovy soucet rain impulsu: %d bude formatovano jako r%03d\n", rainSum60, rainSum60);
    return (int)rainSum60;
}
// Nastaveni LoRa protokolu
void setupLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.setTxPower(17);  // nebo 20 17 14 dBm 20-2  45dBm

  // Aktivace PA_BOOST a High Power Mode (20 dBm)
  // LoRa.setTxPower(20, true);  // true = PA_BOOST 42dBm

  //  V EU platí limity dle ETSI EN300.220:
  //  Max EIRP 14 dBm bez omezení.
  //  LoRa.setTxPower(14);

  
  if (!LoRa.begin(433775000)) {  // nastaveni kmitoctu
    Serial.println("LoRa init selhalo!");
    while (1);
  }
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  Serial.println("LoRa init OK");
}
// naformatovani APRS zpravy
void sendMsg(float avgSp, float avgDr, float maxSp, float batv, bool shortFormat,
             float tF = 0, float pres = 0, float hum = 0, int rainSum60 = 0) {

// Zkontroluj pin 13 - pokud je HIGH rozsvit LED na pinu 25
if (digitalRead(OLED_ON_PIN) == HIGH) {
  digitalWrite(GREEN_LED, HIGH); // Zapni LED
}
  // Přepočet impulsy v Hz na mph a zaokrouhlení
  // rychlost MPh  1Hz = rychlost větru 2,4km/h 100 km/h * 0,621371 = 62,1371 mph
  int sp_mph   = round(avgSp * 0.621371f);   // průměr na MP/h
  int max_mph  = round(maxSp * 0.621371f);   //   náraz na MP/h

  // Buffer pro výslednou zprávu
  char msg[128];

  // Společné části zprávy
  char dirStr[4], spStr[4], gustStr[4], batStr[10];
  snprintf(dirStr,  sizeof(dirStr),  "%03d", (int)avgDr);      // DDD
  snprintf(spStr,   sizeof(spStr),   "%03d", sp_mph);          // SSS
  snprintf(gustStr, sizeof(gustStr), "%03d", max_mph);         // GGG
  snprintf(batStr,  sizeof(batStr),  "_B%.2fV", batv);         // _B3.70V
//snprintf(batStr,  sizeof(batStr),  "_BAT=%.2fV", batv);      // _BAT=3.70V

  if (shortFormat) {
    // Krátká zpráva
    snprintf(msg, sizeof(msg), "%s%s/%sg%s%s", station, dirStr, spStr, gustStr, batStr);
  } else {
    // Doplňkové části
    char tempStr[5], humStr[4], presStr[8], rainStr[5];
    snprintf(tempStr, sizeof(tempStr), "t%03d", (int)tF);        // tNNN

    if ((int)hum >= 100)
      snprintf(humStr, sizeof(humStr), "h00");                   // 100% → h00
    else
      snprintf(humStr, sizeof(humStr), "h%02d", (int)hum);       // hNN

    snprintf(presStr, sizeof(presStr), "b%05d", (int)pres);      // bNNNNN
    snprintf(rainStr, sizeof(rainStr), "r%03d", rainSum60);      // rNNN

    // Plná zpráva
    snprintf(msg, sizeof(msg), "%s%s/%sg%s%s%s%s%s%s",
         station, dirStr, spStr, gustStr, tempStr, humStr, presStr, rainStr, batStr);
  }

  // Odeslání přes LoRa
  Serial.print("LoRa vysílá: ");
  Serial.println(msg);
 
  LoRa.beginPacket();
  LoRa.write('<'); // tohle jsem Googlil nekolik hodin :(
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
  setCpuFrequencyMhz(40); // spotreba v deepsleep 2mA vypocet 21mA 90mA vysilani pri 240MHz 2, 60, 140mA
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
  pinMode(GREEN_LED, OUTPUT);       // GREEN LED na pin 25
  digitalWrite(GREEN_LED, LOW);     // LED zhasnuta na začátku
  pinMode(SRDCE, OUTPUT);
  digitalWrite(SRDCE, LOW);  // zhasnout LED

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
  case ESP_SLEEP_WAKEUP_UNDEFINED:{
  
   if (!wasResetMsgSent) {
    Serial.print("Prvni spusteni po resetu – posilam ");
	  Serial.println(rstv);
    
    if (oledHold) {
  display.clearDisplay();
  display.display(); // zaručí, že je displej fyzicky smazaný
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

    digitalWrite(GREEN_LED, HIGH); // Zapni LED

    setupLoRa();  // Inicializuj LoRa (musí být před odesláním)
    // Odeslání zprávy
    LoRa.beginPacket();
    LoRa.write('<');
    LoRa.write(0xFF);
    LoRa.write(0x01);
	  LoRa.print(station);
    LoRa.print(rstv);
    LoRa.endPacket();
    LoRa.sleep();

    digitalWrite(GREEN_LED, LOW); // Vypni LED
    wasResetMsgSent = true;       // Uloží se do RTC a přežije deepsleep
    cyklus1s = 0;
    cyklus20s = 0;
    rainCount5min = 0;
    rainSum60 = 0;
  }
    break; }

//////////////////////////////////////////////////////////////////////////////////////////////
//---------------PROBUZENI OD DESTOVEHO SRAZKOMERU-----------------------------------------///
//////////////////////////////////////////////////////////////////////////////////////////////  
  case ESP_SLEEP_WAKEUP_EXT0:{
 cyklus1s++; // každé probuzení = +1 sekunda
 rainCount5min++;
 Serial.printf("💧 Doslo preruseni od srazkomeru je %d/12 cyklus pocet je %d\n", cyklus20s, rainCount5min );

 
  if (oledHold) {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  oledActive = false;}
  else {
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("!!!RAIN+INTERRUPT!!!");
  display.setCursor(0, 10);
  display.printf("Cyklus20s: %d/12", cyklus20s);
  display.setCursor(0, 20);
  display.printf("RainCount : %.d pocet", rainCount5min);
  display.display();
  oledActive = true;}

  esp_sleep_enable_timer_wakeup(sleepInterval * 1000ULL);
  Serial.printf("😴 set %.d ms DeepSleep!", sleepInterval);
  delay(50);
  esp_deep_sleep_start();


  break;}

//////////////////////////////////////////////////////////////////////////////////////////////
//---------------PROBUZENI OD 20 SEKUNDOVEHO CASOVACE--------------------------------------///
//////////////////////////////////////////////////////////////////////////////////////////////
case ESP_SLEEP_WAKEUP_TIMER:{
cyklus1s++; // každé probuzení = +1 sekunda
  Serial.printf("⏱️ Probuzeni #%d po 1 sekunde\n", cyklus1s);

 //kdyz na pin13 jo 0 display bude zobrazovat jinak ne. O 3mA se snizi spotreba
  if (oledHold) {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  oledActive = false;}

  // každých 18s + 3s základní meteorologické měření = 20s
  if (cyklus1s % 18 == 0) {

  cyklus1s   = 0;
  cyklus20s++;
  Serial.printf("📡 Doslo k preruseni od casovace 20s je %d/11 cyklus.📡\n", cyklus20s);
  Serial.println("😁 Provedese 3 sekundove mereni vetru!");
  attachInterrupt(digitalPinToInterrupt(RAIN_PIN), rainInterrupt, RISING);// kdyby prisla srazka v prubehu mereni z 1->0 50ms
// --- Měření větru (3 s) + 5 vzorků směru ---
  digitalWrite(SRDCE,HIGH); // vynulovani externiho wathdog
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
  //float wDir = avgDir(dir5x, 5);
  float stdDev = 0.0;
  float wDir = avgDirYamartino(dir5x, 5, &stdDev); // vypocet prumeru smeru vetru s Yamartino odchylkou
  rtc_windSpeedBuf[cyklus20s] = wSpeed;
  rtc_windDirBuf[cyklus20s]   = wDir;
  
  //kdyz na pin13 jo 0 display bude zobrazovat jinak ne. O 3mA se snizi spotreba
  if (oledHold) {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  oledActive = false;}
  else {
  display.ssd1306_command(SSD1306_DISPLAYON);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("Cyklus20s: %d/11", cyklus20s);
  display.setCursor(0, 10);
  display.printf("Pulsy: %d", SpeedPulseCount);
  display.setCursor(0, 20);
  display.printf("Rychl: %.1f m/s", (wSpeed*0.44704));// zobrazi rychlost m/s ne v MPh
  display.setCursor(0, 30);
  display.printf("Smer: %.0f st", wDir);
  display.setCursor(0, 40);
  display.printf("Bat: %.2f V", batv);
  display.setCursor(0, 50);
  display.printf("Dest: %.d pocet", rainCount5min);
  display.display();
  oledActive = true;}

//konec 20 sekundoveho cyklu
  Serial.printf("Cyklus %d/11 | %d speed pulse = %.1f m/s  smer %.0f° rain pulse %d bat %.2fV\n", cyklus20s, SpeedPulseCount, (wSpeed*0.44704), wDir, rainCount5min, readBat());
//-----------------------------------------------------------------
//------- Agregace a odeslání dat po 5 minutach = 11 cyklech ------
//-----------------------------------------------------------------  
// 11 cyklu x 20 sekund + 30 tx msg = 300 sekund = 5 minut
  if (cyklus20s >= 11) {
    Serial.println("AGREGACE DAT PO 5MIN!");
        // kdyz je napeti pod 3,3V bude poslana kratka msg jen vitr a baterie a deepsleep bude nastaven na 30 minut!!!
        if (batv < 3.3f) {
            Serial.println("NAPETI JE PO 3,3V!");
  setupLoRa();
// vypocet prumernych hodnot a maximalni hodnoty vwreu
  //float maxSp;
  float maxSp  = rtc_maxWindBuf;
  float avgSp  = avgSpeed(rtc_windSpeedBuf, 11, &maxSp);
  float avgDr  = avgDirYamartino(rtc_windDirBuf, 11);
 
 // smaze hodinovy srazkomer
  for (int i = 0; i < 11; i++) {
  rainBuf60[i] = 0;
}

  // Odešleme kratka zprava jen vítr a napětí setri se baterie usne na 30 minut
  sendMsg(avgSp, avgDr, maxSp, batv, true);
  // Zde zrušíme probouzení od deště (EXT0), aby ESP šlo jen na timer!
  // esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0); // E (5932) sleep: Incorrect wakeup source (2) to disable.
  // preruseni od deste nefunguje protoze se musi aktivovat
  // DeepSleep 30 minut pouze na timer
  Serial.println("Sonda vypne preruseni od destoveho senzoru a usne na 30 minut!");
  esp_sleep_enable_timer_wakeup(1800000ULL * 1000ULL);//30 minut
  // esp_sleep_enable_timer_wakeup(60000000ULL); // 2minuty 2*60 testovani!

  // i kdyz je dispaly on pin 13 0 i tak se vypne!
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  oledActive = false;
  cyklus20s   = 0;
  rtc_maxWindBuf = 0;
  rainCount5min = 0;
  detachInterrupt(digitalPinToInterrupt(RAIN_PIN)); // vypne moznost vzbuzeni uP srazkomerem bude spat 30 + 5minut
  delay(50);
  esp_deep_sleep_start();

}
//-----------------------------------------------------------------  
// Napeti je > 3,3V normalni stav
//-----------------------------------------------------------------  

    setupLoRa();
    if (!bme.begin(0x76)) {
    Serial.println("BME280 init selhalo!");
    }
    //float maxSP;
    float maxSp = rtc_maxWindBuf;
    float avgSp = avgSpeed(rtc_windSpeedBuf, 11, &maxSp);
    float avgDr  = avgDirYamartino(rtc_windDirBuf, 11);

   
    float tC = bme.readTemperature();
    float tF = tC * 1.8f + 32.0f; //prepocet °C na °F
    float pres = (bme.readPressure())/pow((1-ELEVATION/44330.0), 5.255)/10;// prepocet tlaku na hladinu more
    float hum = bme.readHumidity();
    int rainSum60 = rain();
   
    Serial.printf("avgSp je: %.0f\n", avgSp);
    Serial.printf("maxSp je: %.0f\n", maxSp);
    Serial.printf("avgDr je: %.0f\n", avgDr);
    Serial.printf("TeplF je: %.0f\n", tF);
    Serial.printf("TeplC je: %.0f\n", tC);
    Serial.printf("pres  je: %.0f\n", pres);
    Serial.printf("hum   je: %.0f\n", hum);
    Serial.printf("rain  je: %.03d\n", rainSum60);
   

  if (oledHold) {
  display.clearDisplay();
  display.display(); // zaručí, že je displej fyzicky smazaný
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  oledActive = false;}
  
  else {
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
  oledActive = true;}

    // Plná normalni zpráva:
    sendMsg(avgSp, avgDr, maxSp, batv, false, tF, pres, hum, rainSum60);
 
 // Reset cyklu maximalni hodnoty vetru deste
  cyklus20s = 0;
  rainCount5min = 0;
  rtc_maxWindBuf = 0;
   detachInterrupt(digitalPinToInterrupt(RAIN_PIN));
  }
  }
  break;}

}
//------------------------------------------------------------------------------------------
//-----------------------------END SWITCH---------------------------------------------------
//------------------------------------------------------------------------------------------
 
  // --- Pokud ještě nejsme u 11 cyklů, naplánujeme další probuzení za 20s ( + déšť ) ---
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RAIN_PIN, 0);
  esp_sleep_enable_timer_wakeup(sleepInterval * 1000ULL);
  Serial.printf("😴 set %.d ms DeepSleep!", sleepInterval);
  delay(50);
  esp_deep_sleep_start();

}

void loop() {}

/*
  ____   ___ ______   _    _    __  __ _  ____   __
 |  _ \ / _ \__  / \ | |  / \  |  \/  | |/ /\ \ / /
 | |_) | | | |/ /|  \| | / _ \ | |\/| | ' /  \ V /
 |  __/| |_| / /_| |\  |/ ___ \| |  | | . \   | |  
 |_|    \___/____|_| \_/_/   \_\_|  |_|_|\_\  |_|  
                                                                                         
  verze H z duvodu prutrze mracen tak aby se neprodluzovani cas agregace po 5 minutach predelan cyklus1S na 1 sekundu
  verze G kompenzace prutrze mracen
  verze F zmena pin 12 srazkomer vyvojova deska nesla nahrad pokud byla v PCB
  verze E otocena logika on/off OLED
  verze D implementovana Yamartino odchylka
  verze C pri prvnim spusteni bude v poznamce reset
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
