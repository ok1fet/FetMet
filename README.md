<!-- formatovani Markdown nebo html
kdyz neni obrazek 800x600 
<img src="Obrazky/obrazek.jpg" width="400" alt="náhled" />
<p>Celkové schéma zapojení.</p>
<img src="Obrazky/20250606_231952.jpg" width="800" height="600" alt="schema" /></p> 
-->

# FetMet APRS amatérská LoRa meteostanice

🟥**Pozor změna používaní pinu 12 který branil programovaní ESP v PCB OPRAVENO**🟥


Meteostanice je postavena na vývojové desce ESP32 LILYGO T3 v1.6.1, která obsahuje integrovaný LoRa modul (433 MHz) a OLED displej. Díky tomu je celá konstrukce sondy výrazně zjednodušená.

Pro měření směru a rychlosti větru, srážek, atmosférického tlaku, teploty a vlhkosti stačí pouze tři elektronické součástky:
2× kondenzátor, 1× odpor a senzorový modul BME280.

Rychlost větru je měřena každých 20 sekund během intervalu 3 sekund.
Směr větru se vypočítává pomocí Yamartinova algoritmu, který vektorově zpracuje data a provádí korekci směrové odchylky.

Po 13 dvacetisekundových cyklech (tedy každých 5 minut) se provede měření tlaku, teploty a vlhkosti. Výsledná data jsou odesílána prostřednictvím sítě LoRa APRS.

Historická data lze sledovat online na:[www.aprs.fi](https://www.aprs.fi).

🌧️ Měření srážek
Srážky jsou detekovány nepřetržitě s přesností 0,3 mm na impuls.
Data se každých 5 minut ukládají do kruhového bufferu o 12 pozicích, což umožňuje přehled o srážkovém úhrnu za poslední hodinu.
Srážky se detekují nepřetržitě. Každých 5 minut (13 cyklus) se zaznamenávají do **12polohového kruhového bufferu**, čímž získáme přehled za poslední hodinu.

🔩 Použité senzory
Senzory pro vítr a srážky jsou převzaty z běžně dostupných meteostanic WH1080 / WH1090, které lze snadno zakoupit jako náhradní díly za přijatelnou cenu.

🧠 Software
Celý kód je bohatě okomentovaný a všechny výpočty jsou průběžně zobrazovány na sériové konzoli během běhu softwaru.

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

!!!U desky LILYGO není vyveden Vin pin. Zde je Vin bod pro připojení solárního panelu!!!
Nebo můžete solarní panel připojit přes mikro USB.

![Průběh impulsu srážkoměru při změně za kondenzátor M1](Obrazky/20250615_195441.jpg)

Průběh impulsu srážkoměru při změně za kondenzátor M1. Při 10pF občas detekovalo falešnou srážku!

![Provozní zkoušky porovnávání nové sondy v přírodních podmínkách 💪😁](Obrazky/20250620_190045.jpg)

Provozní zkoušky porovnávání nové sondy v přírodních podmínkách 💪😁



