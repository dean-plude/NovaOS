## Qt's socket reads: `WSAIoctl(FIONREAD)`

GOG GALAXY's own requests (its configuration from `cfg.gog.com`, its
metrics and problem reports, the updater's file list) all ended with Qt
error 2, "Connection closed", while its Chromium pages loaded.  The
cause was not TLS but Winsock: before every read, Qt's socket engine
asks how many bytes are waiting with `WSAIoctl(FIONREAD)`, and ws2_32's
`WSAIoctl` answered `WSAEOPNOTSUPP` for it.  Qt took the -1 as a size,
read nothing, and treated that as the server closing the connection, so
every Qt TCP connection (TLS or not) failed at its first read.

`WSAIoctl` now answers `FIONREAD` as `ioctlsocket` does (the bytes
waiting) and `SIOCATMARK` as Windows does without urgent data (TRUE).
`overlaptest` checks both, 64- and 32-bit.

Qt's Schannel backend turns TLS 1.3 off on Windows builds before 20221,
so GALAXY's connections use TLS 1.2 on NovaOS (which reports build
18362); that path needed no change.
