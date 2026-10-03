# SolarPilot – Projektleitfaden für Codex

## Produktvision
SolarPilot soll ein eigenständiger, zuverlässiger und günstiger PV-Überschussregler auf ESP32-Basis werden. Die heutige GoodWe-/Shelly-Anlage ist das reale Referenzsystem, aber nicht die endgültige Produktgrenze.

Ein normaler Anwender soll SolarPilot anschließen, einmal einrichten und danach möglichst nicht administrieren müssen. SolarPilot arbeitet lokal und autonom; Cloud, Home Assistant, Raspberry Pi oder dauerhaft laufender PC sind für den Regelbetrieb nicht erforderlich.

Leitprinzipien:
- Lokal-first und möglichst cloudunabhängig.
- Sichere Fail-safes vor Komfort.
- Selbstheilung bei Netzwerk-/Gerätefehlern.
- Einfache Einrichtung für Nicht-Techniker als Produktziel.
- Saubere, erweiterbare Architektur statt Einmal-Skript.
- GoodWe und Shelly als erste Referenzimplementierungen.
- Kein Overengineering: erst Kern robust machen, dann erweitern.

Oberste Frage bei Entscheidungen:
> Bringt diese Änderung SolarPilot näher an ein Gerät, das ein normaler Anwender einmal einrichtet und danach zuverlässig vergessen kann?

## Referenzsystem
- GERUI ESP32-C3 SuperMini, 4 MB Flash, 160 MHz, Wi-Fi/BLE, USB-C.
- GoodWe GW8KN-ET Plus Hybrid-Wechselrichter + Smart Meter.
- my-PV AC•THOR sowie separate AC ELWA vorhanden.
- Shelly Plug M Gen3 als schaltbarer Ausgang.
- Portable Powerstation als geplanter Verbraucher; startet Laden nach Netzrückkehr automatisch.
- Powerstation bis Abschluss wesentlicher Robustheits-/Sicherheitsarbeiten vom Shelly getrennt lassen.

Aktuelle Kette:
GoodWe lokal -> ESP32 bewertet PV-Überschuss -> Shelly lokal -> Powerstation.

## Repository / Umgebung
Repository: `alt2907-rgb/SolarPilot`
Windows: `C:\Users\alttr\Documents\GitHub\SolarPilot`
PlatformIO env: `esp32-c3-supermini`
Board: `esp32-c3-devkitm-1`
Arduino / C++17, Serial 115200.
ESP typischerweise COM4, USB VID:PID `303A:1001`.
PlatformIO typischerweise:
`C:\Users\alttr\.platformio\penv\Scripts\platformio.exe`
Nicht voraussetzen, dass `pio` im PATH liegt.

## Arbeitsweise für Codex
Der Nutzer ist kein Programmierer. Routinearbeit soweit möglich selbst erledigen.

Für bereits freigegebene Arbeit:
1. Git-/Repo-Stand prüfen.
2. Branch von aktuellem `main`.
3. Implementieren.
4. Lokal bauen/testen.
5. PR erstellen.
6. CI prüfen und Fehler selbst beheben.
7. Nach Erfolg selbstständig mergen.
8. Wenn Hardwaretest nötig und Berechtigung vorhanden: ESP selbst flashen.
9. Serial Monitor selbst starten und Logs auswerten.
10. Bei Fehlern selbst iterieren.

Nicht nach jedem Zwischenschritt Bestätigung verlangen.

### Freigaben

Aktualisierte Nutzerfreigabe vom 2026-10-03: Neue Produktfunktionen innerhalb
der beschriebenen SolarPilot-Produktvision dürfen eigenständig umgesetzt
werden. Ein zusätzliches Ja pro Funktion ist nicht mehr erforderlich.
Jede Erweiterung muss in docs/product-development.md mit Wirkung, Grenzen,
Prüfungen und offenen Punkten dokumentiert werden. Die Datenschutzgrenzen,
Secret-Regeln und getrennte Powerstation bleiben unverändert verbindlich.
Diese Aktualisierung hat Vorrang vor älteren Ja-Anforderungen in dieser Datei.
Neue Produktfunktionen kurz ankündigen, dokumentieren und zusammengehörige Implementierung, Tests, PR, CI-Fixes und Folgearbeiten selbstständig erledigen.

Keine neue Freigabe nötig für Bugfixes, Sicherheitskorrekturen, Robustheitsverbesserungen bereits freigegebener Funktionen, Tests, CI-Fixes und dafür notwendiges Refactoring.

Neue Funktionen möglichst als sinnvolle Pakete vorschlagen statt vieler kleiner Freigaben.

