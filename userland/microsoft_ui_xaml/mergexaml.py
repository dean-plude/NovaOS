# WinUI 2's XAML resources as Microsoft's build makes them: each feature's
# XAML pages (a control's default style, its theme resources) merged into
# one dictionary per Windows release, the files XamlControlsResources and
# each control's DefaultStyleResourceUri name (ms-appx:///Microsoft.UI.Xaml/
# Themes/21h1_themeresources.xaml, 21h1_generic.xaml ...).
#
# A port of Microsoft's MSBuild tasks (tools/CustomTasks in microsoft-ui-xaml,
# MIT): BatchMergeXaml.cs (the pages of each release merged over the
# previous release's result), MergedDictionary.cs (one entry per key, the
# later page's winning; the theme dictionaries merged by theme, Dark folded
# into Default; namespace prefixes standardized) and StripNamespaces.cs
# (conditional XAML resolved for the release's UniversalApiContract
# version), with the page lists and their order as
# dev/dll/Microsoft.UI.Xaml.Common.targets' CategorizeSharedPages makes
# them.  The XAML compiler that turns the merged files into XBF runs on
# Windows only, so the files stay XAML text, which NovaOS's XAML parser
# reads (as for the framework's own resources, userland/
# microsoft_ui_xaml_resources_19h1).
import os, re
from xml.dom import minidom

P = 'http://schemas.microsoft.com/winfx/2006/xaml/presentation'
X = 'http://schemas.microsoft.com/winfx/2006/xaml'
XMLNS = 'http://www.w3.org/2000/xmlns/'

RELEASES = ['RS1', 'RS2', 'RS3', 'RS4', 'RS5', '19H1', '21H1']
# StripNamespaces.cs: the UniversalApiContract version of each release
API = {'RS1': 3, 'RS2': 4, 'RS3': 5, 'RS4': 6, 'RS5': 7, '19H1': 8, '21H1': 14}

STANDARD = {'using:Microsoft.UI.Xaml.Controls': 'controls',
            'using:Microsoft.UI.Xaml.Controls.Primitives': 'primitives',
            'using:Microsoft.UI.Xaml.Media': 'media',
            X: 'x'}
# (as MergedDictionary.cs writes it: the unescaped '?' makes the 'n' before
# it optional, so the pattern never matches a conditional namespace, whose
# prefix is kept as the page spells it, contract7Present)
CONDITIONAL = re.compile(r'http://schemas.microsoft.com/winfx/2006/xaml/presentation?IsApiContract(Not|)Present'
                         r'\(Windows.Foundation.UniversalApiContract,([0-9]+)\)')


def _standard_name(name, value):
    m = CONDITIONAL.search(value)
    if m:
        return 'contract' + m.group(2) + m.group(1) + 'Present'
    return STANDARD.get(value, name)


def _drop_blank_text(node):
    """what XmlDocument (PreserveWhitespace false) leaves of a page: no
    whitespace-only text"""
    for c in list(node.childNodes):
        if c.nodeType == c.TEXT_NODE and not c.data.strip():
            node.removeChild(c)
        elif c.nodeType == c.ELEMENT_NODE:
            _drop_blank_text(c)


def _parse(text):
    doc = minidom.parseString(text.encode('utf-8'))
    _drop_blank_text(doc)
    return doc


def _key(node):
    """MergedDictionary.GetKey: x:Key, x:Name or a style's TargetType,
    prefixed by a conditional namespace's condition"""
    if node.nodeType != node.ELEMENT_NODE:
        return ''
    key = ''
    for i in range(node.attributes.length):
        a = node.attributes.item(i)
        if a.name in ('x:Key', 'x:Name'):
            key = a.value
            break
    if not key and node.nodeName == 'Style':
        key = node.getAttribute('TargetType')
    if key:
        i = (node.namespaceURI or '').find('IsApiContract')
        if i >= 0:
            key = node.namespaceURI[i:] + ':' + key
    return key


def _replace_prefixes(node, repl):
    if node.nodeType != node.ELEMENT_NODE:
        return
    if ':' in node.tagName:
        prefix, local = node.tagName.split(':', 1)
        if prefix in repl:
            node.tagName = node.nodeName = repl[prefix] + ':' + local
            node.prefix = repl[prefix]
    for i in range(node.attributes.length):
        a = node.attributes.item(i)
        v = a.value
        for old, new in repl.items():
            v = v.replace(old + ':', new + ':')
        if v != a.value:
            a.value = v
    for c in node.childNodes:
        _replace_prefixes(c, repl)


