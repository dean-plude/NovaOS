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
# under its Windows names (32.5's first part).  The interfaces MIDL 3
# synthesizes get the IDs Microsoft's own metadata gives them (iids.txt,
# muxiids.py), which apps were compiled with.  The XAML resources (each
# control's default style and theme resources, which Microsoft compiles to
# XBF in Microsoft.UI.Xaml's resources.pri) are merged as its build merges
# them (mergexaml.py) and kept as XAML text in C:\Programs\Microsoft.UI.Xaml\
# Themes, where an unpackaged program's ms-appx: finds them.
# docs/design/windows-ui-xaml.md.
import hashlib, os, re, shutil, subprocess, sys, threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W2 = os.path.join(ROOT, 'third_party', 'winui2')
DEV = os.path.join(W2, 'dev')
CPPWINRT = os.path.join(ROOT, 'third_party', 'cppwinrt')
WINMD = os.path.join(ROOT, 'third_party', 'winmd', 'Windows.winmd')
INC = os.path.join(HERE, 'inc')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'winrt'))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'appx'))
import mergexaml  # noqa: E402
import makepri    # noqa: E402

# The features built so far: what the project always imports (TestHooks,
# Common, Telemetry) and Microsoft's smallest set the DLL builds with (the
# MUXControlsInnerLoop solution's, FeatureAreas.props: the DLL's own sources
# use Reveal and Acrylic), with InfoBadge (and ImageIcon, which IconSource
# needs) and the controls Calculator uses so far: ProgressBar,
# ProgressRing and SwipeControl.  Each is a vcxitems of
# dev/dll/Microsoft.UI.Xaml.vcxproj's ImportGroup.
FEATURES = '''TestHooks/TestHooks.vcxitems Common/Common.vcxitems Telemetry/Telemetry.vcxitems
ResourceHelper/ResourceHelper.vcxitems Collections/Collections.vcxitems Effects/Microsoft.UI.Private.Composition.Effects.vcxitems
Materials/Acrylic/AcrylicBrush.vcxitems Materials/Reveal/RevealBrush.vcxitems Materials/Backdrop/SystemBackdropComponent.vcxitems
Lights/Lights.vcxitems ImageIcon/ImageIcon.vcxitems IconSource/IconSource.vcxitems AnimatedVisualPlayer/AnimatedVisualPlayer.vcxitems
AnimatedIcon/AnimatedIcon.vcxitems SplitButton/SplitButton.vcxitems DropDownButton/DropDownButton.vcxitems
InfoBadge/InfoBadge.vcxitems ProgressBar/ProgressBar.vcxitems ProgressRing/ProgressRing.vcxitems
SwipeControl/SwipeControl.vcxitems Repeater/Repeater.vcxitems
NavigationView/NavigationView.vcxitems RadioButtons/RadioButtons.vcxitems'''.split()

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
    # a runtime class projects its overridable and protected interfaces too,
    # as the C++/WinRT WinUI 2 pins (2.0.210309.3) does: WinUI 2 calls them
    # on its own classes (VirtualizingLayoutContext.ItemCountCore())
    ('cppwinrt/code_writers.h', '            if ((!info.defaulted || info.base) && (!info.is_protected && !info.overridable))',
     '            if (!info.defaulted || info.base)'),
    # windows-rs's Windows.winmd leaves OverridableAttribute off the classes'
    # interface rows, so the Windows.UI.Xaml classes WinUI 2 derives from had
    # no overridable interfaces and its overrides (OnApplyTemplate,
    # MeasureOverride, ...) were never produced: one of those classes'
    # I...Overrides[N] interfaces, the SDK's every overridable interface, is
    # taken as overridable
    ('cppwinrt/helpers.h',
     '            info.overridable = overridable || has_attribute(impl, "Windows.Foundation.Metadata", "OverridableAttribute");',
     '            info.overridable = overridable || has_attribute(impl, "Windows.Foundation.Metadata", "OverridableAttribute") ||\n'
     '                (name.find("Windows::UI::Xaml::") != std::string::npos &&\n'
     '                 name.find("Overrides", name.rfind("::")) != std::string::npos &&\n'
     '                 name.find_first_not_of("0123456789", name.find("Overrides", name.rfind("::")) + 9) == std::string::npos);'),
    # ... and produced: the Windows projection writes the produce<> those
    # overrides need only for an exclusive interface it takes as overridable
    ('cppwinrt/helpers.h',
     '        auto interface_name = type_name(type);\n',
     '        auto interface_name = type_name(type);\n'
     '        if (interface_name.name_space.substr(0, 15) == "Windows.UI.Xaml" &&\n'
     '            interface_name.name.find("Overrides") != std::string_view::npos &&\n'
     '            interface_name.name.find_first_not_of("0123456789", interface_name.name.find("Overrides") + 9) == std::string_view::npos)\n'
     '        {\n'
     '            return true;\n'
     '        }\n'),
    # get_self of a null object is null (MSVC keeps it null; clang's
    # static_cast to the implementation's base gives null minus an offset):
    # ItemsRepeater asks it of an element with no VirtualizationInfo
    ('strings/base_implements.h',
     '        return &static_cast<impl::produce<D, default_interface<I>>*>(get_abi(from))->shim();',
     '        auto abi = get_abi(from);\n'
     '        return abi ? &static_cast<impl::produce<D, default_interface<I>>*>(abi)->shim() : nullptr;'),
    # and so is try_as of an implementation type that the object is not
    # (the same offset null, through wrap_as_result): AnimatedIcon asks it
    # of each element whose AnimatedIcon.State changes
    ('strings/base_windows.h',
     '        return { &static_cast<produce<T, typename default_interface<T>::type>*>(result)->shim(), take_ownership_from_abi };',
     '        return { result ? &static_cast<produce<T, typename default_interface<T>::type>*>(result)->shim() : nullptr,\n'
     '                 take_ownership_from_abi };'),
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
def published_iids(types, own):
    """the interface IDs Microsoft's Microsoft.UI.Xaml.winmd gives (iids.txt,
    see muxiids.py) for the interfaces the IDL names none for: those MIDL 3
    synthesizes, whose IDs apps were compiled with.  An ID the IDL does name
    must be the published one."""
    iids = {}
    for line in open(os.path.join(HERE, 'iids.txt')):
        if line.strip() and not line.startswith('#'):
            n, g = line.split()
            iids[n] = g.strip('{}')
    for n in own:
        g = getattr(types[n], 'guid', None)
        if g and n in iids and g.lower() != iids[n]:
            raise SystemExit(f'WinUI 2 IDL: {n} is {{{g}}}, Microsoft.UI.Xaml.winmd has {{{iids[n]}}}')
    return iids


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
    winmdwrite.write(out, 'Microsoft.UI.Xaml', types, own, abigen.Generator(types, published_iids(types, own)).iid_for)
    return out, types, own


