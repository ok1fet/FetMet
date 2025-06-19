<h1>Fet MET Meteostanice</h1>
<p>je postavena na vývojové desce ESP32 LILYGO T3 v1.6.1, která má integrovaný LoRa modul (433 MHz) a OLED displej. Tato kombinace značně zjednodušuje</p>
<p>konstrukci zařízení, neboť odpadají potřeby samostatného komunikačního modulu a zobrazovací jednotky.</p>
<ul>
<p>Pro měření směru a rychlosti větru, dešťových srážek, atmosférického tlaku, teploty a vlhkosti stačí jen minimální počet součástek – konkrétně:</p>
<p>2 kondenzátory</p>
<p>1 rezistor</p>
<p>1 senzor BME280</p>
<ul>
<p>Data jsou přenášena prostřednictvím LoRa na server aprs.fi, kde je lze sledovat v reálném čase.
<p>Senzory pro vítr a srážky jsou převzaty z komerčně dostupných meteostanic WH1080-90, které lze snadno sehnat jako náhradní díly za přijatelnou cenu.
<img src="Obrazky/fet-wx.svg" width="720" height="500" alt="schema" /></p>

