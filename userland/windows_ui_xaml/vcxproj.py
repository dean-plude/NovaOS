"""Reads what NovaOS's build needs from one of Microsoft's XAML MSBuild
projects (third_party/microsoft-ui-xaml/dxaml/.../*.vcxproj): the sources
it compiles, the include directories and preprocessor definitions it adds
to the shared ones.  Paths are resolved against the files on disk without
regard to case, as Windows does."""
import os, re

_CASE = {}


def find_case(path):
    """@path as it is spelled on disk (each component matched without
    regard to case), or None"""
    path = os.path.normpath(path)
    if os.path.exists(path):
        return path
    head, tail = os.path.split(path)
    if not tail:
        return path
    d = find_case(head) if head and head != path else head
    if d is None or not os.path.isdir(d):
        return None
    names = _CASE.get(d)
    if names is None:
        names = _CASE[d] = {n.lower(): n for n in os.listdir(d)}
    n = names.get(tail.lower())
    return os.path.join(d, n) if n else None


def expand(value, props):
    """$(Name) from @props; None when a name is unknown"""
    out = value
    for name in re.findall(r'\$\((\w+)\)', value):
        if name not in props:
            return None
        out = out.replace('$(%s)' % name, props[name])
    return out


def read(path, props):
    """{'sources': [...], 'includes': [...], 'defines': [...]} of @path"""
    text = open(path, encoding='utf-8-sig').read()
    here = os.path.dirname(path)
    props = dict(props, MSBuildThisFileDirectory=here + os.sep)
    res = {'sources': [], 'includes': [], 'defines': []}
    for m in re.finditer(r'<ClCompile\s+([^>]*?)/?>', text):
        attrs = dict(re.findall(r'(\w+)="([^"]*)"', m.group(1)))
        if 'Condition' in attrs or 'Include' not in attrs:
            continue
        for inc in attrs['Include'].split(';'):
            p = expand(inc.strip(), props)
            if not p:
                continue
            p = p.replace('\\', '/')
            src = find_case(os.path.join(here, p))
            if src and src.endswith(('.cpp', '.c')):
                res['sources'].append(src)
    for block in re.findall(r'<ProjectIncludeDirectories>(.*?)</ProjectIncludeDirectories>', text, re.S):
        for d in block.split(';'):
            d = d.strip()
            if not d or d.startswith('$(ProjectIncludeDirectories)'):
                continue
            p = expand(d, props)
            if p:
                p = find_case(os.path.join(here, p.replace('\\', '/')))
                if p and os.path.isdir(p):
                    res['includes'].append(p)
    for block in re.findall(r'<ProjectPreprocessorDefinitions>(.*?)</ProjectPreprocessorDefinitions>', text, re.S):
        for d in block.split(';'):
            d = d.strip()
            if d and not d.startswith('$('):
                res['defines'].append(d)
    return res
