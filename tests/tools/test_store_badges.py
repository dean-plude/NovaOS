"""tools/ci/store_badges.py: the App Store's badges from nightly corpus results."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'ci'))
import store_badges  # noqa: E402


class App:
    def __init__(self, name, file, store=None, url=None):
        self.name, self.file, self.store, self.url = name, file, store, url


CATALOG = {'schema_version': 1, 'apps': [
    {'name': 'Krita', 'url': 'https://example.org/krita.zip'},
    {'name': 'Teeworlds', 'url': 'https://example.org/tw.zip'},
    {'name': 'VLC media player', 'url': 'https://example.org/vlc.zip'},
    {'name': 'PuTTY', 'url': 'https://example.org/putty.exe'}]}
APPS = [App('Krita', '885-krita.py', store='Krita'),
        App('Teeworlds', '925-teeworlds.py', url='https://example.org/tw.zip'),
        App('Teeworlds (join)', '926-teeworlds-join.py', url='https://example.org/tw.zip'),
        App('VLC', '870-vlc.py', url='https://other.example.org/vlc-portable.zip'),
        App('PuTTY', '950-putty.py', store='PuTTY')]


def row(name, outcome):
    return {'name': name, 'version': '1', 'outcome': outcome, 'reason': outcome}


class StoreBadgesTests(unittest.TestCase):
    def test_verdicts_ratings_and_matching(self):
        rows = [row('PuTTY', 'not run'), row('Teeworlds', 'passed'), row('Teeworlds (join)', 'failed'),
                row('Krita', 'passed'), row('VLC', 'passed')]
        rated = {'885-krita.py': 'Silver', '926-teeworlds-join.py': 'Gold'}
        self.assertEqual(store_badges.badges(rows, APPS, CATALOG, rated), [
            {'name': 'Krita', 'result': 'passed', 'rating': 'Silver'},
            # any failed test fails the program; VLC's test used another download: no badge;
            # PuTTY did not run: no verdict
            {'name': 'Teeworlds', 'result': 'failed', 'rating': 'Gold'}])

    def test_ratings_follow_checked_links(self):
        rated = store_badges.ratings(ROOT / 'docs' / 'compatibility')
        self.assertEqual(rated.get('885-krita.py'),
                         [l for l in (ROOT / 'docs/compatibility/280-krita.md').read_text().splitlines()
                          if l.startswith('rating:')][0].split(':', 1)[1].strip())

    def test_script_writes_a_file_the_store_accepts(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / 'appcorpus-01'
            report.mkdir()
            counts = {'passed': 1, 'failed': 0, 'skipped': 0, 'not run': 0}
            (report / 'results.json').write_text(json.dumps({'counts': counts, 'programs': [row('Krita', 'passed')]}))
            env = dict(os.environ, CORPUS_MATRIX=json.dumps({'include': [{'id': '01', 'app': 'Krita'},
                                                                         {'id': '02', 'app': 'Missing'}]}),
                       GITHUB_SHA='549c6c74ffff', GITHUB_REPOSITORY='dean-plude/os', GITHUB_RUN_ID='7')
            out = Path(tmp) / 'store-badges.json'
            subprocess.run([sys.executable, str(ROOT / 'tools/ci/store_badges.py'), tmp, str(out)],
                           env=env, check=True, capture_output=True)
            doc = json.loads(out.read_text())
            self.assertEqual((doc['schema_version'], doc['commit']), (1, '549c6c7'))
            self.assertEqual([a['name'] for a in doc['apps']], ['Krita'])
            self.assertRegex(doc['date'], r'^\d{4}-\d\d-\d\d$')


if __name__ == '__main__':
    unittest.main()
