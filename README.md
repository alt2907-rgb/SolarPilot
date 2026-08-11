# SolarPilot

Lokale ESP32-C3-Anwendung zur Kommunikation mit GoodWe-Wechselrichtern.

## Meilenstein 1 (implementiert)

- WLAN verbinden
- GoodWe im lokalen Netzwerk finden
- Verbindung vorbereiten
- aktuelle Netzleistung lesen
- Werte über Serial Monitor ausgeben

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
- **GoodWe lokal per UDP**: Discovery über Broadcast (`48899`) und Laufzeitdaten über lokalen GoodWe-Port (`8899`) ohne Cloud- oder Internetabhängigkeit.
- **`/web` und `/output` als vorbereitete Stubs**: Architektur ist vollständig vorbereitet, aber ohne unnötigen Funktionsumfang vor dem nächsten Meilenstein.

## Build & Flash (PlatformIO)

1. WLAN-Zugangsdaten in `/home/runner/work/SolarPilot/SolarPilot/include/config/AppConfig.h` setzen.
2. Build: `pio run`
3. Flash: `pio run -t upload`
4. Monitor: `pio device monitor`
