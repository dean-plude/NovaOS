- Plug and Play: a device tree the Windows way.  Devices plugged in or
  removed while NovaOS runs already work (USB hot-plug: keyboards, mice,
  hubs, sticks, audio, game controllers), and ~~a PnP device tree~~
  Done: the root, the PCI functions, the USB root hubs and devices, their
  functions and HID and disk children, with Windows instance, hardware
  and compatible IDs, classes, drivers, devices not present kept, and
  arrival and removal events for programs (`CTL_PNP`).  Still to do: the
  PlugPlay service reported running, `WM_DEVICECHANGE` and
  `RegisterDeviceNotification` (`DBT_DEVICEARRIVAL`,
  `DBT_DEVICEREMOVECOMPLETE`, `DBT_DEVNODES_CHANGED`, volumes),
  `CM_Register_Notification`, and setupapi's and cfgmgr32's enumeration
  of the tree (`SetupDiGetClassDevs` for every class, `CM_Locate_DevNode`,
  `CM_Get_Child`/`Sibling`/`Parent`, device properties) with
  `pnputil /enum-devices` matching the driver store.
