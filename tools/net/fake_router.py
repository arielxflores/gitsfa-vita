#!/usr/bin/env python3
# b108 (tests of engine/src/net_nat.h on this machine): a stand-in for a home router and for a public STUN server.
#   fake_router.py [--wan 203.0.113.7] [--stun-says 198.51.100.9] [--refuse] [--pmp] [--no-upnp] [--http 5431] [--stun 3478]
# - UPnP IGD: answers SSDP searches on UDP 1900 (LOCATION -> http://127.0.0.1:<http>/desc.xml), serves the description and
#   the WANIPConnection SOAP actions AddPortMapping / DeletePortMapping / GetExternalIPAddress (--refuse: error 718)
# - NAT-PMP on UDP 5351 with --pmp
# - STUN binding on UDP <stun>: answers with --stun-says (default: the sender's own address) and the sender's port
# Every request is printed; the mappings are listed on exit (Ctrl+C / SIGTERM).
import socket, threading, struct, sys, argparse, time, signal
from http.server import BaseHTTPRequestHandler, HTTPServer
ap = argparse.ArgumentParser(); ap.add_argument('--wan', default='203.0.113.7'); ap.add_argument('--stun-says', default=''); ap.add_argument('--refuse', action='store_true')
ap.add_argument('--pmp', action='store_true'); ap.add_argument('--no-upnp', action='store_true'); ap.add_argument('--http', type=int, default=5431); ap.add_argument('--stun', type=int, default=3478)
A = ap.parse_args(); MAPS = {}
def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)
DESC = '''<?xml version="1.0"?><root xmlns="urn:schemas-upnp-org:device-1-0"><device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
<deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType><deviceList><device><deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType>
<serviceList><service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>
<controlURL>/ctl/IPConn</controlURL><eventSubURL>/evt/IPConn</eventSubURL><SCPDURL>/WANIPCn.xml</SCPDURL></service></serviceList></device></deviceList></device></deviceList></device></root>'''
class H(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *a): pass
    def out(self, code, body):
        b = body.encode(); self.send_response(code); self.send_header('Content-Type', 'text/xml'); self.send_header('Content-Length', str(len(b))); self.send_header('Connection', 'close'); self.end_headers(); self.wfile.write(b)
    def do_GET(self): log('HTTP GET', self.path); self.out(200 if self.path == '/desc.xml' else 404, DESC if self.path == '/desc.xml' else '')
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0))).decode(errors='replace'); act = self.headers.get('SOAPAction', '').strip('"').split('#')[-1]
        def arg(n):
            a = body.find('<' + n + '>'); b = body.find('</' + n + '>'); return body[a + len(n) + 2:b] if a >= 0 and b > a else ''
        env = lambda inner: '<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>%s</s:Body></s:Envelope>' % inner
        if act == 'AddPortMapping':
            if A.refuse: log('SOAP AddPortMapping REFUSED'); return self.out(500, env('<s:Fault><detail><UPnPError><errorCode>718</errorCode><errorDescription>ConflictInMappingEntry</errorDescription></UPnPError></detail></s:Fault>'))
            MAPS[(arg('NewProtocol'), arg('NewExternalPort'))] = (arg('NewInternalClient'), arg('NewInternalPort'), arg('NewLeaseDuration')); log('SOAP AddPortMapping', arg('NewProtocol'), arg('NewExternalPort'), '->', arg('NewInternalClient'), arg('NewInternalPort'), 'lease', arg('NewLeaseDuration'))
            return self.out(200, env('<u:AddPortMappingResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"/>'))
        if act == 'DeletePortMapping': MAPS.pop((arg('NewProtocol'), arg('NewExternalPort')), None); log('SOAP DeletePortMapping', arg('NewProtocol'), arg('NewExternalPort')); return self.out(200, env('<u:DeletePortMappingResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"/>'))
        if act == 'GetExternalIPAddress': log('SOAP GetExternalIPAddress ->', A.wan); return self.out(200, env('<u:GetExternalIPAddressResponse xmlns:u="urn:schemas-upnp-org:service:WANIPConnection:1"><NewExternalIPAddress>%s</NewExternalIPAddress></u:GetExternalIPAddressResponse>' % A.wan))
        log('SOAP ?', act); self.out(500, env(''))
def ssdp():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(('0.0.0.0', 1900))
    while True:
        d, a = s.recvfrom(2048)
        if d.startswith(b'M-SEARCH'):
            st = [l.split(b':', 1)[1].strip().decode() for l in d.split(b'\r\n') if l.upper().startswith(b'ST:')]; log('SSDP search from', a, st)
            s.sendto(('HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\nST: %s\r\nUSN: uuid:fake::%s\r\nEXT:\r\nSERVER: fake/1.0 UPnP/1.0\r\nLocation: http://127.0.0.1:%d/desc.xml\r\n\r\n' % (st[0] if st else '', st[0] if st else '', A.http)).encode(), a)
def pmp():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('0.0.0.0', 5351))
    while True:
        d, a = s.recvfrom(64)
        if len(d) >= 2 and d[1] == 0: log('NAT-PMP address request'); s.sendto(struct.pack('>BBHI', 0, 128, 0, int(time.time()) & 0xffffffff) + socket.inet_aton(A.wan), a)
        elif len(d) >= 12 and d[1] == 1:
            ip, ep, life = struct.unpack('>HHI', d[4:12]); log('NAT-PMP map UDP', ip, '->', ep, 'life', life)
            if life: MAPS[('UDP-PMP', str(ep))] = (a[0], str(ip), str(life))
            else: MAPS.pop(('UDP-PMP', str(ep)), None)
            s.sendto(struct.pack('>BBHIHHI', 0, 129, 0, int(time.time()) & 0xffffffff, ip, ep, life), a)
def stun():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('0.0.0.0', A.stun))
    while True:
        d, a = s.recvfrom(512)
        if len(d) >= 20 and d[:2] == b'\x00\x01' and d[4:8] == b'\x21\x12\xa4\x42':
            ip = socket.inet_aton(A.stun_says or a[0]); x = bytes(p ^ q for p, q in zip(ip, b'\x21\x12\xa4\x42')); log('STUN binding from', a, '-> says', A.stun_says or a[0], a[1])
            attr = struct.pack('>HHBBH', 0x0020, 8, 0, 1, a[1] ^ 0x2112) + x
            s.sendto(struct.pack('>HH', 0x0101, len(attr)) + d[4:20] + attr, a)
def bye(*_):
    log('mappings at exit:', MAPS); sys.exit(0)
signal.signal(signal.SIGTERM, bye); signal.signal(signal.SIGINT, bye)
if not A.no_upnp: threading.Thread(target=ssdp, daemon=True).start(); threading.Thread(target=lambda: HTTPServer(('0.0.0.0', A.http), H).serve_forever(), daemon=True).start()
if A.pmp: threading.Thread(target=pmp, daemon=True).start()
threading.Thread(target=stun, daemon=True).start(); log('fake router up (wan %s)' % A.wan)
while True: time.sleep(1)
