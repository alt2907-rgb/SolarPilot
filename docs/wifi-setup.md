# Integrierte WLAN-Einrichtung

## Ablauf für bestehende Geräte

Auf der geschützten Adminseite den Abschnitt WLAN einrichten öffnen.
Einrichtungs-WLAN einschalten fordert zunächst einen frischen Shelly-AUS-Befehl
an. Ohne Bestätigung wird der Modus abgelehnt. Während der Einrichtung pausiert
die Regelung; Messwerte gelten nicht als frisch. Der ESP bleibt zehn Minuten
im Modus, dann stellt er die bisher konfigurierte Verbindung wieder her.

WLAN-Name und individuell zufälliger Zugang stehen nur im angemeldeten
Einrichtungsbereich, nicht im öffentlichen Status oder Protokoll. Vor dem
Wechsel mit dem Handy den Zugang notieren. Mit diesem WLAN verbinden und
`http://192.168.4.1/admin` öffnen; erneut mit dem Adminzugang anmelden.
Netze suchen, eines auswählen oder seinen Namen eingeben, Schlüssel verdeckt
eingeben und Verbindung 30 Sekunden testen. Derselbe Ablauf funktioniert
auch aus dem vorhandenen Heimnetz, soweit die IP weiter erreichbar bleibt.

Nach Ablauf des Tests ist Übernahme nur möglich, wenn mindestens 20 Sekunden
verbunden, kein beobachteter Abbruch und die Verbindung am Ende noch aktiv
ist. Die STA-Verbindung kann den AP-Kanal wechseln; bei Verbindungsverlust
des Handys das Einrichtungs-WLAN erneut auswählen. Das ist ein Anmelde-/
Verbindungsdauertest, kein Ping-, GoodWe- oder Langzeitstabilitätsnachweis.

Übernehmen speichert das geprüfte Netz bewusst als einen versionierten
NVS-Blob. Danach endet das Einrichtungs-WLAN und die Regelung kehrt zurück.
Abbrechen oder der Zehn-Minuten-Ablauf speichert keine Kandidaten und
verwendet das bisherige Netz. Serielle Testbefehle sind währenddessen
gesperrt; `WC` beendet den Modus über USB als Rückfall. OTA und Schalt-
Webtests sind gesperrt. Geräteaufzeichnung bleibt aktiv.

## Daten und Security

Der Kandidatenschlüssel bleibt bis zur bewussten Übernahme im Arbeitsspeicher.
Er wird nicht angezeigt, im CSV gespeichert oder protokolliert. Nach Test-
Ende wird er noch für die mögliche Übernahme vorgehalten; nach Ende des
Einrichtungsmodus werden eigene String-Kopien bestmöglich überschrieben.
Framework-/Browser-/Treiberkopien sind nicht garantiert vollständig bereinigt.
Maskierung ist keine Verschlüsselung. HTTP hat kein TLS. Das WPA2-geschützte
Einrichtungs-WLAN besitzt einen neuen zufälligen Zugang pro Aktivierung.
Adminanmeldung und zufällige Aktionskennung schützen sämtliche Mutationen.

Gespeicherte WLAN-Zugangsdaten liegen in NVS. Auf dem Entwicklungsboard ist
keine Flash-Verschlüsselung aktiviert; physischer Zugriff kann Daten offenlegen.
Boot verwendet einen gültigen gespeicherten Blob, andernfalls die ignorierte
lokale Build-Konfiguration. Änderungen an LocalCredentials.h überschreiben
eine zuvor gespeicherte Netzkonfiguration nicht. Aktuell fehlt noch eine
gezielte Reset-/Migrationsoberfläche. Keine Passwörter in Export/Logs/PRs.

## Produktgrenzen

Dies ist die integrierte, bewusst gestartete Einrichtung eines bereits
erreichbaren Geräts. Automatische fabrikneue Einrichtung, individuelle
gedruckte Erstzugänge und die Recovery-Einrichtung bei vollständig verlorenem
Heimnetz sind noch offen. Der Adminzugang wird heute lokal vorkonfiguriert.
Insbesondere ersetzt dieses Paket noch keine komplette Erstinbetriebnahme.
Keine automatischen Scans im Regelbetrieb; Suche nur im Einrichtungsmodus.
Der bestehende synchrone Webserver und dessen Parser-/TLS-Grenzen bleiben
relevant für spätere Security-Arbeiten. Der sichtbare AP-Key ist nur der
temporäre Zugang zum Einrichtungs-WLAN, niemals der Heimnetzschlüssel.

## Validierung

Build und vollständige Web-/OTA-Installation bestanden. Hardwareprüfer
`tools/wifi-setup-hardware-check.py http://DEVICE_IP` prüft Anmeldung,
Aktionskennung, bestätigtes AUS, Sperren, Netzwerksuche und Rückkehr nach
Abbruch. `--connect` verwendet die bereits für Build/Hardware autorisierte
lokale Konfiguration intern, prüft den 30-s-Test und übernimmt dasselbe Netz;
keine Inhalte werden ausgegeben. Handy-/AP-Bedienung wird gesondert geprüft,
nicht aus internen HTTP-/SoftAP-Ergebnissen abgeleitet.

2026-10-03: reguläre Web-/OTA-Prüfung erfolgreich; Zugriffsschutz, bestätigtes
AUS, Sperren, Scan und Abbruch mit Rückkehr zum Normalbetrieb bestanden.
Der 30-s-Test der vorhandenen lokalen Konfiguration bestand mit mindestens
20 verbundenen Sekunden und ohne beobachteten Abbruch; bewusste Übernahme
erfolgreich. Nach OTA und anschließendem sicheren Neustart bleibt die
gespeicherte Netzkonfiguration aktiv. Historienexport/Persistenz ebenfalls
geprüft. Ein erster Scan hatte einen vorübergehenden HTTP-Timeout; finaler
Prüfer wiederholt lesende Anfragen, Browserabfragen besitzen Zeitlimits und
reduzieren konkurrierende Polls im Einrichtungsmodus. JavaScript-Syntax
geprüft; optische Handy-/AP-Prüfung ist separat beim Nutzer angefragt.
