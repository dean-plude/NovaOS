"""tools/appx/storefetch.py without the network: reading SyncUpdates'
answer, choosing the packages and the app's framework dependencies."""
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools' / 'appx'))
import storefetch

WU = 'http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService'
MS = '8wekyb3d8bbwe'
APP = f'Microsoft.WindowsCalculator_{MS}'


def entry(full, ext):
    return (full, f'{full[:8]}.{ext}', 100, 'uid-' + full, '1')


class ChooseTests(unittest.TestCase):
    def test_newest_bundle_and_frameworks_for_the_processor(self):
        files = [
            entry(f'Microsoft.WindowsCalculator_2021.2606.0.0_neutral_~_{MS}', 'msixbundle'),
            entry(f'Microsoft.WindowsCalculator_2021.2607.0.0_neutral_~_{MS}', 'msixbundle'),
            entry(f'Microsoft.WindowsCalculator_2021.2608.0.70_neutral_~_{MS}', 'emsixbundle'),
            entry(f'Microsoft.VCLibs.140.00_14.0.33519.0_x64__{MS}', 'appx'),
            entry(f'Microsoft.VCLibs.140.00_14.0.33600.0_arm64__{MS}', 'appx'),
            entry(f'Microsoft.VCLibs.140.00_14.0.30000.0_x64__{MS}', 'appx'),
        ]
        pick = storefetch.choose(files, APP, 'x64')
        self.assertEqual(pick['app'][0], f'Microsoft.WindowsCalculator_2021.2607.0.0_neutral_~_{MS}')
        self.assertEqual(pick['Microsoft.VCLibs.140.00'][0], f'Microsoft.VCLibs.140.00_14.0.33519.0_x64__{MS}')


def package(manifest_deps):
    deps = ''.join(f'<PackageDependency Name="{d}" MinVersion="1.0.0.0" Publisher="CN=x"/>' for d in manifest_deps)
    xml = ('<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">'
           f'<Dependencies>{deps}</Dependencies></Package>')
    b = io.BytesIO()
    with zipfile.ZipFile(b, 'w') as z:
        z.writestr('AppxManifest.xml', xml)
    return b.getvalue()


class DependencyTests(unittest.TestCase):
    def test_the_bundles_x64_package_decides(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'calc.msixbundle'
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr('AppxMetadata/AppxBundleManifest.xml', '<Bundle/>')
                z.writestr('Calc_1.0.0.0_ARM64.msix', package(['Arm.Only']))
                z.writestr('Calc_1.0.0.0_x64.msix', package(['Microsoft.UI.Xaml.2.8', 'Microsoft.VCLibs.140.00']))
            self.assertEqual(storefetch.dependencies(str(path), 'x64'),
                             ['Microsoft.UI.Xaml.2.8', 'Microsoft.VCLibs.140.00'])


class SyncTests(unittest.TestCase):
    def test_files_are_matched_to_their_secured_update_identities(self):
        full = f'Microsoft.VCLibs.140.00_14.0.33519.0_x64__{MS}'
        ext = ('&lt;Files&gt;&lt;File FileName="abc.appx" Size="896581" '
               f'InstallerSpecificIdentifier="{full}"/&gt;&lt;/Files&gt;')
        new = ('&lt;UpdateIdentity UpdateID="11111111-2222" RevisionNumber="7"/&gt;'
               '&lt;Properties&gt;&lt;SecuredFragment/&gt;&lt;/Properties&gt;')
        answer = (f'<Envelope><Body><SyncUpdatesResponse xmlns="{WU}"><SyncUpdatesResult>'
                  f'<NewUpdates><UpdateInfo><ID>5</ID><Xml>{new}</Xml></UpdateInfo>'
                  f'<UpdateInfo><ID>6</ID><Xml>&lt;UpdateIdentity UpdateID="x" RevisionNumber="1"/&gt;</Xml></UpdateInfo></NewUpdates>'
                  f'<ExtendedUpdateInfo><Updates><Update><ID>5</ID><Xml>{ext}</Xml></Update></Updates></ExtendedUpdateInfo>'
                  '</SyncUpdatesResult></SyncUpdatesResponse></Body></Envelope>')
        with patch.object(storefetch, 'soap', return_value=ET.fromstring(answer)):
            files = storefetch.sync(None, 'cookie', 'category')
        self.assertEqual(files, [(full, 'abc.appx', 896581, '11111111-2222', '7')])


if __name__ == '__main__':
    unittest.main()
