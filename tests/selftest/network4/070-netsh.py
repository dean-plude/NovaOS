# manual network settings (what Settings > Network's Edit sets): netsh gives
# the adapter a static address and DNS server, ping still reaches QEMU's
# gateway on it, and source=dhcp hands the address back to DHCP
import re
import time


def static(nova):
    nova.run('netsh interface ipv4 set address name=Ethernet static 10.0.2.16 255.255.255.0 10.0.2.2', 15)
    nova.run('netsh interface ipv4 set dnsservers name=Ethernet static 10.0.2.3', 15)


def dhcp_back(nova, timeout=90):
    """back to DHCP, then retry `ipconfig` until the lease is back (QEMU
    hands out 10.0.2.15 again)"""
    nova.run('netsh interface ipv4 set dnsservers name=Ethernet source=dhcp', 15)
    nova.run('netsh interface ipv4 set address name=Ethernet source=dhcp', 15)
    deadline = time.time() + timeout
    while time.time() < deadline:
        out, _ = nova.run('ipconfig', 15)
        if re.search(r'IPv4 Address[ .]*: 10\.0\.2\.15', out):
            return
        time.sleep(1)


TESTS = [
    Test('netsh static address', 'netsh interface ipv4 show config',
         [r'DHCP enabled:\s+No', r'IP Address:\s+10\.0\.2\.16', r'Subnet Mask:\s+255\.255\.255\.0',
          r'Default Gateway:\s+10\.0\.2\.2', r'Statically Configured DNS Servers:\s+10\.0\.2\.3'],
         builtin=True, before=static),
    Test('ping on a static address', 'ping 10.0.2.2', [r'Received = 4, Lost = 0'], builtin=True),
    Test('netsh back to DHCP', 'netsh interface ipv4 show config',
         [r'DHCP enabled:\s+Yes', r'IP Address:\s+10\.0\.2\.15', r'DNS servers configured through DHCP:\s+10\.0\.2\.3'],
         builtin=True, before=dhcp_back),
]
