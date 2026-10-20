"""Reader for the MIDL 3 IDL that Microsoft's XAML sources declare their
API in (microsoft-ui-xaml dxaml/xcp/dxaml/idl), into the model in model.py.

Handles what those files use: namespaces, API contracts, enums, structs,
delegates, interfaces (with `requires`), runtime classes (base class,
constructors, instance/static/overridable/protected members, member
blocks with their own interface names), `declare` blocks of generic
instances, and classic COM interfaces ([uuid] interface IFoo : IUnknown
{ HRESULT ... }) whose parameters are kept as C++ text.  Interfaces MIDL
would synthesize get their names and member order the way MIDL 3 makes
them; their IIDs come from the caller (see abigen.py).
"""
import re

from model import (Enum, Struct, Interface, Delegate, RuntimeClass, Contract,
                   Method, Param, TypeRef, GenericInst, Prim, Array, Attribute)

PRIMS = {
    'Boolean': 'Boolean', 'boolean': 'Boolean', 'Char': 'Char16', 'Char16': 'Char16',
    'WCHAR': 'Char16', 'Int8': 'Int8', 'UInt8': 'UInt8', 'BYTE': 'UInt8',
    'Int16': 'Int16', 'INT16': 'Int16', 'UInt16': 'UInt16', 'UINT16': 'UInt16',
    'Int32': 'Int32', 'INT32': 'Int32', 'INT': 'Int32', 'UInt32': 'UInt32',
    'UINT32': 'UInt32', 'Int64': 'Int64', 'INT64': 'Int64', 'UInt64': 'UInt64',
    'UINT64': 'UInt64', 'Single': 'Single', 'FLOAT': 'Single', 'Double': 'Double',
    'DOUBLE': 'Double', 'String': 'String', 'HSTRING': 'String', 'Object': 'Object',
    'IInspectable': 'Object', 'Guid': 'Guid', 'GUID': 'Guid',
}
TYPEDEFS = {'HRESULT': 'Windows.Foundation.HResult'}

TOKEN = re.compile(r'''
    (?P<ws>\s+|//[^\n]*|/\*.*?\*/)
  | (?P<pp>^[ \t]*\#[^\n]*)
  | (?P<str>"(?:[^"\\]|\\.)*")
  | (?P<num>0[xX][0-9a-fA-F]+|\d+)
  | (?P<id>[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)
  | (?P<p>[{}()\[\]<>,;:=|*&~+-])
''', re.S | re.M | re.X)


def tokenize(text):
    out = []
    pos = 0
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if not m:
            raise SyntaxError('IDL: bad character %r at %d' % (text[pos], pos))
        pos = m.end()
        k = m.lastgroup
        if k in ('ws', 'pp'):
            continue
        out.append((k, m.group(k)))
    return out


