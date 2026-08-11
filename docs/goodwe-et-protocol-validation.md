# GoodWe ET / ET Plus Protokollvalidierung

Diese Validierung bezieht sich auf Milestone 1: zuverlässiges Auslesen der Netzleistung eines GoodWe **GW8KN-ET Plus** über das lokale UDP-Protokoll.

## Verwendete Quellen

Primäre technische Referenz:

- OSS-Bibliothek `goodwe`: https://github.com/marcelblijleven/goodwe
  - `goodwe/__init__.py`
  - `goodwe/et.py`
  - `goodwe/modbus.py`
  - `goodwe/protocol.py`
  - `goodwe/sensor.py`
  - `goodwe/const.py`

Sekundäre Plausibilisierung:

- Home-Assistant-Integration `mletenay/home-assistant-goodwe-inverter`, die die Bibliothek `goodwe` verwendet: https://github.com/mletenay/home-assistant-goodwe-inverter

## Ergebnis der Validierung

Für **ET / ET Plus** ist die bisherige Annahme eines alten `AA 55 C0 7F ...`-Runtime-Protokolls **nicht korrekt**.  
Für diese Gerätefamilie wird laut der bewährten `goodwe`-Implementierung **Modbus RTU über UDP** verwendet.

## Verifizierte Protokollwerte im Repository

| Verwendeter Wert | Status | Verwendung im Repository | Quelle | Hinweise |
|---|---|---|---|---|
| Discovery-Request `WIFIKIT-214028-READ` | verified | Broadcast-Suche im LAN | `goodwe/__init__.py`, Funktion `search_inverters()` | ASCII-String ohne weitere Nutzdaten |
| Discovery-Port `48899` | verified | Broadcast-Suche im LAN | `goodwe/__init__.py`, Funktion `search_inverters()` | Wird explizit an `255.255.255.255:48899` gesendet |
| Runtime-Port `8899` | verified | Laufzeitabfrage | `goodwe/const.py`, `GOODWE_UDP_PORT = 8899` | Wird für ET/EH/BT/BH verwendet |
| Modbus-Adresse `0xF7` | unverified for this exact device, verified as default in OSS | Runtime-Request | `goodwe/__init__.py` Docstring und `goodwe/et.py` Konstruktor | Laut Bibliothek Standardwert für ET/EH/BT/BH; kann gerätespezifisch geändert sein |
| Modbus-Funktion `0x03` | verified | Lesen von Holding-Registern | `goodwe/modbus.py`, `MODBUS_READ_CMD = 0x3` | Standard Modbus-Read |
| Startregister `0x891C` / `35100` | verified | Start der ET-Runtime-Daten | `goodwe/et.py`, `_READ_RUNNING_DATA = self._read_command(0x891C, 0x007D)` | Basisbereich der Laufzeitdaten |
| Registeranzahl `0x007D` / `125` | verified | Umfang der ET-Runtime-Daten | `goodwe/et.py`, `_READ_RUNNING_DATA = self._read_command(0x891C, 0x007D)` | Entspricht 250 Byte Payload |
| Response-Payload-Offset `5` | verified | Parsen der ET-Antwort | `goodwe/protocol.py`, `ModbusRtuProtocolCommand.trim_response()` | `raw_response[5:-2]` |
| Netzleistungsregister `35140` | verified | Auslesen der Netzleistung | `goodwe/et.py`, Sensor `PowerS("active_power", 35140, ...)` | Für Milestone 1 die relevante Messgröße |
| Byte-Offset `80` innerhalb der ET-Payload | verified | Position von `active_power` | Herleitung aus `goodwe/protocol.py`, `get_offset(address) = (address - first_address) * 2` mit Start `35100` und Ziel `35140` | `(35140 - 35100) * 2 = 80` |
| Werteformat `signed int16`, Big Endian | verified | Dekodierung der Netzleistung | `goodwe/sensor.py`, `read_bytes2_signed(..., byteorder="big", signed=True)` | Positive/negative Vorzeichen sind relevant |
| Bedeutungsrichtung des Vorzeichens | verified | Interpretation der Netzleistung | `goodwe/sensor.py`, `read_grid_mode()` + `goodwe/const.py`, `GRID_IN_OUT_MODES` | Positiv = Export, negativ = Import |
| Response-Präfix `AA 55` | verified in OSS | Validierung der ET-Antwort | `goodwe/protocol.py`, Kommentar in `ModbusRtuProtocolCommand` und `goodwe/modbus.py`, `validate_modbus_rtu_response()` | Gehört zum GoodWe-UDP-Wrapper um Modbus RTU |
| Checksumme `CRC-16/Modbus` | verified | Request-/Response-Validierung | `goodwe/modbus.py`, `_modbus_checksum()` | Nicht die alte Summen-Checksumme |
| CRC-Byte-Reihenfolge Little Endian | verified | Request-/Response-Validierung | `goodwe/modbus.py`, `data.append(checksum & 0xFF)` dann High-Byte | Low-Byte zuerst |

