# SolarPilot

Lokale ESP32-C3-Anwendung zur Kommunikation mit GoodWe-Wechselrichtern, aktuell technisch validiert für **GoodWe ET / ET Plus**.

## Meilenstein 4A (implementiert)

- Automatische Erkennung kompatibler Shelly-Geräte im lokalen Netzwerk per mDNS (`_shelly._tcp`)
- Für gefundene Geräte lokaler Abruf von Geräteinformationen über Shelly RPC `Shelly.GetDeviceInfo` (HTTP, lokal)
- Erfasst: IP/Host, Geräte-ID, MAC-Adresse, Modell und Generation (soweit vom Gerät geliefert)
- Rein informativ: läuft einmalig beim Start **nach** erfolgreicher GoodWe-Initialisierung, gibt die gefundenen Geräte im Serial Monitor aus
- Entwicklungs-Testhilfe: `D` (oder `d`) über den Serial Monitor senden führt die Discovery jederzeit erneut aus – kein periodisches Polling, keine Auswirkung auf GoodWe-/Surplus-Ablauf
- **Robustheit:** mDNS (`MDNS.begin()`) wird pro WLAN-Verbindung nur genau einmal initialisiert (nicht bei jedem Discovery-Aufruf erneut) – wiederholtes Initialisieren hat sich auf echter Hardware als Ursache für sporadisch fehlschlagende GoodWe-Broadcast-Discovery erwiesen. GoodWe wird beim Start außerdem zuerst initialisiert; die Shelly-Discovery folgt erst danach und entkoppelt
- Kein Shelly Cloud, kein MQTT, keine Internetabhängigkeit
- Keine Weboberfläche, keine dauerhafte Speicherung/Konfiguration und **keine** automatische Umschaltung von `ShellyPlugOutput` in diesem Schritt
- Die bestehende feste Shelly-IP-Konfiguration (Meilenstein 3) bleibt unverändert nutzbar; Discovery ist rein additiv

## Meilenstein 3 (implementiert)

- Lokale Steuerung eines **Shelly Plug M Gen3** über WLAN/LAN
- Kein Shelly Cloud-Konto erforderlich, kein MQTT
- Steuerung per lokalem Shelly Gen3 RPC/HTTP-API (`Switch.Set`)
- `VirtualSocketOutput` bleibt als sicherer Testmodus erhalten
- Standardmäßig ist VirtualSocketOutput aktiv – erst nach expliziter Konfiguration wird ein reales Gerät geschaltet

## Meilenstein 2 (implementiert)

- WLAN verbinden
- GoodWe im lokalen Netzwerk finden
- Verbindung vorbereiten
- aktuelle Netzleistung lesen
- Werte über Serial Monitor ausgeben
- simulierte Überschuss-Schaltung mit Hysterese und Zeitqualifikation

## Projektstruktur

- `/src/core` und `/include/core`: Basisdienste (Logging, WLAN)
- `/src/inverter` und `/include/inverter`: Inverter-Abstraktion und GoodWe-Implementierung
- `/src/output` und `/include/output`: Ausgabe-/Laststeuerungs-Schicht (`VirtualSocketOutput`, `ShellyPlugOutput`)
- `/src/discovery` und `/include/discovery`: Meilenstein 4A – lokale Shelly-mDNS-Discovery (`ShellyDiscovery`, `ShellyDeviceInfo`)
- `/include/web`: Platzhalter für spätere Weboberfläche
- `/include/config`: zentrale Konfiguration

## Designentscheidungen

