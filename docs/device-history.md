# Langzeitaufzeichnung direkt auf dem ESP

SolarPilot benötigt zur Aufzeichnung ausschließlich seine Stromversorgung.
WLAN ist zum späteren Abruf notwendig, nicht zum Speichern. Die bisherige
PC-Aufzeichnung ist beendet und bleibt nur eine optionale Testhilfe.

## Bedienung

Adminseite -> Langzeitaufzeichnung im Gerät -> Speicherstatus prüfen.
Aktuelle Aufzeichnung speichern schreibt noch gepufferte Werte ohne Test
oder Schaltaktion. Danach CSV-Segmente 0 bis 7 herunterladen. Ein noch nicht
angelegtes Segment meldet 404. Der aktuelle Index ist im Speicherstatus
sichtbar; davor liegende Segmente in Ringfolge enthalten ältere Daten.
Nur angemeldete Administratoren können Status/Export lesen. Speichern
verlangt zusätzlich die zufällige Aktionskennung der aktuellen Sitzung.

## Speicher und Zeit

LittleFS in der vorhandenen spiffs-Datenpartition; keine Änderung der
Partitionstabelle oder OTA-Offsets. Acht Segmente mit je maximal 32 KiB,
2 KiB RAM-Puffer. Minutenprobe und Zustandswechsel, höchstens eine neue
Zustandsprobe pro Sekunde. Speicherung spätestens nach fünf Minuten
laufendem Hauptloop oder bei fast vollem Puffer, zusätzlich vor regulärem
Web-Neustart/Update. Ein Stillstand des Loops verlängert diese Frist.
Keine zusätzliche Netzwerkaktivität für die Aufzeichnung.

Bei Rotation wird das älteste Segment ersetzt. Aufbewahrungsdauer hängt von
der Häufigkeit der Zustandswechsel ab; keine garantierten 24 Stunden bei
ständig wechselnden Fehlern. Flash-Schreibzugriffe sind gebündelt, aber
keine Lebensdauerzusage für das konkrete Entwicklungsboard. Gespeicherte
Proben bleiben über Software-Neustarts/OTA erhalten. Stromausfall kann den
Puffer und eine gerade geschriebene Zeile verlieren; unvollständige Zeilen
sind bei der Auswertung auszuschließen. Stromausfall-/Rotationstest offen.

Ein leeres, vollständig als 0xFF erkanntes Datenvolume wird initialisiert.
Ein nicht leeres Volume wird bei fehlgeschlagenem Mount nicht formatiert.
Ein Speicherfehler stoppt die normale Aufzeichnung und wird im Adminstatus
sichtbar, während die Regelung weiterarbeitet. Nichts enthält Zugangsdaten,
Netznamen, Tokens oder unbeschränkte Logtexte.

`boot` ist eine zufällige 32-Bit-Kennung, keine weltweit eindeutige ID.
`seconds` zählt Sekunden seit diesem Start mit 64-Bit-Gerätezeit. Keine
Kalenderzeit und keine Dauerbestimmung über eine stromlose Phase hinweg.

| Feld | Bedeutung |
| --- | --- |
| boot | Hexadezimale Startkennung |
| seconds | Sekunden seit Start |
| power_w | Positiv Einspeisung, negativ Bezug; ohne Flag 1 kein Messwert |
| rssi | Empfang in dBm; bei getrenntem WLAN 0 als Platzhalter |
| flags | Summe der unten erklärten Zustandsbits |
| timeouts | GoodWe-Antworttimeouts seit Start |

Flags: 1 gültiger Messwert, 2 WLAN verbunden, 4 GoodWe vorbereitet,
8 Ausgang zuletzt bestätigt EIN, 16 AUS noch pending, 32 Ausgangsretry,
64 Testbetrieb. Diese Werte zeigen Softwarebeobachtungen; sie ersetzen
keine unabhängige Messung des Relais. Ereignisse zwischen zwei Durchläufen
können fehlen. Export ist auf 32 KiB pro Abruf begrenzt; der synchrone
Webserver kann beim Download weiterhin zeitweise die Regelung blockieren.

## Prüfstand

2026-10-03: Build, WLAN-Installation, Zugriffsschutz, Aktionskennung,
begrenzter CSV-Export und Persistenz über sicheren Web-Neustart bestanden.
Neue Startkennung sowie inaktiver Testmodus geprüft. 24-h-Betrieb am Netzteil,
Stromverlust und vollständige Ringrotation noch nicht als bestanden gewertet.
Hardwareprüfer: `tools/history-hardware-check.py http://DEVICE_IP`;
optional `--wifi-loss` prüft den 40-s-WLAN-Test und die Offline-Probe.

Auch `--wifi-loss` auf echter Hardware bestanden: WLAN-Verlust bei aktivem
Test im Flash erfasst, automatische Recovery, nachfolgender Neustart und
erhaltene CSV-Daten geprüft. Keine Simulation aktiv; GoodWe wieder OK.

Implementierungsgrundlage: [Espressif LittleFS API, Arduino 2.0.17](https://raw.githubusercontent.com/espressif/arduino-esp32/2.0.17/libraries/LittleFS/src/LittleFS.h).
