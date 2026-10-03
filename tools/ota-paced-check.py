"""Single paced OTA transport comparison. Requires disconnected consumer.
Never outputs credentials, tokens or firmware contents.
Usage: PlatformIO Python tools/ota-paced-check.py http://DEVICE_IP
"""
import re
import socket
import sys
import time
from pathlib import Path
from urllib.parse import urlsplit
import requests
from requests.auth import HTTPDigestAuth

sys.excepthook = lambda kind, value, tb: print('Test abgebrochen: ' + (str(value) if kind is RuntimeError else kind.__name__), file=sys.stderr)
base = sys.argv[1].rstrip('/')
credentials = Path('include/config/LocalAdminCredentials.h').read_text(encoding='utf-8-sig')
def value(name):
    return re.search(r'\b' + name + r'\[\]\s*=\s*"([^"]*)"', credentials).group(1)
def auth(): return HTTPDigestAuth(value('kAdminUser'), value('kAdminPassword'))
session = requests.Session()
session.trust_env = False
authentication = auth()
r = session.get(base + '/api/logs', auth=authentication, timeout=(5, 15))
r.raise_for_status()
before = r.json()['token']
parts = urlsplit(base)
path = '/update?token=' + before
boundary = 'SolarPilotPacedUpload'
image = Path('.pio/build/esp32-c3-supermini/firmware.bin').read_bytes()
prefix = ('--' + boundary + '\r\nContent-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
footer = ('\r\n--' + boundary + '--\r\n').encode()
digest = authentication.build_digest_header('POST', base + path)
headers = ('POST ' + path + ' HTTP/1.1\r\nHost: ' + parts.netloc + '\r\nAuthorization: ' + digest + '\r\nContent-Type: multipart/form-data; boundary=' + boundary + '\r\nContent-Length: ' + str(len(prefix)+len(image)+len(footer)) + '\r\nConnection: close\r\n\r\n').encode()
with socket.create_connection((parts.hostname, parts.port or 80), timeout=10) as connection:
    connection.settimeout(15)
    connection.sendall(headers + prefix)
    for offset in range(0, len(image), 1024):
        connection.sendall(image[offset:offset+1024])
        time.sleep(0.02)
        if offset % 262144 == 0: print('Gesendet:', offset, 'Bytes', flush=True)
    connection.sendall(footer)
    reply = b''
    while b'\r\n' not in reply and len(reply) < 2048:
        data = connection.recv(1024)
        if not data: break
        reply += data
    match = re.match(rb'HTTP/1\.[01] ([0-9]{3})', reply)
    code = match.group(1).decode() if match else 'keine Antwort'
    print('Upload-Antwort:', code, flush=True)
    if code != '200': raise RuntimeError('Kein bestätigtes Update')
session.close()
deadline = time.monotonic() + 70
while time.monotonic() < deadline:
    try:
        with requests.Session() as probe:
            probe.trust_env = False
            r = probe.get(base + '/api/logs', auth=auth(), timeout=(2, 5))
            if r.status_code == 200 and r.json()['token'] != before:
                print('OK: vollständiger Upload und Admin nach Neustart erreichbar.', flush=True)
                break
    except requests.RequestException:
        pass
    time.sleep(2)
else: raise RuntimeError('Keine bestätigte Wiederkehr')
