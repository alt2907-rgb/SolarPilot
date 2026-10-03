# Gemeinsamer Vertrag für Messquellen

Ziel: Weitere Wechselrichter oder lokale Stromzähler liefern der Regelung
denselben Messwert. GoodWe ET ist weiterhin der einzige vorhandene reale
Adapter; diese Änderung ist kein Beleg anderer Herstellerkompatibilität.

## Semantik und Sicherheitsgrenze

`IInverterClient` ist die bestehende Adaptergrenze. Der bisherige Name bleibt
für eine kleine, nachvollziehbare Migration erhalten. Auch ein Stromzähler
kann später diese Schnittstelle erfüllen. Erforderlich ist die aktive
Netzleistung am gemeinsamen Netzanschlusspunkt: positiv Einspeisung,
negativ Bezug, Einheit Watt. PV-Erzeugung allein ist kein Überschusswert.

Ein erfolgreicher Read liefert eine neue, endliche Zahl. Fehlgeschlagene
Abfragen dürfen keinen alten Wert als frische Messung ausgeben. main lehnt
NaN und Unendlich unabhängig vom Adapter ab. Pending AUS hat weiterhin
Vorrang vor einer wieder verfügbaren Messung.

Recovery, Reset, Sicherheitshook und Diagnosezähler werden über die gemeinsame
Schnittstelle aufgerufen. Der synchrone Hook muss während begrenzter
Netzwerkwarteschleifen auf dem Haupttask laufen; keine Reentranz und keine
unbeschränkten Wartezeiten. `sourceId` ist eine konstante, vertrauenswürdige
ASCII-Kennung aus Kleinbuchstaben, Ziffern und Bindestrichen. Sie enthält
keine Zugangsdaten oder frei konfigurierbaren Gerätenamen.

`SystemHealth` beschreibt die Aktualität der Messquelle herstellerneutral.
Der Status enthält `source` und `source_connected`. Das bisherige JSON-Feld
`goodwe` bleibt als Kompatibilitätsalias erhalten; neue Bedienung verwendet
die neuen Felder. Flash-CSV-Flags behalten ihre Bitwerte und Bedeutung.

## Noch nicht verallgemeinert

Die konkrete GoodWe-Instanz, deren Ports/Protokoll, Discovery und die heutigen
Recovery-Parameter bleiben am Zusammensetzungspunkt in main konfiguriert.
Ein Quellenauswahlmenü, weitere Adapter und geräteübergreifende Erkennung
sind noch nicht implementiert. Bestehende GoodWe-Testbefehle bleiben erhalten.
Für einen neuen Adapter müssen dessen Transport, Einheit, Vorzeichen,
Aktualität und Failure-Verhalten real oder gegen überprüfte Protokollfixtures
validiert werden. Keine Marketingliste ohne entsprechenden Nachweis.

## Validierung

Build stellt sicher, dass GoodWe den vollständigen gemeinsamen Vertrag
erfüllt. Anschließend werden reale Messwerte, sichere Updates und vorhandene
Historien geprüft. Ein späterer Adaptertest muss bewusst NaN/Unendlich,
veraltete Werte, Timeouts und Wiederverbindung einspeisen. Die aktuelle
GoodWe-Hardware liefert int16-Werte und kann NaN nicht erzeugen; daher ist
eine echte NaN-Hardwareprüfung mit dieser Quelle nicht als bestanden anzusehen.

2026-10-03: lokaler Build und vollständige Hardware-Webprüfung inklusive
WLAN-Installation und Admin-Wiederkehr bestanden. Status zeigt `goodwe-et`,
`source_connected` entspricht dem bisherigen Alias, frischer endlicher
Messwert vorhanden, kein Testmodus aktiv. Geschützter Historienexport und
erhaltene CSV-Daten nach anschließendem sicheren Neustart erneut bestätigt.