class MergedDictionary:
    """MergedDictionary.cs"""

    def __init__(self, doc=None, parent=None):
        self.doc = doc or minidom.Document()
        self.el = self.doc.createElementNS(P, 'ResourceDictionary')
        if parent is None:
            self.doc.appendChild(self.el)
            self.el.setAttribute('xmlns', P)
            self.el.setAttribute('xmlns:x', X)
        self.parent = parent
        self.nodes, self.ignore, self.index, self.themes, self.namespaces = [], set(), {}, {}, []

    def _add_namespace(self, prefix, uri):
        if uri not in self.namespaces:
            self.el.setAttribute('xmlns:' + prefix, uri)
            self.namespaces.append(uri)

    def merge(self, text):
        doc = _parse(text)
        root = doc.documentElement
        if root.nodeName != 'ResourceDictionary':
            return
        repl = {}
        for i in range(root.attributes.length):
            a = root.attributes.item(i)
            if a.name.startswith('xmlns:'):
                name = a.name[6:]
                std = _standard_name(name, a.value)
                self._add_namespace(std, a.value)
                if name != std:
                    repl[name] = std
        for c in list(root.childNodes):
            self._add(c, repl)

    def _add(self, node, repl):
        if node.nodeType == node.COMMENT_NODE:
            return
        node = self.doc.importNode(node, True)
        _replace_prefixes(node, repl)
        if node.nodeName == 'ResourceDictionary.ThemeDictionaries':
            for child in node.childNodes:
                key = _key(child)
                if not key:
                    continue
                if key == 'Dark':
                    key = 'Default'
                theme = self.themes.get(key)
                if theme is None:
                    theme = self.themes[key] = MergedDictionary(self.doc, self)
                for c in child.childNodes:
                    theme._add(c, repl)
            return
        key = _key(node)
        if not key or key not in self.index:
            if key:
                self.index[key] = len(self.nodes)
            self.nodes.append(node)
        else:
            self.nodes[self.index[key]] = node
        if key and self.parent is not None:
            self.parent._remove_ancestors(key)

    def _remove_ancestors(self, key):
        if key in self.index:
            self.ignore.add(self.index[key])
        if self.parent is not None:
            self.parent._remove_ancestors(key)

    def _xaml(self):
        if self.themes:
            td = self.doc.createElementNS(P, 'ResourceDictionary.ThemeDictionaries')
            self.el.appendChild(td)
            for key, theme in self.themes.items():
                e = theme._xaml()
                td.appendChild(e)
                e.setAttribute('x:Key', key)
        for i, n in enumerate(self.nodes):
            if i not in self.ignore and n is not None:
                self.el.appendChild(n)
        return self.el

    def text(self):
        root = self._xaml()
        _declare_used_prefixes(root)
        return root.toxml()


def _declare_used_prefixes(root):
    """declare on the root a prefix a merged node uses that its own page
    declared under a namespace another page had first (XmlWriter declares
    such a prefix where it is used)"""
    declared = {a.name[6:]: a.value for a in root.attributes.values() if a.name.startswith('xmlns:')}

    def visit(n):
        if n.nodeType != n.ELEMENT_NODE:
            return
        local = {a.name[6:] for a in n.attributes.values() if a.name.startswith('xmlns:')}
        names = [(n.prefix, n.namespaceURI)] + [(a.prefix, a.namespaceURI) for a in n.attributes.values()]
        for prefix, uri in names:
            if prefix and prefix not in ('xmlns', 'xml') and prefix not in local and uri:
                if prefix not in declared:
                    root.setAttribute('xmlns:' + prefix, uri)
                    declared[prefix] = uri
                elif declared[prefix] != uri:
                    raise SystemExit(f'WinUI 2 XAML: prefix {prefix} names both {declared[prefix]} and {uri}')
        for c in n.childNodes:
            visit(c)
    for c in root.childNodes:
        visit(c)


def strip_namespaces(text, api):
    """StripNamespaces.StripNamespaceForAPIVersion: the elements and
    attributes of a condition that does not hold at UniversalApiContract
    @api removed, those of one that holds kept with their condition's prefix
    taken off"""
    gone = {P + '?IsApiContractNotPresent(Windows.Foundation.UniversalApiContract,%d)' % i
            for i in range(min(API.values()), api + 1)}
    gone |= {P + '?IsApiContractPresent(Windows.Foundation.UniversalApiContract,%d)' % i
             for i in range(api + 1, max(API.values()) + 1)}
    doc = minidom.parseString(text.encode('utf-8'))

    def visit(n):
        for c in list(n.childNodes):
            if c.nodeType != c.ELEMENT_NODE:
                continue
            if c.namespaceURI in gone:
                n.removeChild(c)
                continue
            for a in list(c.attributes.values()):
                if a.namespaceURI in gone:
                    c.removeAttributeNode(a)
            visit(c)
    root = doc.documentElement
    for a in list(root.attributes.values()):
        if a.namespaceURI in gone:
            root.removeAttributeNode(a)
    visit(root)
    out = root.toxml()
    out = re.sub(r'xmlns:contract[0-9]+(Not|)Present="[^"]+"', '', out)
    return re.sub(r'contract[0-9]+(Not|)Present:', '', out)


def merge(pages):
    """BatchMergeXaml: @pages maps each release to its pages' paths, in
    order; returns {release: (merged text, text for the release)}, each
    release's merge starting from the previous one's"""
    out, base = {}, None
    for rel in RELEASES:
        md = MergedDictionary()
        if base is not None:
            md.merge(base)
        for p in pages.get(rel, []):
            with open(p, encoding='utf-8-sig') as f:
                md.merge(f.read())
        base = md.text()
        out[rel] = (base, strip_namespaces(base, API[rel]))
    return out