- **Interface vor Implementierung (`IInverterClient`)**: bereitet die spätere Unterstützung weiterer Wechselrichter vor, ohne den Anwendungsfluss in `main.cpp` ändern zu müssen.
- **Klare Verantwortlichkeiten je Modul**: WLAN-Handling, Inverter-Protokoll und Ausgabe sind getrennt und dadurch wartbar/testbar.
- **Konfiguration zentral in `AppConfig`**: reduziert verstreute Magic Numbers und vereinfacht OTA-/Web-Konfiguration im nächsten Schritt.
- **GoodWe ET / ET Plus lokal per UDP**: Discovery über Broadcast (`48899`) und Laufzeitdaten über lokalen GoodWe-Port (`8899`) per Modbus RTU over UDP ohne Cloud- oder Internetabhängigkeit.
- **`ISwitchOutput`-Abstraktion**: `SurplusSwitchController` kennt weder `VirtualSocketOutput` noch `ShellyPlugOutput` – die Auswahl erfolgt in `main.cpp` anhand der Konfiguration.
- **`ShellyPlugOutput` für mehrere Instanzen ausgelegt**: Host, Switch-ID und Timeout sind Konstruktorparameter; mehrere Shelly-Geräte können später ohne Code-Änderungen instanziiert werden.
- **`ShellyDiscovery` als eigenständiges, dependency-freies Modul (Meilenstein 4A)**: liefert reine `ShellyDeviceInfo`-Daten (IP, Host, ID, MAC, Modell, Generation) ohne Kenntnis von `ShellyPlugOutput` oder einer Weboberfläche – dadurch später direkt für ein "Gerät hinzufügen"-Flow in einer Web-UI wiederverwendbar, ohne dass die Discovery-Logik geändert werden muss.
- **mDNS-Lebenszyklus explizit verwaltet**: `ShellyDiscovery` merkt sich intern, ob `MDNS.begin()` bereits erfolgreich lief, und startet mDNS nie erneut (auch nicht über mehrere manuelle `D`-Trigger hinweg). `MDNSResponder::begin()` ruft `mdns_init()` auf, das nicht mehrfach sicher aufrufbar ist; wiederholte Aufrufe destabilisierten auf echter Hardware auch die unabhängige GoodWe-UDP-Broadcast-Discovery. Setup-Reihenfolge in `main.cpp`: WLAN → GoodWe-Discovery/Connect zuerst → danach erst die informative Shelly-Discovery, damit ein mDNS-/RPC-Fehler die GoodWe-Initialisierung nie verzögern oder verhindern kann.

## GoodWe-Protokollvalidierung

- Die technische Validierung für den Zielwechselrichter **GW8KN-ET Plus** ist in `/docs/goodwe-et-protocol-validation.md` dokumentiert.
- Dort ist für jeden im Repository verwendeten GoodWe-Protokollwert die Quelle und der Verifikationsstatus festgehalten.

## GoodWe-Vorzeichenkonvention (verifiziert)

- **Positives `gridPowerW`** = Einspeisung ins Netz / PV-Überschuss
- **Negatives `gridPowerW`** = Bezug aus dem Netz

## Build & Flash (PlatformIO)

1. `include/config/LocalCredentials.example.h` nach `include/config/LocalCredentials.h` kopieren und WLAN-Zugangsdaten dort setzen.
2. Build: `pio run`
3. Flash: `pio run -t upload`
4. Monitor: `pio device monitor`

Für den Windows-Serial-Monitor und die Fehlersuche nach einer längeren Pause siehe [docs/development-runbook.md](docs/development-runbook.md) und den Helfer [tools/serial-monitor.ps1](tools/serial-monitor.ps1).

## Entwicklungs-Testmodus für die Überschusssteuerung

Die Befehle werden im Serial Monitor mit `115200` Baud zeilenweise nach Enter ausgewertet. GoodWe wird weiterhin normal initialisiert. Bei `T<number>` werden GoodWe-Lesevorgänge weiterhin ausgeführt, aber der eingegebene Wert wird ausschließlich an den normalen `SurplusSwitchController` übergeben. Der Modus liegt nur im RAM; nach Reset oder Neustart sind wieder echte GoodWe-Werte aktiv.

| Befehl | Wirkung |
| --- | --- |
| `T100` | Testmodus mit simulierten `+100 W` Netzleistung aktivieren |
| `T0` | Im manuellen Testmodus `0 W` einspeisen |
| `T-400` | Im manuellen Testmodus `-400 W` einspeisen |
| `T-` | Testmodus beenden und sofort wieder echte GoodWe-Messwerte für die Regelung verwenden |
| `TA` | Nicht blockierenden EIN/AUS-Zyklus mit den konfigurierten Schwellen und Verzögerungen starten; danach automatische Rückkehr zu GoodWe |
| `TF` | GoodWe-Leseausfall simulieren und den unveränderten Fail-safe-Pfad testen; kein Leistungswert wird eingespeist |
| `TX` | Shelly-Schaltfehler simulieren bzw. Simulation wieder deaktivieren (nur bei aktiviertem Shelly-Ausgang) |
| `D` oder `d` | Shelly-mDNS-Discovery wie bisher erneut ausführen |

