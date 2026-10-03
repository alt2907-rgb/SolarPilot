# Geräteoberfläche und Bedienprüfung

## Vorbild und eigener Aufbau

Als Bedienvorbild dient die offizielle lokale Shelly-Oberfläche:
https://kb.shelly.cloud/knowledge-base/shelly-1pm-gen4-web-interface-guide
Sie trennt Gerätestatus, WLAN und Wartung über eine Navigation. Auch das
my-PV-Handbuch trennt Betriebsansicht und Einstellungen:
https://www.my-pv.com/de/manuals/acthor-acthor-9s/
SolarPilot verwendet eigene Farben, Texte und Komponenten. Keine fremden
Grafiken oder proprietären Oberflächendateien übernommen.

## Bereiche

- Übersicht: Hausanschlussleistung, zuletzt bestätigter Ausgang und Empfang.
- Geräte & Regelung: vorhandene Quelle und Steckdose sowie echte Schaltwerte
  aus der Firmware. Aktuell nur lesbar, keine vorgetäuschte Konfiguration.
- WLAN: Start, Netzauswahl, 30-Sekunden-Test und bewusste Übernahme.
- Aufzeichnung: Speicherzustand und Downloads tatsächlich vorhandener Dateien.
- Wartung: Verbindungsprüfung, sicherer Neustart, Update und getrennte Tests.
- Ereignisse: begrenztes Live-Protokoll und erklärte Diagnosezahlen.

Adminbereiche bleiben serverseitig geschützt. Öffentliche Ansicht bietet nur
Übersicht und Geräte/Regeln. Navigation per URL-Fragment lädt keine neue Seite.
Handybreite zeigt alle Bereiche als zweispaltige Navigation. Beschriftete
Formfelder, Tastaturfokus, Statusmeldungen und eigene deutsche Bestätigungen
unterstützen einfache Bedienung.

## Korrigierte Anzeigeprobleme

Bei HTTP-Ausfall gelten Steckdose, Empfang und Messaktualität als unbekannt.
Einrichtungsmodus und Simulation sind getrennte API-Zustände. Ein fehlender
Messwert wird nicht als aktuelle Nullleistung ausgegeben. Ein vorbereitetes
GoodWe-Ende ist kein Nachweis einer aktuellen Messung. RSSI beschreibt Empfang,
nicht Netzwerkstabilität. Regeln stammen aus AppConfig, keine UI-Parallelwerte.
Leere Historiensegmente haben keine irreführenden Downloadlinks.

WLAN-Felder und Aktionen werden nur im passenden Modus aktiviert. Nach Änderung
eines Kandidaten ist ein neuer Test nötig; Erfolg eines früheren Kandidaten
darf den Speichernknopf nicht aktivieren. Der Schlüssel wird beim Absenden aus
dem Eingabefeld entfernt. Netzwerksuche fasst gleiche WLAN-Namen zusammen.
Schalt-/Updateaktionen sind während Einrichtung gesperrt. Alle Webaktionen
haben Zeitgrenzen; Mutationen werden nicht automatisch erneut gesendet.

## Grenzen und Prüfungen

Kein Cloudkonto, keine fremden Geräteadapter, keine erfundenen Ertragskurven,
kein automatischer Softwaredownload. CSV bleibt ein technischer Export mit
Gültigkeitsflags; die Seite erklärt Umfang und Zeitbasis. Offenes Einrichtungs-
WLAN und fehlendes TLS bleiben als bestehende Nutzerentscheidung dokumentiert.

Node-Prüfer tools/web-ui-check.js prüft die echte eingebettete JS-Logik gegen
DOM/API-Fixtures: veraltete Werte, Modi, Schaltregeln, Aktionen und Navigation.
Lokale Browservorschau verwendet ausschließlich fiktive Daten; sie ersetzt
weder echten AP-Handytest noch Hardware-/OTA-Checks. Prüfstände werden nach
tatsächlicher Durchführung in product-development.md ergänzt.