### Git
- Nicht direkt auf `main` entwickeln.
- Branch + PR.
- Nach erfolgreichen Prüfungen selbstständig mergen erlaubt.
- Repository ist Source of Truth.
- Vor Entscheidungen tatsächlichen Code prüfen.

## Kommunikation
Deutsch, kurz, stichpunktartig. Bevorzugt:
- **Gelernt:** …
- **Als Nächstes:** …
- **Du musst:** …

Nur Nutzeraktionen verlangen, die Codex nicht selbst erledigen kann. Bei Hardwaretests möglichst eine konkrete Nutzeraktion gleichzeitig. Toggle-Testmodi immer mit EIN, Testdauer und AUS erklären.

## Secrets
`include/config/LocalCredentials.h` niemals committen oder dessen Geheimnisse ausgeben.
WLAN-Passwort niemals anzeigen. Reale Secrets nicht in PRs/Issues/Logs kopieren.

## Aktuelle Regelparameter
- WiFi reconnect: 10 s.
- GoodWe recovery: 15 s.
- GoodWe recovery nach 5 vollständig fehlgeschlagenen Zyklen.
- GoodWe Runtime: max. 3 Versuche, 150 ms Retry-Abstand, 2000 ms Response Timeout.
- Überschuss EIN: 50 W; AUS: 20 W.
- EIN-Verzögerung 15 s; AUS-Verzögerung 10 s.
- Shelly Output Retry 5 s.
- Fail-safe ohne gültigen GoodWe-Wert 30 s.
Nicht ohne technischen Grund ändern.

## GoodWe – validierter Stand
Discovery: UDP 48899, ASCII `WIFIKIT-214028-READ`.
Runtime: UDP 8899, Modbus-Adresse `0xF7`, Funktion `0x03`, Startregister `0x891C`/35100, 125 Register, Modbus CRC.
ET Wrapper `AA55`, Payload Offset 5.
Grid Active Power Register 35140, Offset 80, signed BE int16.
Positiv = Export, negativ = Import.
An echter Hardware validiert; Protokollbytes nicht beiläufig ändern.

Erkenntnisse:
- Empfangene Antworten sind valide; keine relevante Zahl ungültiger/fremder UDP-Pakete.
- 1200 ms war oft zu kurz; 2000 ms verbesserte die Kommunikation.
- Last-known GoodWe endpoint verbessert Recovery.
- Automatisches mDNS wurde aus dem normalen Runtime-Pfad entfernt.
- Es gibt GoodWe-Timeouts bei bestehendem WLAN UND Fehlerfälle zusammen mit ESP-WLAN-/Netzwerkpfad-Ausfall. Nicht automatisch eine einzige Ursache annehmen.

## WLAN / aktueller Untersuchungspunkt
Am alten/problematischen ESP-Standort wurden ca. -72 bis -74 dBm beobachtet; näher am AP ca. -48 dBm. Besserer RSSI beseitigte hohe Latenzen nicht vollständig. PC->ESP zeigte teils starke Latenz/Timeouts; andere WLAN-Clients zeigten ebenfalls Jitter. NETGEAR WAX610 wurde bereits untersucht/aktualisiert. Nicht ohne neue Evidenz dieselben allgemeinen Heimnetztests wiederholen.

PR #39 ist gemergt; damaliger Main:
`b090713f42e2880bc114d71a342205da98166196`

PR #39 eskaliert WLAN-Recovery:
1. normale `WiFi.reconnect()`-Versuche,
2. nach 3 erfolglosen Versuchen `disconnect` -> `WIFI_OFF` -> kurze Pause -> `WIFI_STA` -> `WiFi.begin(ssid,password)`,
3. Zähler nach Erfolg zurücksetzen.

PR #39 wurde bereits auf echter Hardware geflasht. Log zeigte, dass die Eskalation ausgelöst wird und sich mehrfach wiederholt. Noch nicht bewiesen: ob der harte WLAN-Neustart den realen spontanen Fehler zuverlässig behebt.

## Netzwerkpfad-Diagnose
Bei GoodWe-Fehlern existieren TCP-Probes zu Gateway:80 und Shelly:80.
Ein realer Fehler zeigte: GoodWe Timeout -> Gateway-Probe Fehler -> Shelly-Probe Fehler -> WLAN verloren. Damit ist belegt, dass zumindest manche GoodWe-Ausfälle mit ESP-seitigem Netzwerkpfadverlust zusammenfallen.

