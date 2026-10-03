# Langzeitprüfung des robusten Kerns

Die Powerstation bleibt getrennt. Ziel: mindestens 24 Stunden normale Regelung
ohne manuelle Eingriffe, anschließend längere Beobachtung am Einsatzort.
Die PC-Aufzeichnung ist eine Testhilfe; der Regelbetrieb benötigt keinen PC.

## Sicherheitskorrektur

GoodWe prüft über einen synchronen Wait-Hook während UDP-Wartezeiten,
Discovery und Retry-Abständen die vorhandene Sicherheitsfrist mit frischem
millis(). Der Hook läuft ausschließlich auf dem aufrufenden Haupttask und
startet keine weitere GoodWe-Abfrage. Physisch unbestätigtes AUS bleibt pending.
Die Protokollbytes und bisherigen Zeit-/Schaltparameter bleiben unverändert.
Der Hook macht die komplette Anwendung nicht nicht-blockierend: Shelly-HTTP,
Webserver und andere Diagnosepfade sind weiterhin gesondert zu bewerten.
Insbesondere bestätigt ein TF-Test nicht allein den neuen UDP-Wartepfad.

Die Summe der Antwortzeiten verwendet uint64_t. Bei dauerhaft 100 ms pro
Messung alle fünf Sekunden konnte uint32_t nach etwa 6,8 Jahren überlaufen;
bei längeren Antwortzeiten entsprechend früher. Einzelzeiten und Zähler
bleiben uint32_t; deren verbleibende Grenzen sind kein unbegrenzter Betrieb.

## Aufzeichnung

PlatformIO-Python: `tools/longrun-check.py http://DEVICE_IP --hours 24`.
Eine Statusabfrage pro Minute, keine Diagnoseaktionen, keine Anmeldedaten,
keine kompletten HTTP-Antworten oder fremden Netzwerkdaten. Ausgabe unter
ignoriertem `.local/longrun-*.jsonl`; nicht committen. PC-Ruhezustand und
Netzwerkausfall des PCs verursachen Beobachtungslücken. Die Stichproben
beweisen weder lückenlose Erreichbarkeit noch jeden realen Schaltvorgang.

Abnahmekriterien: keine unaufgeklärten Neustarts; nach Kommunikationsausfällen
automatische Recovery; keine aktiven Testmodi; AUS bei fehlenden gültigen
Messwerten wird angefordert und bleibt bis Bestätigung pending. Shelly-Relais
und Fail-safe separat kontrolliert prüfen. Übertragungsfehler, RSSI, GoodWe-
Timeouts und Recovery-Zeiten gemeinsam bewerten, keine pauschale WLAN-Ursache.
Ein erfolgreicher Tag ist ein erster Nachweis, keine Produktfreigabe.

## Prüfstand 2026-10-03

Lokaler Build erfolgreich. Vollständige Hardware-Webprüfung einschließlich
WLAN-Installation dieser Korrektur und Admin-Wiederkehr bestanden. Leere
Shelly-Steckdose über T100 nach normaler Einschaltverzögerung eingeschaltet,
TF gestartet: nach 30 Sekunden ohne Werte AUS angefordert und bestätigt.
Test beendet, echte GoodWe-Messwerte und Gesamtzustand OK wieder verfügbar.
Der gezielte Paketverlust-Test während der echten UDP-Warteschleife sowie
die 24-Stunden-Auswertung bleiben offen; kein vollständiger Produktnachweis.
