## Programs answer query-remove before the drivers

`DBT_DEVICEQUERYREMOVE` reached programs only as a notice: by the time a
window saw it, the PnP manager had already sent
`IRP_MN_QUERY_REMOVE_DEVICE` down the stack, so a program could neither
refuse the removal nor close its handle first.

Now it is Windows' synchronous round.  user32 tells the kernel's device
tree about each handle registration (`RegisterDeviceNotification` with a
`DEV_BROADCAST_HANDLE`), and before the drivers are asked
(`PnpQueryRemove` in `kernel/io/pnp.c`, called from `kernel/nt/pnp.c`) the
PnP manager waits for every such program to answer the query.  A window
or service returning `BROADCAST_QUERY_DENY` vetoes the removal: the device
gets no IRP and the programs get `DBT_DEVICEQUERYREMOVEFAILED`.  The wait
is bounded at ten seconds, so a hung program cannot hold a removal up,
and a program that exits stops being waited for; a surprise removal asks
nobody.  `edutest notify` (the devices suite's edu boot) refuses once and
then agrees, closing its handle, so the driver accepts.

A HID collection re-plugged with an installed third-party driver now also
keeps the class its INF gave (the Enum key's `Class`) instead of coming
back as `HIDClass`.
