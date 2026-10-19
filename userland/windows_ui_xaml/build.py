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
# So far the XAML core's lowest layers build: the component libraries in
# LIBRARIES, as one static library (xaml_components.lib) that the
# self-tests xamlbasetest and xamlparsetest link, up to the type metadata,
# CDependencyObject's property system, the collections and the resources,
# and the markup parser (CORE_LIBRARIES: core/Parser, compiled with the
# core's include path, its precompiled header and C++ exceptions, for
# programs that say "xaml": "parser"), and the core services, CCoreServices
# (xaml_core.lib, for programs that say "xaml": "core", with stand-ins
# for the rest of the core: unbuilt()).  inc/ also stands in for the closed
# Windows App SDK headers those include (FrameworkUdk's helpers, the lifted
# IXP interop headers); the rest of the core links as it builds.
import os, re, subprocess, threading

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
# lowest layer, the type metadata and CDependencyObject)
LIBRARIES = '''base/lib/Microsoft.UI.Xaml.Base.vcxproj
xstring/lib/Microsoft.UI.Xaml.XString.vcxproj
criticalsection/lib/Microsoft.UI.Xaml.CriticalSection.vcxproj
dependencyLocator/lib/Microsoft.UI.Xaml.DependencyLocator.vcxproj
runtimeEnabledFeatures/lib/Microsoft.UI.Xaml.RuntimeEnabledFeatures.vcxproj
com/lib/Microsoft.UI.Xaml.Com.vcxproj
threading/lib/Microsoft.UI.Xaml.Threading.vcxproj
staticpal/lib/Microsoft.UI.Xaml.StaticPal.vcxproj
qualifiers/lib/Microsoft.UI.Xaml.Qualifiers.vcxproj
metadata/lib/Microsoft.UI.Xaml.Metadata.vcxproj
metadata/DynamicMetadataStorage/Microsoft.UI.Xaml.DynamicMetadataStorage.vcxproj
DependencyObject/lib/Microsoft.UI.Xaml.DependencyObject.vcxproj
DependencyObject/minlib/Microsoft.UI.Xaml.DependencyObjectMin.vcxproj
CValue/lib/Microsoft.UI.Xaml.CValue.vcxproj
flyweight/lib/Microsoft.UI.Xaml.Flyweight.vcxproj
primitiveDependencyObjects/lib/Microsoft.UI.Xaml.PrimitiveDependencyObjects.vcxproj
math/lib/Microsoft.UI.Xaml.Math.vcxproj
theming/lib/Microsoft.UI.Xaml.Theming.vcxproj
Collection/Lib/Microsoft.UI.Xaml.Collection.vcxproj
resources/lib/Microsoft.UI.Xaml.XamlResources.vcxproj
legacy/lib/Microsoft.UI.Xaml.Legacy.vcxproj
deferral/lib/Microsoft.UI.Xaml.Deferral.vcxproj
winuri/lib/winuri.vcxproj
colors/lib/Microsoft.UI.Xaml.Colors.vcxproj
simple/lib/Microsoft.UI.Xaml.Framework.SimpleProperties.vcxproj
namescope/lib/Microsoft.UI.Xaml.NameScope.vcxproj
theminginterop/lib/Microsoft.UI.Xaml.ThemingInterop.vcxproj'''.split()