class IdlReader:
    def __init__(self, disabled_features=(), keep_disabled_types=False):
        self.types = {}
        # a whole type of a disabled feature is kept when asked (headers
        # for building XAML itself, which uses such types unconditionally);
        # members of a disabled feature are always left out
        self.keep_disabled_types = keep_disabled_types
        self.declared = []          # generic instances from declare blocks
        self.classic_out = {}       # file -> [('quote', text) | ('iface', name)]: global COM IDL
        self.imports = {}           # file -> [header names its imports give]
        self.file_ns = {}           # file -> {namespaces whose Windows Runtime types it declares}
        self.disabled = set(disabled_features)

    # -- token stream ---------------------------------------------------------
    def load(self, path):
        self.toks = tokenize(open(path, encoding='utf-8-sig').read())
        self.i = 0
        self.ns = None
        self.file = path
        while self.i < len(self.toks):
            self.top()

    def peek(self, k=0):
        j = self.i + k
        return self.toks[j][1] if j < len(self.toks) else None

    def next(self):
        t = self.toks[self.i][1]
        self.i += 1
        return t

    def expect(self, v):
        t = self.next()
        if t != v:
            ctx = ' '.join(x[1] for x in self.toks[max(0, self.i - 12):self.i + 3])
            raise SyntaxError('%s: expected %r, got %r near: %s' % (self.file, v, t, ctx))

    def accept(self, v):
        if self.peek() == v:
            self.i += 1
            return True
        return False

    # -- attributes -----------------------------------------------------------
    def attributes(self):
        attrs = []
        while self.peek() == '[':
            self.next()
            while True:
                name = self.next()
                args = []
                if self.accept('('):
                    depth = 1
                    cur = []
                    while depth:
                        t = self.next()
                        if t == '(':
                            depth += 1
                        elif t == ')':
                            depth -= 1
                            if not depth:
                                break
                        if t == ',' and depth == 1:
                            args.append(' '.join(cur))
                            cur = []
                        else:
                            cur.append(t)
                    if cur:
                        args.append(' '.join(cur))
                attrs.append(Attribute(name, [a[1:-1] if a.startswith('"') else a for a in args]))
                if not self.accept(','):
                    break
            self.expect(']')
        return attrs

    def disabled_by(self, attrs):
        return any(a.name == 'feature' and a.args and a.args[0] in self.disabled for a in attrs)

    # -- types ----------------------------------------------------------------
    def type(self):
        name = self.next()
        if name == 'const':
            name = self.next()
        if self.peek() == '<':
            self.next()
            args = [self.type()]
            while self.accept(','):
                args.append(self.type())
            self.expect('>')
            t = GenericInst('%s`%d' % (name, len(args)), args)
            t.ctx_ns = self.ns
        elif name in PRIMS:
            t = Prim(PRIMS[name])
        elif name in TYPEDEFS:
            t = TypeRef(TYPEDEFS[name])
        else:
            t = TypeRef(name)
            t.ctx_ns = self.ns
        while self.accept('*'):
            pass
        if self.peek() == '[' and self.peek(1) == ']':
            self.i += 2
            t = Array(t)
        return t

    def qualify(self, name):
        return name if '.' in name or self.ns is None else self.ns + '.' + name

    # -- top level ------------------------------------------------------------
    def top(self):
        t = self.peek()
        if t == 'import':
            self.next()
            f = self.next()[1:-1]
            self.expect(';')
            if f.endswith('.idl'):
                self.imports.setdefault(self.file, []).append(f[:-4].lower() + '.h')
        elif t == 'cpp_quote':
            self.next()
            self.expect('(')
            s = self.next()
            self.expect(')')
            if self.ns is None:
                self.classic_out.setdefault(self.file, []).append(('quote', s[1:-1].encode().decode('unicode_escape')))
        elif t == 'namespace':
            self.next()
            self.ns = self.next()
            self.expect('{')
            while not self.accept('}'):
                self.decl()
            self.ns = None
        elif t == ';':
            self.next()
        else:
            self.decl()

    def decl(self):
        attrs = self.attributes()
        t = self.peek()
        if t == ';':
            self.next()
            return
        if t == 'cpp_quote':
            self.top()
            return
        if t == 'typedef':
            toks = []
            depth = 0
            while True:
                if self.peek() == '[':
                    self.attributes()
                    continue
                x = self.next()
                depth += {'{': 1, '}': -1}.get(x, 0)
                toks.append(x)
                if x == ';' and depth == 0:
                    break
            if self.ns is None and not (len(toks) == 5 and toks[1:3] == ['RECT', '*']):
                self.classic_out.setdefault(self.file, []).append(('quote', _join(toks)))
            return
        if t == 'declare':
            self.next()
            self.expect('{')
            while not self.accept('}'):
                self.expect('interface')
                self.declared.append((self.ns, self.type()))
                self.expect(';')
            self.accept(';')
            return
        mods = set()
        while self.peek() in ('unsealed', 'sealed', 'static', 'partial'):
            mods.add(self.next())
        kind = self.next()
        skip = self.disabled_by(attrs) and not self.keep_disabled_types
        if kind == 'apicontract':
            name = self.qualify(self.next())
            self.expect('{')
            self.expect('}')
            self.accept(';')
            ty = Contract(name)
        elif kind == 'enum':
            ty = self.enum(attrs)
        elif kind == 'struct':
            ty = self.struct()
        elif kind == 'delegate':
            ty = self.delegate()
        elif kind == 'interface':
            ty = self.interface(attrs)
        elif kind == 'runtimeclass':
            ty = self.runtimeclass(attrs, mods)
        elif kind == 'attribute':
            self.next()
            self.skip_block()
            return
        else:
            raise SyntaxError('%s: unknown declaration %r' % (self.file, kind))
        if ty is None or skip:
            return
        for x in (ty if isinstance(ty, list) else [ty]):
            if x.namespace and not getattr(x, 'classic', False):
                self.file_ns.setdefault(self.file, set()).add(x.namespace)
        if isinstance(ty, list):
            for x in ty:
                self.types[x.name] = x
            return
        ty.attrs = attrs + ty.attrs
        self.types[ty.name] = ty

    def skip_block(self):
        self.expect('{')
        depth = 1
        while depth:
            t = self.next()
            depth += {'{': 1, '}': -1}.get(t, 0)
        self.accept(';')

    def enum(self, attrs):
        e = Enum(self.qualify(self.next()))
        if any(a.name == 'flags' for a in attrs):
            e.underlying = 'UInt32'
        self.expect('{')
        val = -1
        names = {}
        while not self.accept('}'):
            vattrs = self.attributes()
            n = self.next()
            if self.accept('='):
                expr = []
                while self.peek() not in (',', '}'):
                    expr.append(self.next())
                val = _enum_value(expr, names)
            else:
                val += 1
            names[n] = str(val)
            if not self.disabled_by(vattrs):
                e.values.append((n, val))
            self.accept(',')
        self.accept(';')
        return e

    def struct(self):
        from model import Field
        s = Struct(self.qualify(self.next()))
        self.expect('{')
        while not self.accept('}'):
            self.attributes()
            t = self.type()
            s.fields.append(Field(self.next(), t))
            self.expect(';')
        self.accept(';')
        return s

    def params(self):
        self.expect('(')
        ps = []
        while not self.accept(')'):
            self.attributes()
            out = False
            ref = False
            if self.peek() == 'out':
                self.next()
                out = True
            elif self.peek() == 'ref':
                self.next()
                ref = True
                self.accept('const')
            t = self.type()
            if ref and not isinstance(t, Array):
                t = t.copy()
                t.const = True
                t.byref = True
            elif ref:
                out = True                         # fill array
            if out and isinstance(t, Array):
                t = t.copy()
                t.byref = True
            name = self.next()
            ps.append(Param(name, t, out))
            self.accept(',')
        return ps

    def delegate(self):
        ret = self.type()
        name = self.qualify(self.next())
        gens = []
        if self.accept('<'):
            gens = [self.next()]
            while self.accept(','):
                gens.append(self.next())
            self.expect('>')
            name += '`%d' % len(gens)
        d = Delegate(name, generics=gens)
        ps = self.params()
        self.expect(';')
        d.methods.append(Method('Invoke', ps, None if _void(ret) else ret))
        return d

    def interface(self, attrs):
        name = self.qualify(self.next())
        if self.accept(';'):                       # forward declaration
            if self.ns is None:
                self.classic_out.setdefault(self.file, []).append(('quote', 'struct %s;' % name))
            return None
        it = Interface(name)
        uuid = next((a.args[0] for a in attrs if a.name == 'uuid'), None)
        if uuid:
            it.guid = uuid.replace(' ', '').lower()
        if self.accept(':'):                       # classic COM interface
            it.base = self.next()
            it.classic = True
            self.expect('{')
            while not self.accept('}'):
                mattrs = self.attributes()
                rtype = self.next()
                mname = self.next()
                if any(a.name == 'propget' for a in mattrs):
                    mname = 'get_' + mname
                elif any(a.name == 'propput' for a in mattrs):
                    mname = 'put_' + mname
                self.expect('(')
                parts = []
                cur = []
                depth = 1
                while True:
                    if self.peek() == '[':
                        self.attributes()
                        continue
                    t = self.next()
                    if t == '(':
                        depth += 1
                    elif t == ')':
                        depth -= 1
                        if not depth:
                            break
                    if t == ',' and depth == 1:
                        parts.append(cur)
                        cur = []
                    else:
                        cur.append(t)
                if cur:
                    parts.append(cur)
                m = Method(mname, [], None)
                m.raw_ret = rtype
                m.raw = [_join(p) for p in parts if p != ['void']]
                it.methods.append(m)
                self.expect(';')
            self.accept(';')
            if self.ns is None:
                self.classic_out.setdefault(self.file, []).append(('iface', name))
            return it
        if self.accept('requires'):
            while True:
                it.interfaces.append((self.type(), []))
                if not self.accept(','):
                    break
        self.expect('{')
        synth = _Synth(self)
        while not self.accept('}'):
            mattrs = self.attributes()
            synth.member(it, mattrs, 'instance')
        self.accept(';')
        return it

    def runtimeclass(self, attrs, mods):
        name = self.qualify(self.next())
        rc = RuntimeClass(name)
        rc.sealed = 'unsealed' not in mods
        rc.static = 'static' in mods
        if self.accept(':'):
            while True:
                battrs = self.attributes()
                t = self.type()
                if self.disabled_by(battrs):
                    if not self.accept(','):
                        break
                    continue
                if any(a.name == 'default' for a in battrs):
                    rc.explicit_default = t
                rc.pending_bases = getattr(rc, 'pending_bases', []) + [t]
                if not self.accept(','):
                    break
        self.expect('{')
        names = {
            'instance': self.qualify(_attr(attrs, 'interface_name') or _iname(name, '')),
            'static': self.qualify(_attr(attrs, 'static_name') or _iname(name, 'Statics')),
            'overridable': self.qualify(_attr(attrs, 'overridable_name') or _iname(name, 'Overrides')),
            'protected': self.qualify(_attr(attrs, 'protected_name') or _iname(name, 'Protected')),
            'factory': self.qualify(_attr(attrs, 'constructor_name') or _iname(name, 'Factory')),
        }
        out = [rc]
        groups = {k: Interface(v) for k, v in names.items()}
        order = []                     # (attrs, {kind: Interface}) for member blocks
        hidden = []                    # the same, of a disabled feature
        counters = {}
        synth = _Synth(self)
        while not self.accept('}'):
            mattrs = self.attributes()
            if self.peek() == '{':
                self.next()
                bnames = {
                    'instance': _attr(mattrs, 'interface_name'),
                    'static': _attr(mattrs, 'static_name'),
                    'overridable': _attr(mattrs, 'overridable_name'),
                    'protected': _attr(mattrs, 'protected_name'),
                    'factory': _attr(mattrs, 'constructor_name'),
                }
                bgroups = {}
                while not self.accept('}'):
                    a2 = self.attributes()
                    kind, mem = synth.class_member(rc, a2)
                    if kind not in bgroups:
                        n = bnames.get(kind)
                        if not n and kind == 'instance' and not groups['instance'].methods and getattr(rc, 'explicit_default', None) and 'instance' not in counters:
                            counters['instance'] = 1
                            n = _iname(name, '')
                        if not n:
                            counters[kind] = counters.get(kind, 1) + 1
                            n = _iname(name, {'instance': '', 'static': 'Statics', 'overridable': 'Overrides', 'protected': 'Protected', 'factory': 'Factory'}[kind] + str(counters[kind]))
                        bgroups[kind] = Interface(self.qualify(n))
                    bgroups[kind].methods.extend(mem)
                self.accept(';')
                if not self.disabled_by(mattrs):
                    order.append((mattrs, bgroups))
                elif self.keep_disabled_types:
                    # (a disabled feature's block of members, kept for
                    # XAML's own build as its interfaces alone: the class
                    # does not list them, as the public metadata does not)
                    # (MIDL makes the block's named instance interface
                    # even when it has no members, a constructor's block)
                    if bnames['instance'] and 'instance' not in bgroups:
                        bgroups['instance'] = Interface(self.qualify(bnames['instance']))
                    hidden.append((mattrs, bgroups))
                continue
            kind, mem = synth.class_member(rc, mattrs)
            if self.disabled_by(mattrs):
                continue
            groups[kind].methods.extend(mem)
        self.accept(';')
        rc.groups = groups
        rc.blocks = order
        rc.hidden_blocks = hidden
        rc.class_attrs = attrs
        rc.has_default_attr = any(a.name == 'default_interface' for a in attrs)
        return out


