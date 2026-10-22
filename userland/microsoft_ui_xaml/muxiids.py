# Writes iids.txt: the interface IDs of Microsoft.UI.Xaml 2.x as Microsoft's
# own metadata has them (lib/uap10.0/Microsoft.UI.Xaml.winmd of the
# Microsoft.UI.Xaml NuGet package), one "name {iid}" per line.
#
# MIDL 3 makes up the IDs of the interfaces it synthesizes for a runtime
# class (its default, factory, statics, overridable and protected
# interfaces) by an algorithm Microsoft has not published, and WinUI 2's IDL
# names no IDs for them.  An app compiled against the NuGet package
# (Calculator, 2.8.7) asks Microsoft.UI.Xaml.dll for those IDs, so
# build.py gives each interface the ID listed here, and the name-based ID
# of abigen's only to an interface the package does not have.  The IDs are
# the interfaces' published identities, kept as facts, as MinGW-w64's and
# Wine's headers keep Windows'; the package itself is not used by the
# build.
#
#   python3 muxiids.py [version]     (default 2.8.7; downloads the package)
import io, os, sys, urllib.request, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', 'winrt'))


def main():
    import winmd
    from model import Interface, Delegate
    version = sys.argv[1] if len(sys.argv) > 1 else '2.8.7'
    url = 'https://www.nuget.org/api/v2/package/Microsoft.UI.Xaml/' + version
    data = urllib.request.urlopen(url).read()
    tmp = os.path.join(HERE, '.Microsoft.UI.Xaml.winmd')
    with zipfile.ZipFile(io.BytesIO(data)) as z, open(tmp, 'wb') as f:
        f.write(z.read('lib/uap10.0/Microsoft.UI.Xaml.winmd'))
    try:
        r = winmd.Reader()
        r.load(tmp)
    finally:
        os.remove(tmp)
    rows = sorted((n, t.guid.lower()) for n, t in r.types.items()
                  if isinstance(t, (Interface, Delegate)) and t.guid and not t.generics)
    with open(os.path.join(HERE, 'iids.txt'), 'w') as f:
        f.write('# Microsoft.UI.Xaml %s interface IDs (muxiids.py writes this file)\n' % version)
        for n, g in rows:
            f.write('%s {%s}\n' % (n, g))
    print(len(rows), 'interface IDs')


if __name__ == '__main__':
    main()
