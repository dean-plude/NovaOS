# Windows.UI.Xaml's build: Microsoft's XAML framework (WinUI's dxaml tree,
# third_party/microsoft-ui-xaml, MIT) compiled for NovaOS, roadmap step 32.3
# (docs/design/windows-ui-xaml.md).  tools/build_userland.py loads this
# module for a C++ program whose NAME.json says "xaml": true and calls
# program_flags(b) and program_objs(b, odir).
#
# The sources are compiled as Microsoft compiles them (clang in MSVC mode,
# C++17, their MSBuild projects' sources, include directories and
# definitions, read by vcxproj.py), over Microsoft's STL and the C runtime
# and Windows headers userland/msvcp140/build.py uses (MinGW-w64's), plus:
#   inc/            what the Windows SDK has and MinGW-w64 lacks or spells
#                   differently: NovaOS's WRL (wrl/), weakreference.h,
#                   xamlom.h, TraceLogging (compiled out), SAL annotations,
#                   the Windows Runtime's HRESULTs
#   third_party/wil Microsoft's Windows Implementation Library (MIT)
#   the Windows Runtime ABI headers tools/winrt/abigen.py generates (the
#   ones MIDL makes from the XAML IDL), and the ETW header etwgen.py makes
#   from XAML's manifest (as mc.exe would)
#   a directory of one-line headers giving every include spelling in the
#   sources the file it names on a case-insensitive disk (Windows.h for
#   windows.h, wil\Result.h for wil/result.h)
#
# So far the XAML core's lowest layer builds: the component libraries in
# LIBRARIES, as one static library (xaml_components.lib) that the
# self-test xamlbasetest links.  The rest of the core (CDependencyObject
# and up) needs the closed Windows App SDK pieces (FrameworkUdk, lifted
# IXP) NovaOS has to write.
import os, re, threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MUX = os.path.join(ROOT, 'third_party', 'microsoft-ui-xaml')
XCP = os.path.join(MUX, 'dxaml', 'xcp')
WIL = os.path.join(ROOT, 'third_party', 'wil', 'include')
INC = os.path.join(HERE, 'inc')

import importlib.util
def _load(name):
    spec = importlib.util.spec_from_file_location('nova_xaml_' + name, os.path.join(HERE, name + '.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod
vcxproj = _load('vcxproj')
etwgen = _load('etwgen')

# MSBuild's properties as the projects use them
PROPS = {'XcpPath': XCP, 'XamlSourcePath': os.path.join(MUX, 'dxaml'), 'RepoRoot': MUX}

# The component libraries built so far (dxaml/xcp/components/...: the
# lowest layer, below CDependencyObject)
LIBRARIES = '''base/lib/Microsoft.UI.Xaml.Base.vcxproj
xstring/lib/Microsoft.UI.Xaml.XString.vcxproj
criticalsection/lib/Microsoft.UI.Xaml.CriticalSection.vcxproj
dependencyLocator/lib/Microsoft.UI.Xaml.DependencyLocator.vcxproj
runtimeEnabledFeatures/lib/Microsoft.UI.Xaml.RuntimeEnabledFeatures.vcxproj
com/lib/Microsoft.UI.Xaml.Com.vcxproj
threading/lib/Microsoft.UI.Xaml.Threading.vcxproj
staticpal/lib/Microsoft.UI.Xaml.StaticPal.vcxproj
qualifiers/lib/Microsoft.UI.Xaml.Qualifiers.vcxproj'''.split()

# The include directories every component sees (components/base.props,
# in its order) and the definitions components/common.props and
# base.props add
COMPONENT_INCLUDES = '''components components/pch components/base/inc components/brushes/inc
components/criticalsection/inc components/focus/inc components/FocusRect/inc components/com/inc components/common/inc
components/deferral/inc components/metadata/inc components/xstring/inc components/legacy/inc
components/primitiveDependencyObjects/inc components/dependencyLocator/inc components/lifetime/inc
components/DependencyObject/inc components/associative/inc components/flyweight/inc components/simple/inc
components/runtimeEnabledFeatures/inc components/accesskeys/inc components/allocation/inc components/moco/inc
components/mrt/inc components/animation/inc components/math/inc components/ContentRoot/inc components/gestures/inc
components/collection/inc components/scaling/inc components/Switcher/inc components/imaging/inc
components/resources/inc inc core/compositor core/hw core/inc core/text/RichTextServices/inc
core/imaging/ImagingProvider pal/inc win/inc'''.split()
DEFINES = '''UNICODE _UNICODE WIL_SUPPRESS_PRIVATE_API_USE MIDL_NS_PREFIX ENABLE_WINRT_EXPERIMENTAL_TYPES=1
__WRL_STRICT__ __WRL_FORCE_INSPECTABLE_CLASS_MACRO__ _HAS_EXCEPTIONS=0 WIL_SUPPRESS_EXCEPTIONS=1 DBG=0
_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000F STRSAFE_NO_DEPRECATE _ENFORCE_MATCHING_ALLOCATORS=0
_HAS_FUNCTION_ALLOCATOR_SUPPORT=1'''.split()
PCH = os.path.join(XCP, 'components', 'pch', 'precomp.h')
# the DLLs XAML's sources call beyond a program's usual ones
LIBS = ['dbghelp', 'shlwapi']
ETW_MANIFEST = os.path.join(XCP, 'plat', 'win', 'desktop', 'Microsoft-Windows-XAML-ETW.man')

_SETUP = threading.Lock()
_done = {}


def _msvcp(b):
    return b.HOOKS['msvcp140']


def generated(b):
    """the directory of generated headers (ETW events, include spellings),
    made once per build"""
    with _SETUP:
        d = os.path.join(b.out, 'include', 'xaml')
        if 'gen' not in _done:
            os.makedirs(d, exist_ok=True)
            etwgen.main(ETW_MANIFEST, os.path.join(d, 'Microsoft-Windows-XAML-ETWEvents.h'))
            # the ABI headers' base (IInspectable...), alone of NovaOS's own headers
            text = '#include "%s"\n' % os.path.join(os.path.dirname(HERE), 'include', 'winrt_abi.h')
            p = os.path.join(d, 'winrt_abi.h')
            if not os.path.exists(p) or open(p).read() != text:
                open(p, 'w').write(text)
            fold(os.path.join(d, 'fold'), [os.path.join(XCP, p) for p in COMPONENT_INCLUDES] +
                 [INC, WIL, os.path.join(b.inc_gen, 'winrt-abi'), d, _msvcp(b).mingw_include(),
                  os.path.join(ROOT, 'third_party', 'msstl', 'inc'), _msvcp(b).SHIM])
            _done['gen'] = d
        return d


def fold(out, dirs):
    """write OUT/<spelling> for every include spelling in the XAML and WIL
    sources that names a file in @dirs only when case is ignored: a header
    that includes the file by its real name (so its own relative includes
    still resolve beside it)"""
    real, lower = set(), {}
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), d).replace(os.sep, '/')
                real.add(rel)
                lower.setdefault(rel.lower(), os.path.join(root, f))
    pat = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
    want = set()
    for top in (XCP, WIL):
        for root, _, files in os.walk(top):
            for f in files:
                if f.endswith(('.h', '.cpp', '.inl', '.hpp')):
                    for m in pat.finditer(open(os.path.join(root, f), errors='replace').read()):
                        s = m.group(1).replace('\\', '/')
                        if s not in real and not s.startswith('..') and not os.path.exists(os.path.join(root, s)):
                            want.add(s)
    os.makedirs(out, exist_ok=True)
    for s in sorted(want):
        target = lower.get(s.lower())
        if target:
            p = os.path.join(out, s)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            text = '#include "%s"\n' % os.path.abspath(target)
            if not os.path.exists(p) or open(p).read() != text:
                open(p, 'w').write(text)


