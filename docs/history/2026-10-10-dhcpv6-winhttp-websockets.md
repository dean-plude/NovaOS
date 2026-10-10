## Stateless DHCPv6, kept WinHTTP connections and WinHTTP WebSockets

lwIP's stateless DHCPv6 client now runs: when a router's advertisement
sets the "other configuration" flag, NovaOS asks for DNS servers by
DHCPv6 and adds them after the ones DHCPv4 and router advertisements gave,
never replacing them.  Stateful DHCPv6 (an address handed out by the
server) is still not supported by lwIP's client.

WinHTTP keeps an HTTP/1.1 connection on its connection handle when a
response ends cleanly and reuses it for the next request to the same
server for up to 30 seconds; a request on a kept connection the server has
since closed is sent once more on a new one.  `WINHTTP_DISABLE_KEEP_ALIVE`
and `Connection: close` turn it off.

WinHTTP WebSockets work the way Windows programs ask for them: a request
with `WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET` sends the upgrade headers and
a random key, the server's `Sec-WebSocket-Accept` is checked against it,
and `WinHttpWebSocketCompleteUpgrade` hands back a WebSocket that sends
and receives whole messages or fragments (continuation frames), answers
pings and closes with a status and reason.  The calls are synchronous,
including on asynchronous sessions.  `httptest` checks connection reuse and
an echoed WebSocket message, whole and in two fragments.
