## Corpus downloads survive a bad minute at github.com

The scheduled nightly of 2026-10-12 (main `810709f`) failed only because
the SuperTux shard could not download 7-Zip from `github.com/ip7z/7zip`
(`curl` exit 22, an HTTP error), and PR #325's shard (05) lost the same way on
MinGit.  Every program and the SMP stress passed.  `curl --retry` retries
only 408, 429 and 5xx answers, so a hosted runner that got a 403 or 404 for
a moment gave up at once.

- **`tools/download_cache.py`** retries every failed download (`curl
  --retry-all-errors`, then up to four more attempts after 10, 30, 60 and
  120 s) before the shard fails.  Pinned hashes are still checked on the
  bytes that finally arrive.
- **`tests/tools/test_download_cache.py`** covers the retry pauses and the
  final failure.
