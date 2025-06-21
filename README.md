<h1>LoRa meteostanice Fet-Met</h1>
<p>je postavená na vývojové desce ESP32 LILYGO T3 v1.6.1, která má integrovaný LoRa modul na 433 MHz) a</p>
<p>OLED displej. Díky tomu se celá konstrukce značně zjednodušila.</p>
<p>Pro měření směru a rychlosti větru, dešťových srážek, atmosférického tlaku, teploty a vlhkosti stačí </p>
<p>jen čtyři elektronické součástky 2x kondenzátor 1 odpor a modul BME280!</p>
<p>Sonda měří rychlost větru každých 20 sekund po dobu 3 sekund. Směr změří 5x který zprůměruje.</p>
<p>Po 13 cyklech změří tlak, teplotu a vlhkost data z agreguje a prostřednictvím LoRa APRS sítě odešle.</p>
<p>Na serveru www.aprs.fi lze sledovat v historická data.</p>
<p>Srážky se detekují nepřetžitě. Každých 5 s v daném cyklu zapíše do 12-ti kruhového buferu.</p>
<p>Tím máme přehled za poslednední hodinu.</p>
<p>Senzory pro vítr a srážky jsou použity jako náhradní díl z meteostanic WH1080-90, která jsou na trhu dostupnél</p>
<p>za přijatelnou cenu.</p>

<h1>LoRa meteostanice Fet-Met</h1>
<p>Meteostanice je postavena na vývojové desce ESP32 LILYGO T3 v1.6.1, která má integrovaný LoRa modul na 433&nbsp;MHz a OLED displej. Díky tomu je celá konstrukce výrazně zjednodušená.</p>

<p>Pro měření směru a rychlosti větru, dešťových srážek, atmosférického tlaku, teploty a vlhkosti stačí pouze čtyři elektronické součástky: 2× kondenzátor, 1× odpor a senzor BME280!</p>

<p>Sonda měří rychlost větru každých 20 sekund po dobu 3 sekund. Směr větru se měří pětkrát a výsledky se zprůměrují.</p>

<p>Po 13 cyklech se provede měření tlaku, teploty a vlhkosti. Data se zpracují a odešlou prostřednictvím sítě LoRa APRS.</p>

<p>Na serveru <a href="https://www.aprs.fi" target="_blank">www.aprs.fi</a> lze sledovat historická data.</p>

<p>Srážky se detekují nepřetržitě. Každých 5 sekund se zaznamenávají do 12polohového kruhového bufferu, čímž získáme přehled za poslední hodinu.</p>

<p>Senzory pro vítr a srážky jsou použity z meteostanic WH1080/WH1090, které jsou běžně dostupné jako náhradní díly za přijatelnou cenu.</p>

# LoRa meteostanice Fet-Met

Meteostanice je postavena na vývojové desce **ESP32 LILYGO T3 v1.6.1**, která má integrovaný LoRa modul na **433 MHz** a OLED displej. Díky tomu je celá konstrukce výrazně zjednodušená.

Pro měření směru a rychlosti větru, dešťových srážek, atmosférického tlaku, teploty a vlhkosti stačí pouze čtyři elektronické součástky: **2× kondenzátor**, **1× odpor** a **senzor BME280**!

Sonda měří rychlost větru každých 20 sekund po dobu 3 sekund. Směr větru se měří pětkrát a hodnoty se zprůměrují.

Po 13 cyklech se provede měření tlaku, teploty a vlhkosti. Data se zpracují a odešlou prostřednictvím sítě **LoRa APRS**.

Na serveru [www.aprs.fi](https://www.aprs.fi) lze sledovat historická data.

Srážky se detekují nepřetržitě. Každých 5 sekund se zaznamenávají do **12polohového kruhového bufferu**, čímž získáme přehled za poslední hodinu.

Senzory pro vítr a srážky jsou použity z meteostanic **WH1080 / WH1090**, které jsou běžně dostupné jako náhradní díly za přijatelnou cenu.


<img src="Obrazky/fet-wx.svg" width="800" height="600" alt="schema" /></p>
<p>Celkové schéma zapojení.</p>
<img src="Obrazky/20250606_231952.jpg" width="800" height="600" alt="schema" /></p> 
<p>Pohled na prototyp meteosondy MetFet 🙂.</p>
<img src="Obrazky/20250604_143648.jpg" width="800" height="600" alt="schema" /></p> 
<p>Montáž sondy do vodotěsné krabičky.</p>
<img src="Obrazky/20250604_133616.jpg" width="800" height="600" alt="schema" /></p>
<p>Pohled na stranu plošného spoje.</p>
<img src="Obrazky/20250620_184324.jpg" width="600" height="800" alt="schema" /></p>
<p>Ukázka montáže za využití 3D tištěných dílů.</p>
<img src="Obrazky/20250620_184329.jpg" width="800" height="600" alt="schema" /></p>
<p>Detail konzole se senzory větru.</p>
<img src="Obrazky/20240724_181448.jpg" width="600" height="800" alt="schema" /></p>
<p>Testování J antény.</p>
<img src="Obrazky/20240807_154659.jpg" width="600" height="800" alt="schema" /></p>
<p>Upravené Moxon antény.</p>
<img src="Obrazky/20250605_061626.jpg" width="800" height="600" alt="schema" /></p>
<p>Orientační klibrace srážkoměru za pomocí zahradního kolečka. 😅</p>
<img src="Obrazky/20250613_223206.jpg" width="600" height="800" alt="schema" /></p>
<p>Vin bod pro připojení solárního panelu!!!</p>
<img src="Obrazky/20250615_195441.jpg" width="600" height="800" alt="schema" /></p>
<p>Průběh impulsu srážkoměru při změne za kondezator M1.</p>
<img src="Obrazky/20250620_190045.jpg" width="600" height="800" alt="schema" /></p>
<p>Provozní zkoušky porovnávání nové sondy v přírodních podmínkách 💪😁 </p>



