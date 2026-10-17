## USB unplug in Windows order

Unplugging a USB device took its PnP node away on the usb thread at once,
while the Windows driver stacks on it (HidUsb, the HID collections and
their kbdhid, mouhid and hiddigi class drivers) came down later on a work
item.  For a moment the device's collections were gone but its pen,
keys or buttons were still there: the touch boot's digitizer test had to
wait for the pen to leave after its collections did.

Now the usb thread waits for that work item (`NtPnpSurpriseRemoval` in
`kernel/nt/pnp.c`, bounded at five seconds) before it removes the node,
as Windows sends `IRP_MN_SURPRISE_REMOVAL` and `IRP_MN_REMOVE_DEVICE` up
from the leaves before the hub's removal completes.  A device that comes
straight back (a keyboard after sleep) is enumerated only after its old
stacks are down, so the old removal can no longer land on the new node.
`digitest` step 9 checks the pen once the collections are gone, without
waiting for it.
