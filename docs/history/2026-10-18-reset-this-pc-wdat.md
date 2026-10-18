## Reset this PC, the firmware's watchdog and Recovery in Settings

The last pieces of roadmap step 33.3 ([design](../design/recovery.md)).
Reset this PC works as Windows' "Keep my files": `systemreset /keep` in
the Terminal, or Reset PC in Settings > System > Recovery, restarts, and
the next start removes the apps, drivers and settings, keeps the accounts
and their files (`C:\Users` and the first account's folders at the top of
C:), writes a report to `C:\Windows\Logs\SystemReset\ResetReport.txt` and
restarts once more.  The update's hang watch now uses the firmware's
watchdog when the ACPI WDAT table describes one, as Windows does, before
the ICH9 TCO timer NovaOS knew already; `watchdog` in the Terminal says
which.  The same Settings page restarts into the Advanced startup menu
(Restart now).  The devices suite's `rollbacktest` boot gives QEMU a WDAT
table (`tools/mkwdat.py`), lets the watchdog reset the PC through it, and
resets the PC with a file, a folder and a setting to check.
