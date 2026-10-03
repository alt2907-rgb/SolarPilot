# WLAN-Recovery-Untersuchung nach PR #39

## Ausgangsstand

Lokales und Remote-`main`: `b090713f42e2880bc114d71a342205da98166196`.
Der unveränderte Stand und die unten beschriebene Korrektur bauen lokal mit
PlatformIO Core 6.2.0 für `esp32-c3-supermini` erfolgreich.

## Codebefund

- Bei WLAN-Verlust bleibt die Fail-safe-Prüfung aktiv. GoodWe wird zurückgesetzt.
- Alle 10 Sekunden startet ein Recovery-Versuch. Die ersten beiden Aufrufe
  verwenden `WiFi.reconnect()`; der dritte startet die Station vollständig neu.
- Der Neustart verwendet `disconnect(false, false)`, `WIFI_OFF`, 100 ms Pause,
  `WIFI_STA` und `begin`. Die Zugangsdaten werden nicht gelöscht.
- Bei wiederhergestelltem WLAN wird der Versuchszähler zurückgesetzt.
- `TW` unterdrückt Recovery absichtlich. Es testet Verlustbehandlung und
  Wiederkehr nach Deaktivierung, aber nicht die Eskalation bei einem realen
  dauerhaft fehlschlagenden Reconnect.

## Sicherheitskorrektur der Diagnose

GoodWe-Retries blockieren nominal bis ca. 6,3 Sekunden plus Verarbeitung.
Danach liefen bisher zwei TCP-Probes mit je 1200 ms Timeout vor der
Fail-safe-Prüfung. Diese verwendete außerdem den Zeitwert vor dem Leseversuch.

Die Korrektur prüft mit aktuellem `millis()` vor der Diagnose und nochmals
danach. Ein ausstehendes physisch unbestätigtes Fail-safe-AUS verhindert die
Übernahme neuer Messwerte. Erfolgreiche Messwerte erhalten einen aktuellen
Zeitstempel. Probe-Timeouts sind auf je 400 ms reduziert; Gateway-Tests sind
ausdrücklich als TCP-Port-80-Tests bezeichnet. Ein geschlossener Port 80 beweist
keinen Verlust der allgemeinen IP-Erreichbarkeit.

Dies macht den Control-Loop noch nicht vollständig nichtblockierend. Der
30-s-Fail-safe kann weiterhin durch laufende GoodWe-Abfragen und Shelly-Aufrufe
verzögert werden. Physisches AUS benötigt eine erfolgreiche Shelly-Verbindung.

## Hardwarestatus und nächste Prüfung

PlatformIO erkennt COM4 mit USB VID:PID `303A:1001`. Der zunächst verweigerte
Zugriff wurde durch eine verbliebene PlatformIO-Monitorprozessgruppe verursacht:
nach gezieltem Beenden der drei Monitorprozesse ließ sich COM4 öffnen.

Die bisherige Firmware wiederholte mehrere harte WLAN-Neustarts ohne im
Beobachtungszeitraum wieder verbunden zu werden. Der Upload der Korrektur
erfolgte erfolgreich mit verifiziertem Flash-Hash. Der folgende Start meldete
zunächst ebenfalls fehlende WLAN-Verbindung. Bei der späteren Beobachtung am
2. Oktober 2026 liefen WLAN und echte GoodWe-Messwerte wieder; der genaue
Zeitpunkt und die Ursache der Wiederkehr wurden nicht aufgezeichnet.

Kontrollierte Tests auf der geänderten Firmware:
- `TW` aktiviert, länger als 40 Sekunden beobachtet, anschließend deaktiviert:
  WLAN und GoodWe wiederhergestellt, echte Messwerte, Health `OK`.
- `T100` bis zum bestätigten Shelly-EIN; anschließend `T-` und `TG` aktiviert:
  Diagnoseprobes, fünf Fehlerzyklen, GoodWe-Recovery und anschließend
  Fail-safe-AUS-Anforderung mit bestätigtem `[SHELLY] Steckdose AUS` beobachtet.
- `TG` deaktiviert und `T-`: echte Messwerte und Health `OK` wiederhergestellt.
- Alle verwendeten Testmodi sind beendet. Die Powerstation bleibt getrennt.

Die genaue physische Abschaltlatenz wurde nicht vermessen. Der Retry-Fall eines
nicht bestätigten Shelly-AUS wurde in dieser Testfolge nicht provoziert.

Neue Standort-Evidenz: während der GoodWe-Verlustsimulation lagen die
WLAN-Pegel wiederholt bei -86 bis -91 dBm. Das ist deutlich schwächer als die
früher dokumentierten Standorte. Ein gezielter Vergleich näher am AP ist sinnvoll;
die Beobachtung beweist aber keine alleinige Ursache der spontanen Ausfälle.

