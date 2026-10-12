## A default set of drivers and firmware

Windows and Linux ship a default set of drivers.  NovaOS's drivers are
its own, built into the kernel as Linux's in-tree drivers are, so every
one was already on the image; what a Linux distribution downloads on top
is firmware, the code a driver loads into the device, published by the
vendors in linux-firmware.  NovaOS now does the same
([drivers.md](drivers.md)).

- **`tools/firmware.json`** names the default firmware set, grouped by
  device: Intel Raptor Lake-P graphics (`i915` DMC, GuC and HuC), Intel
  AX211 Wi-Fi and Bluetooth, Qualcomm WCN6855 (NFA725A) Wi-Fi and
  Bluetooth, the reference ThinkPad's devices whose NovaOS drivers come
  next.  Each file is pinned to linux-firmware tag `20260916` and its
  SHA-256, with its vendor's licence, all of which allow redistributing
  the files unchanged.
- **`tools/fetch_firmware.py`** downloads them (git.kernel.org, then the
  GitLab mirror, with retries), checks every hash, keeps files it already
  has and runs `tools/fetch_sof_firmware.py --all` for the audio DSP;
  `--set`, `--list` and `--from` (a local checkout) choose and inspect.
- **The build** puts the files in `C:\Windows\Firmware` in
  linux-firmware's layout (`C:\Windows\Firmware\i915\adlp_dmc.bin`), with
  the licences in `C:\Windows\Firmware\LICENSES`; only files the manifest
  names, at their size, go in.  Release builds fetch the set before
  building; pull requests do not.
- **Windows driver packages** stay what they were: they install into the
  driver store, but their `.sys` files are Windows kernel-mode drivers
  that NovaOS's kernel does not host, so downloading Windows drivers would
  give nothing that runs; drivers.md says why.
