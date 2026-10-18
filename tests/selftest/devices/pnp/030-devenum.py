# pnp: walking the device tree as Windows programs do, after 020 (only
# phantoms left on the xHCI controller).  devenumtest asks for a USB
# keyboard at "[PNP] step 1" and, once it is in the tree, walks it with
# cfgmgr32 (CM_Locate_DevNode, CM_Get_Child/Sibling/Parent, status,
# properties, device ID lists and their filters, a phantom, interface
# lists, the device's key) and setupapi (SetupDiGetClassDevs by class, by
# enumerator and by interface, SetupDiOpenDeviceInfo/Interface,
# SetupDiOpenDevRegKey).  The keyboard stays plugged in for pnputil: it
# publishes tools/drivertest/mkdriver.py's novakbd.inf, and
# /enum-devices /drivers lists the package as matching the keyboard,
# outranked by NovaOS's own keyboard class driver (kbdhid); /disconnected lists the phantoms.


def _plug_keyboard(nova):
    r = nova.qmp.cmd('device_add', driver='usb-kbd', id='kbd3', bus='xhci.0', port='1')
    if 'error' in r:
        print('device_add:', r['error'], flush=True)


TESTS = [
    Test('pnp device enumeration', 'devenumtest', [r'devenumtest: \d+ passed, 0 failed'], timeout=180,
         acts=[(r'\[PNP\] step 1: plug in a USB keyboard', _plug_keyboard)], elevated=True),
    Test('pnp driver package', r'pnputil /add-driver C:\Tests\PnpDriver\novakbd.inf',
         [r'Driver package added successfully', r'Published Name: +oem\d+\.inf']),
    Test('pnp devices and drivers', 'pnputil /enum-devices /connected /class Keyboard /ids /drivers',
         [r'Instance ID: +HID\\VID_0627&PID_0001\\\S+',
          r'Device Description: +HID Keyboard Device',
          r'Class GUID: +\{4d36e96b-e325-11ce-bfc1-08002be10318\}',
          r'Status: +Started',
          r'Driver Name: +kbdhid \(a NovaOS driver\)',
          r'Hardware IDs:\s+HID\\VID_0627&PID_0001',
          r'Original Name: +novakbd\.inf',
          r'Matching Device ID: +HID\\VID_0627&PID_0001',
          r'Driver Status: +Outranked by the NovaOS driver kbdhid']),
    Test('pnp disconnected devices', 'pnputil /enum-devices /disconnected',
         [r'Instance ID: +USBSTOR\\Disk&Ven_&Prod_\S+', r'Status: +Disconnected']),
]
