<!-- formatovani Markdown nebo html
kdyz neni obrazek 800x600 
<img src="Obrazky/obrazek.jpg" width="400" alt="náhled" />
<p>Celkové schéma zapojení.</p>
<img src="Obrazky/20250606_231952.jpg" width="800" height="600" alt="schema" /></p> 
-->

# FetMet LoRa amaterská meteostanice

Meteostanice je postavena na vývojové desce **ESP32 LILYGO T3 v1.6.1**, která má integrovaný LoRa modul na **433 MHz** a OLED displej. Díky tomu je celá konstrukce výrazně zjednodušená.

Pro měření směru a rychlosti větru, dešťových srážek, atmosférického tlaku, teploty a vlhkosti stačí pouze čtyři elektronické součástky: **2× kondenzátor**, **1× odpor** a **senzor BME280**!

Sonda měří rychlost větru každých 20 sekund po dobu 3 sekund. Směr větru se měří pětkrát a hodnoty se zprůměrují.

Po 13 cyklech se provede měření tlaku, teploty a vlhkosti. Data se zpracují a odešlou prostřednictvím sítě **LoRa APRS**.

Na serveru [www.aprs.fi](https://www.aprs.fi) kde lze sledovat historická data.

Srážky se detekují nepřetržitě. Každých 5 minut se zaznamenávají do **12polohového kruhového bufferu**, čímž získáme přehled za poslední hodinu.

Senzory pro vítr a srážky jsou použity z meteostanic **WH1080 / WH1090**, které jsou běžně dostupné jako náhradní díly za přijatelnou cenu.

**Celý kód je bohatě okomentován a veškeré výpočty se prezentují na seriové konsoly při běhu softwaru.**

![Celkové schéma zapojení](Obrazky/fet-wx.svg)

**Celkové schéma zapojení.**

![Pohled na prototyp meteosondy MetFet 🙂](Obrazky/20250606_231952.jpg)

Pohled na prototyp meteosondy MetFet 🙂.

![Montáž sondy do vodotěsné krabičky](Obrazky/20250604_143648.jpg)

Montáž sondy do vodotěsné krabičky.

![Pohled na stranu plošného spoje](Obrazky/20250604_133616.jpg)

Pohled na stranu plošného spoje.

![Ukázka montáže za využití 3D tištěných dílů](Obrazky/20250620_184324.jpg)

Ukázka montáže za využití 3D tištěných dílů.

![Detail konzole se senzory větru](Obrazky/20250620_184329.jpg)

Detail konzole se senzory větru.

![Testování J antény](Obrazky/20240724_181448.jpg)

Testování J antény.

![Upravené Moxon antény](Obrazky/20240807_154659.jpg)

Upravené Moxon antény.

![Orientační kalibrace srážkoměru za pomocí zahradního kolečka 😅](Obrazky/20250605_061626.jpg)

Orientační kalibrace srážkoměru za pomocí zahradního kolečka. 😅

![Vin bod pro připojení solárního panelu!!!](Obrazky/20250613_223206.jpg)

!!!U desky LILYGO není vyveden Vin pin. Zde jeVin bod pro připojení solárního panelu!!!

![Průběh impulsu srážkoměru při změně za kondenzátor M1](Obrazky/20250615_195441.jpg)

Průběh impulsu srážkoměru při změně za kondenzátor M1.

![Provozní zkoušky porovnávání nové sondy v přírodních podmínkách 💪😁](Obrazky/20250620_190045.jpg)

Provozní zkoušky porovnávání nové sondy v přírodních podmínkách 💪😁



