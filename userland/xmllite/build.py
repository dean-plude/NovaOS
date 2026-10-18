# xmllite.dll's build hooks (tools/build_userland.py calls them with itself
# as b).  The reader is libxml2's xmlTextReader (third_party/libxml2, MIT),
# compiled into the DLL with the sources msxml6.dll uses plus xmlreader.c.
# msxml6's libxml2 is built without the reader, so this DLL compiles its own
# copy against an xmlversion.h that turns it on (written into the build
# directory from the vendored one) and msxml6's config.h and Windows shims.
import os, threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
LIBXML = os.path.join(ROOT, 'third_party', 'libxml2')
SHIMS = os.path.join(os.path.dirname(HERE), 'msxml6', 'libxml')
SRCS = ['SAX2', 'buf', 'chvalid', 'dict', 'encoding', 'entities', 'error', 'globals', 'hash', 'list', 'parser',
        'parserInternals', 'threads', 'tree', 'uri', 'valid', 'xmlIO', 'xmlmemory', 'xmlsave', 'xmlstring', 'xpath',
        'xmlreader']
_LOCK = threading.Lock()


def includes(b):
    """-I for the generated xmlversion.h (first), the shims, libxml2's headers"""
    d = os.path.join(b.out, 'include', 'xmllite')
    with _LOCK:
        p = os.path.join(d, 'libxml', 'xmlversion.h')
        src = open(os.path.join(LIBXML, 'include', 'libxml', 'xmlversion.h')).read()
        old = '#if 0\n#define LIBXML_READER_ENABLED'
        if old not in src:
            raise SystemExit('third_party/libxml2/include/libxml/xmlversion.h: no LIBXML_READER_ENABLED switch')
        text = src.replace(old, '#if 1\n#define LIBXML_READER_ENABLED')
        if not os.path.exists(p) or open(p).read() != text:
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'w').write(text)
    return ['-I', d, '-I', SHIMS, '-I', os.path.join(LIBXML, 'include'), '-DLIBXML_STATIC', '-DLIBXML_STATIC_FOR_DLL']


def cflags(b):
    """flags for xmllite's own sources"""
    return includes(b)


def objs(b, odir):
    """libxml2 with its reader, for this architecture"""
    flags = b.cflags() + includes(b) + ['-DHAVE_CONFIG_H', '-w']
    headers = [os.path.join(SHIMS, h) for h in os.listdir(SHIMS)]
    headers += [os.path.join(LIBXML, 'include', 'libxml', h) for h in os.listdir(os.path.join(LIBXML, 'include', 'libxml'))]
    return b.compile_many([os.path.join(LIBXML, s + '.c') for s in SRCS], odir, 'xmllite_libxml_', flags, headers)
