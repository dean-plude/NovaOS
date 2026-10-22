# Microsoft.UI.Xaml.dll 2.x's build hooks (tools/build_userland.py calls
# them with itself as b): WinUI 2, the controls library UWP apps ship on top
# of the system XAML (roadmap step 32.5), built from Microsoft's own sources
# (third_party/winui2, the winui2/main branch of microsoft-ui-xaml, MIT) as
# Microsoft builds it with MSBuild, MIDL and C++/WinRT:
#
#   1. the IDL of the features built (FEATURES: the vcxitems the DLL's
#      project imports, each listing its sources and IDL) is merged into one
#      file, run through the C preprocessor and read by tools/winrt/idl.py,
#      then written as Microsoft.UI.Xaml.winmd by tools/winrt/winmdwrite.py
#      (MIDL's job);
#   2. C++/WinRT's cppwinrt (third_party/cppwinrt, MIT, built here for the
#      build machine) writes the projection of Windows' metadata and of that
#      .winmd, with the component's implementation templates (Foo.g.h) and
#      its activation table (module.g.cpp), as the CppWinRT NuGet package's
#      targets run it;
#   3. xamlmeta.py writes XamlMetadataProviderGenerated.cpp, the XAML type
#      table Microsoft's T4 template (dll/XamlMetadataProviderGenerated.tt)
#      writes from the .winmd;
#   4. the sources are compiled as the project compiles them (clang in MSVC
#      mode, C++17, the project's include directories and definitions, no
#      RTTI, the precompiled header dll/pch.h) over Microsoft's STL and
#      MinGW-w64's Windows headers, with the Windows Runtime ABI headers
#      tools/winrt/abigen.py writes for the few classic COM interfaces it
#      uses (IReferenceTracker*), and linked as a DLL exporting what
#      dll/Microsoft.UI.Xaml.def exports.
#
# WinUI 2's classes then call the system XAML through the projection of
# Windows.UI.Xaml (RoGetActivationFactory), which NovaOS's framework answers
# under its Windows names (32.5's first part).  The XAML resources (each
# control's template and theme resources, compiled to XBF and kept in
# Microsoft.UI.Xaml's resources.pri) come later.  docs/design/windows-ui-xaml.md.
import hashlib, os, re, shutil, subprocess, sys, threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W2 = os.path.join(ROOT, 'third_party', 'winui2')
DEV = os.path.join(W2, 'dev')
CPPWINRT = os.path.join(ROOT, 'third_party', 'cppwinrt')
WINMD = os.path.join(ROOT, 'third_party', 'winmd', 'Windows.winmd')
INC = os.path.join(HERE, 'inc')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'winrt'))

# The features built so far: what the project always imports (TestHooks,
# Common, Telemetry) and Microsoft's smallest set the DLL builds with (the
# MUXControlsInnerLoop solution's, FeatureAreas.props: the DLL's own sources
# use Reveal and Acrylic), with InfoBadge (and ImageIcon, which IconSource needs).  Each is a vcxitems of
# dev/dll/Microsoft.UI.Xaml.vcxproj's ImportGroup.
FEATURES = '''TestHooks/TestHooks.vcxitems Common/Common.vcxitems Telemetry/Telemetry.vcxitems
ResourceHelper/ResourceHelper.vcxitems Collections/Collections.vcxitems Effects/Microsoft.UI.Private.Composition.Effects.vcxitems
Materials/Acrylic/AcrylicBrush.vcxitems Materials/Reveal/RevealBrush.vcxitems Materials/Backdrop/SystemBackdropComponent.vcxitems
Lights/Lights.vcxitems ImageIcon/ImageIcon.vcxitems IconSource/IconSource.vcxitems AnimatedVisualPlayer/AnimatedVisualPlayer.vcxitems
AnimatedIcon/AnimatedIcon.vcxitems SplitButton/SplitButton.vcxitems DropDownButton/DropDownButton.vcxitems
InfoBadge/InfoBadge.vcxitems'''.split()

# dev/dll's own sources (Microsoft.UI.Xaml.Common.targets and the project);
# module.g.cpp and XamlMetadataProviderGenerated.cpp are generated
DLL_SOURCES = '''CommandingHelpers.cpp MUXControlsFactory.cpp dllmain.cpp DoubleUtil.cpp DownlevelHelper.cpp FloatUtil.cpp
RegUtil.cpp SharedHelpers.cpp XamlMember.cpp XamlMetadataProvider.cpp XamlType.cpp XamlControlsResources.cpp
../Generated/XamlControlsResources.properties.cpp'''.split()

# the project's definitions (Release, Unicode) besides each feature's
DEFINES = ['UNICODE', '_UNICODE', 'NDEBUG', 'DISABLE_WINRT_DEPRECATION', 'WINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP',
           '_HAS_STATIC_RTTI=0']

