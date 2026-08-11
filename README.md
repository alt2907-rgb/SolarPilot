# SolarPilot

Lokale ESP32-C3-Anwendung zur Kommunikation mit GoodWe-Wechselrichtern, aktuell technisch validiert für **GoodWe ET / ET Plus**.

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
- `/src/output` und `/include/output`: Ausgabe-/Laststeuerungs-Schicht (aktuell nur Konsole)
- `/include/web`: Platzhalter für spätere Weboberfläche
- `/include/config`: zentrale Konfiguration

## Designentscheidungen

- **Interface vor Implementierung (`IInverterClient`)**: bereitet die spätere Unterstützung weiterer Wechselrichter vor, ohne den Anwendungsfluss in `main.cpp` ändern zu müssen.
- **Klare Verantwortlichkeiten je Modul**: WLAN-Handling, Inverter-Protokoll und Ausgabe sind getrennt und dadurch wartbar/testbar.
- **Konfiguration zentral in `AppConfig`**: reduziert verstreute Magic Numbers und vereinfacht OTA-/Web-Konfiguration im nächsten Schritt.
- **GoodWe ET / ET Plus lokal per UDP**: Discovery über Broadcast (`48899`) und Laufzeitdaten über lokalen GoodWe-Port (`8899`) per Modbus RTU over UDP ohne Cloud- oder Internetabhängigkeit.
- **`/web` und `/output` als vorbereitete Stubs**: Architektur ist vollständig vorbereitet, aber ohne unnötigen Funktionsumfang vor dem nächsten Meilenstein.

## GoodWe-Protokollvalidierung

- Die technische Validierung für den Zielwechselrichter **GW8KN-ET Plus** ist in `/docs/goodwe-et-protocol-validation.md` dokumentiert.
- Dort ist für jeden im Repository verwendeten GoodWe-Protokollwert die Quelle und der Verifikationsstatus festgehalten.

## Build & Flash (PlatformIO)

1. `include/config/LocalCredentials.example.h` nach `include/config/LocalCredentials.h` kopieren und WLAN-Zugangsdaten dort setzen.
2. Build: `pio run`
3. Flash: `pio run -t upload`
4. Monitor: `pio device monitor`


## Überschuss-Schaltlogik (Teststand)

- **Verifiziert auf dieser Installation:** Positive `gridPowerW`-Werte bedeuten Netzeinspeisung (PV-Überschuss).
- **Noch nicht verifiziert:** Bei Netzbezug ist aktuell unklar, ob negative Werte geliefert werden oder der Wert nur gegen `0 W` läuft.
- **Aktuelle Testwerte (konfigurierbar in `AppConfig`)**:
  - EIN ab `>= 2000 W` Export für mindestens `15 s`
  - AUS ab `<= 500 W` Export für mindestens `10 s`
  - Zwischen `500 W` und `2000 W` bleibt der Zustand unverändert (Hysterese)
- Es wird **keine reale Last** geschaltet; nur ein virtueller Zustand mit Serial-Log:
  - `[CONTROL] Virtuelle Steckdose EIN`
  - `[CONTROL] Virtuelle Steckdose AUS`

