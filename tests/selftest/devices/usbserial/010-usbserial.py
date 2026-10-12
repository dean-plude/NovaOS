# usbserial: a Windows USB client driver (step 30.8).  QEMU's usb-serial
# device, an FTDI FT232 (USB\VID_0403&PID_6001), sits on an xHCI controller
# with no NovaOS driver, so it waits with problem code 28.
# "pnputil /scan-devices /search-online /install" finds FTDI's own driver
# by its hardware ID on tools/drivertest/ftdi_catalog.py at 10.0.2.2:18092
# (search pages recorded from the Microsoft Update Catalog; the cabinet is
# FTDI's real one, signed by Microsoft), installs ftdibus.inf on it: the PnP
# manager starts ftdibus.sys on the PDO of the USB bus interface
# (kernel/nt/usbd.c), which talks to the device through URBs and reports a
# child, the COM port (hardware ID FTDIBUS\COMPORT&VID_0403&PID_6001).  pnputil searches
# again, installs ftdiport.inf on that, and ftser2k.sys starts on it as
# COM3 (the ports class's first free name).  serialtest then opens COM3,
# sets 115200 8N1 and loops 4 KiB through the converter and back (QEMU's
# end of the line is a socket that sends back what it is sent).
DOC = ('QEMU\'s usb-serial device (an FTDI FT232) on xHCI: FTDI\'s own driver downloaded by hardware ID from the '
       'Update Catalog (recorded) and installed with `pnputil /scan-devices /search-online /install`; ftdibus.sys '
       'runs on the USB bus interface and reports the COM port, ftser2k.sys makes it COM3, and `serialtest COM3` '
       'loops data through it')
SOURCE = 'http://10.0.2.2:18092'
FT232 = r'USB\\VID_0403&PID_6001'
PORT = r'FTDIBUS\\COMPORT&VID_0403&PID_6001'               # its hardware ID
PORT_DEV = r'FTDIBUS\\VID_0403\+PID_6001\+'                  # its instance (+ serial number)
TESTS = [
    Test('usbserial problem 28', 'pnputil /enum-devices /connected /ids',
         [r'Instance ID: +' + FT232 + r'\\\S+\s+Device Description:[^\n]*\n(?:[^\n]+\n)*?'
          r'Problem Code: +28 \(0x1C\)']),
    Test('usbserial install', f'pnputil /scan-devices /search-online /source {SOURCE} /install',
         [r'Searching for: +' + FT232 + r' \(\d+ updates?, \d+ for this PC\)',
          r'Matching Device ID: +' + FT232,
          r'Driver package installed on device: ' + FT232,
          r'\[PNP\] ' + FT232 + r'\S*: started \(driver FTDIBUS\)',
          r'\[PNP\] ' + PORT_DEV + r'\S*: reported by',
          r'Searching for: +' + PORT,
          r'Driver package installed on device: ' + PORT_DEV,
          r'\[PNP\] ' + PORT_DEV + r'\S*: started \(driver FTSER2K\)'], timeout=600),
    Test('usbserial devices', 'pnputil /enum-devices /class Ports',
         [r'Instance ID: +' + PORT_DEV, r'Class Name: +Ports', r'Status: +Started', r'Driver Name: +FTSER2K']),
    # (the serial driver names its port where programs list them, as on Windows)
    Test('usbserial port name', r'reg query HKLM\HARDWARE\DEVICEMAP\SERIALCOMM', [r'REG_SZ +COM3']),
    Test('usbserial loopback', 'serialtest COM3',
         [r'115200 8N1 set: ok', r'loopback data matches: ok', r'serialtest: \d+ passed, 0 failed']),
]
