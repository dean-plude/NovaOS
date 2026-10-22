"""Writes Windows Runtime metadata (.winmd, ECMA-335) without MIDL.

    winmdwrite.write(path, assembly, types, names, iid_for)

Turns the types of the model in model.py that the IDL reader (idl.py) made
from MIDL 3 IDL into the .winmd MIDL would write for them, as far as the
tools that read it need: C++/WinRT's cppwinrt (third_party/cppwinrt),
which writes the C++ projection and a component's implementation
templates from it.  So each type gets its TypeDef with the flags MIDL
gives, its methods (an interface's in vtable order, property and event
accessors marked special) with their parameters, an enum's values, a
struct's fields, the interfaces a class or interface implements, and the
Windows.Foundation.Metadata attributes those tools read: GuidAttribute,
ExclusiveToAttribute, DefaultAttribute, OverridableAttribute,
ProtectedAttribute, ActivatableAttribute, StaticAttribute,
ComposableAttribute, OverloadAttribute and DefaultOverloadAttribute,
FlagsAttribute.  Every type it names, its own included, is named through
a TypeRef, as MIDL's output does.

@types maps full names to model types (those the file defines and those
they use, from Windows' metadata); @names are the ones to define; @iid_for
gives an interface's or delegate's IID (as abigen.py does, so the ABI
headers and the projection agree).  See docs/design/windows-ui-xaml.md.
"""
import struct, uuid

from model import (Enum, Struct, Interface, Delegate, RuntimeClass, Contract,
                   TypeRef, GenericInst, GenericVar, Prim, Array)

WFM = 'Windows.Foundation.Metadata'
PRIM_ELEM = {'Boolean': 0x02, 'Char16': 0x03, 'Int8': 0x04, 'UInt8': 0x05, 'Int16': 0x06,
             'UInt16': 0x07, 'Int32': 0x08, 'UInt32': 0x09, 'Int64': 0x0A, 'UInt64': 0x0B,
             'Single': 0x0C, 'Double': 0x0D, 'String': 0x0E, 'Object': 0x1C,
             'IntPtr': 0x18, 'UIntPtr': 0x19}

# ECMA-335 II.22 columns of the tables written: 'u1'/'u2'/'u4', 's' string,
# 'g' guid, 'b' blob, ('t', n) a row of table n, ('c', kind) coded index
COLS = {
    0x00: ['u2', 's', 'g', 'g', 'g'],                                  # Module
    0x01: [('c', 'ResolutionScope'), 's', 's'],                        # TypeRef
    0x02: ['u4', 's', 's', ('c', 'TypeDefOrRef'), ('t', 0x04), ('t', 0x06)],  # TypeDef
    0x04: ['u2', 's', 'b'],                                            # Field
    0x06: ['u4', 'u2', 'u2', 's', 'b', ('t', 0x08)],                   # MethodDef
    0x08: ['u2', 'u2', 's'],                                           # Param
    0x09: [('t', 0x02), ('c', 'TypeDefOrRef')],                        # InterfaceImpl
    0x0A: [('c', 'MemberRefParent'), 's', 'b'],                        # MemberRef
    0x0B: ['u1', 'u1', ('c', 'HasConstant'), 'b'],                     # Constant
    0x0C: [('c', 'HasCustomAttribute'), ('c', 'CustomAttributeType'), 'b'],  # CustomAttribute
    0x12: [('t', 0x02), ('t', 0x14)],                                  # EventMap
    0x14: ['u2', 's', ('c', 'TypeDefOrRef')],                          # Event
    0x15: [('t', 0x02), ('t', 0x17)],                                  # PropertyMap
    0x17: ['u2', 's', 'b'],                                            # Property
    0x18: ['u2', ('t', 0x06), ('c', 'HasSemantics')],                  # MethodSemantics
    0x1B: ['b'],                                                       # TypeSpec
    0x20: ['u4', 'u2', 'u2', 'u2', 'u2', 'u4', 'b', 's', 's'],         # Assembly
    0x23: ['u2', 'u2', 'u2', 'u2', 'u4', 'b', 's', 's', 'b'],          # AssemblyRef
    0x2A: ['u2', 'u2', ('c', 'TypeOrMethodDef'), 's'],                 # GenericParam
}
CODED = {
    'TypeDefOrRef': [0x02, 0x01, 0x1B],
    'HasConstant': [0x04, 0x08, 0x17],
    'HasCustomAttribute': [0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11,
                           0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B],
    'MemberRefParent': [0x02, 0x01, 0x1A, 0x06, 0x1B],
    'HasSemantics': [0x14, 0x17],
    'CustomAttributeType': [None, None, 0x06, 0x0A, None],
    'ResolutionScope': [0x00, 0x1A, 0x23, 0x01],
    'TypeOrMethodDef': [0x02, 0x06],
}
# tables the format wants sorted, by their key column
SORTED = {0x09: 0, 0x0B: 2, 0x0C: 0, 0x18: 2, 0x2A: 2}