Nach Freigabe des Ports zuerst die bestehende Firmware beobachten. Für einen
kontrollierten Verlusttest: `TW` EIN, 40 Sekunden beobachten, `TW` AUS und
WLAN-/GoodWe-Wiederkehr sowie ausstehendes Fail-safe-AUS prüfen. Die Powerstation
bleibt getrennt. Der Test muss immer deaktiviert werden. Ein erfolgreicher
kontrollierter Test ersetzt keinen Langzeittest des spontanen Fehlers.

Ein Hard-Recovery-Erfolg darf erst anhand realer Logs mit Neustartmeldung,
anschließender WLAN-Wiederkehr und wieder erfolgreichen GoodWe-Messungen
behauptet werden. Keine zusätzlichen automatischen Discovery-Aktivitäten
einführen.

## Isolierter WLAN-Empfangstest (2026-10-02)

Ein separates Minimalprogramm unter `.local/wifi-scan` wurde per USB gebaut
und geflasht. Es verwendet keine Zugangsdaten und ruft kein `WiFi.begin()` auf.
Nur STA-Modus, deaktivierte automatische Wiederverbindung und manuelle Scans.
Ausgabe enthält ausschließlich Anzahl, RSSI und Kanal, keine Netzwerknamen.
Mehrere Läufe fanden 4 bis 9 Funknetze, stärkster Empfang -64 bis -68 dBm.
Damit funktioniert der Scan-/Empfangspfad grundsätzlich. Dies beweist weder
Sichtbarkeit des konfigurierten Zielnetzes noch erfolgreiche Authentifizierung.
Anschließend wurde die normale SolarPilot-Firmware wieder übertragen.

## Sendeleistungs-Vergleich (2026-10-02)

Das WPA2-Einrichtungs-WLAN war laut Nutzer am Handy nicht sichtbar, obwohl
AP-Konfiguration, sichtbare SSID, Kanal 6 und 20 dBm vom Treiber bestätigt wurden.
Ein reines AP-Minimalprogramm ohne SolarPilot/Webserver auf Kanal 1 und mit
deaktiviertem Stromsparmodus blieb ebenfalls unsichtbar. Im selben Minimaltest
wurde ausschließlich die maximale Sendeleistung auf 34 Viertel-dBm (8,5 dBm)
reduziert; Setzen/Lesen lieferten Erfolg. Danach bestätigte der Nutzer erstmals
Sichtbarkeit. Das ist Evidenz für eine Abhängigkeit von der Sendeleistung, aber
kein Beweis der konkreten Hardware-/Versorgungs-/Treiberursache. Empfangsscans
funktionierten bereits zuvor. STA-Anmeldung und stabiler Betrieb mit reduzierter
Leistung sind noch zu prüfen. Die separate Testoberfläche übernimmt 8,5 dBm.

## Bereinigter HTTP-Vergleich (2026-10-03)

Offizielles Referenzbeispiel der verwendeten Arduino-Version:
https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/WiFi/examples/WiFiAccessPoint/WiFiAccessPoint.ino

`tools/ap-http-probe` ist ein separater WiFiServer-HTML-Test auf Port 8080,
Kanal 1, 8,5 dBm, ohne JavaScript, WebServer-Bibliothek, Regler oder Heimnetzkeys.
Anfragen werden verworfen, niemals ausgegeben; Kopfzeilen sind auf 4096 Bytes
und drei Sekunden begrenzt. Exakte Content-Length, kurze verzögerte Trennung.
`B` im seriellen Monitor prüft intern eine vollständige HTTP- und HTML-Antwort.
Build und Upload erfolgreich. Interner Hardwaretest: 124/124 Kopfzeilenbytes,
324/324 HTML-Bytes, HTTP 200 und vollständige Testkennung bestätigt.
Dies validiert nicht die tatsächliche Funkübertragung zum Handy. Externer
HTML-Aufruf steht noch aus. Der AP-Test endet nach 15 Minuten automatisch.

## Direkte WLAN-Testoberfläche (2026-10-03)

Der Nutzer bestätigte HTML-Zugriff vom Android auf `tools/ap-http-probe`.
Darauf aufbauend erstellt `tools/wifi-portal` WLAN-Auswahl, verdeckte POST-Eingabe
und 30-s-Diagnose ohne dauerhafte Zugangsdaten-Speicherung. Keine WebServer-
Bibliothek, kein JavaScript. Asynchroner Scan, kurze begrenzte HTTP-Anfragen,
HTML-Escaping, Tokenprüfung, Validierung von SSID und Schlüssellänge.
Ein frisches millis() nach der HTTP-Verarbeitung verhindert einen vorzeitigen
Testabschluss durch einen älteren Zeitstempel. 8,5 dBm auch im STA-Test gesetzt.
Build/Upload erfolgreich. Drei interne Hardwarechecks: HTML 1442/1442 Bytes,
HTTP 200 und Formular vorhanden; ungültige Testeingabe HTTP 400; übergroße
Content-Length HTTP 400. Alle Antworten vollständig. Reale WLAN-Anmeldung
über das Formular steht noch aus; kein Produktionsfreigabe-Nachweis.

