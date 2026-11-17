## A window parented across processes no longer lands at the wrong place

The `xpwin` selftest failed now and then on 64-bit (never 32-bit) with
every child window off by the same amount: "child at 30,40 200x150,
wanted 151,202", the numbers of its place in the parent but no parent
origin added.  The host's `MoveWindow` on a window another process made
and parented (`SetParent`) is answered by that process from its
sent-message path, while the desktop's news that the window is now
embedded (`WM_NOVA_EMBED`) comes through the kernel message queue.  When
the process took the sent call first it did not yet know the window was
embedded, read the place as a screen position and kept it, and nothing
corrected it afterwards.  `SetWindowPos` now asks the kernel whether the
window is embedded when it has not heard yet, so a place given to an
embedded window is always read in its parent's client area.