def _tagbits(n):
    b = 0
    while (1 << b) < n:
        b += 1
    return b


def compressed(n):
    if n < 0x80:
        return bytes([n])
    if n < 0x4000:
        return bytes([0x80 | n >> 8, n & 0xFF])
    return bytes([0xC0 | n >> 24, n >> 16 & 0xFF, n >> 8 & 0xFF, n & 0xFF])


def ser_string(s):
    if s is None:
        return b'\xff'
    b = s.encode('utf-8')
    return compressed(len(b)) + b


class _Writer:
    def __init__(self, assembly, types, names, iid_for):
        self.assembly = assembly
        self.types = types
        self.names = list(names)
        self.own = set(names)
        self.iid_for = iid_for
        self.strings = {'': 0}
        self.sheap = bytearray(b'\0')
        self.blobs = {b'': 0}
        self.bheap = bytearray(b'\0')
        self.rows = {t: [] for t in COLS}
        self.typerefs = {}
        self.typespecs = {}
        self.memberrefs = {}
        self.asmrefs = {}

    # -- heaps ------------------------------------------------------------------
    def s(self, text):
        if text not in self.strings:
            self.strings[text] = len(self.sheap)
            self.sheap += text.encode('utf-8') + b'\0'
        return self.strings[text]

    def b(self, data):
        data = bytes(data)
        if data not in self.blobs:
            self.blobs[data] = len(self.bheap)
            self.bheap += compressed(len(data)) + data
        return self.blobs[data]

    def add(self, table, row):
        self.rows[table].append(row)
        return len(self.rows[table])

    # -- references ---------------------------------------------------------------
    def asmref(self, name):
        if name not in self.asmrefs:
            if name == 'mscorlib':
                row = [4, 0, 0, 0, 0, self.b(bytes.fromhex('b77a5c561934e089')), self.s(name), 0, 0]
            else:
                row = [255, 255, 255, 255, 0x200, 0, self.s(name), 0, 0]
            self.asmrefs[name] = self.add(0x23, row)
        return self.asmrefs[name]

    def typeref(self, name):
        """the TypeRef row naming full name @name"""
        if name not in self.typerefs:
            ns, short = name.rsplit('.', 1)
            if name in self.own:
                scope = ('ResolutionScope', 0x00, 1)
            else:
                scope = ('ResolutionScope', 0x23, self.asmref('mscorlib' if ns.startswith('System') else 'Windows'))
            self.typerefs[name] = self.add(0x01, [scope, self.s(short), self.s(ns)])
        return self.typerefs[name]

    def tdor(self, ty):
        """a TypeDefOrRef coded index for a named type or generic instance"""
        if isinstance(ty, GenericInst):
            sig = bytes(self.sig(ty))
            if sig not in self.typespecs:
                self.typespecs[sig] = self.add(0x1B, [self.b(sig)])
            return ('TypeDefOrRef', 0x1B, self.typespecs[sig])
        name = ty if isinstance(ty, str) else ty.name
        return ('TypeDefOrRef', 0x01, self.typeref(name))

    def encode(self, coded):
        kind, table, row = coded
        tabs = CODED[kind]
        return row << _tagbits(len(tabs)) | tabs.index(table)

    def sig(self, ty):
        """a type's signature bytes"""
        out = bytearray()
        if getattr(ty, 'const', False) and getattr(ty, 'byref', False):
            out += b'\x20' + compressed(self.encode(self.tdor('System.Runtime.CompilerServices.IsConst')))
        if getattr(ty, 'byref', False) and not getattr(ty, 'fill', False):
            out += b'\x10'
        if isinstance(ty, Prim):
            if ty.name == 'Guid':
                out += b'\x11' + compressed(self.encode(self.tdor('System.Guid')))
            else:
                out.append(PRIM_ELEM[ty.name])
        elif isinstance(ty, Array):
            out += b'\x1d' + self.sig(ty.elem)
        elif isinstance(ty, GenericVar):
            out += b'\x13' + compressed(ty.index)
        elif isinstance(ty, GenericInst):
            out += b'\x15\x12' + compressed(self.encode(self.tdor(ty.name))) + compressed(len(ty.args))
            for a in ty.args:
                out += self.sig(a)
        else:
            t = self.types.get(ty.name)
            value = isinstance(t, (Enum, Struct)) or ty.name in ('Windows.Foundation.HResult',
                                                               'Windows.Foundation.EventRegistrationToken')
            out += (b'\x11' if value else b'\x12') + compressed(self.encode(self.tdor(ty.name)))
        return out

    # -- attributes ------------------------------------------------------------------
    def attribute(self, parent, name, params=(), args=()):
        """a custom attribute @name (a type of Windows.Foundation.Metadata unless
        qualified) on @parent (a HasCustomAttribute coded index), its
        constructor taking @params ('UInt32', 'String', 'Type', 'UInt16',
        'UInt8', 'Enum:N' ...) given @args"""
        if '.' not in name:
            name = WFM + '.' + name
        ctor = bytearray(b'\x20' + compressed(len(params)) + b'\x01')
        blob = bytearray(b'\x01\x00')
        for p, a in zip(params, args):
            if p == 'Type':
                ctor += b'\x12' + compressed(self.encode(self.tdor('System.Type')))
                blob += ser_string(a)
            elif p == 'String':
                ctor += b'\x0e'
                blob += ser_string(a)
            elif p.startswith('Enum:'):
                ctor += b'\x11' + compressed(self.encode(self.tdor(p[5:])))
                blob += struct.pack('<i', a)
            else:
                ctor.append(PRIM_ELEM[p])
                blob += struct.pack({'UInt32': '<I', 'Int32': '<i', 'UInt16': '<H', 'UInt8': '<B',
                                     'Boolean': '<?'}[p], a)
        blob += b'\x00\x00'
        key = (name, bytes(ctor))
        if key not in self.memberrefs:
            self.memberrefs[key] = self.add(0x0A, [('MemberRefParent', 0x01, self.typeref(name)),
                                                   self.s('.ctor'), self.b(ctor)])
        self.add(0x0C, [parent, ('CustomAttributeType', 0x0A, self.memberrefs[key]), self.b(blob)])

    def guid_attr(self, parent, g):
        u = uuid.UUID(g)
        f = u.fields
        d = u.bytes[8:]
        self.attribute(parent, 'GuidAttribute', ['UInt32', 'UInt16', 'UInt16'] + ['UInt8'] * 8,
                       [f[0], f[1], f[2]] + list(d))

    # -- definitions -------------------------------------------------------------------
    def methods(self, tdrow, t, delegate=False):
        """@t's methods as MethodDef rows (with their Param rows, overload
        names, and property and event rows)"""
        first = len(self.rows[0x06]) + 1
        if delegate:
            self.add(0x06, [0, 0x0003, 0x1886, self.s('.ctor'), self.b(b'\x20\x02\x01\x1c\x18'),
                            len(self.rows[0x08]) + 1])
            self.add(0x08, [0, 1, self.s('object')])
            self.add(0x08, [0, 2, self.s('method')])
        seen = {}
        names = [m.name for m in t.methods]
        props, events = {}, {}
        for m in t.methods:
            special = not delegate and m.name.split('_', 1)[0] in ('get', 'put', 'add', 'remove') and '_' in m.name
            flags = 0x01C6 if delegate else 0x05C6
            if special:
                flags |= 0x0800
            sig = bytearray(b'\x20' + compressed(len(m.params)))
            sig += self.sig(m.ret) if m.ret is not None else b'\x01'
            for p in m.params:
                pt = p.type
                if p.out and not isinstance(pt, Array) and not pt.byref:
                    pt = pt.copy()
                    pt.byref = True
                sig += self.sig(pt)
            prow = len(self.rows[0x08]) + 1
            mrow = self.add(0x06, [0, 0x0003 if delegate else 0, flags, self.s(m.name), self.b(sig), prow])
            if m.ret is not None:
                self.add(0x08, [0, 0, self.s(m.retname or 'result')])
            for i, p in enumerate(m.params):
                self.add(0x08, [2 if p.out else 1, i + 1, self.s(p.name)])
            cm = ('HasCustomAttribute', 0x06, mrow)
            overload = m.overload() if m.overload() != m.name else None
            if overload is None and names.count(m.name) > 1:
                seen[m.name] = seen.get(m.name, 0) + 1
                overload = m.name + (str(seen[m.name]) if seen[m.name] > 1 else '')
                if seen[m.name] == 1:
                    self.attribute(cm, 'DefaultOverloadAttribute')
            if overload:
                self.attribute(cm, 'OverloadAttribute', ['String'], [overload])
            if special:
                kind, pname = m.name.split('_', 1)
                if kind in ('get', 'put'):
                    props.setdefault(pname, {})[kind] = (mrow, m)
                else:
                    events.setdefault(pname, {})[kind] = (mrow, m)
        if props:
            first_prop = len(self.rows[0x17]) + 1
            for pname, acc in props.items():
                ty = acc['get'][1].ret if 'get' in acc else acc['put'][1].params[0].type
                prow = self.add(0x17, [0, self.s(pname), self.b(b'\x28\x00' + self.sig(ty))])
                for kind, sem in (('get', 0x2), ('put', 0x1)):
                    if kind in acc:
                        self.add(0x18, [sem, acc[kind][0], ('HasSemantics', 0x17, prow)])
            self.add(0x15, [tdrow, first_prop])
        if events:
            first_ev = len(self.rows[0x14]) + 1
            for ename, acc in events.items():
                ty = acc['add'][1].params[0].type if 'add' in acc else None
                if ty is None:
                    continue
                erow = self.add(0x14, [0, self.s(ename), self.tdor(ty)])
                for kind, sem in (('add', 0x8), ('remove', 0x10)):
                    if kind in acc:
                        self.add(0x18, [sem, acc[kind][0], ('HasSemantics', 0x14, erow)])
            self.add(0x12, [tdrow, first_ev])
        return first

    def define(self, name):
        t = self.types[name]
        ns, short = name.rsplit('.', 1)
        fields = len(self.rows[0x04]) + 1
        methods = len(self.rows[0x06]) + 1
        tdrow = len(self.rows[0x02]) + 1
        row = [0, self.s(short), self.s(ns), ('TypeDefOrRef', 0x02, 0), fields, methods]
        self.add(0x02, row)              # (filled in below: its flags and base)
        ca = ('HasCustomAttribute', 0x02, tdrow)
        if isinstance(t, Enum):
            row[0] = 0x4101
            row[3] = self.tdor('System.Enum')
            under = t.underlying
            self.add(0x04, [0x0606, self.s('value__'), self.b(b'\x06' + bytes([PRIM_ELEM[under]]))])
            self_sig = b'\x06' + self.sig(TypeRef(name))
            for vname, v in t.values:
                f = self.add(0x04, [0x8056, self.s(vname), self.b(self_sig)])
                self.add(0x0B, [PRIM_ELEM[under], 0, ('HasConstant', 0x04, f),
                                self.b(struct.pack('<I' if under == 'UInt32' else '<i', v & 0xFFFFFFFF if under == 'UInt32' else v))])
            if under == 'UInt32':
                self.attribute(ca, 'System.FlagsAttribute')
        elif isinstance(t, Struct):
            row[0] = 0x4109
            row[3] = self.tdor('System.ValueType')
            for f in t.fields:
                self.add(0x04, [0x0006, self.s(f.name), self.b(b'\x06' + self.sig(f.type))])
        elif isinstance(t, Contract):
            row[0] = 0x4109
            row[3] = self.tdor('System.ValueType')
            self.attribute(ca, 'ApiContractAttribute')
        elif isinstance(t, Delegate):
            row[0] = 0x4101
            row[3] = self.tdor('System.MulticastDelegate')
            self.methods(tdrow, t, delegate=True)
            self.guid_attr(ca, self.iid_for(t))
            self.generics(tdrow, t)
        elif isinstance(t, Interface):
            exclusive = getattr(t, 'exclusive_to', None)
            row[0] = 0x40A0 if exclusive else 0x40A1
            row[3] = ('TypeDefOrRef', 0x02, 0)
            self.methods(tdrow, t)
            for it, attrs in t.interfaces:
                self.add(0x09, [tdrow, self.tdor(it)])
            self.guid_attr(ca, self.iid_for(t))
            if exclusive:
                self.attribute(ca, 'ExclusiveToAttribute', ['Type'], [exclusive])
            self.generics(tdrow, t)
        elif isinstance(t, RuntimeClass):
            row[0] = 0x4001 | (0x100 if t.sealed else 0) | (0x180 if t.static else 0)
            row[3] = self.tdor(t.base or 'System.Object')
            for i, (it, attrs) in enumerate(t.interfaces):
                irow = self.add(0x09, [tdrow, self.tdor(it)])
                ica = ('HasCustomAttribute', 0x09, irow)
                for a in attrs:
                    short_a = a.name.rsplit('.', 1)[-1]
                    if short_a in ('DefaultAttribute', 'OverridableAttribute', 'ProtectedAttribute'):
                        self.attribute(ica, short_a)
            if getattr(t, 'activatable_default', False):
                self.attribute(ca, 'ActivatableAttribute', ['UInt32'], [1])
            for k, iname in getattr(t, 'synth', []):
                if k == 'factory':
                    if t.sealed:
                        self.attribute(ca, 'ActivatableAttribute', ['Type', 'UInt32'], [iname, 1])
                    else:
                        self.attribute(ca, 'ComposableAttribute',
                                       ['Type', 'Enum:' + WFM + '.CompositionType', 'UInt32'], [iname, 2, 1])
                elif k == 'static':
                    self.attribute(ca, 'StaticAttribute', ['Type', 'UInt32'], [iname, 1])
        return tdrow

    def generics(self, tdrow, t):
        for i, g in enumerate(getattr(t, 'generics', [])):
            self.add(0x2A, [i, 0, ('TypeOrMethodDef', 0x02, tdrow), self.s(g)])

    # -- the file ------------------------------------------------------------------------
    def build(self):
        self.add(0x00, [0, self.s(self.assembly + '.winmd'), 1, 0, 0])
        self.add(0x02, [0, self.s('<Module>'), self.s(''), ('TypeDefOrRef', 0x02, 0), 1, 1])
        for n in self.names:
            self.define(n)
        self.add(0x20, [0x8004, 255, 255, 255, 255, 0x200, 0, self.s(self.assembly), 0])
        # (sorting is stable: InterfaceImpl and Constant rows, which other rows
        # name, are made in their keys' order already and keep their places)
        for t, col in SORTED.items():
            if t == 0x2A:
                self.rows[t].sort(key=lambda r: (self.encode(r[2]), r[0]))
            else:
                self.rows[t].sort(key=lambda r: self.encode(r[col]) if isinstance(r[col], tuple) else r[col])
        return self.image()

    def tables(self):
        counts = {t: len(r) for t, r in self.rows.items() if r}
        big_s = len(self.sheap) >= 0x10000
        big_b = len(self.bheap) >= 0x10000
        heap = (1 if big_s else 0) | (4 if big_b else 0)

        def size(c):
            if c in ('u1', 'u2', 'u4'):
                return int(c[1])
            if c == 's':
                return 4 if big_s else 2
            if c == 'g':
                return 2
            if c == 'b':
                return 4 if big_b else 2
            if c[0] == 't':
                return 2 if counts.get(c[1], 0) < 0x10000 else 4
            tabs = CODED[c[1]]
            m = max(counts.get(x, 0) for x in tabs if x is not None)
            return 2 if m < (1 << (16 - _tagbits(len(tabs)))) else 4
        out = bytearray(struct.pack('<IBBBB', 0, 2, 0, heap, 1))
        valid = sum(1 << t for t in counts)
        sorted_mask = sum(1 << t for t in SORTED) | 0x0000160000000000
        out += struct.pack('<QQ', valid, sorted_mask)
        for t in sorted(counts):
            out += struct.pack('<I', counts[t])
        for t in sorted(counts):
            cols = COLS[t]
            for r in self.rows[t]:
                for c, v in zip(cols, r):
                    if isinstance(v, tuple):
                        v = self.encode(v)
                    out += struct.pack({1: '<B', 2: '<H', 4: '<I'}[size(c)], v)
        while len(out) % 4:
            out.append(0)
        return bytes(out)

    def image(self):
        def pad4(b):
            b = bytearray(b)
            while len(b) % 4:
                b.append(0)
            return bytes(b)
        streams = [('#~', self.tables()), ('#Strings', pad4(self.sheap)), ('#US', pad4(b'\0')),
                   ('#GUID', uuid.uuid5(uuid.NAMESPACE_URL, 'novaos:' + self.assembly).bytes),
                   ('#Blob', pad4(self.bheap))]
        version = pad4(b'WindowsRuntime 1.4\0')
        hdr = bytearray(struct.pack('<IHHII', 0x424A5342, 1, 1, 0, len(version)) + version +
                        struct.pack('<HH', 0, len(streams)))
        hsize = len(hdr) + sum(8 + len(pad4(n.encode() + b'\0')) for n, _ in streams)
        off = hsize
        body = bytearray()
        for n, data in streams:
            hdr += struct.pack('<II', off, len(data)) + pad4(n.encode() + b'\0')
            off += len(data)
            body += data
        md = bytes(hdr + body)
        # a PE32 DLL with one section holding the CLI header and the metadata
        sect_rva, file_align = 0x2000, 0x200
        cli = struct.pack('<IHHII', 72, 2, 5, sect_rva + 72, len(md)) + struct.pack('<I', 1) + b'\0' * 52
        text = cli + md
        raw = (len(text) + file_align - 1) // file_align * file_align
        dos = bytearray(0x80)
        dos[0:2] = b'MZ'
        struct.pack_into('<I', dos, 0x3C, 0x80)
        coff = struct.pack('<4sHHIIIHH', b'PE\0\0', 0x14C, 1, 0, 0, 0, 0xE0, 0x2102)
        opt = bytearray(0xE0)
        struct.pack_into('<HBBIII', opt, 0, 0x10B, 11, 0, raw, 0, 0)
        struct.pack_into('<III', opt, 16, 0, sect_rva, sect_rva)
        struct.pack_into('<III', opt, 28, 0x10000000, 0x2000, file_align)
        struct.pack_into('<HHHHHH', opt, 40, 4, 0, 0, 0, 4, 0)
        struct.pack_into('<II', opt, 56, sect_rva + ((len(text) + 0x1FFF) & ~0x1FFF), file_align)
        struct.pack_into('<HH', opt, 68, 3, 0x8540)
        struct.pack_into('<IIII', opt, 72, 0x100000, 0x1000, 0x100000, 0x1000)
        struct.pack_into('<II', opt, 88, 0, 16)
        struct.pack_into('<II', opt, 96 + 14 * 8, sect_rva, 72)
        sect = struct.pack('<8sIIIIIIHHI', b'.text', len(text), sect_rva, raw, file_align, 0, 0, 0, 0, 0x60000020)
        head = bytes(dos) + coff + bytes(opt) + sect
        head += b'\0' * (file_align - len(head))
        return head + text + b'\0' * (raw - len(text))


def write(path, assembly, types, names, iid_for):
    data = _Writer(assembly, types, sorted(names), iid_for).build()
    with open(path, 'wb') as f:
        f.write(data)
