## App Store badges from the nightly corpus

Roadmap step 27.5 (which completes 20.7, ratings kept by CI): the App
Store shows, beside each program's name, how it did in the latest
scheduled nightly app corpus run.

- **The verdicts.**  After a scheduled run that ran the corpus, the
  **Corpus result** job turns the per-program reports into
  `store-badges.json` (`tools/ci/store_badges.py`): one entry per App
  Store program the corpus tests with the Store's own download (its
  test's `store=` name, or the same download URL), `passed` when every
  one of its tests passed and `failed` when one failed, with its rating
  from the `docs/compatibility/` file that links the test.  The job
  force-pushes that file as the one commit of the `store-badges` branch.
- **In the App Store.**  A catalog refresh then downloads the file from
  `raw.githubusercontent.com` (no redirects, the same HTTPS path as the
  catalog), validates it with the catalog's JSON reader and saves it as
  `C:\Windows\AppStore\badges.json`.  Each row shows a green "Verified:
  Silver" (or plain "Verified" without a rating) or a red "Failed last
  nightly", and the sidebar names the night ("Nightly 2026-10-14").  A failed badge download
  keeps the old badges, and offline the App Store shows the saved ones.
- **What has no badge.**  Programs the corpus does not test, and those
  whose test downloads another build than the catalog's (for example
  VLC's portable package, or an older 7-Zip), so a badge always means
  "this download passed".  Moving those tests onto the Store's download
  is part of growing the corpus (27.1).

Checked with the host tests (`test_store_catalog.py` runs the native
parser and refresh on the host compiler; `test_store_badges.py` builds the
file from reports) and by booting NovaOS in QEMU with a canned
`badges.json` on drive C:.
