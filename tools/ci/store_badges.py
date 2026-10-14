#!/usr/bin/env python3
"""The App Store's badges from a nightly app corpus run (Phase 27.5).

    tools/ci/store_badges.py REPORTS OUT.json

Reads the per-program reports the corpus-result job collected (REPORTS,
as tools/ci/corpus_report.py does; CORPUS_MATRIX names the jobs) and
writes OUT.json, which the nightly workflow pushes to the store-badges
branch and the App Store fetches after each catalog refresh
(docs/app-store-catalog.md).  Each App Store program the corpus tests
gets "passed" when every one of its corpus tests passed and "failed"
when any failed; a test that was skipped or did not run gives no
verdict.  A program is matched to its App Store entry by the test's
store= name, else by its download URL, and its rating comes from the
docs/compatibility/ file whose "checked:" line links the test."""
import datetime
import json
import os
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / 'tools'))
from corpus_report import collect  # noqa: E402

RATINGS = ('Platinum', 'Gold', 'Silver', 'Bronze', 'Borked')


def ratings(compat_dir):
    """{corpus test file name: rating} from docs/compatibility/*.md"""
    out = {}
    for f in sorted(Path(compat_dir).glob('*.md')):
        fields = dict(line.split(':', 1) for line in f.read_text().splitlines() if ':' in line)
        rating = fields.get('rating', '').strip()
        if rating not in RATINGS:
            continue
        for test in re.findall(r'tests/appcorpus/([\w.+-]+\.py)', fields.get('checked', '')):
            out.setdefault(test, rating)
    return out


def store_name(app, catalog):
    """@app's name in the App Store catalog, or None"""
    if app.store:
        return app.store
    names = [e['name'] for e in catalog['apps'] if e.get('url') == app.url]
    return names[0] if len(names) == 1 else None


def badges(rows, apps, catalog, rated):
    """The badge list from corpus result @rows (corpus_report.collect)"""
    by_name = {a.name: a for a in apps}
    verdicts, rating = {}, {}
    for row in rows:
        app = by_name.get(row['name'])
        name = store_name(app, catalog) if app else None
        if not name or row['outcome'] not in ('passed', 'failed'):
            continue
        verdicts[name] = 'failed' if 'failed' in (verdicts.get(name), row['outcome']) else 'passed'
        if rated.get(app.file):
            rating.setdefault(name, rated[app.file])
    order = [e['name'] for e in catalog['apps']]
    return [{'name': n, 'result': verdicts[n], 'rating': rating.get(n)}
            for n in sorted(verdicts, key=order.index)]


def main():
    reports, out = sys.argv[1], sys.argv[2]
    entries = json.loads(os.environ['CORPUS_MATRIX'])['include']
    rows = []
    for entry in entries:
        try:
            rows += collect(reports, {'include': [entry]})
        except (OSError, ValueError, KeyError, TypeError) as e:
            print(f'no report for {entry.get("app")}: {e}')
    import appcorpus
    catalog = json.loads((ROOT / 'userland' / 'store' / 'catalog.json').read_text())
    sha = os.environ.get('GITHUB_SHA', '')
    doc = {'schema_version': 1,
           'date': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%d'),
           'commit': sha[:7] or 'unknown',
           'run': f"https://github.com/{os.environ.get('GITHUB_REPOSITORY', '')}/actions/runs/"
                  f"{os.environ.get('GITHUB_RUN_ID', '')}",
           'apps': badges(rows, appcorpus.APPS, catalog, ratings(ROOT / 'docs' / 'compatibility'))}
    text = json.dumps(doc, indent=1) + '\n'
    if len(text.encode()) > 64 * 1024:
        sys.exit('store badges exceed the App Store\'s 64 KiB limit')
    Path(out).write_text(text)
    print(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