Simulierte Werte und Phasen sind im Log mit `[TESTMODE]` gekennzeichnet. `TA` lässt den Controller zunächst einen Wert unter der EIN-Schwelle, danach lange genug Überschuss für die normale EIN-Verzögerung und anschließend einen Wert unter der AUS-Schwelle für die normale AUS-Verzögerung sehen. Es schaltet den Shelly nie direkt.

`TF` startet nur, wenn der Controller-Ausgang bereits EIN ist. Ist er AUS, erscheint ein Hinweis: zuerst mit `T100` und der normalen Einschaltverzögerung einschalten. Während `TF` wird kein GoodWe-Lesevorgang ausgeführt; jeder reguläre Lesezeitpunkt meldet stattdessen `noteReadFailure()`. Der vorhandene 30-s-Fail-safe schaltet den Ausgang über den normalen Controllerpfad aus. Danach verwendet die Regelung wieder echte GoodWe-Daten. Keine Testeinstellung wird in Flash/NVS gespeichert.

`TX` ist ein zusätzlicher RAM-only Entwicklungsschalter für Ausgangsfehler. Er ist nur verfügbar, wenn `kLocalShellyOutputEnabled = true` ist. Während der Simulation wird kein Shelly-HTTP-Befehl gesendet; stattdessen schlagen die drei begrenzten Schaltversuche kontrolliert fehl. Der Controller behält den zuletzt bestätigten Zustand und startet für denselben weiterhin nötigen Wechsel frühestens 5 Sekunden nach Ende der fehlgeschlagenen Versuchsgruppe einen neuen Dreierblock. Je nach Regelzyklus kann dafür mehr als ein Zyklus vergehen. Nach Reset ist die Simulation automatisch deaktiviert. Ein endgültig fehlgeschlagenes reales AUS wird mit `[SAFETY] [ERROR]` markiert, da der Verbraucher eingeschaltet bleiben kann.

### Konkreter Shelly-Hardwaretest

1. Einen sicheren, beaufsichtigten Lastaufbau verwenden und sicherstellen, dass die Shelly-IP sowie `kLocalShellyOutputEnabled = true` korrekt konfiguriert sind. `T100` einschalten; etwa 15–20 Sekunden später muss `[SHELLY] Steckdose EIN` erscheinen und das Relais schalten.
2. `T-400` senden. Etwa 10–15 Sekunden später muss `[SHELLY] Steckdose AUS` erscheinen. Mit `T0` kann im manuellen Modus ein Wert ohne Überschuss geprüft werden.
3. Optional `TA` senden und den automatischen EIN/AUS-Zyklus anhand der `[TESTMODE]`- und `[SHELLY]`-Logs beobachten.
4. Für den Fail-safe-Aus-Test zunächst wieder mit `T100` einschalten und danach `TF` senden. Der Ausgang muss nach dem vorhandenen 30-s-Timeout über den Controller ausgeschaltet werden.
5. Mit `T-` jederzeit zum echten GoodWe-Regelbetrieb zurückkehren. Vor dem unbeaufsichtigten Betrieb sicherstellen, dass der Testmodus beendet ist; ein Reset aktiviert ebenfalls wieder den echten Betrieb.
6. Für die Fehlerbehandlung `TX` aktivieren und mit `T-400` ein erforderliches AUS auslösen. Die simulierten drei Fehlversuche dürfen den Controllerzustand nicht auf AUS setzen. `TX` erneut senden und auf den ersten Regelzyklus mindestens 5 Sekunden nach Ende der fehlgeschlagenen Gruppe warten; dann muss der echte Shelly-AUS-Befehl erneut versucht werden. `T100` kann analog den fehlgeschlagenen EIN-Befehl prüfen.

## Ausgabemodus konfigurieren

### Testmodus (Standard, kein reales Gerät)

In `include/config/LocalCredentials.h`:

```cpp
// Kein Shelly-Block nötig → VirtualSocketOutput ist automatisch aktiv
```

Beim Serial Monitor erscheint:

```
[CONFIG] Ausgabe: VirtualSocketOutput (Testmodus)
[CONTROL] Virtuelle Steckdose EIN
[CONTROL] Virtuelle Steckdose AUS
```

### Shelly Plug M Gen3 aktivieren