# C++/WinRT as NovaOS builds its tool: (file, old, new)
CPPWINRT_PATCHES = [
    # a method's result parameter row with no name (windows-rs's Windows.winmd
    # has some) is named as one with no row is, not left as "void* {};"
    ('cppwinrt/helpers.h', '            if (m_return)\n            {\n                name = m_return.Name();',
     '            if (m_return && !m_return.Name().empty())\n            {\n                name = m_return.Name();'),
]


def ci_path(p):
    """@p on a case-sensitive disk, spelled as the projects spell it (Windows ignores case)"""
    if os.path.exists(p):
        return p
    d, base = os.path.split(p)
    d = ci_path(d)
    for x in os.listdir(d):
        if x.lower() == base.lower():
            return os.path.join(d, x)
    raise SystemExit(f'third_party/winui2: {p} not found')


def items(vcxitems):
    """a vcxitems' (sources, IDL files, definitions)"""
    path = os.path.join(DEV, vcxitems)
    text = open(path, encoding='utf-8-sig').read()
    d = os.path.dirname(path)
    def files(tag):
        return [ci_path(os.path.normpath(os.path.join(d, f.replace('\\', '/'))))
                for f in re.findall(r'<%s Include="\$\(MSBuildThisFileDirectory\)([^"]+)"' % tag, text)]
    defines = [x for t in re.findall(r'<PreprocessorDefinitions>([^<]*)', text) for x in t.split(';')
               if x and not x.startswith('%(')]
    return files('ClCompile'), files('Midl'), defines


def feature_items():
    srcs, idls, defines = [], [os.path.join(W2, 'idl', 'Microsoft.UI.Xaml.idl')], list(DEFINES)
    for f in FEATURES:
        s, i, d = items(f)
        srcs += s
        idls += i
        defines += [x for x in d if x not in defines]
    srcs += [ci_path(os.path.normpath(os.path.join(DEV, 'dll', f))) for f in DLL_SOURCES]
    return srcs, idls, defines


