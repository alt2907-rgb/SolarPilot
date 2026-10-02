"""Checks the real ESP web endpoints without exposing local credentials.

Usage: PlatformIO Python tools/web-hardware-check.py http://DEVICE_IP [--ota]
--ota deliberately installs the locally built firmware over WiFi.
"""
import argparse
import re
import time
import sys
from pathlib import Path
import requests
from requests.auth import HTTPDigestAuth

parser = argparse.ArgumentParser()
parser.add_argument('url')
parser.add_argument('--ota', action='store_true')
args = parser.parse_args()
sys.excepthook = lambda kind, value, tb: print('Prüfung abgebrochen: ' + kind.__name__ + '. Verbindung und Live-Protokoll prüfen; keine Zugangsdaten werden ausgegeben.', file=sys.stderr)
base = args.url.rstrip('/')
credentials = Path('include/config/LocalAdminCredentials.h').read_text(encoding='utf-8-sig')
def constant(name):
    return re.search(r'\b' + name + r'\[\]\s*=\s*"([^"]*)"', credentials).group(1)
auth = HTTPDigestAuth(constant('kAdminUser'), constant('kAdminPassword'))
client = requests.Session()
client.trust_env = False
def request(method, path, **kwargs):
    return client.request(method, base + path, timeout=(5, 30), **kwargs)
def check(condition, label):
    if not condition:
        raise RuntimeError('Prüfung fehlgeschlagen: ' + label)
    print('OK: ' + label, flush=True)

check(request('GET', '/').status_code == 200, 'Öffentliche Übersicht erreichbar')
for path in ['/admin', '/api/logs', '/update']:
    r = request('GET', path)
    check(r.status_code == 401, path + ' ohne Anmeldung gesperrt')
check(request('POST', '/api/action', data={'command': 'restart'}).status_code == 401,
      'Neustart ohne Anmeldung gesperrt')
check(request('GET', '/admin', auth=auth).status_code == 200, 'Admin-Anmeldung funktioniert')
status = request('GET', '/api/status').json()
check('health' in status and 'power' in status, 'Messwerte als gültiges JSON verfügbar')
logs = request('GET', '/api/logs', auth=auth).json()
check(isinstance(logs['entries'], list) and len(logs['entries']) <= 64, 'Begrenztes Live-Protokoll verfügbar')
check(all(constant('kAdminPassword') not in e['text'] for e in logs['entries']), 'Kein Adminpasswort im Protokoll')
token = logs['token']
check(request('POST', '/api/action', auth=auth, data={'command': 'restart', 'token': 'invalid'}).status_code == 403,
      'Aktion ohne gültige Sicherheitskennung gesperrt')
check(request('POST', '/api/action', auth=auth, data={'command': 'unknown', 'token': token}).status_code == 400,
      'Unbekannte Aktion abgelehnt')
check(request('POST', '/api/action', auth=auth, data={'command': 'diagnose', 'token': token}).status_code == 200,
      'Verbindungsdiagnose über WLAN ausgeführt')
bad = request('POST', '/update?token=' + token, auth=auth, files={'firmware': ('invalid.bin', b'invalid firmware')})
check(bad.status_code == 409, 'Ungültige Software abgelehnt')
check(request('GET', '/api/status').status_code == 200, 'Status nach abgelehntem Update verfügbar')
if status['real']:
    test = request('POST', '/api/action', auth=auth, data={'command': 'switch-failure', 'token': token})
    check(test.status_code == 200, 'Schaltfehler-Test gestartet')
    refused = request('POST', '/update?token=' + token, auth=auth, files={'firmware': ('invalid.bin', b'invalid firmware')})
    check(refused.status_code == 409, 'Update bei unbestätigtem AUS abgelehnt')
    entries = request('GET', '/api/logs', auth=auth).json()['entries']
    check(any('[OTA] Abgelehnt: physisches AUS nicht bestaetigt.' in e['text'] for e in entries),
          'Ablehnung erfolgt vor Updatebeginn wegen fehlender AUS-Bestätigung')
stop = request('POST', '/api/action', auth=auth, data={'command': 'stop', 'token': token})
check(stop.status_code == 200, 'Alle Tests beendet und AUS bestätigt')
if args.ota:
    with Path('.pio/build/esp32-c3-supermini/firmware.bin').open('rb') as firmware:
        r = request('POST', '/update?token=' + token, auth=auth, files={'firmware': ('firmware.bin', firmware)})
    check(r.status_code == 200, 'Gültige Firmware über WLAN installiert')
    deadline = time.monotonic() + 60
    recovered = False
    while time.monotonic() < deadline:
        time.sleep(2)
        try:
            if client.get(base + '/api/status', timeout=(2, 3)).status_code == 200:
                recovered = True
                break
        except requests.RequestException:
            pass
    if not recovered:
        raise RuntimeError('ESP nach WLAN-Update nicht erreichbar')
    check(request('GET', '/api/logs', auth=auth).status_code == 200, 'Adminzugang nach WLAN-Update verfügbar')
print('Hardware-Webprüfung abgeschlossen.', flush=True)
