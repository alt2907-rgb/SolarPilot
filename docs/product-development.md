# SolarPilot – Entwicklungsstand und Produktplan

## Ziel und Arbeitsauftrag

Ein lokaler, autonomer PV-Überschussregler: Anwender richten Messquelle und
Steckdose einmal ein; SolarPilot steuert geeignete Verbraucher ohne Cloud,
Home Assistant oder dauerhaft laufenden PC. GoodWe ET und Shelly Plug M Gen3
sind das geprüfte Referenzsystem, keine pauschale Kompatibilitätszusage.
Andere Hersteller werden über eigene Messquellenadapter ergänzt und getestet.
EIN/AUS-Steckdosen verändern keine Ladeleistung stufenlos.

Seit 2026-10-03 sind neue Funktionen innerhalb dieser Vision autonom freigegeben.
Jede Lieferung braucht Branch, lokale Prüfung, PR/CI und aktualisierte
Dokumentation. Datenschutzgrenzen und Secrets bleiben verbindlich; Powerstation
bleibt bis zum Abschluss der wesentlichen Sicherheitsarbeiten getrennt.
Physische Handlungen und tatsächlich fehlende Produktentscheidungen erfordern
weiterhin den Nutzer. Eine Freigabe ersetzt keine Hardware-/Verkaufsprüfung.

## Lieferstände

| Paket | Stand | Nachweis / offene Grenze |
| --- | --- | --- |
| GoodWe ET -> Regelung -> Shelly | Hardwarevalidiert | Ein reales Referenzsystem; Protokoll unverändert |
| Webübersicht, Admin, begrenzte Tests, sichere OTA | PR #41 gemergt | Vollupload und bestätigtes AUS geprüft; optische Prüfung offen |
| Sicherheitsprüfung während GoodWe-Wartezeiten | PR #42 gemergt | Build, Webtest und TF bestanden; echter UDP-Paketverlust gezielt offen |
| WLAN-Recovery | Implementiert, teilweise geprüft | Kontrollierter Verlust bestanden; spontane Fehler/Langzeit offen |
| Rechnerlose Langzeitaufzeichnung | Implementiert, Hardware-Grundprüfung bestanden | Flash-Ring, geschützter Export, Neustartpersistenz, 40-s-Offline-Aufzeichnung geprüft; 24-h am Netzteil und Rotation/Stromverlust offen |
| Einrichtungs-WLAN | Experimentell | Frühere Android-Verbindung; zuverlässige Einrichtung nicht abgeschlossen |
| Weitere Messquellen | Geplant | Adaptergrenze vorhanden; keine ungeprüften Herstellerzusagen |

## Nächste Reihenfolge

1. Geräteaufzeichnung und Langzeit-Auswertung; reale Safety-/Recovery-Lücken
   beheben, bevor ein angeschlossener Verbraucher freigegeben wird.
2. Verlässliche Einrichtung und Wiederherstellung nach Routerwechsel.
3. Messquellen/Steckdosen modularisieren; anhand offizieller Protokolle und
   verfügbarer Hardware weitere lokale Adapter entwickeln.
4. Verbraucherparameter, Mindestlaufzeiten und später Prioritäten; sicherer
   Ausgangszustand und sinnvolle Standardwerte bleiben Voraussetzung.
5. Produktvalidierung: geschützter Konfigurations-/Updatepfad, Datenmigration,
   dokumentierte Kompatibilität, Produktionshardware, Gehäuse, Support und
   erforderliche Verkaufsprüfungen. Noch keine Verkaufsreife behaupten.

## Paket: eigenständige Aufzeichnung

Der ESP zeichnet am Netzteil selbst auf, auch ohne WLAN. Erfasst werden
Leistung mit Gültigkeitsflag, WLAN/GoodWe-Zustand, Ausgang, pending AUS,
Retry/Testzustand sowie Timeoutzähler. Zustandswechsel ergänzen Minutenproben.
Neustarts erhalten eine Bootkennung und eine Zeit seit Boot; ohne verlässliche
Uhr gibt es keine erfundenen Kalenderzeiten. Keine Passwörter, SSIDs, Tokens
oder freien Logtexte im Flash.

Speicher und RAM-Puffer sind begrenzt; älteste Segmente werden ersetzt.
Schreibzugriffe werden gebündelt. Ein plötzlicher Stromausfall kann noch
nicht gespeicherte Proben verlieren; eine Aufzeichnung ist keine vollständige
Bestätigung jedes physischen Relaisvorgangs. Export nur nach Admin-Anmeldung.
Formatierung darf vorhandene oder beschädigte Daten nicht stillschweigend
löschen. Start einer 24-h-Phase am Netzteil und deren tatsächlicher Ausgang
werden erst nach realem Test als bestanden dokumentiert.