def _enum_value(toks, names):
    """An enum value: literals and earlier values, or-ed together."""
    val = 0
    for part in ' '.join(toks).split('|'):
        part = part.replace(' ', '')
        neg = part.startswith('-')
        part = part.lstrip('-')
        v = int(names[part]) if part in names else int(part, 0)
        val |= -v if neg else v
    return val


def _void(t):
    return isinstance(t, TypeRef) and t.name == 'void'


def _join(toks):
    s = ''
    for t in toks:
        if s and (s[-1].isalnum() or s[-1] == '_') and (t[0].isalnum() or t[0] == '_'):
            s += ' '
        s += t
    return s


def _attr(attrs, name):
    for a in attrs:
        if a.name == name and a.args:
            return a.args[0]
    return None


def _iname(cls, suffix):
    ns, short = cls.rsplit('.', 1)
    return ns + '.I' + short + suffix


class _Synth:
    """Turns MIDL 3 member declarations into ABI methods."""

    def __init__(self, reader):
        self.r = reader

    def member(self, it, attrs, kind):
        k, mems = self.class_member(None, attrs, iface=True)
        if not self.r.disabled_by(attrs):
            it.methods.extend(mems)

    def class_member(self, rc, attrs, iface=False):
        r = self.r
        kind = 'instance'
        while r.peek() in ('static', 'overridable', 'protected', 'event', 'partial'):
            w = r.next()
            if w == 'static':
                kind = 'static'
            elif w == 'overridable':
                kind = 'overridable'
            elif w == 'protected':
                kind = 'protected' if kind == 'instance' else kind
            elif w == 'event':
                return kind, self.event(attrs)
        mname = _attr(attrs, 'method_name')
        # constructor?
        if rc is not None and r.peek() == rc.short and r.peek(1) == '(':
            r.next()
            ps = r.params()
            r.expect(';')
            m = Method(mname or 'CreateInstance', ps, TypeRef(rc.name), 'value')
            m.ctor = True
            return 'factory', [m]
        t = r.type()
        name = r.next()
        if r.peek() == '(':
            ps = r.params()
            r.expect(';')
            ret = None if _void(t) else t
            m = Method(mname or name, ps, ret, _attr(attrs, 'return_name') or 'result')
            return kind, [m]
        # property
        get = put = True
        if r.accept('{'):
            get = put = False
            while not r.accept('}'):
                w = r.next()
                if w == 'get':
                    get = True
                elif w == 'set':
                    put = True
                r.accept(';')
            r.accept(';')
        else:
            r.expect(';')
        out = []
        if get:
            out.append(Method('get_' + name, [], t, 'value'))
        if put:
            out.append(Method('put_' + name, [Param('value', t, False)], None))
        return kind, out

    def event(self, attrs):
        r = self.r
        t = r.type()
        name = r.next()
        r.expect(';')
        add = Method('add_' + name, [Param('handler', t, False)], TypeRef('Windows.Foundation.EventRegistrationToken'), 'token')
        rem = Method('remove_' + name, [Param('token', TypeRef('Windows.Foundation.EventRegistrationToken'), False)], None)
        return [add, rem]


