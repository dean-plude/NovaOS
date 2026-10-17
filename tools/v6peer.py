#!/usr/bin/env python3
"""An IPv6-only network for testing NovaOS, as a QEMU datagram netdev.

    tools/v6peer.py [--port 10600]

QEMU sends each Ethernet frame of the guest's NIC as one UDP datagram:

    -netdev dgram,id=v6,local.type=inet,local.host=127.0.0.1,local.port=10601,
            remote.type=inet,remote.host=127.0.0.1,remote.port=10600
    -device virtio-net-pci,netdev=v6

and this program answers them as the guest's only neighbor, a router:

  * router advertisements (on start, every few seconds and on router
    solicitations) with the prefix fd00:6e6f:7661::/64 for SLAAC,
    itself as DNS server (RDNSS) and the M and O flags,
  * a stateful DHCPv6 server (RFC 8415): Solicit, Request, Renew, Rebind,
    Release and Decline for one address, fd00:6e6f:7661::100, with a
    second DNS server (fd00:6e6f:7661::2) and the search list nova6.test,
    example.test.  The first lease is short (T1 4 s, T2 8 s) and its
    Renews go unanswered, so the client must Rebind; a Renew after that is
    answered and the lease made long.  Information-requests (stateless
    DHCPv6) get the DNS server too,
  * neighbor solicitations for its addresses, and echo requests (ping -6),
  * DNS over UDP: nova6.test has the AAAA record fd00:6e6f:7661::1 and no
    A record; every other name is NXDOMAIN,
  * HTTP on TCP port 80: any GET is answered with a short page.

It is just enough IPv6, UDP and TCP for those exchanges, and needs no IPv6
on the host (unlike QEMU's own user-mode network, which reaches the host
over the host's IPv6 loopback).  Used by tools/selftest.py --suite network.
"""
import argparse, os, socket, struct, sys, time

PREFIX = bytes.fromhex('fd006e6f766100000000000000000000')[:8]
MY_MAC = bytes.fromhex('020000000001')
MY_LL = bytes.fromhex('fe800000000000000000000000000001')
MY_GL = PREFIX + bytes.fromhex('0000000000000001')
DHCP6_DNS = PREFIX + bytes.fromhex('0000000000000002')
ALL_NODES = bytes.fromhex('ff020000000000000000000000000001')
ALL_DHCP6 = bytes.fromhex('ff020000000000000000000000010002')   # ff02::1:2
LEASED = PREFIX + bytes.fromhex('0000000000000100')                # the DHCPv6 address
SEARCH = ['nova6.test', 'example.test']
NAME = 'nova6.test'
PAGE = (b'<html><body><h1>Hello over IPv6</h1>'
        b'<p>Served by the NovaOS test peer (tools/v6peer.py).</p></body></html>\n')


