"""Integrated setup checks. Never prints keys, SSIDs, request bodies or tokens.
Default tests safety, scan and cancellation. --connect tests the existing local
build configuration and explicitly stores that same network on the device.
"""
import re
import sys
import time
from pathlib import Path
import requests
from requests.auth import HTTPDigestAuth
sys.excepthook=lambda kind,value,tb:print('Prüfung abgebrochen: '+(str(value) if kind is RuntimeError else kind.__name__),file=sys.stderr)
base=sys.argv[1].rstrip('/')
secret=Path('include/config/LocalAdminCredentials.h').read_text(encoding='utf-8-sig')
def constant(text,name): return re.search(r'\b'+name+r'\[\]\s*=\s*"([^"]*)"',text).group(1)
auth=HTTPDigestAuth(constant(secret,'kAdminUser'),constant(secret,'kAdminPassword'))
client=requests.Session();client.trust_env=False
def call(method,path,**kwargs):
    for attempt in range(3 if method=='GET' else 1):
        try:return client.request(method,base+path,timeout=(5,15),**kwargs)
        except requests.RequestException:
            if method!='GET' or attempt==2:raise
            time.sleep(2)
def check(ok,label):
    if not ok:raise RuntimeError(label)
    print('OK: '+label,flush=True)
token=call('GET','/api/logs',auth=auth).json()['token']
def action(command,**extra):return call('POST','/api/action',auth=auth,data={'command':command,'token':token,**extra})
for path in ('/api/setup','/api/setup/networks'):
    check(call('GET',path).status_code==401,'Einrichtung ohne Anmeldung gesperrt')
check(call('POST','/api/action',auth=auth,data={'command':'wifi-setup','token':'invalid'}).status_code==403,'Einrichtung ohne Aktionskennung gesperrt')
check(action('wifi-setup').status_code==200,'Einrichtung nach bestätigtem AUS gestartet')
try:
    state=call('GET','/api/setup',auth=auth).json()
    check(state['active'] and state['ap_key']=='','Einrichtungs-WLAN ohne WLAN-Passwort aktiv')
    status=call('GET','/api/status').json()
    check(not status['on'] and status['test'] and not status['valid'],'Regelung pausiert, Ausgang AUS, kein frischer Regelwert behauptet')
    check(action('cycle').status_code==409,'Schalttests während Einrichtung gesperrt')
    check(action('wifi-save').status_code==409,'Speichern ohne erfolgreichen Test gesperrt')
    check(action('wifi-test',ssid='invalid',password='short').status_code==409,'Zu kurzer Schlüssel abgelehnt')
    check(call('POST','/update?token='+token,auth=auth,files={'firmware':('invalid.bin',b'invalid')}).status_code==409,'OTA während Einrichtung gesperrt')
    check(action('wifi-scan').status_code==200,'Asynchrone Netzwerksuche gestartet')
    for _ in range(20):
        time.sleep(1)
        found=call('GET','/api/setup/networks',auth=auth).json()
        if not found['scanning']:break
    check(not found['scanning'] and len(found['networks'])>0,'Netzwerke gefunden, ohne Namen auszugeben')
    if '--connect' in sys.argv:
        local=Path('include/config/LocalCredentials.h').read_text(encoding='utf-8-sig')
        ssid=constant(local,'kLocalWifiSsid');password=constant(local,'kLocalWifiPassword')
        try:action('wifi-test',ssid=ssid,password=password)
        except requests.RequestException:pass  # Station re-association may close TCP.
        password=local=''
        time.sleep(33)
        state=call('GET','/api/setup',auth=auth).json()
        check(state['can_save'] and state['connected_seconds']>=20 and state['drops']==0,'30-Sekunden-Verbindungstest erfolgreich')
        check(action('wifi-save').status_code==200,'Geprüfte lokale Konfiguration ausdrücklich übernommen')
    else:
        check(action('wifi-cancel').status_code==200,'Abbruch angenommen, bisherige Konfiguration bleibt')
finally:
    # Also attempt cleanup on assertion failures. USB command WC is the fallback.
    try:
        if call('GET','/api/setup',auth=auth).json()['active']:action('wifi-cancel')
    except requests.RequestException:pass
deadline=time.monotonic()+60
while time.monotonic()<deadline:
    time.sleep(2)
    try:
        state=call('GET','/api/status').json()
        if not state['test'] and state['valid']:break
    except requests.RequestException:pass
else:raise RuntimeError('Normalbetrieb nicht wieder verfügbar; USB WC prüfen')
check(not state['test'] and state['valid'],'Normalbetrieb und Messwerte wieder verfügbar')
print('Integrierte WLAN-Einrichtung geprüft; Handy/AP-Bedienung bleibt separat zu prüfen.',flush=True)