def finish_classes(types, lookup):
    """Turn each runtime class's member groups into the interfaces MIDL 3
    synthesizes, and settle base class versus implemented interfaces.
    `lookup(name)` finds a type among every loaded source."""
    out = {}
    for rc in [t for t in types.values() if isinstance(t, RuntimeClass)]:
        groups = getattr(rc, 'groups', None)
        if groups is None:
            continue
        for b in getattr(rc, 'pending_bases', []):
            bt = lookup(b.name) if isinstance(b, TypeRef) else None
            if isinstance(bt, RuntimeClass) and rc.base is None:
                rc.base = bt.name
            else:
                rc.interfaces.append((b, [Attribute('Windows.Foundation.Metadata.DefaultAttribute', [])]
                                      if b is getattr(rc, 'explicit_default', None) else []))
        made = []
        inst = groups['instance']
        explicit = getattr(rc, 'explicit_default', None) is not None
        if inst.methods or ((not rc.sealed or rc.has_default_attr) and not explicit):
            if not rc.static or inst.methods:
                made.append(('instance', inst))
        fac = groups['factory']
        ctors = fac.methods
        fac.methods = []
        for m in ctors:
            if not rc.sealed:
                m.params = m.params + [Param('baseInterface', Prim('Object'), False),
                                       Param('innerInterface', Prim('Object'), True)]
                fac.methods.append(m)
            elif m.params:
                fac.methods.append(m)
            else:
                rc.activatable_default = True
        # (an unsealed class with no constructors still has its factory,
        # empty, as MIDL makes it: the class is composable, UIElement's
        # IUIElementFactory)
        if fac.methods or (not rc.sealed and not rc.static):
            made.append(('factory', fac))
        for k in ('static', 'overridable', 'protected'):
            if groups[k].methods:
                made.append((k, groups[k]))
        for battrs, bg in rc.blocks:
            for k, it in bg.items():
                if k == 'factory':
                    ms = it.methods
                    it.methods = []
                    for m in ms:
                        if not rc.sealed:
                            m.params = m.params + [Param('baseInterface', Prim('Object'), False),
                                                   Param('innerInterface', Prim('Object'), True)]
                        it.methods.append(m)
                made.append((k, it))
        for k, it in made:
            it.exclusive_to = rc.name
            it.attrs = [a for a in rc.class_attrs if a.name == 'contract']
            out[it.name] = it
            if k in ('instance', 'overridable', 'protected'):
                attrs = [Attribute('Windows.Foundation.Metadata.DefaultAttribute', [])] if it is inst else []
                if k == 'overridable':
                    attrs.append(Attribute('Windows.Foundation.Metadata.OverridableAttribute', []))
                if k == 'protected':
                    attrs.append(Attribute('Windows.Foundation.Metadata.ProtectedAttribute', []))
                rc.interfaces.append((TypeRef(it.name), attrs))
            rc.synth = getattr(rc, 'synth', []) + [(k, it.name)]
        for battrs, bg in getattr(rc, 'hidden_blocks', []):
            for k, it in bg.items():
                if k == 'factory' and not rc.sealed:
                    for m in it.methods:
                        m.params = m.params + [Param('baseInterface', Prim('Object'), False),
                                               Param('innerInterface', Prim('Object'), True)]
                it.exclusive_to = rc.name
                it.attrs = [a for a in rc.class_attrs if a.name == 'contract']
                out[it.name] = it
    types.update(out)
