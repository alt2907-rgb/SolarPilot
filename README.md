# SolarPilot

Lokale ESP32-C3-Anwendung zur Kommunikation mit GoodWe-Wechselrichtern, aktuell technisch validiert für **GoodWe ET / ET Plus**.

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
- `/include/web`: Platzhalter für spätere Weboberfläche
- `/include/config`: zentrale Konfiguration

## Designentscheidungen

- **Interface vor Implementierung (`IInverterClient`)**: bereitet die spätere Unterstützung weiterer Wechselrichter vor, ohne den Anwendungsfluss in `main.cpp` ändern zu müssen.
- **Klare Verantwortlichkeiten je Modul**: WLAN-Handling, Inverter-Protokoll und Ausgabe sind getrennt und dadurch wartbar/testbar.
- **Konfiguration zentral in `AppConfig`**: reduziert verstreute Magic Numbers und vereinfacht OTA-/Web-Konfiguration im nächsten Schritt.
- **GoodWe ET / ET Plus lokal per UDP**: Discovery über Broadcast (`48899`) und Laufzeitdaten über lokalen GoodWe-Port (`8899`) per Modbus RTU over UDP ohne Cloud- oder Internetabhängigkeit.
- **`ISwitchOutput`-Abstraktion**: `SurplusSwitchController` kennt weder `VirtualSocketOutput` noch `ShellyPlugOutput` – die Auswahl erfolgt in `main.cpp` anhand der Konfiguration.
- **`ShellyPlugOutput` für mehrere Instanzen ausgelegt**: Host, Switch-ID und Timeout sind Konstruktorparameter; mehrere Shelly-Geräte können später ohne Code-Änderungen instanziiert werden.

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

## Überschuss-Schaltlogik

- **Verifiziert auf dieser Installation:** Positive `gridPowerW`-Werte bedeuten Netzeinspeisung (PV-Überschuss), negative Werte bedeuten Netzbezug.
- **Aktuelle Testwerte (konfigurierbar in `AppConfig`)** – temporäre Werte, noch nicht für den Produktiveinsatz:
  - EIN ab `>= 200 W` Export für mindestens `15 s`
  - AUS ab `<= 100 W` Export für mindestens `10 s`
  - Zwischen `100 W` und `200 W` bleibt der Zustand unverändert (Hysterese)