# -- 1. the metadata ------------------------------------------------------------
def write_winmd(gen, idls):
    """Microsoft.UI.Xaml.winmd from the features' IDL, as MIDL would write it
    (with the experimental members, as C++/WinRT's -ignore_velocity keeps
    them for the DLL's own build); returns (path, the model's types)"""
    import abigen, winmdwrite
    merged = ''.join('#include "%s"\n' % f for f in idls)
    r = subprocess.run(['clang', '-E', '-P', '-x', 'c', '-I', DEV, '-I', os.path.join(W2, 'idl'), '-'],
                       input=merged, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit('WinUI 2 IDL: ' + r.stderr)
    pp = os.path.join(gen, 'Microsoft.UI.Xaml.idl')
    open(pp, 'w').write(r.stdout.replace('﻿', ''))
    types, *_ = abigen.load([WINMD], [pp], [], [])
    own = sorted(n for n, t in types.items() if n.startswith('Microsoft.') and not getattr(t, 'classic', False))
    out = os.path.join(gen, 'Microsoft.UI.Xaml.winmd')
    winmdwrite.write(out, 'Microsoft.UI.Xaml', types, own, abigen.Generator(types, {}).iid_for)
    return out, types, own


# -- 2. C++/WinRT ---------------------------------------------------------------
_TOOL = threading.Lock()

def cppwinrt_tool(b, odir):
    """the cppwinrt executable for this machine, built once (both
    architectures' passes share it)"""
    out = os.path.join(odir if b.ARCH == 'x64' else os.path.dirname(odir), 'cppwinrt')
    exe = os.path.join(out, 'cppwinrt')
    with _TOOL:
        stamp = hashlib.sha1(open(__file__, 'rb').read() + b''.join(
            open(os.path.join(r, f), 'rb').read() for r, _, fs in sorted(os.walk(CPPWINRT)) for f in sorted(fs))).hexdigest()
        if os.path.exists(exe) and open(exe + '.stamp').read() == stamp:
            return exe
        src = os.path.join(out, 'src')
        if os.path.exists(src):
            shutil.rmtree(src)
        shutil.copytree(CPPWINRT, src)
        for f, old, new in CPPWINRT_PATCHES:
            p = os.path.join(src, f)
            text = open(p).read()
            if old not in text:
                raise SystemExit(f'third_party/cppwinrt/{f}: patch no longer applies')
            open(p, 'w').write(text.replace(old, new, 1))
        # the strings its writers emit (strings/*.h), as its prebuild tool makes them
        h = ['#pragma once', 'namespace cppwinrt::strings {', 'extern "C++" {']
        c = ['#include "strings.h"', 'namespace cppwinrt::strings {']
        for name in sorted(os.listdir(os.path.join(src, 'strings'))):
            text = open(os.path.join(src, 'strings', name), encoding='utf-8').read()
            n = os.path.splitext(name)[0]
            h.append(f'extern char const {n}[{len(text.encode()) + 1}];')
            chunks = [text[i:i + 8000] for i in range(0, len(text), 8000)] or ['']
            c.append(f'extern char const {n}[] = ' + '\n'.join('R"xyz(%s)xyz"' % x for x in chunks) + ';')
        open(os.path.join(src, 'strings.h'), 'w').write('\n'.join(h + ['}', '}']) + '\n')
        open(os.path.join(src, 'strings.cpp'), 'w').write('\n'.join(c + ['}']) + '\n')
        cxx = shutil.which('clang++') or 'c++'
        subprocess.run([cxx, '-std=c++17', '-O1', '-DCPPWINRT_VERSION_STRING="2.0.250303.1"',
                        '-I', src, '-I', os.path.join(src, 'winmd', 'src'),
                        os.path.join(src, 'cppwinrt', 'main.cpp'), os.path.join(src, 'strings.cpp'),
                        '-o', exe, '-lpthread'], check=True)
        open(exe + '.stamp', 'w').write(stamp)
    return exe


def projection(b, odir, gen, winmd):
    """the C++/WinRT projection of Windows and of WinUI 2, and WinUI 2's
    component templates, in GEN (rewritten when its inputs change)"""
    exe = cppwinrt_tool(b, odir)
    stamp = os.path.join(gen, 'projection.stamp')
    key = hashlib.sha1(open(exe, 'rb').read() + open(winmd, 'rb').read() + open(WINMD, 'rb').read()).hexdigest()
    if os.path.exists(stamp) and open(stamp).read() == key:
        return
    for d in ('winrt', 'Controls', 'Media', 'Automation', 'XamlTypeInfo', 'Microsoft'):
        if os.path.exists(os.path.join(gen, d)):
            shutil.rmtree(os.path.join(gen, d))
    b.run([exe, '-in', WINMD, '-out', gen])
    b.run([exe, '-in', winmd, '-ref', WINMD, '-out', gen, '-component', '-opt', '-ignore_velocity',
           '-name', 'Microsoft.UI.Xaml'])
    open(stamp, 'w').write(key)


# -- 4. compiling -----------------------------------------------------------------
def fold(out, dirs, srcdirs):
    """OUT/<spelling> for every include spelling in WinUI 2's sources that
    names a file in @dirs only when case is ignored (Windows.Foundation.h,
    SDKDDKVer.h), and, for the headers CppWinRTIncludes.h tests with
    __has_include("winrt\\X.h") (which clang does not read as a path on
    this disk), a file of that literal name"""
    real, lower = set(), {}
    for d in dirs:
        for root, _, files in os.walk(d):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), d).replace(os.sep, '/')
                real.add(rel)
                lower.setdefault(rel.lower(), (d, rel))
    pat = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.M)
    want, has = set(), set()
    for top in srcdirs:
        for root, _, files in os.walk(top):
            for f in files:
                if f.endswith(('.h', '.cpp')):
                    text = open(os.path.join(root, f), errors='replace').read()
                    for m in pat.finditer(text):
                        s = m.group(1).replace('\\', '/')
                        if s not in real and not os.path.exists(os.path.join(root, s)):
                            want.add(s)
                    has.update(re.findall(r'__has_include\("(winrt\\[^"]+)"\)', text))
    os.makedirs(out, exist_ok=True)
    texts = {}
    for s in want:
        target = lower.get(s.lower())
        if target:
            texts[s] = '#include "%s"\n' % os.path.join(*target)
    for s in has:
        if os.path.exists(os.path.join(dirs[-1], s.replace('\\', '/'))):
            texts[s] = '#include <%s>\n' % s.replace('\\', '/')
    for s, text in texts.items():
        p = os.path.join(out, s)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        if not os.path.exists(p) or open(p).read() != text:
            open(p, 'w').write(text)


def flags(b, gen, defines, dirs):
    """clang's flags for WinUI 2's sources, as its project compiles them"""
    m = b.HOOKS['msvcp140']
    base = ['-std=c++20' if f.startswith('-std=') else f for f in m.program_flags(b)] + ['-fno-rtti']
    abi = [x for x in b.winrt_abi_flags({'winrt_abi': True}) if x != '-I'][0]
    # (the project's AdditionalIncludeDirectories: the generated files'
    # folders, dev/inc, the generated properties, then each feature's)
    inc = [os.path.join(gen, 'fold'), INC, os.path.join(DEV, 'dll'), gen] + \
          [os.path.join(gen, *d.split('/')) for d in ('Automation/Peers', 'Controls', 'Controls/Primitives', 'Media',
                                                     'XamlTypeInfo', 'Microsoft/UI/Private/Composition/Effects',
                                                     'Microsoft/UI/Private/Controls', 'Microsoft/UI/Private/Media')] + \
          [os.path.join(DEV, 'inc'), os.path.join(gen, 'Generated')] + dirs + \
          [os.path.join(ROOT, 'userland', 'windows_ui_xaml', 'inc'), abi]   # (the SDK's weakreference.h ...)
    return [f for d in inc for f in ('-I', d)] + base + ['-D' + d for d in defines] + \
        ['-idirafter', os.path.join(ROOT, 'userland', 'include'), '-Wno-everything',
         '-include', os.path.join(INC, 'nova_muxc.h')]