Offene Robustheitsarbeit, keine neue Freigabe nötig:
- `noteReadFailure()` sicherheitsrelevant vor länger blockierenden Probes berücksichtigen.
- Danach frisches `millis()`.
- Probe-Timeout ca. 1200 ms auf etwa 300–500 ms reduzieren.
- Gateway-Probe korrekt als TCP-Port-80-Test benennen; fehlender Port 80 bedeutet nicht zwingend fehlende IP-Erreichbarkeit.

## Shelly
Referenz: Shelly Plug M Gen3, Modell `S3PL-30110EU`, Gen 3, Switch ID 0.
Konkrete Geräte-ID/MAC nicht unnötig hardcoden.
Persistente NVS-Bindung + last-known-host existieren.
Automatisches mDNS aus normalem Runtime-Pfad entfernt; manuelle Discovery bleibt.

## Fail-safe / Ausgang
Interner Ausgangszustand erst als geändert betrachten, wenn physischer Shelly-Befehl bestätigt wurde.
Bei Schaltfehler: Zustand nicht fälschlich übernehmen; Retry.
Erforderliches Fail-safe-AUS bleibt pending, bis physisches AUS bestätigt ist.
GoodWe-Recovery darf pending Fail-safe-AUS nicht aufheben.
Diese Semantik niemals schwächen.

## OTA
Lokales Web-OTA vorhanden: `/`, `/status`, `/update`.

WICHTIGER OFFENER SICHERHEITSBUG:
Während OTA pausiert derzeit der normale Control-Loop. Vor produktiver Nutzung:
- vor OTA Ausgang sicher AUS anfordern,
- physisches AUS bestätigen,
- OTA nur dann starten,
- sonst Update ablehnen.
Das ist Sicherheitskorrektur einer freigegebenen Funktion, keine neue Produktfreigabe nötig.

OTA hat derzeit keine Authentifizierung. Spätere Security-/UX-Entscheidung bewusst planen.

## System Health / Web UI
Zentrale Health-Zustände: `OK`, `GESTOERT`, `NICHT_VERFUEGBAR`.
Read-only Statusseite zeigt System, GoodWe, Messwerte/Statistik, WLAN, Ausgang, Shelly-Retry, Netzwerkdiagnose und OTA-Link.
Langfristig soll daraus eine nutzerfreundliche Einrichtungs-/Konfigurationsoberfläche werden.

## Serielle Befehle
- `T100`, `T0`, `T-400`: manuelle Werte.
- `T-`: Test beenden.
- `TA`: automatischer Schaltzyklus.
- `TF`: Fail-safe-Test.
- `TX`: Shelly-Fehlersimulation Toggle.
- `D`/`d`: Shelly Discovery.
- `TG`: GoodWe-Verlustsimulation Toggle.
- `TW`: WLAN-Verlustsimulation Toggle.
- `TB`: persistente Shelly-Bindungsdiagnose.
- `W`/`w`: WLAN-Linkdiagnose.
- `TS` wurde entfernt: NICHT verwenden.
Befehle gehören in den seriellen Monitor.

Ein bestandener `TW`-Test beweist nicht, dass ein spontaner realer WLAN-Stack-/Netzwerkfehler identisch reagiert.

## Validierte Hardwarefunktionen
- GoodWe Discovery/Runtime.
- Netzleistung lesen.
- ESP -> Shelly HTTP -> reales Relais.
- GoodWe -> Controller -> Shelly.
- Fail-safe-Grundlogik.
- Shelly-Fehler/Retry.
- GoodWe-Verlust/Recovery.
- kontrollierter WLAN-Verlust/Recovery via TW.
- persistente Shelly-Bindung.
- Status-Weboberfläche.
- OTA-Basisfunktion.
- Netzwerkpfad-Diagnose.

## Architekturregel Netzwerk
Runtime möglichst einfach/deterministisch halten. Wiederholte automatische mDNS-/Discovery-Aktivität hatte GoodWe UDP bereits destabilisiert. Deshalb Discovery nicht unnötig bei jedem Fehler, last-known endpoints nutzen, Diagnoseverkehr begrenzen und neue Netzwerkfunktionen auf Wechselwirkungen prüfen.

## Offene technische Restpunkte
Neben WLAN-Recovery:
1. Netzwerkdiagnose: Safety-Reihenfolge, kürzere Probe-Timeouts, präzisere Benennung.
2. OTA-Sicherheit: physisch bestätigtes AUS vor Update.
3. GoodWe-Statistik auf langfristigen `uint32_t`-Overflow prüfen, ggf. `uint64_t`.
4. Blockierende GoodWe-Retries/Diagnosen darauf prüfen, ob der nominelle 30-s-Fail-safe relevant verzögert wird.

