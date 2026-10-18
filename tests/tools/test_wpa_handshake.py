"""The kernel's WPA2 supplicant (kernel/nt/wpa.c) against wditest.sys's
authenticator (tools/drivertest/wpaap.h) on the host, without a VM: the
4-way and group key handshakes, a wrong passphrase, a downgrade and a
tampered frame refused, and the keys checked against Python's hashlib."""
from pathlib import Path
import hashlib
import hmac
import shutil
import subprocess
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[2]
MBEDTLS = ROOT / 'third_party/mbedtls'


def prf(key, label, data, n):
    out, i = b'', 0
    while len(out) < n:
        out += hmac.new(key, label + b'\0' + data + bytes([i]), hashlib.sha1).digest()
        i += 1
    return out[:n]


class WpaHandshakeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_handshake(self):
        with tempfile.TemporaryDirectory() as d:
            def build(src):
                obj = str(Path(d) / (src.stem + '.o'))
                subprocess.run(['gcc', '-c', '-w', '-O1', '-ffunction-sections', '-fdata-sections',
                                f'-I{MBEDTLS / "include"}', f'-I{MBEDTLS / "library"}', str(src), '-o', obj],
                               check=True, capture_output=True)
                return obj
            with ThreadPoolExecutor() as pool:                   # (Mbed TLS on the host's C library)
                objs = list(pool.map(build, sorted((MBEDTLS / 'library').glob('*.c'))))
            exe = str(Path(d) / 'wpa_handshake')
            subprocess.run(['gcc', '-O1', '-w', f'-I{MBEDTLS / "include"}', str(ROOT / 'tests/host/wpa_handshake.c'),
                            *objs, '-Wl,--gc-sections', '-o', exe], check=True, capture_output=True)
            out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
        got = dict(line.split(' ', 1) for line in out.splitlines() if ' ' in line)
        self.assertEqual(got['psk_ieee'], hashlib.pbkdf2_hmac('sha1', b'password', b'IEEE', 4096, 32).hex())
        pmk = hashlib.pbkdf2_hmac('sha1', b'nova-wpa2-passphrase', b'NovaNet', 4096, 32)
        self.assertEqual(got['pmk'], pmk.hex())
        self.assertEqual(got['psk_hex'], pmk.hex())
        aa, spa = bytes.fromhex('024e6f766101'), bytes.fromhex('024e6f766100')
        anonce, snonce = bytes((i * 7 + 1) & 0xFF for i in range(32)), b'\x5a' * 32
        ptk = prf(pmk, b'Pairwise key expansion',
                  min(aa, spa) + max(aa, spa) + min(anonce, snonce) + max(anonce, snonce), 48)
        self.assertEqual(got['ptk'], ptk.hex())
        self.assertIn('handshake ok', out)
        self.assertIn('wrong key refused: a wrong MIC', out)
        self.assertIn('downgrade refused', out)
        self.assertIn("tampered refused: message 3's MIC is wrong", out)


if __name__ == '__main__':
    unittest.main()
