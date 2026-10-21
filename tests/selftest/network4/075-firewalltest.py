# the firewall (kernel/net/wfp.c, kernel/net/firewall.c): firewalltest's
# filtering-platform and INetFwPolicy2 checks, with the inbound connections
# it asks for ("firewalltest: knock N") made from the host through QEMU's
# port forwarding (net4_boot: host 18190+N -> TCP 8190+N, 18290+N -> UDP
# 8290+N); then netsh advfirewall adding, showing and deleting a rule
import socket
import threading
import time

DOC = '`firewalltest` (fwpuclnt: a dynamic session, sublayer, block and permit filters by weight, enumeration, an aborted transaction, application-id and UDP filters; hnetcfg INetFwPolicy2: inbound connections from the host dropped on a Public network until a rule allows them, rules switched off, per-profile rules, the network made Private, a profile switched off, block-all, an outbound block rule, the rules stored in FirewallRules), 64- and 32-bit; `netsh advfirewall`'


def knock(n):
    """connect to the forwarded TCP port and send "knock N", and send the
    same as datagrams to the forwarded UDP port, for a few seconds (in a
    thread: the test reads on meanwhile)"""
    def tcp():
        try:
            s = socket.create_connection(('127.0.0.1', 18190 + n), timeout=3)
            s.sendall(b'knock %d\n' % n)
            time.sleep(5)
            s.close()
        except OSError:
            pass

    def udp():
        u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        for _ in range(15):
            try:
                u.sendto(b'knock %d\n' % n, ('127.0.0.1', 18290 + n))
            except OSError:
                pass
            time.sleep(0.4)
        u.close()

    def act(nova):
        threading.Thread(target=tcp, daemon=True).start()
        threading.Thread(target=udp, daemon=True).start()
    return act


KNOCKS = [(r'firewalltest: knock %d\b' % n, knock(n)) for n in range(1, 9)]

TESTS = [
    Test('firewalltest', 'firewalltest', [r'firewalltest: \d+ passed, 0 failed'], acts=KNOCKS, elevated=True,
         timeout=300, boot_expect=[r'\[FIREWALL\] \d+ rule\(s\); Domain on, Private on, Public on',
                                   r'\[FIREWALL\] new network "Network[^"]*" \S+: Public']),
    Test('firewalltest x86', r'C:\Programs\x86\firewalltest.exe', [r'firewalltest: \d+ passed, 0 failed'],
         acts=KNOCKS, elevated=True, timeout=300),
    Test('firewall netsh show currentprofile', 'netsh advfirewall show currentprofile',
         [r'Public Profile Settings:', r'State\s+ON', r'Firewall Policy\s+BlockInbound,AllowOutbound', r'Ok\.'],
         builtin=True),
    Test('firewall netsh add rule', 'netsh advfirewall firewall add rule name="Nova test" dir=in action=allow '
         'protocol=TCP localport=8080 profile=private,public', [r'Ok\.'], builtin=True),
    Test('firewall netsh show rule', 'netsh advfirewall firewall show rule name="Nova test"',
         [r'Rule Name:\s+Nova test', r'Enabled:\s+Yes', r'Direction:\s+In', r'Profiles:\s+Private,Public',
          r'Protocol:\s+TCP', r'LocalPort:\s+8080', r'Action:\s+Allow'], builtin=True),
    Test('firewall netsh delete rule', 'netsh advfirewall firewall delete rule name="Nova test"',
         [r'Deleted 1 rule\(s\)\.'], builtin=True),
    Test('firewall netsh rule gone', 'netsh advfirewall firewall show rule name="Nova test"',
         [r'No rules match the specified criteria\.'], builtin=True),
]
