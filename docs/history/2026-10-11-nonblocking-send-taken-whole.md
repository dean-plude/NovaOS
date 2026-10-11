## Non-blocking TCP sends taken whole, as on Windows

A non-blocking `send` or `WSASend` on a TCP socket now takes the whole
buffer, as Windows' AFD driver does, instead of only what fits in lwIP's
11,680-byte send buffer.  What does not fit waits in a per-socket buffer
(up to 4 MB) and goes out as the peer acknowledges; later sends join it,
and only past that limit does a send see `WSAEWOULDBLOCK`.  A
`shutdown(SD_SEND)` or `closesocket` waits for that data, so the peer
still gets every byte before the end of the stream.  Programs written for
Windows treat a short non-blocking send as all-or-nothing and never send
the rest: Steam's client answered its UI's controller list request
(`GetControllerAccessibilityStrings`, a 28 KB websocket frame) with only
its first 11,680 bytes, so the UI waited for ever and the login window
never opened.  `looptest` in the network suite sends 100,000 bytes on a
non-blocking socket, closes it at once, and checks the peer reads them
all.
