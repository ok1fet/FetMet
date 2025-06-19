Meteostanice je postavena na vývojové desce ESP32 LILYGO T3 v1.6.1, která má integrovaný LoRa modul (433 MHz) a OLED displej. Tato kombinace značně zjednodušuje konstrukci zařízení, neboť odpadají potřeby samostatného komunikačního modulu a zobrazovací jednotky.
Pro měření směru a rychlosti větru, dešťových srážek, atmosférického tlaku, teploty a vlhkosti stačí jen minimální počet součástek – konkrétně:
2 kondenzátory
1 rezistor
1 senzor BME280

Data jsou přenášena prostřednictvím LoRa na server aprs.fi, kde je lze sledovat v reálném čase.
Senzory pro vítr a srážky jsou převzaty z komerčně dostupných meteostanic WH1080-90, které lze snadno sehnat jako náhradní díly za přijatelnou cenu.
