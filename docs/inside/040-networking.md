- **Networking** (`kernel/net/`): lwIP for TCP/IP over IPv4 and IPv6.
  - DHCP, SLAAC and DNS over either, or a static address and DNS servers
    set in Settings > Network or with the Terminal's `netsh`, kept across
    restarts. A loopback interface carries 127.0.0.1 and ::1, and
    `localhost` resolves to both without DNS.
  - An HTTP/1.1 client and Mbed TLS with the Mozilla root store in the
    kernel, which the App Store and updates download with.
  - Winsock (`ws2_32`, and `wsock32` with Winsock 1.1's ordinals): IPv6 and
    dual-stack sockets, `getaddrinfo`, socket options that reach the stack
    and read back (`TCP_NODELAY`, timeouts, `SO_LINGER`, `SO_REUSEADDR`,
    `SO_KEEPALIVE`, buffer sizes, `IPV6_V6ONLY` on by default as on
    Windows), hundreds of sockets per program, `select`, `WSAPoll` and
    `WSAEventSelect` on any number of them, connected UDP sockets and
    `WSADuplicateSocket`.
  - Overlapped `AcceptEx`, `ConnectEx` and `WSARecv` stay pending and
    complete on an I/O completion port, as proactor event loops such as
    Python's asyncio need.
  - `winhttp` is an HTTP client over Schannel TLS, with HTTP/2 through
    ALPN (nghttp2).
