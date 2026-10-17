# The App Store downloading as from a real site (kernel/apps/store.c) against
# tools/h2server.js: its certificate trusted (certutil), the catalog taken
# from https://10.0.2.2:18443/store/catalog.json, and "Store Download Test"
# fetched: a redirect to a signed address of ~1,000 characters, as GitHub's
# release downloads (a cut one gets 403), a 503 the Store tries again
# after, then 6 MB trickled over ~40 s (once more than the limit on a
# whole request).  Last: the catalog stays the test's for this boot.
import re, time


def catalog_refreshed(nova):
    """Wait until the refresh the test started has finished"""
    done = re.compile(r'\[STORE\] Catalog: (Catalog and badges updated|Catalog updated; badges kept|Refresh failed|Invalid)')
    end = time.time() + 180
    while time.time() < end and not done.search(open(nova.serial_path, 'rb').read().decode('latin-1')):
        time.sleep(1)


CATALOG = 'https://10.0.2.2:18443/store/catalog.json'
TESTS = [
    Test('store trust', 'wget http://10.0.2.2:18080/store/ca.pem', [r'Saved \d+ bytes'], builtin=True),
    Test('store certutil', r'certutil -addstore root C:\Downloads\ca.pem', [r'completed successfully'], builtin=True),
    Test('store catalog', rf'cmd /c "echo {CATALOG}> C:\Windows\AppStore\catalog-url.txt"'),
    Test('store refresh', 'store refresh', [r'Refreshing catalog'], builtin=True),
    Test('store download', 'store install Store Download Test', [r'Downloaded 6\.0 MB'], store='Store Download Test',
         before=catalog_refreshed, timeout=420),
    Test('store retried', r'dir C:\Downloads\storetest.exe', [r'6291456\s+storetest\.exe'], builtin=True,
         boot_expect=[r'\[STORE\] Catalog: now 1 apps',
                      r'\[STORE\] Download of Store Download Test from 10\.0\.2\.2 \(attempt 1 of 3\) failed: '
                      r'10\.0\.2\.2 replied 503; trying again']),
]
