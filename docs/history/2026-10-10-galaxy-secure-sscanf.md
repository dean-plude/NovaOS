## The secure scanf: `sscanf_s`'s buffer sizes

GOG GALAXY stopped about three seconds after its "drive C: is not
NTFS" warning was answered, with an access violation in `ucrtbase.dll`
under Qt's network code.  Qt6Network parses with `sscanf_s`, which
MSVC's headers inline as `__stdio_common_vsscanf` with option 1
(`_CRT_INTERNAL_SCANF_SECURECRT`): after each `%s`, `%c` and `%[` comes
the buffer's size.  NovaOS's C runtime ignored the option, so it took
that size as the next conversion's pointer and wrote an `int` to
address 4.

The scanner now reads the size in the secure forms (`sscanf_s`, and the
UCRT's `__stdio_common_vsscanf`, `__stdio_common_vfscanf` and
`__stdio_common_vswscanf` with that option), takes none after `%*s`,
and fails a string that does not fit, emptying the buffer, as Windows
does.  `rttest` checks it, 64- and 32-bit.  GALAXY now keeps running
and shows its sign-in window.
