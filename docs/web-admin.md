# SolarPilot im Browser bedienen

## Öffnen und anmelden

Die Oberfläche läuft direkt auf dem ESP, ohne Cloud. Öffne seine lokale Adresse
im Browser. Die zuletzt ermittelte Adresse ist `http://192.168.178.194/`.
Die Adresse kann sich ändern; die aktuelle Adresse wird beim Start am USB-Monitor
angezeigt. Gerät und Browser müssen im gleichen erreichbaren Heimnetz sein.

Die Übersicht zeigt Einspeisung oder Netzbezug, Alter der Messung, zuletzt
bestätigten Steckdosenzustand und WLAN-Empfang. Bei fehlender Browserverbindung
werden alte Werte nicht als aktuelle Messung dargestellt.

Wähle **Administration**. Der Browser fragt nach Benutzername und Passwort.
Die lokalen Zugangsdaten stehen in `.local/admin-zugang.txt`. Diese Datei und
`include/config/LocalAdminCredentials.h` sind von Git ausgeschlossen. Zugangsdaten
nicht in Chat, Screenshots, Issues oder Protokolle kopieren.

Das Passwort wird für lokale Builds aus `LocalAdminCredentials.h` verwendet.
Ohne diese Datei bzw. mit leerem Passwort sind Adminzugang und Updates gesperrt.
Für ein weiteres Gerät gibt es `LocalAdminCredentials.example.h` als Vorlage.
Der bisherige WLAN-/Shelly-Zugang in `LocalCredentials.h` bleibt separat.

## Live-Protokoll

Hier stehen dieselben vom SolarPilot-Logger erzeugten Meldungen wie am seriellen
USB-Monitor: Messwerte, Schaltbefehle, Bestätigungen und Verbindungsversuche.
Treiber-/Boot-ROM-Ausgaben außerhalb dieses Loggers gehören nicht dazu.

Die letzten **64 Meldungen** werden im RAM gespeichert und alle zwei Sekunden
abgefragt. Während einer WLAN-Unterbrechung stoppt die Anzeige. Nach Rückkehr
werden die noch gespeicherten Meldungen angezeigt; ältere sind überschrieben.
Nach einem Neustart beginnt der Speicher neu. Die Zeiten sind Sekunden seit
Gerätestart, keine Uhrzeit. „Anzeige pausieren“ pausiert nur die Browseranzeige,
nicht die Regelung. Technische Meldungen bleiben zur Diagnose erhalten; die
Übersicht und die Bedienelemente erklären ihre Bedeutung auf Deutsch.

## Diagnose und Tests

Die Powerstation bleibt für die Tests von der Steckdose getrennt.

- **Verbindung prüfen:** WLAN-Informationen und begrenzte Netzwerkprüfungen ins
  Live-Protokoll schreiben. Ein geschlossener Port 80 am Router allein beweist
  keine fehlende IP-Erreichbarkeit.
- **Ein/Aus-Test:** einen kontrollierten Zyklus mit simuliertem Überschuss
  starten. Dabei kann das reale Relais schalten.
- **Sicherheitsabschaltung:** erst mit simuliertem Überschuss einschalten,
  dann fehlende GoodWe-Messwerte simulieren. AUS-Anforderung und Bestätigung im
  Protokoll prüfen.
- **WLAN-Unterbrechung:** WLAN-Verlust für etwa 40 Sekunden simulieren. Die
  Browseranzeige fällt absichtlich aus. Die Simulation endet am Gerät selbst,
  danach versucht es die Wiederverbindung.
- **Schaltfehler:** fehlende Shelly-Bestätigungen innerhalb eines Testzyklus
  simulieren. Für den Test wird keine positive Gerätebestätigung erfunden.
- **Alle Tests beenden:** Simulationen deaktivieren und frisches, bestätigtes
  AUS anfordern. Normale automatische Regelung wird danach fortgesetzt.

Webtests enden automatisch nach etwa zwei Minuten, der WLAN-Test nach etwa
40 Sekunden. Die Prüfung erfolgt im Control-Loop; blockierende Geräteabfragen
können diese Zeiten verlängern. Das ersetzt keinen Langzeittest spontaner Fehler.

## Neustart und Softwareupdate

Neustart und Update benötigen Anmeldung und eine Sicherheitskennung der
aktuellen Adminseite. Lade die Seite nach einem Geräteneustart neu.

Vor beiden Aktionen fordert SolarPilot ein **neues AUS am realen Shelly** an.
Ein lediglich zwischengespeicherter AUS-Zustand reicht nicht. Ohne erfolgreiche
Geräteantwort bleibt die Aktion abgelehnt und die AUS-Anforderung ausstehend.
Normale Überschusswerte können sie nicht aufheben.

Für ein Update wähle die für diesen ESP32-C3 gebaute Datei
`.pio/build/esp32-c3-supermini/firmware.bin` und **Software installieren**.
Während des Schreibens pausiert die Regelung erst nach bestätigtem AUS.
Stromversorgung nicht trennen. Nach Erfolg startet SolarPilot neu. Abgelehnte
oder abgebrochene Updates setzen die normale Regelung wieder fort.

Dieser erste Adminzugang verwendet HTTP-Digest im vertrauenswürdigen lokalen
Netz. Die Webseite und das Protokoll sind nicht per HTTPS verschlüsselt.
Keine Portfreigabe ins Internet einrichten. Bei nicht erreichbarem WLAN oder
nicht startender Firmware bleibt USB der Reparaturweg.

## Entwicklerprüfung

### Bisheriger Prüfstand

- Lokaler ESP32-C3-Build erfolgreich; Firmware per USB mit verifiziertem Hash
  übertragen. JavaScript- und Python-Syntax geprüft.