# -- 1b. the XAML resources -------------------------------------------------------
# the features whose XAML pages are merged besides FEATURES' (CommonStyles has
# only pages: WinUI 2's styles of the framework's own controls)
PAGE_FEATURES = ['CommonStyles/CommonStyles.vcxitems']
XAML_TYPE_NS = ('using:Microsoft.UI.Xaml.Controls', 'using:Microsoft.UI.Xaml.Controls.Primitives',
                'using:Microsoft.UI.Xaml.Media')


def pages():
    """each kind of merged file's pages by release, as the project's
    CategorizeSharedPages lists them: the Page items of the features built,
    in the order Microsoft.UI.Xaml.vcxproj imports their vcxitems, by
    Priority (1-8, then none), each in its variant (ControlsResourcesVersion
    Version2 -> '', Version1 -> '_v1', Any -> both)"""
    proj = open(os.path.join(DEV, 'dll', 'Microsoft.UI.Xaml.vcxproj'), encoding='utf-8-sig').read()
    order = [f.replace('\\', '/') for f in re.findall(r'<Import Project="\.\.\\([^"]+\.vcxitems)"', proj)]
    feats = sorted(FEATURES + PAGE_FEATURES, key=lambda f: order.index(f) if f in order else len(order))
    shared = []
    for f in feats:
        path = os.path.join(DEV, f)
        text = open(path, encoding='utf-8-sig').read()
        for inc, body in re.findall(r'<Page Include="\$\(MSBuildThisFileDirectory\)([^"]+)"\s*>(.*?)</Page>', text, re.S):
            meta = dict(re.findall(r'<(\w+)>([^<]*)</\1>', body))
            shared.append((ci_path(os.path.normpath(os.path.join(os.path.dirname(path), inc.replace('\\', '/')))), meta))
    prio = [str(i) for i in range(1, 9)] + ['Undefined']
    shared.sort(key=lambda p: prio.index(p[1].get('Priority', 'Undefined')))
    out = {}
    for v, skip in (('', 'Version1'), ('_v1', 'Version2')):
        for kind, typ in (('generic', 'DefaultStyle'), ('themeresources', 'ThemeResources')):
            out[kind + v] = {rel: [p for p, m in shared if m.get('Version') == rel and m.get('Type') == typ
                                   and m.get('ControlsResourcesVersion', 'Any') != skip]
                             for rel in mergexaml.RELEASES}
        compact = os.path.join(DEV, 'dll', 'DensityStyles', 'Compact.xaml')
        out['compact_themeresources' + v] = {rel: ps + [compact] for rel, ps in out['themeresources' + v].items()}
    return out


# the markup the framework cannot make yet, whose resources are left out as
# an unbuilt type's are (none since data binding, {Binding}, built: step
# 32.5's fourth part)
FRAMEWORK_LACKS = ()


