 /*////////////////////////////////////////////////////////////////////////////////////////////
   _____ _____ _____     __  __ _____ _____
  |  ___| ____|_   _|   |  \/  | ____|_   _|
  | |_  |  _|   | |_____| |\/| |  _|   | |  
  |  _| | |___  | |_____| |  | | |___  | |  
  |_|   |_____| |_|     |_|  |_|_____| |_|  Metostanice LoRa verze mini – jen BME280 + baterie

*//////////////////////////////////////////////////////////////////////////////////////////////

#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>

// ==================== NASTAVENÍ ====================
const char* station = "OK1FET-73>APRS:!5006.91N/01436.53E_";
const char* note    = "test BME280";

#define VREF              3.6527f //nové VREF = 3.657 × (napeti 4.17 hw / napeti 4.34 mp ) ≈ 3.657 × 0.960 ≈ 3.513
#define ELEVATION         225    // výška sondy v metrech nad mořem 
#define SLEEP_MINUTES     10

// Piny LilyGO T3
#define PIN_BATTERY       35
#define PIN_OLED_ON       4     //OLED ovládání podle jumperu na pinu
#define PIN_TX_LED        25    // integrovana na LiLYGO

#define LORA_SCK          5
#define LORA_MISO         19
#define LORA_MOSI         27
#define LORA_SS           18
#define LORA_RST          23
#define LORA_DIO0         26

// Debug jen když je OLED jumper otevřený (pin HIGH)
#define DebugPrint(x)      do { if (digitalRead(PIN_OLED_ON)==HIGH) Serial.print(x); } while(0)
#define DebugPrintln(x)    do { if (digitalRead(PIN_OLED_ON)==HIGH) Serial.println(x); } while(0)
#define DebugPrintf(...)   do { if (digitalRead(PIN_OLED_ON)==HIGH) Serial.printf(__VA_ARGS__); } while(0)

// ==================== OBJEKTY ====================
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_BME280 bme;

// ==================== LORA ====================
void setupLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.setTxPower(17);

  if (!LoRa.begin(433775000)) {
    DebugPrintln("LoRa init selhalo");
    while (1) delay(1000);
  }

  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  DebugPrintln("LoRa OK");
}

// APRS bez větru/srážek, baterie v poznámce
void sendMsg(float batv, float tF, float pres_aprs, float hum, bool oledOn) {
  // LED svítí jen když je OLED režim aktivní
  digitalWrite(PIN_TX_LED, oledOn ? HIGH : LOW);

  char msg[160];
  char tempStr[6], humStr[4], presStr[8], batStr[16];

  snprintf(tempStr, sizeof(tempStr), "t%03d", (int)tF);
  snprintf(humStr,  sizeof(humStr),  "h%02d", ((int)hum >= 100) ? 0 : (int)hum);
  snprintf(presStr, sizeof(presStr), "b%05d", (int)pres_aprs);
  snprintf(batStr,  sizeof(batStr),  "BAT %.2f V", batv);

  snprintf(msg, sizeof(msg),
           "%s.../...g...%s%s%s %s %s",
           station, tempStr, humStr, presStr, batStr, note);

  LoRa.beginPacket();
  LoRa.write('<');
  LoRa.write(0xFF);
  LoRa.write(0x01);
  LoRa.print(msg);
  LoRa.endPacket();
  LoRa.sleep();

  DebugPrintln(msg);
  digitalWrite(PIN_TX_LED, LOW);
}

// ==================== SETUP ====================
void setup() {
  setCpuFrequencyMhz(40);
  gpio_deep_sleep_hold_dis();

  Serial.begin(115200);
  delay(200);

  pinMode(PIN_TX_LED, OUTPUT);
  digitalWrite(PIN_TX_LED, LOW);

  // HIGH = OLED zapnutý / debug
  // LOW  = OLED vypnutý (jumper / hold)
  pinMode(PIN_OLED_ON, INPUT_PULLUP);
  bool oledOn  = (digitalRead(PIN_OLED_ON) == HIGH);
  bool oledHold = !oledOn;

  DebugPrintln("\n=== FETMET mini BME280 ===");
  DebugPrintf("OLED: %s\n", oledOn ? "ON" : "OFF");

  // OLED init vždy, pak zapnout/vypnout podle pinu
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    DebugPrintln("OLED nenalezen");
  }
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (oledHold) {
    display.ssd1306_command(SSD1306_DISPLAYOFF);
  } else {
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.clearDisplay();
    display.display();
  }

  // LoRa + BME
  setupLoRa();

  if (!bme.begin(0x76)) {
    DebugPrintln("BME280 nenalezen");
  }

  // Baterie
  float batv = (analogRead(PIN_BATTERY) / 4095.0f * VREF) / 0.5f;

  // BME280
  float tC = bme.readTemperature();
  float tF = tC * 1.8f + 32.0f;
  float p_station_hPa = bme.readPressure() / 100.0f;
  float p_sea_hPa = p_station_hPa / powf((1.0f - ELEVATION / 44330.0f), 5.255f);
  float pres_aprs = p_sea_hPa * 10.0f;   // APRS desetiny hPa
  float hum = bme.readHumidity();

  DebugPrintf("T=%.1f C | H=%.0f %% | P=%.1f hPa | BAT=%.2f V\n",
              tC, hum, p_sea_hPa, batv);

  // OLED výpis jen když není hold
  if (oledOn) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    display.clearDisplay();
    display.setCursor(0, 0);
    display.printf("T: %.1f C", tC);
    display.setCursor(0, 14);
    display.printf("H: %.0f %%", hum);
    display.setCursor(0, 28);
    display.printf("P: %.1f hPa", p_sea_hPa);
    display.setCursor(0, 42);
    display.printf("B: %.2f V", batv);
    display.setCursor(0, 56);
    display.printf("TX LoRa...");
    display.display();
  }

  // Odeslání
  sendMsg(batv, tF, pres_aprs, hum, oledOn);

  if (oledOn) {
    display.setCursor(0, 56);
    display.printf("Sleep %d min   ", SLEEP_MINUTES);
    display.display();
    delay(300);
  }

  // Deep sleep
  DebugPrintf("Spánek %d minut\n", SLEEP_MINUTES);
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_MINUTES * 60ULL * 1000000ULL);
  delay(50);
  esp_deep_sleep_start();
}

void loop() {}
