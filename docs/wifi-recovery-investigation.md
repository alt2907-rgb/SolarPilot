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

PlatformIO erkennt COM4 mit USB VID:PID `303A:1001`. Öffnen des Ports schlug
innerhalb und außerhalb der Sandbox mit Zugriff verweigert fehl. Keine neuen
Hardwarelogs erfasst, kein Upload durchgeführt. Ein belegter serieller Port ist
eine mögliche Ursache, bisher nicht bestätigt.

Nach Freigabe des Ports zuerst die bestehende Firmware beobachten. Für einen
kontrollierten Verlusttest: `TW` EIN, 40 Sekunden beobachten, `TW` AUS und
WLAN-/GoodWe-Wiederkehr sowie ausstehendes Fail-safe-AUS prüfen. Die Powerstation
bleibt getrennt. Der Test muss immer deaktiviert werden. Ein erfolgreicher
kontrollierter Test ersetzt keinen Langzeittest des spontanen Fehlers.

Ein Hard-Recovery-Erfolg darf erst anhand realer Logs mit Neustartmeldung,
anschließender WLAN-Wiederkehr und wieder erfolgreichen GoodWe-Messungen
behauptet werden. Keine zusätzlichen automatischen Discovery-Aktivitäten
einführen.
