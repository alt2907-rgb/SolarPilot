"""Check protected history export and persistence across a safe restart.
Run from repo with PlatformIO Python: history-hardware-check.py http://DEVICE_IP
"""
import csv
import io
import re
import sys
import time
from pathlib import Path
import requests
from requests.auth import HTTPDigestAuth

sys.excepthook = lambda kind, value, tb: print('Prüfung abgebrochen: ' +
    (str(value) if kind is RuntimeError else kind.__name__), file=sys.stderr)
base = sys.argv[1].rstrip('/')
secret = Path('include/config/LocalAdminCredentials.h').read_text(encoding='utf-8-sig')
def auth():
    def value(name): return re.search(r'\b' + name + r'\[\]\s*=\s*"([^"]*)"', secret).group(1)
    return HTTPDigestAuth(value('kAdminUser'), value('kAdminPassword'))
client = requests.Session()
client.trust_env = False
authentication = auth()
def call(method, path, **kwargs):
    return client.request(method, base + path, timeout=(3, 20), **kwargs)
def check(value, label):
    if not value: raise RuntimeError(label)
    print('OK: ' + label, flush=True)
for path in ('/api/history', '/history.csv?segment=0'):
    check(call('GET', path).status_code == 401, 'Aufzeichnung ohne Anmeldung gesperrt')
state = call('GET', '/api/history', auth=authentication).json()
check(state['ready'] and not state['error'], 'Flash-Aufzeichnung verfügbar')
check(call('POST', '/api/action', auth=authentication,
           data={'command': 'history-flush', 'token': 'invalid'}).status_code == 403,
      'Speichern ohne Aktionskennung gesperrt')
token = call('GET', '/api/logs', auth=authentication).json()['token']
def save():
    check(call('POST', '/api/action', auth=authentication,
          data={'command': 'history-flush', 'token': token}).status_code == 200,
          'RAM-Proben gespeichert')
save()
segment = state['active']
def export():
    response = call('GET', '/history.csv?segment=' + str(segment), auth=authentication)
    check(response.status_code == 200, 'Geschützter CSV-Export verfügbar')
    rows = list(csv.DictReader(io.StringIO(response.text)))
    check(rows and set(rows[0]) == {'boot','seconds','power_w','rssi','flags','timeouts'},
          'Nur vorgesehene Mess- und Zustandsfelder im Export')
    check(len(response.content) <= 32768, 'Exportsegment begrenzt')
    return response.text, rows
before, rows = export()
if '--wifi-loss' in sys.argv:
    try:
        call('POST', '/api/action', auth=authentication,
             data={'command':'wifi-loss','token':token})
    except requests.RequestException:
        pass  # The response may be lost when the station disconnects.
    time.sleep(45)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        try:
            recovered = call('GET', '/api/status')
            if recovered.status_code == 200 and recovered.json()['test'] is False:
                break
        except requests.RequestException: pass
        time.sleep(3)
    else: raise RuntimeError('WLAN-Test nicht automatisch beendet/erholt')
    save()
    before, rows = export()
    check(any(int(r['seconds']) > 10 and (int(r['flags']) & 66) == 64 for r in rows),
          'WLAN-Verlust mit aktivem Test ohne PC-Aufzeichnung im Flash erfasst')
old_boot = rows[-1]['boot']
check(call('GET', '/history.csv?segment=../0', auth=authentication).status_code == 400,
      'Ungültige Segmentauswahl abgelehnt')
check(call('POST', '/api/action', auth=authentication,
      data={'command':'restart','token':token}).status_code == 200,
      'Neustart nur nach bestätigtem AUS')
client.close()
client = requests.Session(); client.trust_env = False; authentication = auth()
deadline = time.monotonic() + 70
while time.monotonic() < deadline:
    time.sleep(2)
    try:
        response = call('GET', '/api/logs', auth=authentication)
        if response.status_code == 200 and response.json()['token'] != token:
            token = response.json()['token']; break
    except requests.RequestException: pass
else: raise RuntimeError('Keine bestätigte Wiederkehr')
save()
after, rows = export()
check(after.startswith(before), 'Gespeicherte Daten nach Neustart erhalten')
check(rows[-1]['boot'] != old_boot, 'Neue Startkennung nach Neustart aufgezeichnet')
check(call('GET', '/api/status').json()['test'] is False, 'Keine Simulation aktiv')
print('Geräteaufzeichnung und Neustartpersistenz geprüft.', flush=True)