def flags(b):
    """clang's flags for XAML's sources"""
    gen = generated(b)
    # as Microsoft builds XAML: C++17, no C++ exceptions (errors are HRESULTs)
    base = [f for f in _msvcp(b).cxx_flags(b) if f not in ('-D_CRTBLD', '-D_VCRT_ALLOW_INTERNALS',
                                                          '-D_HAS_OLD_IOSTREAMS_MEMBERS=1', '-fcxx-exceptions', '-fexceptions')]
    base = ['-std=c++17' if f.startswith('-std=') else f for f in base] + ['-fno-exceptions']
    inc = [os.path.join(gen, 'fold'), INC] + [x for x in b.winrt_abi_flags({'winrt_abi': True}) if x != '-I'] + \
          [gen, WIL] + [os.path.join(XCP, p) for p in COMPONENT_INCLUDES] + [os.path.join(MUX, 'external', 'inc')]
    # MSVC accepts, with a warning, what clang makes errors by default
    # (narrowing an HRESULT case label or an enumerator)
    return [f for d in inc for f in ('-I', d)] + base + ['-D' + d for d in DEFINES] + \
        ['-fdelayed-template-parsing', '-Wno-everything']


_LIB = threading.Lock()


def library(b, odir):
    """xaml_components.lib: LIBRARIES' sources, built once per architecture"""
    lib = os.path.join(odir, 'xaml_components.lib')
    with _LIB:
        f = flags(b)
        headers = [os.path.join(INC, h) for h in os.listdir(INC) if h.endswith('.h')] + \
                  [os.path.join(INC, 'wrl', h) for h in os.listdir(os.path.join(INC, 'wrl')) if h.endswith('.h')] + \
                  [os.path.abspath(__file__)]
        objs = []
        for proj in LIBRARIES:
            p = vcxproj.read(os.path.join(XCP, 'components', proj), PROPS)
            extra = [x for i in p['includes'] for x in ('-I', i)] + ['-D' + d for d in p['defines']] + ['-include', PCH]
            prefix = 'xaml_' + proj.split('/')[0] + '_'
            objs += b.compile_many(p['sources'], odir, prefix, f + extra, headers, compiler='clang++')
        if not os.path.exists(lib) or any(os.path.getmtime(o) > os.path.getmtime(lib) for o in objs):
            b.run(['lld-link', '/lib', f'/out:{lib}'] + objs)
    return lib


def program_flags(b):
    """for a C++ program built against XAML's sources ("xaml" in its NAME.json)"""
    return flags(b) + ['-include', PCH]


def program_objs(b, odir):
    """what such a program links besides the C++ library: xaml_components.lib"""
    return [library(b, odir)]