# The XAML core's libraries built so far (dxaml/xcp/core/...): the markup
# parser, its object writer runtime (which makes the core's objects from
# the node stream) and the elements built with exceptions (the resource
# dictionary, CResourceDictionary, among them).  Each is a project, the
# sources.props its sources share (whose include path, with the props files
# it imports, is theirs), its precompiled header and sources compiled with
# it that it does not list (src/pal.cpp: the platform layer's the parser
# calls; the deferred resource keys a dictionary keeps, from the elements'
# other library).  The core is
# compiled with C++ exceptions, as Microsoft compiles it (/EHsc,
# _HAS_EXCEPTIONS=1, WIL's exceptions on).
CORE_LIBRARIES = [('core/Parser/core/Parser.Core.vcxproj', 'core/Parser/sources.props', 'core/Parser/pch/precomp.h',
                   [os.path.join(HERE, 'src', 'pal.cpp')]),
                  ('core/Parser/runtime/Parser.Runtime.vcxproj', 'core/Parser/sources.props', 'core/Parser/pch/precomp.h',
                   []),
                  ('core/core/elements/libexcept/elements.except.vcxproj', 'core/core/elements/sources.props',
                   'core/core/elements/pchexcept/precomp.h',
                   [os.path.join(XCP, 'core', 'core', 'elements', 'DeferredKeys.cpp')]),
                  ('core/xcpcore.vcxproj', 'core/common.props', os.path.join(HERE, 'src', 'core_precomp.h'),
                   [os.path.join(XCP, 'core', 'hw', f) for f in ('MaxTextureSizeProvider.cpp', 'AtlasRequestProvider.cpp')] +
                   [os.path.join(XCP, 'core', 'text', 'common', 'TextCore.cpp')]),
                  ('core/native/text/Common/Native.Text.Common.vcxproj', 'core/common.props',
                   'core/native/text/Common/precomp.h', []),
                  ('core/core/elements/lib/elements.vcxproj', 'core/core/elements/sources.props',
                   'core/core/elements/pch/precomp.h', []),
                  ('plat/win/browserdesktop/platwinbrowserdesktop.vcxproj', 'plat/win/browserdesktop/sources.props',
                   'plat/win/browserdesktop/precomp.h', [])]
# the sources of a CORE_LIBRARIES project built so far, when not all of
# them, which make xaml_core.lib: of the core's own library
# (xcpcore.vcxproj), the core services, CCoreServices (dll/xcpcore.cpp,
# with its name scopes; and, from the hardware rendering's library,
# core/hw, the two providers it holds), and what CCoreServices::Create
# makes: the media queue manager and the render target bitmap manager;
# the text core, CTextCore (core/text's TextCore.cpp, compiled with the
# core's sources, src/core_precomp.h: the text core's own precompiled
# header includes the closed Page/Table Layout Services' header), and its
# Windows half, WinTextCore (core/native/text);
# the geometry builder, CGeometryBuilder (the elements' geometry.cpp); and
# the platform layer, CWindowsServices (plat/win/browserdesktop: gps, the
# platform services ObtainPlatformServices hands the core, with its
# thread pool work items).  What they call in the rest of the core stands
# in until it builds (unbuilt())
CORE_SOURCES = {'core/xcpcore.vcxproj': ('dll/xcpcore.cpp', 'dll/xcpcore_namescope.cpp', 'dll/rendertargetbitmapmgr.cpp',
                                         'dethunk/elements/MediaQueue.cpp'),
                'core/native/text/Common/Native.Text.Common.vcxproj': ('WinTextCore.cpp',),
                'core/core/elements/lib/elements.vcxproj': ('../geometry.cpp',),
                'plat/win/browserdesktop/platwinbrowserdesktop.vcxproj': ('xcpwin.cpp', 'PlatformServices.cpp',
                                                                          'WinThreadPool.cpp', 'early_init.cpp')}
# The Windows SDK's namespace headers include the namespaces they import in
# full; tools/winrt/abigen.py's declare them only (a fraction of the
# compile time), so a library whose headers use an imported namespace's
# interfaces as complete types is given those headers first: the elements'
# DXamlCore.h wraps Windows.ApplicationModel's suspending operation and
# holds Windows.Devices.Input's keyboard capabilities
CORE_FIRST = {'core/core/elements/libexcept/elements.except.vcxproj': ['windows.applicationmodel.h', 'windows.devices.input.h'],
              'core/xcpcore.vcxproj': ['windows.ui.viewmanagement.h', 'windows.applicationmodel.h',
                                       'windows.devices.input.h'],
              'core/core/elements/lib/elements.vcxproj': ['windows.applicationmodel.h', 'windows.devices.input.h']}
# (and the native text's and the platform layer's as the core's, with the
# composition's compositor, which they hold through their DirectManipulation
# service's header)
CORE_FIRST['core/native/text/Common/Native.Text.Common.vcxproj'] = \
    CORE_FIRST['plat/win/browserdesktop/platwinbrowserdesktop.vcxproj'] = \
    CORE_FIRST['core/xcpcore.vcxproj'] + ['microsoft.ui.composition.h']
