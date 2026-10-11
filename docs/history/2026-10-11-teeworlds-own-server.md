## Teeworlds joins its own server

Teeworlds now joins a game on its own server in the nightly corpus
(`tests/appcorpus/926-teeworlds-join.py`): its dedicated server
(`teeworlds_srv.exe`, in the same zip) is opened from the Terminal, the
game starts in full screen with `connect 127.0.0.1:8303` on its command
line, and the connection goes over NovaOS's loopback interface into the
server's map: the server must log "player has entered the game" and
neither end may drop the connection for the next 20 seconds.  Where the
tee lands differs from run to run, so the corpus keeps this screenshot
without a reference picture (`App(reference=False)` in
`tools/appcorpus.py`) and the interaction decides.  Under QEMU's TCG the join still times out (Mesa
compiles its shaders for longer than the ten seconds both ends wait), so
only the corpus's KVM runs judge it.

The server's log showed one NovaOS fault on the way: its IPv6 socket
could not bind `[::]:8303` after its IPv4 socket had `0.0.0.0:8303`
(`WSAEADDRINUSE`), because NovaOS's IPv6 sockets always took both
families and ignored `IPV6_V6ONLY`.  On Windows that option is on by
default, so the two binds coexist.  Now NovaOS keeps the option per
socket (`setsockopt`/`getsockopt` at `IPPROTO_IPV6`), on by default: an
IPv6 socket bound to `::` takes IPv6 alone and leaves the port's IPv4
side to an IPv4 socket, and one with the option turned off takes both
families as before.  `looptest` checks both.
