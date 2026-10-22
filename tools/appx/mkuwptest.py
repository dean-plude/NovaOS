#!/usr/bin/env python3
"""mkuwptest.py: the app package the winui2test self-test installs

    tools/appx/mkuwptest.py OUTDIR WINUI2TEST.EXE

tools/build_userland.py runs it and puts what it writes in C:\\Tests\\Appx:

  XamlUwpTest.msix  NovaOS.XamlUwpTest 1.0.0.0 (x64), unsigned with the
                    "unsigned" publisher OID: winui2test.exe as a UWP app
                    (tools/appx/test/XamlUwpManifest.xml: its entry point a
                    class, so kernel32's AppPolicyGetWindowingModel says
                    Universal and Windows.UI.Xaml's Application.Start takes
                    the UWP path, CoreApplication.Run)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import makeappx  # noqa: E402


def main(argv):
    if len(argv) != 2:
        raise SystemExit(__doc__)
    out, exe = argv
    os.makedirs(out, exist_ok=True)
    manifest = open(os.path.join(HERE, 'test', 'XamlUwpManifest.xml'), 'rb').read()
    files = [('winui2test.exe', open(exe, 'rb').read()), ('AppxManifest.xml', manifest)]
    makeappx.write(os.path.join(out, 'XamlUwpTest.msix'), makeappx.pack(files))


if __name__ == '__main__':
    main(sys.argv[1:])
