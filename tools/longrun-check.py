"""Read-only endurance observation. Writes only selected public status fields.

Run from the repository: longrun-check.py http://DEVICE_IP --hours 24
The PC is needed for this recording only, never for SolarPilot regulation.
"""
import argparse
import json
import time
from datetime import datetime, timezone
from pathlib import Path
import requests

parser = argparse.ArgumentParser()
parser.add_argument('url')
parser.add_argument('--hours', type=float, default=24)
parser.add_argument('--interval', type=float, default=60)
args = parser.parse_args()
if args.hours <= 0 or args.interval < 10:
    parser.error('Dauer muss positiv sein; Abfrageabstand mindestens 10 Sekunden.')
root = Path(__file__).resolve().parents[1]
output = root / '.local' / ('longrun-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S') + '.jsonl')
output.parent.mkdir(exist_ok=True)
fields = ('health', 'power', 'valid', 'age', 'on', 'pending', 'retry',
          'rssi', 'goodwe', 'test', 'retries', 'timeouts', 'probes')
deadline = time.monotonic() + args.hours * 3600
samples = failures = degraded = 0
print('Nur lesende Langzeitaufzeichnung gestartet: ' + str(output), flush=True)
with requests.Session() as client, output.open('w', encoding='utf-8') as log:
    client.trust_env = False
    try:
        while time.monotonic() < deadline:
            started = time.monotonic()
            row = {'utc': datetime.now(timezone.utc).isoformat()}
            samples += 1
            try:
                response = client.get(args.url.rstrip('/') + '/api/status', timeout=(3, 10))
                response.raise_for_status()
                status = response.json()
                row.update({key: status.get(key) for key in fields})
                row['reachable'] = True
                if status.get('health') != 'In Ordnung' or status.get('test'):
                    degraded += 1
            except (requests.RequestException, ValueError):
                # Never print exception text: it may contain request details.
                failures += 1
                row['reachable'] = False
            row['request_ms'] = round((time.monotonic() - started) * 1000)
            log.write(json.dumps(row, ensure_ascii=False) + '\n')
            log.flush()
            time.sleep(max(0, min(args.interval - (time.monotonic() - started),
                                  deadline - time.monotonic())))
    except KeyboardInterrupt:
        print('Aufzeichnung beendet; SolarPilot läuft unabhängig weiter.', flush=True)
print(f'Stichproben={samples}, nicht erreichbar={failures}, eingeschränkt/Test={degraded}', flush=True)
