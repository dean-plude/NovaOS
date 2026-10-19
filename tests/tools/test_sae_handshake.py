"""The kernel's WPA3-Personal station (kernel/nt/sae.c, kernel/nt/wpa.c)
against wditest.sys's access point (tools/drivertest/saeap.h, wpaap.h) on
the host, without a VM: IEEE 802.11-2020 Annex J.10's SAE vector for both,
SAE between them, the WPA3 4-way and group key handshakes with the IGTK, a
wrong password, a reflected or broken commit, and the PTK checked against
Python's hmac."""
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


def kdf_sha256(key, label, data, bits):
    out, i = b'', 1
    while len(out) < bits // 8:
        out += hmac.new(key, i.to_bytes(2, 'little') + label + data + bits.to_bytes(2, 'little'),
                        hashlib.sha256).digest()
        i += 1
    return out[:bits // 8]


class SaeHandshakeTests(unittest.TestCase):
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
            exe = str(Path(d) / 'sae_handshake')
            subprocess.run(['gcc', '-O1', '-w', f'-I{MBEDTLS / "include"}', str(ROOT / 'tests/host/sae_handshake.c'),
                            *objs, '-Wl,--gc-sections', '-o', exe], check=True, capture_output=True)
            out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
        got = dict(line.split(' ', 1) for line in out.splitlines() if ' ' in line)
        self.assertEqual(got['j10_pmk'], '4e4dfab1a2dd8ac1a91790f953faaa452ae5c6873ab75b63605ba663f8a7fe59')
        for line in ('annex j.10 ok', 'sae ok', 'wpa3 handshake ok', 'parsers ok'):
            self.assertIn(line, out)
        pmk = bytes.fromhex(got['sae_pmk'])
        aa, spa = bytes.fromhex('024e6f766102'), bytes.fromhex('024e6f766100')
        anonce, snonce = bytes((i * 7 + 1) & 0xFF for i in range(32)), b'\x5a' * 32
        ptk = kdf_sha256(pmk, b'Pairwise key expansion',
                         min(aa, spa) + max(aa, spa) + min(anonce, snonce) + max(anonce, snonce), 384)
        self.assertEqual(got['sae_ptk'], ptk.hex())
        self.assertIn('version refused: a key descriptor version other than 0', out)
        self.assertIn("wrong password refused: the station's confirm is wrong", out)
        self.assertIn("wrong password at the station: the access point's confirm is wrong", out)
        self.assertIn('reflection refused', out)
        self.assertIn('bad element refused', out)
        self.assertIn('bad scalar refused', out)


if __name__ == '__main__':
    unittest.main()
