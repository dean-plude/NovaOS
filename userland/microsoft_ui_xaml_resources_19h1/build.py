# Microsoft.UI.Xaml.Resources.19H1.dll's build hooks (tools/build_userland.py
# calls them with itself as b): the XAML framework's resource DLL, which
# holds the default styles of its controls and its theme resources
# (roadmap step 32.4).  The framework loads it as a data file and finds
# them as TEXTFILE (256) resources named Styles.xbf and ThemeResources.xbf
# (DXamlCore::GetResourceBytes, StyleCache in dxaml/lib/DefaultStyles.cpp).
#
# Microsoft makes the two from themes/generic.xaml: SplitGenericXaml.cs
# (dxaml/xcp/dxaml/themes/autogen, MIT) splits it into Styles.xaml (the
# implicit styles, by TargetType) and ThemeResources.xaml (the theme
# dictionaries, the other resources and the keyed styles), and the XAML
# compiler turns each into XBF.  split() below is SplitGenericXaml.cs in
# Python.  The XAML compiler runs on Windows only, so the resources hold
# the split XAML as text (UTF-8), which NovaOS's GetResourceBytes
# (userland/windows_ui_xaml/src/resource_bytes.cpp) hands the framework's
# parser as text; a resource that is XBF is handed over as XBF, as on
# Windows.
#
# XBF defers a dictionary's resources: a resource's types resolve when it
# is first looked up.  XAML text resolves every type as it parses, and
# generic.xaml names a few types the framework's own type table does not
# have: those of its phone extension library, Microsoft.UI.Xaml.Phone.dll
# (dxaml/phone: Pivot, the looping selector, the pickers' presenters), and
# of WinUI's controls library (controls/dev: ProgressBar,
# TextCommandBarFlyout).  Until those libraries are built (OUTSIDE), the
# split leaves out the resources and styles that use them, and those that
# name such a resource ({StaticResource KEY}: the text boxes' styles name
# the text command bar flyouts).
import os, re, xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GENERIC = os.path.join(ROOT, 'third_party', 'microsoft-ui-xaml', 'dxaml', 'xcp', 'dxaml', 'themes', 'generic.xaml')

P = 'http://schemas.microsoft.com/winfx/2006/xaml/presentation'
X = 'http://schemas.microsoft.com/winfx/2006/xaml'
KEY = '{%s}Key' % X
STYLE, RD, THEMES = '{%s}Style' % P, '{%s}ResourceDictionary' % P, '{%s}ResourceDictionary.ThemeDictionaries' % P
TEXTFILE = 256
OUTSIDE = {'Pivot', 'PivotItem', 'PivotHeaderItem', 'PivotHeaderPanel', 'PivotPanel', 'LoopingSelector',
           'LoopingSelectorItem', 'DatePickerFlyoutPresenter', 'TimePickerFlyoutPresenter', 'ListPickerFlyoutPresenter',
           'PickerFlyoutPresenter', 'ProgressBar', 'TextCommandBarFlyout'}
# what the framework asks for, and the part of generic.xaml it is
RESOURCES = (('Styles.xbf', 'Styles.xaml'), ('ThemeResources.xbf', 'ThemeResources.xaml'))


def _static_refs(el):
    """the keys @el's attributes name in {StaticResource KEY} (as
    SplitGenericXaml.cs's GetDirectStaticResourceReferencesInStyle)"""
    return [m.group(1) for e in el.iter() for v in e.attrib.values() if 'StaticResource' in v
            for m in [re.search(r'{StaticResource\s+(\w+)}', v)] if m]


def _leave_out_outside(root):
    """take out of @root (generic.xaml's dictionary) the resources and
    styles that use a type in OUTSIDE, or a resource taken out"""
    gone = set()

    def outside(el):
        for e in el.iter():
            if e.tag.split('}')[-1].split('.')[0] in OUTSIDE or e.get('TargetType', '').split(':')[-1] in OUTSIDE:
                return True
            if any(k in gone for v in e.attrib.values()
                   for k in re.findall(r'{(?:StaticResource|ThemeResource)\s+(\w+)}', v)):
                return True
        return False
    while True:
        out = [el for el in root if el.tag != THEMES and outside(el)]
        if not out:
            return
        for el in out:
            gone.add(el.get(KEY) or el.get('TargetType'))
            root.remove(el)


def split(path):
    """generic.xaml at @path as (Styles.xaml, ThemeResources.xaml), each a
    ResourceDictionary's markup (bytes)"""
    ET.register_namespace('', P)
    ET.register_namespace('x', X)
    root = ET.parse(path).getroot()
    _leave_out_outside(root)
    styles = ET.Element(root.tag)
    # the implicit styles: a TargetType and no x:Key
    for el in root:
        if el.tag == STYLE and 'TargetType' in el.attrib and KEY not in el.attrib:
            styles.append(el)
    themes = ET.Element(root.tag)
    # the theme dictionaries, each with its keyed resources
    dicts = ET.SubElement(themes, THEMES)
    for theme in (el for el in root.find(THEMES) if el.tag == RD):
        rd = ET.SubElement(dicts, RD, {KEY: theme.get(KEY)})
        rd.extend(el for el in theme if KEY in el.attrib)
    # the other resources the styles use
    themes.extend(el for el in root if el.tag not in (STYLE, RD, THEMES))
    # and the keyed styles, each after the keyed styles it names as static
    # resources (SplitGenericXaml.cs: OrderResourcesForSerialization)
    keyed = [el for el in root if el.tag == STYLE and KEY in el.attrib]
    by_key = {el.get(KEY): el for el in keyed}
    done = set()

    def write(el, seen=()):
        if id(el) in done or id(el) in seen:
            return
        for k in _static_refs(el):
            if k in by_key:
                write(by_key[k], seen + (id(el),))
        done.add(id(el))
        themes.append(el)
    for el in keyed:
        write(el)
    return tuple(ET.tostring(r, encoding='utf-8', xml_declaration=False) for r in (styles, themes))


def objs(b, odir):
    """the resources, compiled (llvm-rc) from the split generic.xaml"""
    out = os.path.join(odir, 'xaml_resources')
    os.makedirs(out, exist_ok=True)
    res = os.path.join(odir, 'microsoft_ui_xaml_resources_19h1.res')
    if not os.path.exists(res) or any(os.path.getmtime(i) > os.path.getmtime(res) for i in (GENERIC, __file__)):
        for (name, file), text in zip(RESOURCES, split(GENERIC)):
            with open(os.path.join(out, file), 'wb') as f:
                f.write(text)
        rc = os.path.join(out, 'resources.rc')
        with open(rc, 'w') as f:
            for name, file in RESOURCES:
                f.write(f'{name} {TEXTFILE} "{os.path.join(out, file)}"\n')
        b.run([b.build_netsurf.llvm_rc(), '/FO', res, rc])
    return [res]