# a program's "xaml" (in its NAME.json) naming a CORE_LIBRARIES entry whose
# flags it is built with, as Microsoft builds that library's unit tests
PROGRAM_CORE = {'parser': 0, 'elements': 2, 'core': 3}
EXCEPTIONS = ['-fcxx-exceptions', '-fexceptions', '-U_HAS_EXCEPTIONS', '-D_HAS_EXCEPTIONS=1',
              '-UWIL_SUPPRESS_EXCEPTIONS']

# Sources compiled in place of Microsoft's, where clang has to be told what
# MSVC does on its own (src/, each says what)
REPLACED = {'CValue/CValueStores.cpp': os.path.join(HERE, 'src', 'CValueStores.cpp')}
# Sources compiled with a project's that it does not list: the platform
# layer's the components call (pal/common: character classes, the 2D
# matrix; the URI string getters, with the URI library), and NovaOS's
# definitions of what MSVC defines from a declaration (src/definitions.cpp)
ADDED = {'math/lib/Microsoft.UI.Xaml.Math.vcxproj': [os.path.join(XCP, 'pal', 'common', 'ctypes.cpp'),
                                                     os.path.join(XCP, 'pal', 'common', 'matrix.cpp')],
         'winuri/lib/winuri.vcxproj': [os.path.join(XCP, 'pal', 'common', 'UriXStringGetters.cpp')],
         'DependencyObject/lib/Microsoft.UI.Xaml.DependencyObject.vcxproj': [os.path.join(HERE, 'src', 'definitions.cpp')]}

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
__WRL_STRICT__ __WRL_FORCE_INSPECTABLE_CLASS_MACRO__ _HAS_EXCEPTIONS=0 WIL_SUPPRESS_EXCEPTIONS=1
_WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A00000F STRSAFE_NO_DEPRECATE _ENFORCE_MATCHING_ALLOCATORS=0
_HAS_FUNCTION_ALLOCATOR_SUPPORT=1'''.split()
# What the libraries call in the XAML core above them, for programs that
# test the components on their own (xamlbasetest): Microsoft's isolated unit
# tests' stubs and metadata mocks (built as Microsoft builds them, with
# __XAML_UNITTESTS__; MSVC lets the mocks reach a base class's protected
# members through another object), less the stubs of the element tree and
# of what now builds for real (the resource dictionary, theme resources,
# the parser's runtimes), and NovaOS's stand-ins for the few element
# members the libraries call
# (src/core_stubs.cpp, compiled with the collections' include
# directories).  As a library, a program takes only the stubs it needs; it
# shrinks as the core builds.
TEST_STUBS = [('unittests/stubs/Microsoft.UI.Xaml.Stubs.vcxproj',
               ('UIElement_Stub.cpp', 'DCompVisual_Stub.cpp', 'Geometry_Stub.cpp', 'VisualStateManager_Stub.cpp',
                'ResourceDictionary_Stub.cpp', 'ThemeResourceExtension_Stub.cpp', 'Parser_Stub.cpp'), []),
              ('metadata/mocks/Microsoft.UI.Xaml.Tests.Isolated.Mocks.Framework.Metadata.vcxproj', (),
               ['-fno-access-control', '-I', os.path.join(XCP, 'components', 'metadata', 'mocks')])]
# stubs whose answer the resource dictionary cannot work with (Microsoft's
# say E_NOTIMPL where the core says S_OK for an object with no managed peer,
# which fails every collection add, and give no resource lookup logger, which
# every lookup reads): renamed out of the stub file, as the core has them in
# src/elements_stubs.cpp and src/core_services_stubs.cpp; and the metadata mocks' property getters, which
# know no field offsets (so a field-backed property, a dictionary's merged
# and theme dictionaries, read the object's vtable) and make no default
# objects, and their class's core constructor (asked of a test's callbacks,
# where the core reads the type table), as the core has them in
# src/property_data.cpp
STUB_RENAMES = {'DependencyObject_Stub.cpp': ('AddPeerReferenceToItem', 'RemovePeerReferenceToItem'),
                'CoreServices_Stub.cpp': ('GetResourceLookupLogger',),
                'MockDependencyProperty.cpp': ('GetPropertyMethod', 'GetOffset', 'GetGroupOffset', 'GetGroupCreator',
                                               'GetRenderChangedHandler', 'CreateDefaultValueObject'),
                'MockClassInfo.cpp': ('GetCoreConstructor',)}
PCH = os.path.join(XCP, 'components', 'pch', 'precomp.h')
# the DLLs XAML's sources call beyond a program's usual ones (the parser
# reads XML with XmlLite, the URI library builds URIs with urlmon)
LIBS = ['dbghelp', 'shlwapi', 'xmllite', 'urlmon']
ETW_MANIFEST = os.path.join(XCP, 'plat', 'win', 'desktop', 'Microsoft-Windows-XAML-ETW.man')

_SETUP = threading.Lock()
_done = {}


def _msvcp(b):
    return b.HOOKS['msvcp140']


def generated(b):
    """the directory of generated headers (ETW events, include spellings),
    made once per build"""
    abi = [x for x in b.winrt_abi_flags({'winrt_abi': True}) if x != '-I']  # generated first: fold() maps to them
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
            fold(os.path.join(d, 'fold'), component_includes() + project_includes() + core_includes() + source_dirs() +
                 [INC, WIL] + abi + [d, _msvcp(b).mingw_include(),
                  os.path.join(ROOT, 'third_party', 'msstl', 'inc'), _msvcp(b).SHIM])
            _done['gen'] = d
        return d


def component_includes():
    """COMPONENT_INCLUDES as spelled on disk (the projects name
    collection\\inc for Collection/Inc)"""
    return [p for p in (vcxproj.find_case(os.path.join(XCP, d)) for d in COMPONENT_INCLUDES) if p]


def project_includes():
    """the include directories LIBRARIES' projects add"""
    return [i for proj in LIBRARIES for i in vcxproj.read(os.path.join(XCP, 'components', proj), PROPS)['includes']]


def source_dirs():
    """the directories of LIBRARIES' sources (which include headers beside
    them by spellings of their own: deferral's "ICustomWriter.h")"""
    out = []
    for proj in LIBRARIES:
        for src in vcxproj.read(os.path.join(XCP, 'components', proj), PROPS)['sources']:
            if os.path.dirname(src) not in out:
                out.append(os.path.dirname(src))
    return out


def core_includes(props=None):
    """the include path of CORE_LIBRARIES' sources (of one, @props)"""
    out = []
    for _, sp, _, _ in CORE_LIBRARIES:
        if props in (None, sp):
            out += [d for d in vcxproj.include_path(os.path.join(XCP, sp), PROPS) if d not in out]
    return out


def core_flags(proj, sources, pch, added=()):
    """what a CORE_LIBRARIES entry's sources add to flags()"""
    p = vcxproj.read(os.path.join(XCP, proj), PROPS)
    inc = core_includes(sources) + p['includes']
    return [x for d in inc for x in ('-I', d)] + ['-D' + d for d in p['defines']] + EXCEPTIONS + \
        [x for h in CORE_FIRST.get(proj, []) for x in ('-include', h)] + ['-include', os.path.join(XCP, pch)]


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
                lower.setdefault(rel.lower(), (d, rel))
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
            # NovaOS's own headers by name, so their #include_next goes on
            # to the header they extend
            text = ('#include <%s>\n' % target[1]) if target[0] == INC else \
                '#include "%s"\n' % os.path.abspath(os.path.join(*target))
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
          [gen, WIL] + component_includes() + [os.path.join(MUX, 'external', 'inc')]
    # MSVC accepts, with a warning, what clang makes errors by default
    # (narrowing an HRESULT case label or an enumerator)
    return [f for d in inc for f in ('-I', d)] + base + ['-D' + d for d in DEFINES] + \
        ['-fdelayed-template-parsing', '-Wno-everything']


_LIB = threading.Lock()
_core_objs = []


def library(b, odir):
    """xaml_components.lib: LIBRARIES' sources and CORE_LIBRARIES' (less the
    core's own, CORE_SOURCES', which are xaml_core.lib), built once per
    architecture; returns both"""
    lib = os.path.join(odir, 'xaml_components.lib')
    core_lib = os.path.join(odir, 'xaml_core.lib')
    with _LIB:
        f = flags(b)
        headers = [os.path.join(r, h) for r, _, fs in os.walk(INC) for h in fs if h.endswith('.h')] + \
                  [os.path.abspath(__file__)] + list(REPLACED.values()) + [x for v in ADDED.values() for x in v] + \
                  [x for c in CORE_LIBRARIES for x in c[3]]
        objs = []
        for proj in LIBRARIES:
            p = vcxproj.read(os.path.join(XCP, 'components', proj), PROPS)
            # (MSBuild adds the project's own directory: Xaml.NTLegacy.Cpp.props)
            extra = [x for i in p['includes'] + [os.path.dirname(os.path.join(XCP, 'components', proj))] for x in ('-I', i)] + \
                ['-D' + d for d in p['defines']] + ['-include', PCH]
            prefix = 'xaml_' + proj.split('/')[0] + '_'
            srcs = [REPLACED.get(os.path.relpath(s, os.path.join(XCP, 'components')).replace(os.sep, '/'), s)
                    for s in p['sources']] + ADDED.get(proj, [])
            objs += b.compile_many(srcs, odir, prefix, f + extra, headers, compiler='clang++')
        _core_objs[:] = []
        for proj, sources, pch, added in CORE_LIBRARIES:
            p = vcxproj.read(os.path.join(XCP, proj), PROPS)
            prefix = 'xaml_' + proj.replace('/', '_').rsplit('_', 1)[0] + '_'
            srcs = p['sources'] if proj not in CORE_SOURCES else \
                [os.path.join(os.path.dirname(os.path.join(XCP, proj)), x) for x in CORE_SOURCES[proj]]
            built = b.compile_many(srcs + added, odir, prefix, f + core_flags(proj, sources, pch), headers,
                                   compiler='clang++')
            if proj in CORE_SOURCES:
                _core_objs.extend(built)
            else:
                objs += built
        for out, members in ((lib, objs), (core_lib, _core_objs)):
            if not os.path.exists(out) or any(os.path.getmtime(o) > os.path.getmtime(out) for o in members):
                b.run(['lld-link', '/lib', f'/out:{out}'] + members)
    return lib, core_lib


_STUBS = threading.Lock()


# the stubs that stand in for the core services themselves (the core's
# own CoreServices_Stub.cpp; NovaOS's core members, error service and
# platform services), each alone in its object, which a program on the real
# core ("xaml": "core") does not link
CORE_SERVICES_STUBS = ('CoreServices_Stub.cpp', 'core_services_stubs.cpp', 'error_service.cpp',
                       'platform_services.cpp')


def test_stubs(b, odir):
    """xaml_teststubs.lib (TEST_STUBS) and xaml_corestubs.lib (the same
    less CORE_SERVICES_STUBS), built once per architecture"""
    lib = os.path.join(odir, 'xaml_teststubs.lib')
    core_lib = os.path.join(odir, 'xaml_corestubs.lib')
    with _STUBS:
        f = flags(b) + ['-D__XAML_UNITTESTS__']
        headers = [os.path.join(r, h) for r, _, fs in os.walk(INC) for h in fs if h.endswith('.h')] + \
                  [os.path.abspath(__file__)]
        objs = []
        for proj, skip, more in TEST_STUBS + [('Collection/Lib/Microsoft.UI.Xaml.Collection.vcxproj', None, [])]:
            p = vcxproj.read(os.path.join(XCP, 'components', proj), PROPS)
            extra = more + [x for i in p['includes'] for x in ('-I', i)] + ['-D' + d for d in p['defines']] + \
                ['-include', PCH]
            srcs = [os.path.join(HERE, 'src', 'core_stubs.cpp'), os.path.join(HERE, 'src', 'platform_services.cpp')] \
                if skip is None else \
                [s for s in p['sources'] if os.path.basename(s) not in skip]
            renamed = [x for x in srcs if os.path.basename(x) in STUB_RENAMES]
            objs += b.compile_many([x for x in srcs if x not in renamed], odir, 'xamlstub_', f + extra, headers,
                                   compiler='clang++')
            for x in renamed:
                objs += b.compile_many([x], odir, 'xamlstub_', f + extra + [
                    f'-D{n}={n}_stub' for n in STUB_RENAMES[os.path.basename(x)]], headers, compiler='clang++')
        # and those of what the resource dictionary calls in the core above
        # it (src/elements_stubs.cpp) and the properties' runtime data
        # (src/property_data.cpp), with the elements' flags (and, each alone,
        # the empty type table for programs that make no objects and the
        # core services' members and error service for programs without the
        # core, src/type_activations.cpp, src/core_services_stubs.cpp and
        # src/error_service.cpp)
        objs += b.compile_many([os.path.join(HERE, 'src', x) for x in (
            'elements_stubs.cpp', 'property_data.cpp', 'type_activations.cpp', 'core_services_stubs.cpp',
            'error_service.cpp')], odir, 'xamlstub_', f + core_flags(*CORE_LIBRARIES[PROGRAM_CORE['elements']]),
            headers, compiler='clang++')
        core_objs = [o for o in objs if not os.path.basename(o)[len('xamlstub_'):-4] + '.cpp' in CORE_SERVICES_STUBS]
        for out, members in ((lib, objs), (core_lib, core_objs)):
            if not os.path.exists(out) or any(os.path.getmtime(o) > os.path.getmtime(out) for o in members):
                b.run(['lld-link', '/lib', f'/out:{out}'] + members)
    return lib, core_lib


def program_flags(b, prog=None):
    """for a C++ program built against XAML's sources ("xaml" in its
    NAME.json): the components', or with "xaml": "parser" the parser's
    (CORE_LIBRARIES' first), as the parser's own unit tests are built"""
    if prog and prog.get('xaml') in PROGRAM_CORE:
        return flags(b) + core_flags(*CORE_LIBRARIES[PROGRAM_CORE[prog['xaml']]])
    return flags(b) + ['-include', PCH]


def program_objs(b, odir):
    """what XAML's programs link besides the C++ library (program_link()
    picks a program's)"""
    components, core = library(b, odir)
    stubs, core_stubs = test_stubs(b, odir)
    return [components, stubs, core, core_stubs] + unbuilt(b, odir, [components, core, core_stubs])


def program_link(prog, objs):
    """of program_objs() @objs, what program @prog links: xaml_components.lib
    and, after it, xaml_teststubs.lib; or, for a program on the core itself
    ("xaml": "core"), xaml_core.lib, xaml_corestubs.lib and the stand-ins
    for the core not yet built (unbuilt())"""
    components, stubs, core, core_stubs = objs[:4]
    if prog.get('xaml') == 'core':
        return [components, core, core_stubs] + objs[4:]
    return [components, stubs]


# A function the built core calls in the parts of the core that do not
# build yet (the rendering and composition, input, layout, text, media and
# the element tree's classes beyond the resources) answers as an unbuilt
# part should: it reports its name once (on the debugger's output and the
# program's) and returns "not implemented" (E_NOTIMPL for an HRESULT),
# nothing (a void function, a destructor) or zero (a null pointer, false,
# 0).  A constructor or a function returning an object by value has no such
# answer: it stops the program, naming itself.  Each is the function's
# decorated name made an alternate name (lld-link /alternatename) for a
# thunk here, so a real definition anywhere (a program's, a stub's, the
# core as it builds) takes its place; and the list is the undefined names
# of the libraries a program on the core links less every name they
# define, worked out at each build (llvm-nm, llvm-undname), so it shrinks
# as the core builds.
UNBUILT_REPORT = os.path.join(HERE, 'src', 'unbuilt.cpp')
_UNBUILT = threading.Lock()


def _undecorated_kind(name, demangled):
    """'hr', 'zero', 'void' or 'stop' for a function's answer"""
    if not name.startswith('?'):
        return 'hr'
    d = re.sub(r'^(public|private|protected): ', '', demangled)
    d = re.sub(r'^(static |virtual )+', '', d)
    m = re.match(r'(.*?)\s*__cdecl\s', d)
    ret = m.group(1).strip() if m else ''
    if re.search(r'::~\w+\(', d):
        return 'void'
    if ret in ('long',):
        return 'hr'
    if ret == 'void':
        return 'void'
    if ret.endswith(('*', '&')) or re.fullmatch(r'(unsigned )?(bool|char|wchar_t|short|int|long|__int64|float|double)'
                                                 r'( const)?|enum [\w:]+', ret):
        return 'zero'
    return 'stop'


def unbuilt(b, odir, libs):
    """the stand-ins above, as objects (a library's member would not be
    linked, so its alternate names would not apply), x64 (as XAML's
    programs are)"""
    if b.ARCH != 'x64':
        return []
    with _UNBUILT:
        nm = b.llvm_tool('llvm-nm')
        r = subprocess.run([nm, '-u'] + libs, capture_output=True, text=True)
        undef = {l.split()[-1] for l in r.stdout.splitlines() if l.strip() and not l.endswith(':')}
        r = subprocess.run([nm, '--defined-only', '--extern-only'] + libs, capture_output=True, text=True)
        defined = {l.split()[2] for l in r.stdout.splitlines() if len(l.split()) >= 3}
        # (a name a program's DLLs define, the C runtime's, Windows', the C++
        # library's, is theirs: an alternate name stands only for one no
        # library defines; a C one is a Windows-style function returning an
        # HRESULT, the framework's DllGetActivationFactory)
        names = sorted(n for n in undef - defined if not n.startswith(('__imp_', '.')))
        r = subprocess.run([b.llvm_tool('llvm-undname')], input='\n'.join(names) + '\n', capture_output=True, text=True)
        dem = [l for l in r.stdout.splitlines() if l.strip()]
        demangled = dict(zip(dem[0::2], dem[1::2]))
        # the answers: report (src/unbuilt.cpp, the name in r11), then
        # return E_NOTIMPL, zero (rax and xmm0) or nothing; or stop
        asm = ['.text']
        for kind, ret in (('hr', ['movl $0x80004001, %eax']), ('zero', ['xorl %eax, %eax', 'xorps %xmm0, %xmm0']),
                          ('void', []), ('stop', None)):
            asm += [f'nova_xaml_unbuilt_as_{kind}:', '    subq $40, %rsp', '    movq %r11, %rcx']
            asm += ['    callq nova_xaml_unbuilt_stop'] if ret is None else \
                ['    callq nova_xaml_unbuilt_report', '    addq $40, %rsp'] + ['    ' + x for x in ret] + ['    retq']
        directives = []
        for i, n in enumerate(names):
            asm += [f'.globl nova_xaml_unbuilt_{i}', f'nova_xaml_unbuilt_{i}:',
                    f'    leaq nova_xaml_unbuilt_name_{i}(%rip), %r11',
                    f'    jmp nova_xaml_unbuilt_as_{_undecorated_kind(n, demangled.get(n, ""))}']
            directives.append(f'/alternatename:{n}=nova_xaml_unbuilt_{i}')
        asm += ['.section .rdata,"dr"'] + [f'nova_xaml_unbuilt_name_{i}: .asciz "{n}"' for i, n in enumerate(names)]
        asm += ['.section .drectve,"yn"'] + [f'    .ascii " {d}"' for d in directives]
        text = '\n'.join(asm) + '\n'
        s = os.path.join(odir, 'xaml_unbuilt.s')
        if not os.path.exists(s) or open(s).read() != text:
            open(s, 'w').write(text)
        objs = b.compile_many([s], odir, '', ['--target=' + b.TARGETS[b.ARCH]], [UNBUILT_REPORT])
        objs += b.compile_many([UNBUILT_REPORT], odir, 'xamlreport_', _msvcp(b).cxx_flags(b), [], compiler='clang++')
    return objs

