"""Hardware regression: authenticated partial OTA must recover after inactivity.

Requires the consumer to be physically disconnected. Never prints credentials.
Usage: PlatformIO Python tools/ota-stall-check.py http://DEVICE_IP
"""
import re
import socket
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit
import requests
from requests.auth import HTTPDigestAuth

sys.excepthook = lambda kind, value, tb: print('Prüfung abgebrochen: ' + kind.__name__, file=sys.stderr)
base = sys.argv[1].rstrip('/')
secret = Path('include/config/LocalAdminCredentials.h').read_text(encoding='utf-8-sig')
def credential(name):
    return re.search(r'\b' + name + r'\[\]\s*=\s*"([^"]*)"', secret).group(1)
auth = HTTPDigestAuth(credential('kAdminUser'), credential('kAdminPassword'))
session = requests.Session()
session.trust_env = False
before = session.get(base + '/api/logs', auth=auth, timeout=15)
before.raise_for_status()
logs = before.json()
path = '/update?token=' + logs['token']
parts = urlsplit(base)
boundary = 'SolarPilotStallTest'
prefix = ('--' + boundary + '\r\nContent-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
image = Path('.pio/build/esp32-c3-supermini/firmware.bin').read_bytes()
footer = ('\r\n--' + boundary + '--\r\n').encode()
authorization = auth.build_digest_header('POST', base + path)
headers = ('POST ' + path + ' HTTP/1.1\r\nHost: ' + parts.netloc + '\r\nAuthorization: ' + authorization + '\r\nContent-Type: multipart/form-data; boundary=' + boundary + '\r\nContent-Length: ' + str(len(prefix) + len(image) + len(footer)) + '\r\nConnection: close\r\n\r\n').encode()
with socket.create_connection((parts.hostname, parts.port or 80), timeout=10) as connection:
    connection.sendall(headers + prefix + image[:8192])
    print('Teilübertragung gestartet; Verbindung bleibt absichtlich offen.', flush=True)
    time.sleep(35)
deadline = time.monotonic() + 55
while time.monotonic() < deadline:
    try:
        response = session.get(base + '/api/logs', auth=auth, timeout=(2, 5))
        response.raise_for_status()
        after = response.json()
        # The boot creates a new action token. Reachability alone is not enough.
        if after['token'] != logs['token']:
            print('OK: ESP nach festgefahrener Übertragung neu gestartet und Admin erreichbar.', flush=True)
            break
    except requests.RequestException:
        pass
    time.sleep(2)
else:
    raise RuntimeError('Keine bestätigte Wiederkehr nach Update-Stillstand')
