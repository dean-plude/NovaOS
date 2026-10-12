## Drivers downloaded by hardware ID from the Microsoft Update Catalog

Step 30.6 of the Windows driver model (roadmap Phase 30).  A device
without a driver (problem code 28) gets its package the way Windows
Update finds one: `pnputil /scan-devices /search-online` searches the
Microsoft Update Catalog for the device's hardware IDs, most specific
first, then its compatible IDs, reads the results table, ranks the driver
updates as a 64-bit Windows 11 PC takes them (Windows 11 client first,
then Windows 10, then server; no ARM64; newest first), asks the Catalog's
download dialog for the update's cabinet with its SHA-1 and SHA-256,
downloads it with WinHTTP to
`C:\Windows\SoftwareDistribution\Download\Drivers\<update ID>`, refuses it
unless the SHA-256 matches, expands it with `cabinet.dll`, and stages the
first INF with an x64 model for one of the device's IDs in the driver
store as `oemN.inf`.  The package's `DriverDatabase` key records the
update it came from (ID, title, products, version, date, address, SHA-256,
SHA-1, kept cabinet, matched ID) for the signing checks of step 30.7, and
`pnputil /enum-drivers` shows the source.  Nothing is downloaded unless
the user asks, and nothing Microsoft or a vendor ships goes into the
image.  The package is staged, not started (that is step 30.3).

The new `driversearch` devices boot gives QEMU's virtio-serial port, which
has no NovaOS driver, and a network on which
`tools/drivertest/catalog_fixture.py` answers as the Catalog does, from
pages recorded from it: the search finds nothing for the most specific
ID and three updates for the next, the first-ranked update's cabinet
fails its SHA-256 and is refused, and the next one is downloaded, checked
and staged; a second search finds the package already in the store.
