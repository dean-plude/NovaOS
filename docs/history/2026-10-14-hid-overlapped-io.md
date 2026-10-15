## Overlapped I/O on Windows drivers' devices (hidapi)

The fourth HID step: hidapi, and most programs that talk to a HID
device, open it with `FILE_FLAG_OVERLAPPED` and read with a timeout:
`ReadFile` with an `OVERLAPPED`, `WaitForSingleObject` on its event,
`GetOverlappedResult`, and `CancelIo` when they give up. Until now every
request on a Windows driver's device finished before the system call
returned, so such a read blocked until a report came.

- **The I/O manager** (`kernel/nt/io.c`): on a handle opened for
  overlapped I/O, an IRP the driver leaves pending makes `NtReadFile`,
  `NtWriteFile` and `NtDeviceIoControlFile` return `STATUS_PENDING`.
  When the driver completes it, a kernel I/O completion thread copies
  buffered data back to the program, writes the `OVERLAPPED`'s status
  and sets its event, as Windows' completion APC does.
  `NtCancelIoFile` and `NtCancelIoFileEx` cancel such requests (and wait
  for them to finish); closing the handle cancels them and sends
  `IRP_MJ_CLOSE` after the last; a process that exits leaves none behind.
  This holds for every Windows driver, not only HID.
- **kernel32**: `DeviceIoControl` with an `OVERLAPPED` can now be left
  pending (`ERROR_IO_PENDING`), and finishes into a bound completion
  port like `ReadFile` and `WriteFile`.

`hidclasstest` checks it in the devices suite's "hid" boot, 32-bit and
64-bit: a pending read whose event times out and then completes with
the next report, `CancelIo` and `CancelIoEx`, an overlapped `WriteFile`
and `DeviceIoControl(IOCTL_HID_GET_FEATURE)`, and `CloseHandle` ending
a pending read.
