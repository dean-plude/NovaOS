## DOS programs start like Windows programs

Roadmap step 31.3.  A 16-bit DOS program now starts the way a Windows one
does: by name in `cmd` or the Terminal, by a double-click in File Explorer,
or through `CreateProcess`.  As Windows started `ntvdm.exe` for such an
image, the kernel's process start sees an `MZ` image with no Windows header
after it, or a `.COM` file that is not an `MZ` image, and starts
`ntvdm.exe -run PROGRAM ARGUMENTS` instead.  DOS sees NovaOS's drive C:
through its own network redirector interface (`INT 2Fh AH=11h`), answered
by the BIOS from NovaOS's files, with 8.3 aliases for long names
(`MYDOSP~1`) and the caller's current folder as DOS's; nothing is copied.
`REDIR.COM`, which the boot disk's `AUTOEXEC.BAT` runs, installs the drive
and runs the program; when it ends the console is Windows' again, with what
the program printed still on it and its exit code as the process's.
`dostest` runs a `.COM` from `cmd` by name and an `.EXE` from
`CreateProcess`, each reading and writing a file on C:, and CI's MS-DOS job
runs the same on Linux and types `DIR`, `CD`, `COPY`, `REN`, `DEL` and
`MEM` on drive C:.  See [dos.md](dos.md).
