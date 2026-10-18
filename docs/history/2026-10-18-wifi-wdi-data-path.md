## Wi-Fi: the WDI data path, and open networks connect

Second part of roadmap step 28.1.  NovaOS's WDI upper edge
(`kernel/nt/wdi.c`) could scan, but no frame went through a Wi-Fi driver.
Now it has the data path and joins open networks:

- **The data path** (`kernel/nt/wdi.h`): the TAL TX/RX interface of
  Windows' `wdiwifi.sys`.  `MiniportWdiTalTxRxInitialize` gets NovaOS's
  TxMgr and RxMgr (`NDIS_WDI_DATA_API`) and gives back the driver's
  handlers.  A frame is an NBL of one 802.11 MSDU with its
  `WDI_FRAME_METADATA` at `MiniportReserved[0]`.  To send, NovaOS queues
  it and calls `TxDataSend`; the driver takes it with
  `TxDequeueIndication` (after `TxTargetDescInit`) and gives it back with
  `TxTransferCompleteIndication`.  To receive, the driver calls
  `RxInorderDataIndication`, and NovaOS takes the frames with `RxGetMpdus`
  and returns them with `RxReturnFrames`.
- **Bring-up in Windows' order**: the data path, the adapter's
  capabilities (now with its data path attributes), its configuration
  (`OID_WDI_SET_ADAPTER_CONFIGURATION`: the signal bar map),
  `TalTxRxStart`, then the station port and `TalTxRxAddPort`.  Teardown
  runs the same steps in reverse.
- **Connecting** (`OID_WDI_TASK_CONNECT`): the SSID, open authentication
  and the access points the scan heard.  The driver indicates its peer
  (the access point) and the association's result; NovaOS configures the
  peer.  `OID_WDI_TASK_DISCONNECT` leaves.
- **The network stack over Wi-Fi**: once connected, the adapter is lwIP's
  (`NetAttach`).  Ethernet frames become 802.11 frames to the access point
  (ToDS, LLC/SNAP), and received 802.11 frames become Ethernet again, so
  DHCP, DNS, ping and TCP run over Wi-Fi.  Disconnecting gives the stack
  back the card it had before (`NetDetach` now restores it).
- **`netsh wlan connect name=SSID`** and **`netsh wlan disconnect`** in
  the Terminal.  `netsh wlan show interfaces` shows the connection, and
  `ipconfig` shows the "Wireless LAN adapter Wi-Fi".

The `wifi` boot's `wditest.sys` now has a TAL too.  Behind its open
network it plays a small router at 192.168.77.1 that answers ARP, hands
out 192.168.77.100 by DHCP and answers pings, so the test runs DHCP and
ping through the whole data path, 802.11 frames both ways.  Only open
networks connect so far.  WPA2 and WPA3 come next, then `wlanapi.dll`
and Settings' Wi-Fi page.