## Vollständiger Netzwerkpfad-Vergleich (2026-10-03)

Die normale SolarPilot-Firmware verwendete bisher die Standard-Sendeleistung.
Mit 8,5 dBm nach WiFi.begin() und nach jedem harten STA-Neustart sowie
WiFi.setSleep(false) verband sie sich bei diesem Hardwarelauf direkt wieder.
GoodWe-Messwerte wurden gelesen; öffentliche Übersicht, Digest-Anmeldung,
Aktionsschutz, begrenzte Logs, ungültiges Update und Updateablehnung bei
fehlender AUS-Bestätigung bestanden den Hardwaretest. RSSI weiterhin ca.
-86 bis -88 dBm. Beide Radioeinstellungen wurden gemeinsam geändert; daraus
folgt kein Nachweis, welche einzeln ausschlaggebend ist, und keine Zusicherung
langfristiger WLAN-Recovery.

Primärquellen für die Radio-Hypothese:
- https://www.wemos.cc/en/latest/c3/c3_mini_1_0_0.html
  (anderes C3-Board, expliziter Hinweis auf 8,5 dBm)
- https://github.com/sigmdel/supermini_esp32c3_sketches
  (Vergleich von SuperMini-Platinen mit unterschiedlicher Sendeleistung)

Der Portal-Fehler bleibt separat offen: Bei einem verbundenen Handy wurde
keine externe TCP-Verbindung angenommen; interne Selbsttests prüfen nur den
lokalen Stack. Auch der direkte WiFiServer ist betroffen, deshalb ist eine
alleinige Schuld der WebServer-Bibliothek für diesen AP-Fehler nicht belegt.

Ein großes OTA-Update blieb nach dem bestätigten AUS im Dateiempfang hängen.
Die Arduino-2.0.17-Implementierung wartet in _uploadReadByte ohne Zeitlimit,
solange der Client als verbunden gilt. Die bisherige Prüfung nach handleClient
kann diesen Zustand nicht begrenzen. Ein unabhängiger esp_timer überwacht
nun atomar die Upload-Aktivität: nach 30 s Stillstand Neustart. Er wird nur
nach physisch bestätigtem AUS und erfolgreicher Timerinitialisierung aktiviert.
Unvollständige Images werden nicht als Bootpartition ausgewählt. Logger und
Update werden nicht aus der Timer-Task aufgerufen.
Quelle: https://github.com/espressif/arduino-esp32/blob/2.0.17/libraries/WebServer/src/Parsing.cpp

Setup-Bibliothek WiFiManager wurde als etablierte Alternative geprüft, aber
nicht eingebunden: deren Speichern/Verbinden-Pfad muss zuerst dem ausdrücklich
nur temporären Schlüsselgebrauch angepasst werden. Kein blindes Übernehmen
eines Standardportals mit dauerhafter Speicherung oder Passwort-Debugausgabe.

Beim letzten vollständigen OTA-Versuch antwortete der reale Shelly-AUS-Befehl
mit HTTP -11 (Timeout). OTA wurde deshalb vor dem Schreiben korrekt abgelehnt.
Das ist kein erfolgreicher großer Upload und kein erneuter Upload-Hänger.
Die bisherigen Radioerfolge sind Momentaufnahmen; schlechter RSSI und
gelegentliche Gerätetimeouts bestehen weiterhin.

## Einzeltests und Testwerkzeugkorrekturen (2026-10-03)

Der vollständige Upload wurde erneut allein geprüft. Normale Web- und
Sicherheitsprüfungen bestanden; großer Upload endete mit ReadTimeout. Auch
120 Sekunden Übertragungswartezeit statt 30 Sekunden führten zu ConnectionError.
Danach war der Admin erreichbar; das Protokoll zeigte einen neuen Bootlauf.
Mehr Wartezeit im Test ist somit keine ausreichende Lösung.

Der gezielte Teilupload-Test wurde korrigiert: frühe HTTP-Ablehnung erkennen,
60 Sekunden offen halten (AUS-Bestätigung/Updatevorbereitung berücksichtigen),
nach Reset eine frische HTTP-/Digest-Sitzung verwenden. Der einzelne Hardwarelauf
bestand anschließend vollständig: neue Bootkennung und Admin wieder erreichbar.
Keine gleichzeitigen anderen HTTP-Tests während dieses Laufs.

USB-Diagnose derzeit nicht möglich: COM4 meldet Zugriff verweigert/belegt.
Für den nächsten großen Upload müssen die seriellen Fortschritts-/Abbruchmeldungen
beobachtet werden. Keine weiteren identischen Uploadversuche ohne diese Evidenz.