def generated(gen):
    """dev/Generated (the properties files Microsoft's code generator wrote)
    copied to @gen/Generated, each [[gsl::suppress(con)]] with the string
    argument clang wants"""
    out = os.path.join(gen, 'Generated')
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(os.path.join(DEV, 'Generated')):
        text = open(os.path.join(DEV, 'Generated', f), encoding='utf-8-sig').read() \
            .replace('gsl::suppress(con)', 'gsl::suppress("con")')
        p = os.path.join(out, f)
        if not os.path.exists(p) or open(p, encoding='utf-8').read() != text:
            open(p, 'w', encoding='utf-8').write(text)
    return out


_BUILD = threading.Lock()

def objs(b, odir):
    with _BUILD:
        gen = os.path.join(odir, 'winui2')
        os.makedirs(gen, exist_ok=True)
        srcs, idls, defines = feature_items()
        winmd, types, own = write_winmd(gen, idls)
        projection(b, odir, gen, winmd)
        xamlmeta = b.load_hook('microsoft_ui_xaml_meta', os.path.join(HERE, 'xamlmeta.py'))
        meta = os.path.join(gen, 'XamlMetadataProviderGenerated.cpp')
        text = xamlmeta.generate(types, own)
        if not os.path.exists(meta) or open(meta).read() != text:
            open(meta, 'w').write(text)
        abi = [x for x in b.winrt_abi_flags({'winrt_abi': True}) if x != '-I'][0]
        # each feature's folder (and TabView's, for the TabViewTrace.h
        # TestHooks always includes)
        dirs = sorted({os.path.dirname(os.path.join(DEV, f)) for f in FEATURES} | {os.path.join(DEV, 'TabView')})
        gendir = generated(gen)
        fold(os.path.join(gen, 'fold'), [INC, abi, os.path.join(ROOT, 'userland', 'windows_ui_xaml', 'inc'),
                                         os.path.join(DEV, 'inc'), os.path.join(DEV, 'dll')] + dirs +
             [b.HOOKS['msvcp140'].STL + '/inc', b.HOOKS['msvcp140'].mingw_include(), gen],
             [DEV])
        f = flags(b, gen, defines, dirs)
        # the precompiled header, as the project's pch.cpp makes it
        pch = os.path.join(odir, 'muxc_pch.pch')
        headers = [os.path.join(gen, 'projection.stamp'), os.path.abspath(__file__)] + \
            [os.path.join(r, x) for r, _, fs in os.walk(INC) for x in fs]
        newest = max(os.path.getmtime(h) for h in headers + [os.path.join(DEV, 'dll', 'pch.h')])
        if not os.path.exists(pch) or os.path.getmtime(pch) < newest:
            b.run(['clang++'] + f + ['-x', 'c++-header', os.path.join(DEV, 'dll', 'pch.h'), '-o', pch])
        f = f + ['-include-pch', pch]
        allsrc = [os.path.join(gendir, os.path.basename(s)) if os.path.dirname(s) == os.path.join(DEV, 'Generated') else s
                  for s in srcs] + [os.path.join(gen, 'module.g.cpp'), meta, os.path.join(HERE, 'src', 'nova.cpp')]
        guids = b.compile_many([os.path.join(HERE, 'src', 'd2d1guids.cpp')], odir, 'muxc_',
                               b.HOOKS['msvcp140'].program_flags(b) + ['-I', INC], [], compiler='clang++')
        return b.compile_many(allsrc, odir, 'muxc_', f, headers + [pch], compiler='clang++') + guids + \
            b.HOOKS['msvcp140'].start_obj(b, odir, 'muxc_')


def link(b, odir, objs, deps, base):
    # (and tlregdtr.lib: the thread_local variables' start-up, as programs link it)
    b.HOOKS['msvcp140'].link_dll(b, odir, 'Microsoft.UI.Xaml', objs + [os.path.join(odir, 'tlregdtr.lib')], base,
                                 [d for d in deps if d not in ('msvcrt', 'vcruntime140', 'kernel32', 'ntdll')],
                                 ['/def:' + os.path.join(DEV, 'dll', 'Microsoft.UI.Xaml.def')])
