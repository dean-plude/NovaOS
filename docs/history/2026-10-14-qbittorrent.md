## qBittorrent opens a torrent

Roadmap step 27.1 (grow the corpus), its third app: qBittorrent 5.0.2,
the BitTorrent client the App Store lists, a 64-bit Qt 6 program with an
NSIS installer.  The nightly corpus now runs the Store's download (put in
`C:\Downloads`) silently, as the Store's Install runs it, and opens
`C:\Apps\data\corpus.torrent`, a one-file torrent the corpus makes
(`tests/appcorpus/956-qbittorrent.py`).  Enter accepts the Legal Notice
qBittorrent shows on its first start; its Add torrent dialog (save path,
size, date, info hash and the file list) must then match
`tests/reference/qbittorrent.png`.  Nothing is downloaded: the test has no
peers or tracker.  It needed four NovaOS fixes:

- **UAC is on.**  The installer's UAC plug-in asks `kernel32`'s
  `CheckElevationEnabled`, then the `EnableLUA` policy value, before it
  relaunches itself elevated with "runas"; NovaOS had neither, so the
  plug-in took UAC for switched off and waited on its own Windows XP style
  Run As dialog.  `CheckElevationEnabled` now says UAC is on, and the
  registry carries `EnableLUA` = 1 under
  `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System`, as on
  Windows 11 (`setuptest` checks both).
- **Missing imports.**  qBittorrent imports `dbgeng.dll`'s `DebugCreate`
  for the stack trace it prints when it crashes; the new `dbgeng.dll`
  answers that there is no debugger engine.  `user32` gains
  `ShutdownBlockReasonCreate`, `ShutdownBlockReasonDestroy` and
  `ShutdownBlockReasonQuery` (the reason is kept on the window) and
  `GetPointerFrameTouchInfoHistory`.
- **A local unwind inside a C++ frame handler.**  qBittorrent parses its
  argument as a magnet link first and catches libtorrent's exception.
  Microsoft's C++ runtime of today, linked into the program, leaves a
  `__try` in its own frame handler with `_local_unwind` while that unwind
  runs.  `RtlUnwindEx` took every unwind started inside a frame handler
  for a collided one and restarted it at the frame being unwound, so the
  frame handler came back with the wrong registers and the catch resumed
  `TorrentDescriptor::parse` at the wrong stack pointer (a stack cookie
  failure, status 0xC0000409).  An unwind whose target frame is below the
  frame being unwound now starts at its caller, as one from no handler
  (`unwindtest` checks it).
- **Hiding the active window.**  Qt hides a closed dialog rather than
  destroying it; NovaOS left the hidden window active, so the next key
  went to no window and Alt+F4 closed the Terminal.  As on Windows,
  hiding the active window now activates its owner or else the topmost
  other window.

Under QEMU without KVM the installer finishes in about 35 seconds and the
Add torrent dialog shows within four minutes of starting qBittorrent.
