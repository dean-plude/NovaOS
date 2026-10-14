program: **qBittorrent 5.0.2** (App Store; 64-bit, Qt 6, NSIS installer)
kind: program
rating: Bronze
works: Installs from the App Store (its NSIS installer, silently, which elevates itself through UAC) and opens a `.torrent`: the Legal Notice on first start, then the Add torrent dialog over the main window with the torrent's name, size, date, info hash and file list ([history](history/2026-10-14-qbittorrent.md))
problems: Nothing downloaded or seeded (the test has no peers or tracker); its crash handler's stack trace has no symbols (NovaOS has no debugger engine behind `dbgeng.dll`)
checked: nightly corpus ([`956-qbittorrent.py`](../tests/appcorpus/956-qbittorrent.py))
