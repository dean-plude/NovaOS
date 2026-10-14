- **Firefox**: Mozilla's unmodified Firefox 157.0 for Windows x64 is in the
  image (`tools/bundle_firefox.py` downloads it and checks it against
  Mozilla's SHA-512 sums) at `C:\Programs\Mozilla Firefox\core`, and opens
  web links and local HTML files. NetSurf stays installed and takes over
  when Firefox is missing. The nightly app corpus loads an HTTPS page in
  it and compares a screenshot ([building-firefox.md](building-firefox.md)).