## Kritische Bewertung der bisherigen Annahmen

| Bisherige Annahme | Bewertung für GW8KN-ET Plus / ET Plus |
|---|---|
| Discovery `WIFIKIT-214028-READ` auf UDP `48899` | **korrekt** |
| Runtime-Request `AA 55 C0 7F 01 06 00 02 45` | **nicht korrekt für ET / ET Plus**; dieses Paket gehört zur ES/EM/BP-Familie |
| UDP-Port `8899` | **korrekt** |
| Payload-Offset `7` | **nicht korrekt für ET / ET Plus**; verifiziert ist `5` |
| `PGrid`-Offset `38` | **nicht korrekt für ET / ET Plus** |
| Grid-Direction-Offset `80` | **nicht korrekt für ET / ET Plus** als separates Richtungsbyte |
| Direction `1 = Export`, `2 = Import` | **nur als abgeleitete Zustandswerte belegt**, nicht als separates ET-Byte für dieses Milestone-1-Reading |
| Checksumme als einfache 16-Bit-Summe | **nicht korrekt für ET / ET Plus** |

## Konkretes Paket für ET / ET Plus

Für den im Repository jetzt verwendeten ET-Request gilt:

- Ziel: `<inverter-ip>:8899`
- Inhalt vor CRC:
  - `F7` = Modbus-Adresse (Default, **unverified on this exact hardware**)
  - `03` = Read Holding Registers
  - `89 1C` = Startregister `35100`
  - `00 7D` = `125` Register
- CRC: `CRC-16/Modbus`, **Little Endian**

Damit ergibt sich das Request-Format:

`F7 03 89 1C 00 7D <CRC_LO> <CRC_HI>`

## Was aktuell bewusst **nicht** behauptet wird

Folgende Aussagen konnten für genau dieses Gerät nicht belastbar per offizieller GoodWe-Dokumentation belegt werden und bleiben daher **unverified**:

- Dass die konkrete Modbus-Adresse auf diesem Gerät tatsächlich unverändert `0xF7` ist
- Welcher interne GoodWe-Modell-Tag im Discovery-Response für den **GW8KN-ET Plus** auftaucht
- Ob einzelne Firmwarestände des GW8KN-ET Plus abweichende Registerbereiche oder Response-Wrapper verwenden

## Hardware-Verifikation für unverified Punkte

Wenn wir die unverified Punkte auf echter Hardware absichern wollen:

1. Wechselrichter-IP per Discovery (`WIFIKIT-214028-READ`) ermitteln.
2. Zwei UDP-Requests an Port `8899` testen:
   - Standardadresse `0xF7`
   - falls keine Antwort: weitere bekannte GoodWe-/Modbus-Adressen nur kontrolliert und protokolliert testen
3. Rohantwort vollständig als Hex dump loggen.
4. Prüfen:
   - Präfix `AA 55`
   - Modbus-Funktion `03`
   - Byte Count `250`
   - CRC-16/Modbus gültig
   - Bytes `payload[80:82]` mit realer Netzsituation vergleichen
5. Vergleichsmessung durchführen:
   - definierte Last ein-/ausschalten
   - Netzleistungswert am Smart Meter / GoodWe-App notieren
   - prüfen, ob der signierte Wert in Watt und sein Vorzeichen dazu passen


## Aktueller Status für die Regelungslogik

- Auf der realen Zielhardware ist bestätigt: **positiver** Wert von `active_power`/`gridPowerW` entspricht Netzeinspeisung (PV-Überschuss).
- Für Netzbezug ist das Vorzeichenverhalten auf dieser Installation noch **nicht abschließend verifiziert** (mögliche negative Werte vs. Annäherung an `0`).
- Daher nutzt Milestone 2 bewusst eine export-basierte Schaltlogik, die **nicht** von negativen Importwerten abhängt.
- Die aktuellen Schwellwerte und Verzögerungen (`>= 2000 W` für 15 s EIN, `<= 500 W` für 10 s AUS) sind **Testwerte** und in `AppConfig` konfigurierbar.

## Auswirkung auf Milestone 1

Milestone 1 bleibt bewusst klein:

- keine Shelly-Unterstützung
- keine Regelungslogik
- keine Weboberfläche
- keine zusätzliche Bibliothek

Ziel bleibt ausschließlich: **Netzleistung des GoodWe GW8KN-ET Plus zuverlässig lokal lesen**.
