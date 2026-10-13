#!/usr/bin/env python3
"""ax211_catalog.py — the Microsoft Update Catalog's answers for Intel's
Wi-Fi driver for the AX211, for the AX211 self-test
(tests/selftest/devices/ax211)

    ax211_catalog.py PORT WORKDIR

As ftdi_catalog.py (whose server it reuses), it serves on 127.0.0.1:PORT
(10.0.2.2:PORT from QEMU's user network):

  GET  /Search.aspx?q=ID          search.html for the AX211 of a ThinkPad
                                  T14 Gen 4, PCI\\VEN_8086&DEV_51F1&SUBSYS_00948086
                                  (recorded, cut to its first four of 25
                                  rows, all Intel's 24.40.0.4), and "no
                                  results" for any other
  POST /DownloadDialog.aspx       the update's file: the Windows 11 24H2
                                  update's cabinet, for any of those rows
  GET  /d/msdownload/update/driver/drvs/2026/04/NAME.cab
                                  Intel's signed cabinet (Netwtw6e.inf, its
                                  catalog, Netwtw14.sys, the firmware
                                  netwfw14.dat and the rest), as the Catalog
                                  serves it

The cabinet (24 MB) is the real one, signed by Microsoft (WHQL). It is not
in the repository: it is downloaded once into WORKDIR (or
NOVA_DRIVER_CACHE) and checked against the Catalog's SHA-256.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ftdi_catalog as fixture

fixture.PAGES = os.path.join(fixture.ROOT, 'tests', 'selftest', 'devices', 'ax211', 'catalog')
fixture.SEARCHES = {'pci\\ven_8086&dev_51f1&subsys_00948086': 'search.html'}
fixture.CAB_PATH = '/d/msdownload/update/driver/drvs/2026/04/'
fixture.CAB_NAME = 'a855de7b-b2e8-4192-8bca-2d3c74dc8665_2a7b7fffc6bd37f5093c6a0261f9aea45ac475f4.cab'
fixture.CAB_URL = 'https://catalog.s.download.windowsupdate.com' + fixture.CAB_PATH + fixture.CAB_NAME
fixture.SHA256 = 'Q2XabTrVHpQtXAkTclNtIWPcPKF/peOJ/u3LKa1eM58='
fixture.SHA1 = 'Knt//8a9N/UJPGoCYfmupFrEdfQ='
fixture.TITLE = 'Intel net Driver Update (24.40.0.4)'

if __name__ == '__main__':
    fixture.main()
