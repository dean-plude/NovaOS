## The event log and Event Viewer

Dean's ask of 2026-10-14 ("a device manager, a task manager, a disk
manager and an event manager"), the event part: Windows' event log and
`eventvwr.exe`, "Event Viewer" in Start.

- **The EventLog service.**  `eventlog.exe` (service "EventLog",
  automatic start) keeps the Application, System and Security logs in
  `C:\Windows\System32\winevt\Logs` as classic `.evt` files (the "LfLe"
  header, `EVENTLOGRECORD`s, the end record), each with its size limit
  and sources under `HKLM\SYSTEM\CurrentControlSet\Services\EventLog`.
  It logs its own start and stop (6005, 6006) and a cleared log (104, or
  1102 in Security).
- **A kernel queue in front of it.**  Events go to the kernel
  (`kernel/um/um_eventlog.c`, `NtNovaGuiCtl` CTL_EVENTLOG) and stay
  queued until the service has written them, so events from before the
  service starts are kept.  The kernel logs each boot (Kernel-General
  12), a driver that fails to load (Service Control Manager 7000) and a
  program that crashes (Application Error 1000, with the program, its
  module, the exception code and the address).
- **The service control manager** logs a service entering the running,
  stopped or paused state (7036), failing to start (7000), stopping
  unexpectedly (7034) and being installed (7045).
- **The classic API** in advapi32 is real: `RegisterEventSource`,
  `ReportEvent`, `OpenEventLog`, `ReadEventLog` (sequential and seek,
  both directions, ANSI), `GetNumberOfEventLogRecords`,
  `GetOldestEventLogRecord`, `GetEventLogInformation`,
  `BackupEventLog`/`OpenBackupEventLog`, `ClearEventLog` and
  `NotifyChangeEventLog`.  Messages come from a source's
  `EventMessageFile` (kernel32's `FormatMessage` now reads a module's
  message table) or NovaOS's own table for its sources.
- **The Evt API** (wevtapi) reads the same logs: `EvtQuery` with the
  XPath filters Event Viewer and programs write (level, event ID,
  provider, record, keywords, time), `EvtNext`, `EvtSeek`, `EvtRender`
  (XML and the system values), `EvtFormatMessage`, bookmarks,
  `EvtSubscribe`, log information, channel and publisher enumeration,
  `EvtClearLog` and `EvtExportLog`, which writes a real `.evtx` file
  (ElfFile header, 64 KB chunks with their CRC32s, Binary XML records).
- **Event Viewer.**  The Windows Logs tree, the list (level, date and
  time, source, event ID, task category), and General and Details (XML)
  tabs for the selected event; Filter by level, Clear Log (with a backup
  offer) and Save All Events As `.evtx` or `.xml`.
- **Test.**  `evtlogtest` (core self-tests) reports events with
  `ReportEventW` and `ReportEventA` and reads them back with
  `ReadEventLog` and `EvtQuery`, and checks the boot, service,
  driver-failure and crash events; then Event Viewer is opened from the
  Start menu and `eventvwr --check` finds the test's event, filters,
  saves both formats and clears the log, taking the reference screenshot
  (`docs/screenshots/event-viewer.png`).

Not yet: the Applications and Services Logs channels (Microsoft-Windows-*
operational logs), custom views, event templates in `.evtx` exports
(each record carries its own XML), the Security log's audit events, and
reading `.evtx` files back.
