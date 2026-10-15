- ~~IPv6, HTTP/2~~ Done (Phase 18.8): lwIP's IPv6 (SLAAC, RDNSS, MLD),
  dual-stack Winsock with `getaddrinfo`, `ping -6` and `curl -6`; a
  virtio-net driver; `winhttp` on Schannel with HTTP/2 by ALPN (nghttp2).
  The `network` self-test suite checks both.  Stateful DHCPv6 supplies an
  IPv6 address and DNS configuration.  WinHTTP reuses HTTP/1.1 connections and supports
  WebSockets.  HTTP/3 remains to do; it needs a QUIC transport.