- Auf der ersten übertragenen Version: öffentliche Übersicht, Digest-Anmeldung,
  gesperrte Admin-/Log-/Update-Endpunkte ohne Anmeldung, Aktionsschutz,
  Diagnose, begrenztes JSON-Protokoll und Ablehnung ungültiger Firmware am
  echten ESP erfolgreich geprüft. Danach bestätigtes AUS.
- Der vervollständigte Stand einschließlich automatisch begrenztem WLAN-Test,
  zweistufigem Abschalttest und zusätzlicher AUS-Ablehnungsprüfung ist gebaut
  und per USB übertragen. Die Wiederholung der HTTP-Prüfung und der vollständige
  WLAN-Upload sind momentan durch fehlende ESP-WLAN-Verbindung blockiert.
- Der Treiber meldet zuletzt Abbruchgrund 2 (AUTH_EXPIRE); bei der vorherigen
  Verbindung wurden -86 bis -91 dBm gemessen. Das erklärt nicht allein die
  genaue Ursache. Diagnose erfasst nur einen Zahlwert in einem atomaren
  Speicher; Protokollierung erfolgt weiter ausschließlich im Hauptablauf.
- Die optische Browserprüfung ist noch offen: das Öffnen im eingebauten
  Browser endete mit einer Zeitüberschreitung. HTTP-Erreichbarkeit ist separat
  geprüft und darf nicht mit optisch verifiziertem Rendering gleichgesetzt
  werden.

Erst nach Prüfung des endgültigen Standes einschließlich verweigertem AUS und
echtem WLAN-Update ist die Webwartung vollständig hardwarevalidiert.

`tools/web-hardware-check.py` prüft reale HTTP-Endpunkte, Anmeldung,
Aktionsschutz, ungültige Firmware und Updateablehnung bei simuliert fehlender
AUS-Bestätigung. `--ota` installiert anschließend die lokal gebaute Firmware
über WLAN und prüft die Erreichbarkeit nach dem Neustart. Dieses Testwerkzeug
kann Simulationen auslösen und bestätigt AUS; nur ohne Verbraucher verwenden.
Es liest Zugangsdaten nur aus der lokalen Projektdatei und gibt sie nicht aus.

## Ergänzung 2026-10-03

Die Radioeinstellungen (8,5 dBm, WLAN-Energiesparen aus) werden nun beim
Verbinden und harten WLAN-Neustart gesetzt. Auf echter Hardware gelangen
WLAN-Verbindung, GoodWe-Messungen und sämtliche normalen Webprüfungen,
einschließlich Ablehnung bei fehlender AUS-Bestätigung. Ein späterer Start
zeigte zunächst Abbruchgrund 202 und danach erfolgreiche Wiederverbindung;
langfristige Zuverlässigkeit ist damit noch nicht bewiesen.

Große Übertragungen blieben im synchronen Multipart-Empfang hängen. Dessen
Warteschleife verhindert die bisherige Zeitprüfung im normalen loop().
Ein unabhängiger Timer startet nach 30 Sekunden ohne Uploadfortschritt neu,
aber nur für bereits nach bestätigtem AUS akzeptierte Updates. Im seriellen
Hardwaretest wurden Neustart, erneutes WLAN und GoodWe-Messungen beobachtet.
Die automatisierte Admin-Wiederkehrprüfung dieses Tests bestand noch nicht;
der Befund ist deshalb nicht als vollständig bestandener Test einzustufen.
Abgelehnte Uploads schließen zusätzlich sofort ihre Verbindung, damit auch
vor Aktivierung des Timers kein unbegrenzter Dateiempfang weiterläuft.

Die AP-Testoberfläche ist noch nicht extern validiert und nicht Bestandteil
normaler Firmware. Die normale Webansicht ist über die vom Router vergebene
Adresse erreichbar. Große WLAN-Updates bleiben bis zum erfolgreichen
abschließenden Hardwaretest offen. PR bleibt Entwurf; kein Produktionsnachweis.

### Neuer Hardwareprüfstand

Der korrigierte einzelne Teilupload-Test (`tools/ota-stall-check.py`) bestand:
nach absichtlich unvollständiger Übertragung neuer Bootlauf und geschützter
Adminbereich wieder erreichbar. Der Test prüft frühe Ablehnung, wartet genügend
für AUS-Bestätigung/Vorbereitung und verwendet nach Reset eine frische Sitzung.
Großer vollständiger Upload bleibt offen; mehr Übertragungswartezeit allein
behebt ihn nicht. Für die nächste Diagnose wird ein freier COM4 benötigt.

### Abschlusstest im Referenznetz

Nach intern gebauter, vom Nutzer geänderter lokaler WLAN-Konfiguration betrug
der Empfang -58 dBm. Vollständige Imageübertragung, Prüfung, Installation und
Wiederkehr des Adminbereichs gelangen. Eine doppelte Antwort bei bereits
abgelehntem Upload wurde verhindert; auch der normale, ungedrosselte Upload
besteht danach die Prüfung bis zur bestätigten Imageinstallation. Dieser
Hardwarebefund ergänzt die früheren Fehlerstände; Langzeitstabilität und
optische Browserprüfung sind damit nicht automatisch nachgewiesen.

Abschluss 2026-10-03: `tools/web-hardware-check.py --ota` vollständig bestanden,
einschließlich ungedrosseltem Vollupload und Admin-Anmeldung nach Neustart.
Der Prüfer verwirft dafür alte TCP-Verbindungen und Digest-Anmeldedaten.
WLAN-Suche und Wiederverbindung gelangten zu einem passenden Zugangspunkt mit
-62 dBm; zuvor war derselbe Netzname zeitweise mit deutlich schlechterem
Empfang verbunden. Dauerhaft optimale AP-Auswahl bleibt zu beobachten.
