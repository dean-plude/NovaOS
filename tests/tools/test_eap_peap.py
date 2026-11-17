"""The kernel's WPA2-Enterprise EAP peer (kernel/nt/eap.c: PEAP with
EAP-MSCHAPv2, and EAP-TLS) against wditest.sys's authentication server
(tools/drivertest/eapap.h) on the host, without a VM: RFC 2759's MS-CHAPv2
vector, whole sign-ins (both ends' MSK the same), a wrong password, an
unknown user and an untrusted server certificate refused; EAP-TLS with
alice's certificate from mksys.py's alice.pfx, mallory's refused; and
kernel/net/pfx.c reading PKCS #12 files as OpenSSL encrypts them (AES-256
with PBKDF2, Windows' and OpenSSL 3's default; TripleDES-SHA1, the older
one), a wrong password refused."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[2]
MBEDTLS = ROOT / 'third_party/mbedtls'


def mksys():
    spec = importlib.util.spec_from_file_location('mksys', ROOT / 'tools/drivertest/mksys.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class EapPeapTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'), 'host GCC required')
    def test_peap(self):
        with tempfile.TemporaryDirectory() as d:
            mksys().radius_certs(d, d)                           # (wditest_certs.h, novacorp-ca.cer)

            def build(src):
                obj = str(Path(d) / (src.stem + '.o'))
                subprocess.run(['gcc', '-c', '-w', '-O1', '-ffunction-sections', '-fdata-sections',
                                f'-I{MBEDTLS / "include"}', f'-I{MBEDTLS / "library"}', str(src), '-o', obj],
                               check=True, capture_output=True)
                return obj
            with ThreadPoolExecutor() as pool:                   # (Mbed TLS on the host's C library)
                objs = list(pool.map(build, sorted((MBEDTLS / 'library').glob('*.c'))))
            exe = str(Path(d) / 'eap_peap')
            r = subprocess.run(['gcc', '-O1', '-w', f'-I{MBEDTLS / "include"}', f'-I{d}',
                                str(ROOT / 'tests/host/eap_peap.c'), *objs, '-Wl,--gc-sections', '-o', exe],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            out = subprocess.run([exe, str(Path(d) / 'novacorp-ca.cer'), str(Path(d) / 'alice.pfx'),
                                  str(Path(d) / 'mallory.pfx')], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            untrusted = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(untrusted.returncode, 0, untrusted.stdout + untrusted.stderr)
            pfx = {}
            if shutil.which('openssl'):                          # (the same certificate and key, encrypted)
                pw = 'pass:nova-tls-password'
                pem = str(Path(d) / 'alice.pem')
                subprocess.run(['openssl', 'pkcs12', '-in', str(Path(d) / 'alice.pfx'), '-passin', pw, '-nodes',
                                '-out', pem], capture_output=True, check=True)
                for name, extra in (('aes', []), ('3des', ['-keypbe', 'PBE-SHA1-3DES', '-certpbe',
                                                           'PBE-SHA1-3DES', '-macalg', 'sha1'])):
                    path = str(Path(d) / f'alice-{name}.pfx')
                    subprocess.run(['openssl', 'pkcs12', '-export', '-in', pem, '-passout', pw, '-out', path] + extra,
                                   capture_output=True, check=True)
                    pfx[name] = subprocess.run([exe, 'pfx', path, 'nova-tls-password'], capture_output=True,
                                               text=True).stdout
                    pfx[name + ' wrong'] = subprocess.run([exe, 'pfx', path, 'wrong'], capture_output=True,
                                                          text=True).stdout
        got = dict(line.split(' ', 1) for line in out.stdout.splitlines() if ' ' in line)
        self.assertIn('md4 ok', out.stdout)
        # RFC 2759 9.2
        self.assertEqual(got['nt_response'], '82309ecd8d708b5ea08faa3981cd83544233114a3d85d6df')
        self.assertEqual(got['authenticator_response'], 'S=407A5589115FD0D6209F510FE9C04566932CDA56')
        self.assertIn('peap ok', out.stdout)
        self.assertEqual(got['server'], 'name radius.novacorp.test')
        self.assertEqual(len(got['msk']), 128)
        self.assertIn('domain ok', out.stdout)
        self.assertIn('wrong password refused', out.stdout)
        self.assertIn('unknown user refused', out.stdout)
        self.assertIn('untrusted server refused', untrusted.stdout)
        self.assertIn('pfx wrong password refused', out.stdout)
        self.assertIn('eap-tls ok', out.stdout)
        self.assertEqual(len(got['tls_msk']), 128)
        self.assertNotEqual(got['tls_msk'], got['msk'])
        self.assertIn('eap-tls untrusted certificate refused', out.stdout)
        for name in ('aes', '3des'):
            if name in pfx:
                self.assertEqual(pfx[name].splitlines()[-1], 'pfx ok alice chain 2 key RSA', name)
                self.assertTrue(pfx[name + ' wrong'].splitlines()[-1].startswith('pfx error -2'), pfx[name + ' wrong'])


if __name__ == '__main__':
    unittest.main()
