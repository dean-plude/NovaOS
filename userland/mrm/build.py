# MRM.dll's build hooks (tools/build_userland.py calls them with itself as
# b): the Modern Resource Technology reader, MRT Core (third_party/mrtcore,
# Microsoft's Windows App SDK, MIT), compiled unchanged.  It reads an app
# package's resources.pri: the named resources ("ms-resource:" URIs) and
# their candidates for each language, scale and contrast, and answers the
# MRM.dll exports (MrmCreateResourceManager, MrmLoadStringResource...).
# Windows' in-box resource loader (MrmCoreR.dll) is the same code; NovaOS
# uses this DLL for packages' manifests (shlwapi's SHLoadIndirectString,
# the package installer) as well as for apps that bring MRT Core.
#
# The sources are compiled as their MSBuild projects compile them (C++17,
# UNICODE, mrmmin.vcxproj's sources into MRM.vcxproj's DLL with MRM.def's
# exports), over Microsoft's STL, MinGW-w64's Windows headers and WIL
# (userland/msvcp140/build.py's flags), plus inc/: the Windows App SDK's
# telemetry header (no telemetry) and what MinGW-w64 leaves out.  The
# sources name their headers in the case and with the backslashes of a
# Windows disk ("stdafx.h", "Common\BaseInternal.h"): fold() writes a
# directory of one-line headers giving each such spelling its file, and
# PATCHES gives the one header line clang will not take as MSVC does.
import os, re, threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MRT = os.path.join(ROOT, 'third_party', 'mrtcore', 'mrt')
MRMMIN = os.path.join(MRT, 'mrm', 'mrmmin')
MRM_INC = os.path.join(MRT, 'mrm', 'include')
CORE = os.path.join(MRT, 'Core', 'src')
WIL = os.path.join(ROOT, 'third_party', 'wil', 'include')
INC = os.path.join(HERE, 'inc')
SAL = os.path.join(ROOT, 'userland', 'windows_ui_xaml', 'inc', 'nova_sal.h')
DEFINES = ['WIN32', '_WINDOWS', '_USRDLL', 'UNICODE', '_UNICODE', 'NDEBUG', '_LIB', 'STRSAFE_NO_DEPRECATE']
_LOCK = threading.Lock()

# (header under mrm/include, MSVC's spelling, the standard one): MSVC
# accepts a member function qualified with the interface it overrides
PATCHES = [
    ('mrm/build/MrmBuilders.h',
     'virtual HierarchicalSchemaSectionBuilder* IResourceLinkBuilder::GetSchema() const',
     'virtual HierarchicalSchemaSectionBuilder* GetSchema() const'),
]


def patched(out):
    """OUT/<header> for each of PATCHES, searched before mrm/include"""
    for rel, old, new in PATCHES:
        text = open(os.path.join(MRM_INC, rel), encoding='utf-8').read()
        if old not in text:
            raise SystemExit(f'third_party/mrtcore: patch no longer applies to {rel}: {old}')
        p = os.path.join(out, rel)
        text = text.replace(old, new)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        if not os.path.exists(p) or open(p, encoding='utf-8').read() != text:
            open(p, 'w', encoding='utf-8').write(text)


def sources():
    """mrmmin.vcxproj's sources, then MRM.vcxproj's"""
    proj = open(os.path.join(MRMMIN, 'mrmmin.vcxproj'), encoding='utf-8-sig').read()
    return [os.path.join(MRMMIN, s) for s in re.findall(r'<ClCompile Include="([^"]+\.cpp)"', proj)] + \
        [os.path.join(CORE, 'MRM.cpp')]


def fold(out, dirs):
    """OUT/<spelling> for each include spelling in the sources (and WIL's)
    that names a file in @dirs only when case and slashes are ignored"""
    lower = {}
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), d).replace(os.sep, '/')
                lower.setdefault(rel.lower(), os.path.join(root, f))
    local = {}                      # beside the including file
    for root, _, files in os.walk(MRT):
        for f in files:
            rel = os.path.relpath(os.path.join(root, f), MRT).replace(os.sep, '/')
            local[rel.lower()] = os.path.join(root, f)
    pat = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
    for root, _, files in [w for top in (MRT, WIL) for w in os.walk(top)]:
        for f in files:
            if not f.endswith(('.h', '.cpp')):
                continue
            here = os.path.dirname(os.path.join(root, f))
            for m in pat.finditer(open(os.path.join(root, f), encoding='utf-8', errors='replace').read()):
                s = m.group(1).replace('\\', '/')
                near = os.path.relpath(here, MRT).replace(os.sep, '/').lower()
                target = lower.get(s.lower()) or local.get(near + '/' + s.lower())
                if not target or os.path.exists(os.path.join(here, s)) or \
                        any(os.path.exists(os.path.join(d, s)) for d in dirs):
                    continue
                p = os.path.join(out, s)
                # NovaOS's own headers by name, so their #include_next goes
                # on to the header they extend
                text = ('#include <%s>\n' % os.path.relpath(target, INC)) if target.startswith(INC + os.sep) else \
                    '#include "%s"\n' % target
                os.makedirs(os.path.dirname(p), exist_ok=True)
                if not os.path.exists(p) or open(p).read() != text:
                    open(p, 'w').write(text)


def flags(b):
    folded = os.path.join(b.out, 'include', 'mrm')
    fixed = os.path.join(b.out, 'include', 'mrm_patched')
    with _LOCK:
        fold(folded, [INC, MRM_INC, MRMMIN, CORE, b.HOOKS['msvcp140'].mingw_include()])
        patched(fixed)
    base = [f for f in b.HOOKS['msvcp140'].cxx_flags(b) if f not in ('-D_CRTBLD', '-D_VCRT_ALLOW_INTERNALS',
                                                                     '-D_HAS_OLD_IOSTREAMS_MEMBERS=1')]
    base = ['-std=c++17' if f.startswith('-std=') else f for f in base]
    return ['-I', INC, '-I', fixed, '-I', folded, '-I', MRM_INC, '-I', MRMMIN, '-I', WIL] + base + \
        ['-D' + d for d in DEFINES] + ['-include', SAL, '-include', os.path.join(INC, 'nova_mrm.h'),
                                       '-fdelayed-template-parsing', '-Wno-everything']


def objs(b, odir):
    headers = [os.path.join(r, h) for d in (INC, MRM_INC, MRMMIN, CORE) for r, _, fs in os.walk(d)
               for h in fs if h.endswith('.h')] + [os.path.abspath(__file__)]
    return b.compile_many(sources(), odir, 'mrm_', flags(b), headers, compiler='clang++') + \
        b.HOOKS['msvcp140'].start_obj(b, odir, 'mrm_')


def link(b, odir, objs, deps, base):
    exports = [l.strip() for l in open(os.path.join(CORE, 'MRM.def'), encoding='utf-8-sig')
               if l.strip() and l.strip() not in ('LIBRARY', 'EXPORTS')]
    b.HOOKS['msvcp140'].link_dll(b, odir, 'MRM', objs, base, [d for d in deps if d != 'msvcp140'] + ['msvcp140'],
                                 ['/export:' + e for e in exports])