def csum(data):
    if len(data) % 2:
        data += b'\0'
    s = sum(struct.unpack('!%dH' % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return ~s & 0xFFFF


def l4sum(src, dst, nh, payload):
    return csum(src + dst + struct.pack('!IxxxB', len(payload), nh) + payload)


class Peer:
    def __init__(self, port, log):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(('127.0.0.1', port))
        self.s.settimeout(0.2)
        self.qemu = None                    # (host, port) frames come from
        self.guest_mac = None
        self.log = log
        self.tcp = {}                       # (guest addr, guest port) -> connection
        self.last_ra = 0
        self.rebound = False                # a Rebind was answered (Renews are answered from then on)
        self.renewed = False

    # -- output --------------------------------------------------------
    def eth(self, dst_mac, payload, ethertype=0x86DD):
        if self.qemu:
            self.s.sendto(dst_mac + MY_MAC + struct.pack('!H', ethertype) + payload, self.qemu)

    def ip6(self, src, dst, nh, payload, hlim=64, dst_mac=None):
        hdr = struct.pack('!IHBB', 6 << 28, len(payload), nh, hlim) + src + dst
        if dst_mac is None:
            dst_mac = b'\x33\x33' + dst[12:] if dst[0] == 0xFF else (self.guest_mac or b'\xff' * 6)
        self.eth(dst_mac, hdr + payload)

    def icmp6(self, src, dst, typ, code, body, dst_mac=None):
        msg = struct.pack('!BBH', typ, code, 0) + body
        c = l4sum(src, dst, 58, msg)
        self.ip6(src, dst, 58, msg[:2] + struct.pack('!H', c) + msg[4:], 255, dst_mac)

    def router_advert(self, dst=ALL_NODES):
        body = struct.pack('!BBHII', 64, 0xC0, 1800, 0, 0)                  # M and O: DHCPv6 for addresses and more
        body += struct.pack('!BB', 1, 1) + MY_MAC                            # source link-layer address
        body += struct.pack('!BBBBIII', 3, 4, 64, 0xC0, 86400, 14400, 0) + PREFIX + b'\0' * 8   # prefix: on-link, autonomous
        body += struct.pack('!BBHI', 25, 3, 0, 3600) + MY_GL                 # RDNSS
        self.icmp6(MY_LL, dst, 134, 0, body)
        self.last_ra = time.time()

    def udp(self, src, dst, sport, dport, data):
        seg = struct.pack('!HHHH', sport, dport, 8 + len(data), 0) + data
        c = l4sum(src, dst, 17, seg) or 0xFFFF
        self.ip6(src, dst, 17, seg[:6] + struct.pack('!H', c) + seg[8:])

    def tcp_out(self, c, flags, data=b'', seq=None):
        seq = c['snd_nxt'] if seq is None else seq
        seg = struct.pack('!HHIIBBHHH', 80, c['port'], seq, c['rcv_nxt'], 5 << 4, flags, 65535, 0, 0) + data
        ck = l4sum(c['dst'], c['addr'], 6, seg)
        self.ip6(c['dst'], c['addr'], 6, seg[:16] + struct.pack('!H', ck) + seg[18:])

    # -- input ---------------------------------------------------------
    def frame(self, f):
        if len(f) < 54 or f[12:14] != b'\x86\xdd':
            return
        self.guest_mac = f[6:12]
        ip = f[14:]
        plen, nh = struct.unpack('!HB', ip[4:7])
        src, dst, payload = ip[8:24], ip[24:40], ip[40:40 + plen]
        if nh == 58:
            self.on_icmp6(src, dst, payload)
        elif nh == 17 and dst in (MY_GL, DHCP6_DNS, ALL_DHCP6):
            self.on_udp(src, dst, payload)
        elif nh == 6 and dst in (MY_GL, MY_LL):
            self.on_tcp(src, dst, payload)

    def on_icmp6(self, src, dst, m):
        typ = m[0]
        if typ == 133:                                       # router solicitation
            self.log('router solicitation: advertising %s/64' % socket.inet_ntop(socket.AF_INET6, PREFIX + b'\0' * 8))
            self.router_advert()
        elif typ == 135 and len(m) >= 24:                    # neighbor solicitation
            target = m[8:24]
            if target in (MY_LL, MY_GL) and src != b'\0' * 16:
                body = struct.pack('!I', 0xE0000000) + target + struct.pack('!BB', 2, 1) + MY_MAC
                self.icmp6(target, src, 136, 0, body)
        elif typ == 128 and dst in (MY_LL, MY_GL):           # echo request
            self.log('echo request from %s' % socket.inet_ntop(socket.AF_INET6, src))
            self.icmp6(dst, src, 129, 0, m[4:])

    def dhcp6(self, src, sport, m):
        """A stateful DHCPv6 client's message: the reply, as RFC 8415 section 18.3 says"""
        names = {1: 'Solicit', 3: 'Request', 5: 'Renew', 6: 'Rebind', 8: 'Release', 9: 'Decline'}
        typ, xid, opts, i = m[0], m[1:4], {}, 4
        while i + 4 <= len(m):
            code, n = struct.unpack('!HH', m[i:i + 4])
            opts.setdefault(code, m[i + 4:i + 4 + n])
            i += 4 + n
        client, ia = opts.get(1), opts.get(3)
        if typ not in names or client is None:
            return
        server_id = b'\x00\x03\x00\x01' + MY_MAC
        if typ in (3, 5, 8, 9) and opts.get(2) != server_id:
            self.log('DHCPv6 %s for another server: ignored' % names[typ])
            return
        if typ == 5 and not self.rebound:
            self.log('DHCPv6 Renew: not answered (the client must Rebind at T2)')
            return
        iaid = ia[:4] if ia and len(ia) >= 12 else b'\0\0\0\0'
        if typ in (8, 9):
            self.log('DHCPv6 %s of %s: replying' % (names[typ], socket.inet_ntop(socket.AF_INET6, LEASED)))
            options = struct.pack('!HHH', 13, 2, 0)                          # Status Code: success
        else:
            if typ == 6:
                self.rebound = True
            elif typ == 5:
                self.renewed = True
            t1, t2, pref, valid = (300, 480, 600, 900) if self.renewed else (4, 8, 30, 60)
            iaaddr = LEASED + struct.pack('!II', pref, valid)
            ia_na = iaid + struct.pack('!II', t1, t2) + struct.pack('!HH', 5, len(iaaddr)) + iaaddr
            search = b''.join(b''.join(bytes([len(l)]) + l.encode() for l in name.split('.')) + b'\0' for name in SEARCH)
            options = (struct.pack('!HH', 3, len(ia_na)) + ia_na +
                       struct.pack('!HH', 23, len(DHCP6_DNS)) + DHCP6_DNS +
                       struct.pack('!HH', 24, len(search)) + search)
            self.log('DHCPv6 %s: %s %s (T1 %d s, T2 %d s, valid %d s)' %
                     (names[typ], 'advertising' if typ == 1 else 'leasing',
                      socket.inet_ntop(socket.AF_INET6, LEASED), t1, t2, valid))
        options = struct.pack('!HH', 1, len(client)) + client + struct.pack('!HH', 2, len(server_id)) + server_id + options
        self.udp(MY_LL, src, 547, sport, bytes([2 if typ == 1 else 7]) + xid + options)

    def on_udp(self, src, dst, seg):
        sport, dport = struct.unpack('!HH', seg[:4])
        if dport == 547 and len(seg) >= 12 and seg[8] != 11:
            self.dhcp6(src, sport, seg[8:])
            return
        if dport == 547 and len(seg) >= 12 and seg[8] == 11:
            request = seg[8:]
            server_id = b'\x00\x03\x00\x01' + MY_MAC
            options = (struct.pack('!HH', 2, len(server_id)) + server_id +
                       struct.pack('!HH', 23, len(DHCP6_DNS)) + DHCP6_DNS)
            reply = b'\x07' + request[1:4] + options
            self.log('DHCPv6 information request: replying with DNS %s' %
                     socket.inet_ntop(socket.AF_INET6, DHCP6_DNS))
            self.udp(MY_LL, src, 547, sport, reply)
            return
        if dport != 53 or dst not in (MY_GL, DHCP6_DNS):
            return
        q = seg[8:]
        tid, flags, qd = struct.unpack('!HHH', q[:6])
        i, labels = 12, []
        while q[i]:
            labels.append(q[i + 1:i + 1 + q[i]].decode('ascii', 'replace'))
            i += 1 + q[i]
        qname, (qtype, qclass) = '.'.join(labels).lower(), struct.unpack('!HH', q[i + 1:i + 5])
        question = q[12:i + 5]
        known = qname == NAME
        answers = b''
        if known and qtype == 28:                             # AAAA
            answers = b'\xc0\x0c' + struct.pack('!HHIH', 28, 1, 60, 16) + MY_GL
        rcode = 0 if known else 3
        resp = struct.pack('!HHHHHH', tid, 0x8180 | rcode, 1, 1 if answers else 0, 0, 0) + question + answers
        self.log('DNS %s %s: %s' % ({1: 'A', 28: 'AAAA'}.get(qtype, qtype), qname,
                                    'answered' if answers else 'no record' if known else 'NXDOMAIN'))
        self.udp(dst, src, 53, sport, resp)

    def on_tcp(self, src, dst, seg):
        sport, dport, seq, ack, off, flags = struct.unpack('!HHIIBB', seg[:14])
        data = seg[(off >> 4) * 4:]
        if dport != 80:
            return
        key = (src, sport)
        c = self.tcp.get(key)
        if flags & 0x02:                                     # SYN
            if c is None or c['state'] != 'syn':
                c = self.tcp[key] = {'addr': src, 'dst': dst, 'port': sport, 'state': 'syn',
                                     'iss': 1000, 'snd_nxt': 1001, 'snd_una': 1000, 'rcv_nxt': seq + 1,
                                     'req': b'', 'out': b'', 'fin_sent': False, 'sent_at': 0}
            self.log('TCP connection from [%s]:%d' % (socket.inet_ntop(socket.AF_INET6, src), sport))
            self.tcp_out(c, 0x12, seq=c['iss'])              # SYN+ACK
            return
        if c is None:
            if not flags & 0x04:
                self.tcp_out({'addr': src, 'dst': dst, 'port': sport, 'snd_nxt': ack, 'rcv_nxt': seq + len(data)}, 0x04)
            return
        if flags & 0x04:                                     # RST
            del self.tcp[key]
            return
        if flags & 0x10:                                     # ACK
            if c['state'] == 'syn' and ack == c['iss'] + 1:
                c['state'] = 'open'
            if ack > c['snd_una']:
                done = ack - c['snd_una']
                c['out'] = c['out'][done:] if done <= len(c['out']) else b''
                c['snd_una'] = ack
        if data:
            if seq == c['rcv_nxt']:
                c['rcv_nxt'] += len(data)
                c['req'] += data
                if b'\r\n\r\n' in c['req'] and not c['out'] and not c['fin_sent']:
                    line = c['req'].split(b'\r\n', 1)[0].decode('latin-1')
                    self.log('HTTP request: ' + line)
                    c['out'] = (b'HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\n'
                                b'Connection: close\r\n\r\n' % len(PAGE)) + PAGE
                    self.send_pending(c)
                    return
            self.tcp_out(c, 0x10)                            # ACK (again, for a duplicate)
        if flags & 0x01:                                     # FIN
            if seq + len(data) == c['rcv_nxt']:
                c['rcv_nxt'] += 1
            self.tcp_out(c, 0x10)
            if c['fin_sent'] and not c['out']:
                del self.tcp[key]

    def send_pending(self, c):
        """(Re)send everything not yet acknowledged, then a FIN"""
        off, seq = 0, c['snd_una']
        while off < len(c['out']):
            chunk = c['out'][off:off + 1200]
            self.tcp_out(c, 0x18, chunk, seq=seq)            # PSH+ACK
            off, seq = off + len(chunk), seq + len(chunk)
        c['snd_nxt'] = seq
        if c['out'] or not c['fin_sent']:
            self.tcp_out(c, 0x11, seq=seq)                   # FIN+ACK
            c['fin_sent'] = True
            c['snd_nxt'] = seq + 1
        c['sent_at'] = time.time()

    def run(self):
        while True:
            try:
                f, addr = self.s.recvfrom(65536)
                if self.qemu is None:
                    self.qemu = addr
                    self.log('guest connected')
                self.qemu = addr
                self.frame(f)
            except socket.timeout:
                pass
            now = time.time()
            if self.qemu and now - self.last_ra > 3:
                self.router_advert()
            for c in list(self.tcp.values()):                # retransmit what wasn't acknowledged
                if c['fin_sent'] and c['snd_una'] < c['snd_nxt'] and now - c['sent_at'] > 1:
                    self.send_pending(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=10600)
    a = ap.parse_args()
    log = lambda m: print('[v6peer] ' + m, flush=True)
    log(f'listening on 127.0.0.1:{a.port}')
    Peer(a.port, log).run()


if __name__ == '__main__':
    main()