# the framework's type table (Windows.UI.Xaml's, vendored under
# Microsoft.UI.Xaml names): a type the presentation namespace names that it
# lacks is Windows-only (InkToolbar's, in Windows.UI.Xaml.InkControls.dll)
STATIC_METADATA = os.path.join(ROOT, 'third_party', 'microsoft-ui-xaml', 'dxaml', 'xcp', 'components',
                               'metadata', 'StaticMetadata.g.cpp')


def framework_types():
    """the names of the types the framework's parser knows, as the XAML
    presentation namespace gives them (no prefix)"""
    with open(STATIC_METADATA, encoding='utf-8-sig') as f:
        text = f.read()
    return set(re.findall(r'XSTRING_PTR_STORAGE\(L"(\w+)"\), // Name\s+XSTRING_PTR_STORAGE\(L"[\w.]+"\), // Full name',
                          text))


def leave_out_unbuilt(text, types, framework=None):
    """@text (a merged dictionary) without the resources that name a WinUI 2
    type this build does not have (a feature not built yet) or use markup in
    FRAMEWORK_LACKS, or name such a resource: XAML text resolves each type as it is parsed, where XBF
    resolves a resource's types when it is first looked up"""
    from xml.dom import minidom
    doc = minidom.parseString(text.encode('utf-8'))
    root = doc.documentElement
    prefixes = {a.name[6:]: a.value for a in root.attributes.values() if a.name.split(':')[0] == 'xmlns'}
    # (a name the framework lacks is looked up in WinUI 2's metadata
    # provider under each of its namespaces: AcrylicBrush is WinUI 2's)
    framework = (framework_types() if framework is None else framework) | {t.rsplit('.', 1)[1] for t in types
                                                                       if t.startswith('Microsoft.UI.Xaml.')}
    def known(prefix, name):
        ns = prefixes.get(prefix, '')
        if ns == mergexaml.P:
            return name.split('.')[0] in framework
        return ns not in XAML_TYPE_NS or (ns[len('using:'):] + '.' + name.split('.')[0]) in types
    gone, left = set(), []
    def unbuilt(el):
        for e in [el] + el.getElementsByTagName('*'):
            prefix, _, name = e.tagName.rpartition(':')
            if not known(prefix, name):
                return True
            for a in e.attributes.values():
                m = re.fullmatch(r'(?:(\w+):)?(\w+)', a.value) if a.name in ('TargetType', 'x:DataType') else None
                if m and not known(m.group(1) or '', m.group(2)):
                    return True
                if any(k in gone for k in re.findall(r'{(?:StaticResource|ThemeResource)\s+([\w.]+)}', a.value)):
                    return True
                if any(m in a.value for m in FRAMEWORK_LACKS):
                    return True
        return False
    dicts = [root] + [d for t in root.getElementsByTagName('ResourceDictionary.ThemeDictionaries')
                      for d in t.childNodes if d.nodeType == d.ELEMENT_NODE]
    while True:
        out = [(d, el) for d in dicts for el in d.childNodes
               if el.nodeType == el.ELEMENT_NODE and el.tagName != 'ResourceDictionary.ThemeDictionaries' and unbuilt(el)]
        if not out:
            return root.toxml(), sorted(left)
        for d, el in out:
            k = el.getAttribute('x:Key') or el.getAttribute('x:Name') or el.getAttribute('TargetType')
            gone.add(k)
            left.append(k)
            d.removeChild(el)


def lookup_order(text):
    """@text (a merged dictionary) with each dictionary's resources ordered
    so that one comes after the resources of its dictionary it names
    ({StaticResource}, {ThemeResource}), the merged order kept otherwise:
    XBF defers each resource until it is first looked up, so a resource may
    name one later in its dictionary (BasedOn="{StaticResource
    DefaultMenuFlyoutItemStyle}" before that style), where XAML text lets
    a resource see only the keys before it (the framework's CDeferredKeys
    sets a lookup boundary at the resource being created)"""
    from xml.dom import minidom
    doc = minidom.parseString(text.encode('utf-8'))
    root = doc.documentElement
    for d in [root] + root.getElementsByTagName('ResourceDictionary'):
        items = [e for e in d.childNodes if e.nodeType == e.ELEMENT_NODE and '.' not in e.tagName.split(':')[-1]]
        by_key = {}
        for e in items:
            k = e.getAttribute('x:Key') or e.getAttribute('x:Name')
            if k:
                by_key[k] = e
        def refs(e):
            out = []
            for n in [e] + e.getElementsByTagName('*'):
                for at in n.attributes.values():
                    out += re.findall(r'{(?:StaticResource|ThemeResource)\s+(?:ResourceKey=)?([\w.]+)\s*}', at.value)
                if n.tagName in ('StaticResource', 'ThemeResource') and n.getAttribute('ResourceKey'):
                    out.append(n.getAttribute('ResourceKey'))
            return [by_key[k] for k in out if k in by_key and by_key[k] is not e]
        order, done = [], set()
        for e in items:
            stack = [(e, iter(refs(e)))]
            if id(e) in done:
                continue
            done.add(id(e))
            while stack:
                node, deps = stack[-1]
                dep = next(deps, None)
                if dep is None:
                    order.append(node)
                    stack.pop()
                elif id(dep) not in done:
                    done.add(id(dep))
                    stack.append((dep, iter(refs(dep))))
        if order != items:
            for e in items:
                d.removeChild(e)
            for e in order:
                d.appendChild(e)
    return root.toxml()


