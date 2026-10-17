## App Store downloads that failed on real networks

Dean saw many App Store downloads fail on his own machine.  The tests
never downloaded from the Store (they put the files in `C:\Downloads`
first), so four faults in the download path had gone unnoticed:

- **GitHub's downloads (28 of the catalog's 57 programs):** github.com
  answers with a redirect to a signed address of about 900 characters.
  The Store kept 511 of them, so the signature was cut and the download
  refused (or the request itself was cut short).  URLs and `Location`
  headers up to 2,047 characters (`NET_URL_MAX`) are now followed by the
  App Store, the Terminal's `wget` and NovaOS's own updates, and a
  request too long to send is refused rather than cut.
- **30 seconds for a whole download:** the kernel's HTTP client gave each
  request 30 seconds from start to end, so any installer that took longer
  (GIMP's 330 MB, Krita, OBS Studio...) failed with "Timed out".  The 30
  seconds now count from the last byte received.
- **No second try:** a timeout, a dropped connection, a name lookup that
  failed, or a 408, 429 or 5xx answer now starts the download again from
  the catalog's address after 3 s, then 6 s (three tries in all); a
  certificate problem or a file too large fails at once.  The row names
  the server ("github.com replied 404"), and each failed try is logged.
- **256 MB at most:** LibreOffice (350 MB) and GIMP were refused before
  they started.  Responses may now be up to 1 GB, and one that gives its
  length gets its buffer at that size at once (no copies as it grows,
  never twice its size in memory).  A full drive C: says so.

Tested by `tests/selftest/network4/090-store-download.py`: the Store takes
its catalog from `tools/h2server.js` over HTTPS and downloads through a
~1,000-character signed redirect, a 503 and a body trickled over ~40 s.