In `include/config/LocalCredentials.h` folgende Sektion ergänzen:

```cpp
#define SOLARPILOT_SHELLY_CONFIGURED
namespace solarpilot::config {
inline constexpr bool kLocalShellyOutputEnabled = true;
inline constexpr char kLocalShellyHost[] = "192.168.1.42";  // IP-Adresse des Shelly
inline constexpr uint8_t kLocalShellySwitchId = 0;
}  // namespace solarpilot::config
```

Beim Serial Monitor erscheint:

```
[CONFIG] Ausgabe: Shelly Plug M Gen3 (LAN)
[SHELLY] Steckdose EIN
[SHELLY] Steckdose AUS
```

Kommunikationsfehler werden klar geloggt, z. B.:

```
[SHELLY] Kommunikationsfehler: HTTP -1 (URL: http://192.168.1.42/rpc/Switch.Set?id=0&on=true)
```

### Shelly Plug M Gen3 – Hardwaretest-Prozedur

1. Shelly Plug M Gen3 im lokalen Netzwerk in Betrieb nehmen und IP-Adresse ermitteln.
2. Erreichbarkeit prüfen: `http://<IP>/rpc/Switch.GetStatus?id=0` im Browser aufrufen.
3. IP in `LocalCredentials.h` eintragen und `kLocalShellyOutputEnabled = true` setzen.
4. Firmware flashen, Serial Monitor öffnen.
5. Beim ersten Überschreiten der EIN-Schwelle (nach Ablauf der Qualifikationszeit) erscheint `[SHELLY] Steckdose EIN` und das Relais des Shelly schaltet.
6. Bei Unterschreiten der AUS-Schwelle erscheint `[SHELLY] Steckdose AUS`.

### Shelly-Discovery (Meilenstein 4A) – Testprozedur

1. Mindestens ein Shelly-Gerät (Gen2/Gen3, z. B. Plug S/Plug M) im selben lokalen Netzwerk/Subnetz wie der ESP32-C3 in Betrieb nehmen.
2. Firmware flashen, Serial Monitor öffnen (`115200` Baud).
3. Nach erfolgreicher WLAN-Verbindung initialisiert SolarPilot zuerst GoodWe (Discovery + Verbindung). Erst danach läuft einmalig die Shelly-Discovery; im Log erscheint zuerst genau einmal:

   ```
   [INFO] [SHELLY-DISCOVERY] mDNS einmalig gestartet.
   ```

   gefolgt vom Ergebnis, z. B.:

   ```
   [INFO] [SHELLY-DISCOVERY] 1 Shelly-Gerät(e) per mDNS gefunden.
   [INFO] [SHELLY-DISCOVERY] 1 Gerät(e) gefunden:
   [INFO]   [1] shellyplug-s-XXXXXX.local (192.168.1.55)
   [INFO]        id=shellyplug-s-XXXXXX mac=XXXXXXXXXXXX model=SNPL-00112EU gen=2
   ```

4. Ist kein Shelly-Gerät im Netzwerk erreichbar, erscheinen stattdessen `[SHELLY-DISCOVERY] Keine Shelly-Geräte per mDNS gefunden.` und `[SHELLY-DISCOVERY] Kein Shelly-Gerät im Netzwerk gefunden.`, und SolarPilot fährt normal mit dem Regelbetrieb fort. GoodWe war zu diesem Zeitpunkt bereits erfolgreich initialisiert, unabhängig vom Discovery-Ergebnis.
5. Wird ein Gerät per mDNS gefunden, aber der lokale RPC-Abruf (`Shelly.GetDeviceInfo`) schlägt fehl (z. B. Zeitüberschreitung/`HTTP -1`), erscheint `Geräteinfo nicht abrufbar (nur mDNS-Daten).`; IP/Host aus dem mDNS-Ergebnis werden trotzdem angezeigt. GoodWe-Kommunikation und Überschusssteuerung laufen davon unberührt weiter.
6. Zur Kontrolle: Die bereits konfigurierte feste `kLocalShellyHost`-Schaltlogik aus Meilenstein 3 funktioniert unverändert parallel weiter, unabhängig vom Discovery-Ergebnis.
7. **Erneute Discovery ohne Neustart:** Im Serial Monitor den Buchstaben `D` (oder `d`) senden und Enter/Send drücken. Da die Ausgabe unmittelbar nach dem Neustart über USB CDC leicht verpasst wird, kann so jederzeit erneut getestet werden, ohne das Board neu zu flashen oder zurückzusetzen. `[SHELLY-DISCOVERY] mDNS einmalig gestartet.` erscheint dabei **nicht** erneut (mDNS läuft bereits) – nur das Discovery-Ergebnis wird neu ausgegeben. Es läuft weiterhin keine automatische periodische Discovery – der Trigger ist rein manuell.
8. **Diagnose bei Problemen:** Erscheint `[SHELLY-DISCOVERY] Übersprungen: kein WLAN verbunden.`, liegt das Problem am WLAN selbst. Erscheint dagegen `[SHELLY-DISCOVERY] mDNS-Start fehlgeschlagen (WLAN ist verbunden, es liegt an mDNS/Discovery, nicht am WLAN).`, ist WLAN in Ordnung und nur mDNS/Discovery betroffen – die GoodWe-Kommunikation ist davon nicht betroffen, da sie zu diesem Zeitpunkt bereits läuft.

