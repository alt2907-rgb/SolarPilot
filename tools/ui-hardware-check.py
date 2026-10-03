"""Read-only checks for UI capabilities on the real device; no secret output."""
import re
import sys
from pathlib import Path
import requests
from requests.auth import HTTPDigestAuth

def main():
    base=sys.argv[1].rstrip('/')
    local=Path('include/config/LocalAdminCredentials.h').read_text(encoding='utf-8-sig')
    def value(name):return re.search(r'\b'+name+r'\[\]\s*=\s*"([^"]*)"',local).group(1)
    with requests.Session() as session:
        session.trust_env=False
        auth=HTTPDigestAuth(value('kAdminUser'),value('kAdminPassword'))
        def get(path,private=False):
            r=session.get(base+path,auth=auth if private else None,timeout=(5,15))
            r.raise_for_status()
            return r
        page=get('/admin',True).text
        for marker in ['data-panel="wifi"','data-panel="maintenance"','actionConfirm','candidateEdited']:
            assert marker in page
        status=get('/api/status').json()
        assert status['mode'] in ('normal','setup','test')
        assert set(status['rules'])=={'on_w','off_w','on_seconds','off_seconds','safe_seconds'}
        assert status['rules']==dict(on_w=50,off_w=20,on_seconds=15,off_seconds=10,safe_seconds=30)
        print('OK: Neue Oberfläche, getrennte Betriebsmodi und echte Schaltregeln.',flush=True)
        history=get('/api/history',True).json()
        assert len(history['files'])==8
        assert {f['segment'] for f in history['files']}==set(range(8))
        assert all(isinstance(f['bytes'],int) and 0<=f['bytes']<=32768 for f in history['files'])
        available=[f for f in history['files'] if f['bytes']>0]
        assert available
        for item in available:
            assert len(get('/history.csv?segment='+str(item['segment']),True).content)>=item['bytes']
        print('OK: Dateiliste entspricht vorhandenen geschützten Historienexporten.',flush=True)

try:
    main()
except Exception as error:
    print('UI-Prüfung fehlgeschlagen: '+type(error).__name__,file=sys.stderr)
    raise SystemExit(1)