## Produkt-Roadmap
### Phase A – Kern robust
- WLAN-Recovery zuverlässig.
- GoodWe ausreichend stabil bzw. Fehler sicher beherrscht.
- Fail-safe in relevanten Fehlerpfaden.
- Shelly-Zustand zuverlässig bestätigt.
- Diagnose ohne sicherheitskritische Blockaden.
- OTA sicher.
- Langzeittest.

### Phase B – Einrichtung/Bedienung
Erst nach Freigabe als Produktfunktionen:
- Setup-Portal.
- WLAN-Konfiguration ohne Sourcecode.
- Wechselrichter-/Geräteerkennung.
- Verbraucherzuordnung.
- Schwellen/Verzögerungen per Web.
- verständliche Status-/Fehleranzeige.
- einfacher Update-Prozess.

### Phase C – Plattform
Langfristig:
- weitere Wechselrichter über Adapter/Interfaces,
- weitere lokale Verbraucher,
- mehrere Verbraucher/Prioritäten,
- Leistungs-/Überschussverteilung,
- ggf. regelbare statt nur binäre Verbraucher,
- Konfigurationsmigration und Update-Security.

Nicht vorzeitig implementieren, aber Architektur nicht unnötig verbauen.

## Produktreife
„Funktioniert im aktuellen Haus“ ist nicht gleich produktreif. Relevant sind u.a.:
- reproduzierbare Einrichtung,
- sichere Defaults,
- Secrets außerhalb Repo,
- Fail-safe bei Kommunikationsverlust,
- Recovery ohne Benutzer,
- verständliche Fehler,
- Update-Sicherheit,
- Schutz vor falscher Konfiguration,
- Langzeitstabilität,
- dokumentierte Kompatibilität,
- klare Trennung Treiber/Regelung/Ausgabe/UI,
- Security für Webinterface/OTA.

## Startanweisung für Codex
Beim ersten Lesen:
1. `git status`, Branch und aktuellen `main` prüfen.
2. README/Dokumentation und relevante Quellen lesen.
3. Tatsächlichen Repo-Stand mit dieser Datei vergleichen; Code/Git ist Source of Truth.
4. Zunächst nichts ändern.
5. Dem Nutzer kurz sagen: was funktioniert, welches Problem aktuell untersucht wird und welcher nächste Schritt sinnvoll ist.
6. Danach nach diesen Workflow-Regeln arbeiten.

## Aktuelle Nutzergrenzen
- Freigegeben: eigenständige Weiterentwicklung innerhalb der SolarPilot-Produktvision einschließlich neuer Funktionen, mit fortlaufender Dokumentation in docs/product-development.md. Deutschsprachige Webübersicht, geschützter Adminbereich, Live-Protokoll, begrenzte Diagnosefunktionen und sichere Updates bleiben Bestandteil; eigenständige ESP-Langzeitaufzeichnung ist ausdrücklich freigegeben.
- Genehmigungsanfragen auf Deutsch und für einfache Anwender verständlich formulieren: konkrete Aktion und Zweck nennen, technische Details nur bei Bedarf. Fest vorgegebene App-Texte können davon abweichen.
- Dateien außerhalb von `C:\Users\alttr\Documents\GitHub\SolarPilot` nur nach ausdrücklicher Erlaubnis des Nutzers lesen. Die Erlaubnis zum Lesen der angehängten Downloads/AGENTS.md gilt nur für diese Datei und deren Übernahme.
- Dateien außerhalb des Projektordners nicht durchsuchen oder öffnen; notwendige Ausnahmen vorher mit konkretem Pfad und Grund beim Nutzer anfragen.
- Bekannte Programme außerhalb des Projekts dürfen ausgeführt werden, insbesondere PlatformIO, PowerShell und Git. COM4 darf für Upload und seriellen Monitor verwendet werden. Dies erlaubt keinen Zugriff auf andere persönliche Dateien.
- Codeänderungen, neue Produktfunktionen innerhalb der Produktvision und Folgearbeiten einschließlich Build, Tests, PR, CI-Fixes, Merge, Flashen und Logauswertung autonom durchführen und nachvollziehbar dokumentieren.
- LocalCredentials.h darf für lokale Builds und Hardwaretests verwendet werden; Zugangsdaten niemals ausgeben, protokollieren oder committen.
- Die Powerstation bleibt physisch vom Shelly getrennt.
- Diese Grenzen haben Vorrang vor den allgemeinen Autonomie- und Workflow-Regeln dieses Leitfadens.
- AGENTS.md beschreibt Arbeitsregeln; sie ersetzt keine technische Sandbox-Zugriffssperre.