## Überschuss-Schaltlogik

- **Verifiziert auf dieser Installation:** Positive `gridPowerW`-Werte bedeuten Netzeinspeisung (PV-Überschuss), negative Werte bedeuten Netzbezug.
- **Aktuelle Testwerte (konfigurierbar in `AppConfig`)** – temporäre Werte, noch nicht für den Produktiveinsatz:
  - EIN ab `>= 50 W` Export für mindestens `15 s`
  - AUS ab `<= 20 W` Export für mindestens `10 s`
  - Zwischen `20 W` und `50 W` bleibt der Zustand unverändert (Hysterese)

## Sicherheits-Fail-safe bei ausbleibenden GoodWe-Daten

`SurplusSwitchController` kapselt zusätzlich einen Fail-safe-Zustand für den Fall, dass keine gültigen Netzleistungsdaten mehr vom GoodWe ankommen:

- Jeder erfolgreiche `update(gridPowerW, nowMs)`-Aufruf merkt sich den Zeitpunkt der letzten gültigen Netzleistung.
- Schlägt ein GoodWe-Lesezyklus fehl, ruft `main.cpp` stattdessen `noteReadFailure(nowMs)` auf. Ein einzelner Fehlversuch schaltet **nicht** sofort ab.
- Ist der Ausgang eingeschaltet und seit `AppConfig::kSurplusSwitchFailSafeTimeoutMs` (Standard: **30 s**) keine gültige Netzleistung mehr eingetroffen, schaltet der Controller den Ausgang sicherheitshalber AUS und loggt eindeutig:

  ```
  [SAFETY] Keine gültigen GoodWe-Daten seit 30 s – Ausgang wird ausgeschaltet.
  ```

- Nach dem Fail-safe schaltet der Ausgang erst wieder ein, wenn wieder gültige GoodWe-Daten vorliegen **und** die normale Einschaltbedingung (Schwellwert + Einschaltverzögerung) erneut vollständig erfüllt ist – es gibt keinen Sonderweg zum sofortigen Wiedereinschalten.
- Die bestehende Überschusslogik (Schwellwerte, Ein-/Ausschaltverzögerungen) und die GoodWe-Kommunikation selbst sind davon unberührt.

### Hardwaretest: Fail-safe auslösen

1. Ausgang über die normale Überschusslogik einschalten lassen (Export über der EIN-Schwelle für die Einschaltverzögerung halten) und im Serial Monitor bestätigen, dass er eingeschaltet ist.
2. GoodWe-Kommunikation unterbrechen, z. B. WLAN des GoodWe/Routers kurz deaktivieren oder den Wechselrichter vom Netzwerk trennen, sodass Leseversuche fehlschlagen.
3. Nach ca. 30 Sekunden ununterbrochener Fehlversuche erscheint im Serial Monitor `[SAFETY] Keine gültigen GoodWe-Daten seit 30 s – Ausgang wird ausgeschaltet.`, und der reale Ausgang schaltet ab.
4. GoodWe-Kommunikation wiederherstellen. Der Ausgang bleibt zunächst AUS, bis erneut gültige Netzleistungswerte vorliegen und die normale Einschaltbedingung samt Einschaltverzögerung erneut erfüllt ist.


