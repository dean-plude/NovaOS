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
    # (a list that extends the inherited one names it first, "$(...) A; B")
    for block in re.findall(r'<ProjectPreprocessorDefinitions>(.*?)</ProjectPreprocessorDefinitions>', text, re.S):
        for d in re.split(r'[;\s]+', block):
            if d and not d.startswith('$('):
                res['defines'].append(d)
    return res


def include_path(path, props):
    """the include directories a props file (and the props files it
    imports, before it as MSBuild reads them) sets ProjectIncludeDirectories
    to, in order: what the core's sources.props files give the core's
    projects (core/common.props, common.props, components/includes.props'
    CompIncludes).  Entries naming a property NovaOS does not have (the
    lifted IXP's, the build output's) are left out."""
    state = {'ProjectIncludeDirectories': '', 'CompIncludes': ''}

    def run(p):
        text = open(p, encoding='utf-8-sig').read()
        here = os.path.dirname(p)
        for m in re.finditer(r'<Import\s+Project="([^"]+)"|<(ProjectIncludeDirectories|CompIncludes)>(.*?)</\2>',
                             text, re.S):
            if m.group(1):
                f = expand(m.group(1), dict(props, MSBuildThisFileDirectory=here + os.sep))
                f = f and find_case(os.path.join(here, f.replace('\\', '/')))
                if f and os.path.isfile(f) and f.endswith('.props'):
                    run(f)
                continue
            out = []
            for d in m.group(3).split(';'):
                d = d.strip()
                if not d:
                    continue
                name = re.fullmatch(r'\$\((\w+)\)', d)
                if name and name.group(1) in state:
                    out += [x for x in state[name.group(1)].split(';') if x]
                    continue
                v = expand(d, props)
                v = v and find_case(os.path.join(here, v.replace('\\', '/')))
                if v and os.path.isdir(v):
                    out.append(v)
            state[m.group(2)] = ';'.join(out)

    run(path)
    seen = []
    for d in state['ProjectIncludeDirectories'].split(';'):
        if d and d not in seen:
            seen.append(d)
    return seen