# the release whose merged files are installed: XamlControlsResources
# and each control's DefaultStyleResourceUri pick them by the
# UniversalApiContract ApiInformation reports, which on NovaOS (Windows 10
# 22H2, contract 10: ole32's winrt_classes.c) is 19H1's (21H1's is 14)
RELEASE = '19H1'


def themes(gen, types):
    """WinUI 2's merged XAML resources for RELEASE (19h1_themeresources.xaml,
    19h1_generic.xaml ... in each variant), written under @gen/Themes"""
    out = os.path.join(gen, 'Themes')
    os.makedirs(out, exist_ok=True)
    report = []
    for kind, rels in pages().items():
        name = RELEASE.lower() + '_' + kind + '.xaml'
        text, left = leave_out_unbuilt(mergexaml.merge(rels)[RELEASE][1], types)
        text = lookup_order(text)
        path = os.path.join(out, name)
        if not os.path.exists(path) or open(path, encoding='utf-8').read() != text:
            open(path, 'w', encoding='utf-8').write(text)
        report.append('%s: left out (feature or framework part not built): %s\n' % (name, ', '.join(left) or 'nothing'))
    open(os.path.join(out, 'left-out.txt'), 'w').write(''.join(report))


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
            # (by its own name when it goes on with #include_next, which
            # needs it found through the include path)
            path = os.path.join(*target)
            texts[s] = ('#include <%s>\n' % target[1]) if '#include_next' in open(path, errors='replace').read() \
                else '#include "%s"\n' % path
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
        themes(gen, types)
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
                               b.HOOKS['msvcp140'].program_flags(b) + ['-I', INC, '-I', os.path.join(
                                   ROOT, 'userland', 'windows_ui_xaml', 'inc')], [], compiler='clang++')
        return b.compile_many(allsrc, odir, 'muxc_', f, headers + [pch], compiler='clang++') + guids + \
            b.HOOKS['msvcp140'].start_obj(b, odir, 'muxc_')


def link(b, odir, objs, deps, base):
    # (and tlregdtr.lib: the thread_local variables' start-up, as programs link it)
    b.HOOKS['msvcp140'].link_dll(b, odir, 'Microsoft.UI.Xaml', objs + [os.path.join(odir, 'tlregdtr.lib')], base,
                                 [d for d in deps if d not in ('msvcrt', 'vcruntime140', 'kernel32', 'ntdll')],
                                 ['/def:' + os.path.join(DEV, 'dll', 'Microsoft.UI.Xaml.def')])
    # the XAML resources where an unpackaged program's ms-appx:///
    # Microsoft.UI.Xaml/Themes/... finds them: its own folder (C:\Programs,
    # as the NuGet package copies them into an app's), since WinUI 2 asks
    # its framework package (ms-appx://Microsoft.UI.Xaml.2.x/) only when it
    # runs from one (step 32.6)
    out = os.path.join(odir, 'winui2', 'Themes')
    for name in sorted(os.listdir(out)):
        if name.endswith('.xaml'):
            b.built.append(('\\Programs\\Microsoft.UI.Xaml\\Themes\\' + name, os.path.join(out, name)))
    # and WinUI 2's strings (its controls' names, tooltips and narrator
    # text), every feature's Strings/en-us/Resources.resw in the one scope
    # ResourceAccessor asks for, Microsoft.UI.Xaml/Resources, of the
    # resources.pri the NuGet package merges them into: the program's own,
    # beside it (MrmCoreR.dll's ResourceManager.Current reads it)
    strings = {}
    for feature in sorted(os.listdir(DEV)):
        resw = os.path.join(DEV, feature, 'Strings', 'en-us', 'Resources.resw')
        if os.path.exists(resw):
            strings.update(makepri.read_resw(resw))
    pri = os.path.join(odir, 'winui2', 'resources.pri')
    with open(pri, 'wb') as f:
        f.write(makepri.build('Microsoft.UI.Xaml', [('Microsoft.UI.Xaml/Resources', strings)]))
    b.built.append(('\\Programs\\resources.pri', pri))
