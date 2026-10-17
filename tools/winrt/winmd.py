"""Reader for Windows Runtime metadata (.winmd, ECMA-335).

Loads the type definitions of one or more .winmd files into the model in
model.py: enums, structs, interfaces, delegates, runtime classes and API
contracts, with their GUIDs, methods, generic parameters and the
attributes the ABI headers need (default interface, exclusive-to,
overload names, factory/static/composable interfaces).
"""
import struct

from model import (Enum, Struct, Interface, Delegate, RuntimeClass, Contract,
                   Method, Param, TypeRef, GenericInst, GenericVar, Prim, Array,
                   Field, Attribute)

# ECMA-335 II.22: table number -> column kinds.  'u2'/'u4'/'u1' fixed
# sizes, 's' string heap, 'g' guid heap, 'b' blob heap, ('t', n) index into
# table n, ('c', name) coded index.
TABLES = {
    0x00: ['u2', 's', 'g', 'g', 'g'],
    0x01: [('c', 'ResolutionScope'), 's', 's'],
    0x02: ['u4', 's', 's', ('c', 'TypeDefOrRef'), ('t', 0x04), ('t', 0x06)],
    0x03: [('t', 0x04)],
    0x04: ['u2', 's', 'b'],
    0x05: [('t', 0x06)],
    0x06: ['u4', 'u2', 'u2', 's', 'b', ('t', 0x08)],
    0x07: [('t', 0x08)],
    0x08: ['u2', 'u2', 's'],
    0x09: [('t', 0x02), ('c', 'TypeDefOrRef')],
    0x0A: [('c', 'MemberRefParent'), 's', 'b'],
    0x0B: ['u1', 'u1', ('c', 'HasConstant'), 'b'],
    0x0C: [('c', 'HasCustomAttribute'), ('c', 'CustomAttributeType'), 'b'],
    0x0D: [('c', 'HasFieldMarshal'), 'b'],
    0x0E: ['u2', ('c', 'HasDeclSecurity'), 'b'],
    0x0F: ['u2', 'u4', ('t', 0x02)],
    0x10: ['u4', ('t', 0x04)],
    0x11: ['b'],
    0x12: [('t', 0x02), ('t', 0x14)],
    0x13: [('t', 0x14)],
    0x14: ['u2', 's', ('c', 'TypeDefOrRef')],
    0x15: [('t', 0x02), ('t', 0x17)],
    0x16: [('t', 0x17)],
    0x17: ['u2', 's', 'b'],
    0x18: ['u2', ('t', 0x06), ('c', 'HasSemantics')],
    0x19: [('t', 0x02), ('c', 'MethodDefOrRef'), ('c', 'MethodDefOrRef')],
    0x1A: ['s'],
    0x1B: ['b'],
    0x1C: ['u2', ('c', 'MemberForwarded'), 's', ('t', 0x1A)],
    0x1D: ['u4', ('t', 0x04)],
    0x1E: ['u4', 'u4'],
    0x1F: ['u4'],
    0x20: ['u4', 'u2', 'u2', 'u2', 'u2', 'u4', 'b', 's', 's'],
    0x21: ['u4'],
    0x22: ['u4', 'u4', 'u4'],
    0x23: ['u2', 'u2', 'u2', 'u2', 'u4', 'b', 's', 's', 'b'],
    0x24: ['u4', ('t', 0x23)],
    0x25: ['u4', 'u4', 'u4', ('t', 0x23)],
    0x26: ['u4', 's', 'b'],
    0x27: ['u4', 'u4', 's', 's', ('c', 'Implementation')],
    0x28: ['u4', 'u4', 's', ('c', 'Implementation')],
    0x29: [('t', 0x02), ('t', 0x02)],
    0x2A: ['u2', 'u2', ('c', 'TypeOrMethodDef'), 's'],
    0x2B: [('c', 'MethodDefOrRef'), 'b'],
    0x2C: [('t', 0x2A), ('c', 'TypeDefOrRef')],
}

CODED = {
    'TypeDefOrRef': [0x02, 0x01, 0x1B],
    'HasConstant': [0x04, 0x08, 0x17],
    'HasCustomAttribute': [0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E,
                           0x17, 0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27,
                           0x28, 0x2A, 0x2C, 0x2B],
    'HasFieldMarshal': [0x04, 0x08],
    'HasDeclSecurity': [0x02, 0x06, 0x20],
    'MemberRefParent': [0x02, 0x01, 0x1A, 0x06, 0x1B],
    'HasSemantics': [0x14, 0x17],
    'MethodDefOrRef': [0x06, 0x0A],
    'MemberForwarded': [0x04, 0x06],
    'Implementation': [0x26, 0x23, 0x27],
    'CustomAttributeType': [None, None, 0x06, 0x0A, None],
    'ResolutionScope': [0x00, 0x1A, 0x23, 0x01],
    'TypeOrMethodDef': [0x02, 0x06],
}

ELEMENT_PRIM = {0x02: 'Boolean', 0x03: 'Char16', 0x04: 'Int8', 0x05: 'UInt8',
                0x06: 'Int16', 0x07: 'UInt16', 0x08: 'Int32', 0x09: 'UInt32',
                0x0A: 'Int64', 0x0B: 'UInt64', 0x0C: 'Single', 0x0D: 'Double',
                0x0E: 'String', 0x1C: 'Object', 0x18: 'IntPtr', 0x19: 'UIntPtr'}


def _tagbits(n):
    b = 0
    while (1 << b) < n:
        b += 1
    return b


class _Metadata:
    def __init__(self, path):
        data = open(path, 'rb').read()
        self.path = path
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        nsec = struct.unpack_from('<H', data, pe + 6)[0]
        optsz = struct.unpack_from('<H', data, pe + 20)[0]
        opt = pe + 24
        magic = struct.unpack_from('<H', data, opt)[0]
        dd = opt + (96 if magic == 0x10B else 112)
        cli_rva = struct.unpack_from('<I', data, dd + 14 * 8)[0]
        secs = []
        for i in range(nsec):
            s = pe + 24 + optsz + i * 40
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', data, s + 8)
            secs.append((va, max(vsize, rsize), rptr))

        def off(rva):
            for va, size, ptr in secs:
                if va <= rva < va + size:
                    return rva - va + ptr
            raise ValueError('rva %x' % rva)
        cli = off(cli_rva)
        md_rva = struct.unpack_from('<I', data, cli + 8)[0]
        md = off(md_rva)
        assert struct.unpack_from('<I', data, md)[0] == 0x424A5342
        vlen = struct.unpack_from('<I', data, md + 12)[0]
        p = md + 16 + vlen
        nstreams = struct.unpack_from('<H', data, p + 2)[0]
        p += 4
        streams = {}
        for _ in range(nstreams):
            o, size = struct.unpack_from('<II', data, p)
            p += 8
            e = data.index(b'\0', p)
            name = data[p:e].decode()
            p = (e + 4) & ~3
            streams[name] = data[md + o: md + o + size]
        self.strings = streams['#Strings']
        self.blobs = streams['#Blob']
        self.guids = streams.get('#GUID', b'')
        t = streams['#~']
        heap = t[6]
        self.sidx = 4 if heap & 1 else 2
        self.gidx = 4 if heap & 2 else 2
        self.bidx = 4 if heap & 4 else 2
        valid = struct.unpack_from('<Q', t, 8)[0]
        p = 24
        self.rows = {}
        for n in range(64):
            if valid >> n & 1:
                self.rows[n] = struct.unpack_from('<I', t, p)[0]
                p += 4
        self.tables = {}
        for n in sorted(self.rows):
            cols = TABLES[n]
            sizes = [self._colsize(c) for c in cols]
            rowsize = sum(sizes)
            count = self.rows[n]
            fmt = '<' + ''.join({1: 'B', 2: 'H', 4: 'I'}[s] for s in sizes)
            rows = [struct.unpack_from(fmt, t, p + i * rowsize) for i in range(count)]
            self.tables[n] = rows
            p += rowsize * count

    def _colsize(self, c):
        if c == 'u1':
            return 1
        if c == 'u2':
            return 2
        if c == 'u4':
            return 4
        if c == 's':
            return self.sidx
        if c == 'g':
            return self.gidx
        if c == 'b':
            return self.bidx
        if c[0] == 't':
            return 2 if self.rows.get(c[1], 0) < 0x10000 else 4
        tabs = CODED[c[1]]
        bits = _tagbits(len(tabs))
        m = max(self.rows.get(x, 0) for x in tabs if x is not None)
        return 2 if m < (1 << (16 - bits)) else 4

    def table(self, n):
        return self.tables.get(n, [])

    def string(self, i):
        e = self.strings.index(b'\0', i)
        return self.strings[i:e].decode('utf-8')

    def blob(self, i):
        b = self.blobs
        x = b[i]
        if x & 0x80 == 0:
            return b[i + 1: i + 1 + x]
        if x & 0xC0 == 0x80:
            n = ((x & 0x3F) << 8) | b[i + 1]
            return b[i + 2: i + 2 + n]
        n = ((x & 0x1F) << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]
        return b[i + 4: i + 4 + n]

    @staticmethod
    def decode(name, v):
        tabs = CODED[name]
        bits = _tagbits(len(tabs))
        return tabs[v & ((1 << bits) - 1)], (v >> bits)


class _Sig:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def byte(self):
        x = self.d[self.p]
        self.p += 1
        return x

    def peek(self):
        return self.d[self.p]

    def uint(self):
        x = self.byte()
        if x & 0x80 == 0:
            return x
        if x & 0xC0 == 0x80:
            return ((x & 0x3F) << 8) | self.byte()
        return ((x & 0x1F) << 24) | (self.byte() << 16) | (self.byte() << 8) | self.byte()


class Reader:
    """Reads .winmd files into a dict of full name -> model type."""

    def __init__(self):
        self.types = {}

    def load(self, path):
        m = _Metadata(path)
        self._load(m)

    # -- helpers -------------------------------------------------------------
    def _typedef_name(self, m, row):
        ns = m.string(row[2])
        n = m.string(row[1])
        return ns + '.' + n if ns else n

    def _typeref_name(self, m, idx):
        r = m.table(0x01)[idx - 1]
        ns = m.string(r[2])
        n = m.string(r[1])
        return ns + '.' + n if ns else n

    def _tdor_name(self, m, coded):
        tab, idx = m.decode('TypeDefOrRef', coded)
        if tab == 0x02:
            return self._typedef_name(m, m.table(0x02)[idx - 1])
        if tab == 0x01:
            return self._typeref_name(m, idx)
        return self._sig_type(m, _Sig(m.blob(m.table(0x1B)[idx - 1][0])))

    def _tdor_type(self, m, coded):
        tab, idx = m.decode('TypeDefOrRef', coded)
        if tab == 0x1B:
            return self._sig_type(m, _Sig(m.blob(m.table(0x1B)[idx - 1][0])))
        return TypeRef(self._tdor_name(m, coded))

    def _sig_type(self, m, s):
        while s.peek() in (0x1F, 0x20):        # custom modifiers
            s.byte()
            s.uint()
        e = s.byte()
        if e in ELEMENT_PRIM:
            return Prim(ELEMENT_PRIM[e])
        if e in (0x11, 0x12):
            tab, idx = m.decode('TypeDefOrRef', s.uint())
            if tab == 0x02:
                name = self._typedef_name(m, m.table(0x02)[idx - 1])
            else:
                name = self._typeref_name(m, idx)
            if name == 'System.Guid':
                return Prim('Guid')
            return TypeRef(name)
        if e == 0x15:
            s.byte()
            tab, idx = m.decode('TypeDefOrRef', s.uint())
            name = (self._typedef_name(m, m.table(0x02)[idx - 1]) if tab == 0x02
                    else self._typeref_name(m, idx))
            n = s.uint()
            return GenericInst(name, [self._sig_type(m, s) for _ in range(n)])
        if e == 0x13:
            return GenericVar(s.uint())
        if e == 0x1D:
            return Array(self._sig_type(m, s))
        if e == 0x0F:
            t = self._sig_type(m, s).copy()
            t.ptr = getattr(t, 'ptr', 0) + 1
            return t
        if e == 0x10:
            t = self._sig_type(m, s)
            t = t.copy()
            t.byref = True
            return t
        raise ValueError('signature element %#x in %s' % (e, m.path))

    def _method_sig(self, m, blob):
        s = _Sig(m.blob(blob))
        conv = s.byte()
        if conv & 0x10:
            s.uint()
        n = s.uint()
        ret = None if s.peek() == 0x01 else self._sig_type(m, s)
        if ret is None:
            s.byte()
        params = []
        for _ in range(n):
            const = False
            while s.peek() in (0x1F, 0x20):
                s.byte()
                s.uint()
                const = True
            t = self._sig_type(m, s)
            if const:
                t = t.copy()
                t.const = True
            params.append(t)
        return ret, params

    def _attr(self, m, row):
        tab, idx = m.decode('CustomAttributeType', row[1])
        if tab == 0x0A:
            mr = m.table(0x0A)[idx - 1]
            ptab, pidx = m.decode('MemberRefParent', mr[0])
            tname = (self._typeref_name(m, pidx) if ptab == 0x01 else
                     self._typedef_name(m, m.table(0x02)[pidx - 1]))
            ctor = mr[2]
        else:
            md = m.table(0x06)[idx - 1]
            # find the typedef owning this method
            tname = None
            tds = m.table(0x02)
            for i, td in enumerate(tds):
                end = tds[i + 1][5] if i + 1 < len(tds) else len(m.table(0x06)) + 1
                if td[5] <= idx < end:
                    tname = self._typedef_name(m, td)
                    break
            ctor = md[4]
        _, ptypes = self._method_sig(m, ctor)
        v = m.blob(row[2])
        args = []
        p = 2
        for t in ptypes:
            if isinstance(t, Prim):
                if t.name == 'String':
                    if v[p] == 0xFF:
                        args.append(None)
                        p += 1
                        continue
                    s = _Sig(v[p:])
                    n = s.uint()
                    p += s.p
                    args.append(v[p:p + n].decode('utf-8'))
                    p += n
                else:
                    fmt, size = {'Boolean': ('<?', 1), 'UInt8': ('<B', 1), 'Int8': ('<b', 1),
                                 'Int16': ('<h', 2), 'UInt16': ('<H', 2), 'Int32': ('<i', 4),
                                 'UInt32': ('<I', 4), 'Int64': ('<q', 8),
                                 'UInt64': ('<Q', 8), 'Char16': ('<H', 2),
                                 'Single': ('<f', 4), 'Double': ('<d', 8)}[t.name]
                    args.append(struct.unpack_from(fmt, v, p)[0])
                    p += size
            elif isinstance(t, TypeRef) and t.name == 'System.Type':
                s = _Sig(v[p:])
                n = s.uint()
                p += s.p
                args.append(v[p:p + n].decode('utf-8').split(',')[0])
                p += n
            else:                                  # enum: assume int32
                args.append(struct.unpack_from('<i', v, p)[0])
                p += 4
        return Attribute(tname, args)

    # -- load ----------------------------------------------------------------
    def _load(self, m):
        tds = m.table(0x02)
        nmethods = len(m.table(0x06))
        nfields = len(m.table(0x04))
        attrs = {}
        for row in m.table(0x0C):
            tab, idx = m.decode('HasCustomAttribute', row[0])
            attrs.setdefault((tab, idx), []).append(row)

        def attrs_of(tab, idx):
            return [self._attr(m, r) for r in attrs.get((tab, idx), [])]
        impls = {}
        for i, row in enumerate(m.table(0x09)):
            impls.setdefault(row[0], []).append((i + 1, row[1]))
        gparams = {}
        for row in m.table(0x2A):
            tab, idx = m.decode('TypeOrMethodDef', row[2])
            if tab == 0x02:
                gparams.setdefault(idx, []).append((row[0], m.string(row[3])))
        consts = {}
        for row in m.table(0x0B):
            tab, idx = m.decode('HasConstant', row[2])
            if tab == 0x04:
                consts[idx] = (row[0], m.blob(row[3]))
        params = m.table(0x08)
        props = {}
        for row in m.table(0x18):
            props[row[1]] = row[0]

        for i, td in enumerate(tds):
            flags, ns = td[0], m.string(td[2])
            name = m.string(td[1])
            if not ns or name == '<Module>':
                continue
            full = ns + '.' + name
            extends = self._tdor_name(m, td[3]) if td[3] else None
            fend = tds[i + 1][4] if i + 1 < len(tds) else nfields + 1
            mend = tds[i + 1][5] if i + 1 < len(tds) else nmethods + 1
            a = attrs_of(0x02, i + 1)
            guid = None
            for at in a:
                if at.name == 'Windows.Foundation.Metadata.GuidAttribute':
                    guid = '%08x-%04x-%04x-%02x%02x-%s' % (
                        at.args[0], at.args[1], at.args[2], at.args[3], at.args[4],
                        ''.join('%02x' % x for x in at.args[5:]))
            gp = [n for _, n in sorted(gparams.get(i + 1, []))]
            if flags & 0x20:                                   # interface
                t = Interface(full, guid=guid, generics=gp)
            elif extends == 'System.Enum':
                t = Enum(full)
            elif extends == 'System.ValueType':
                t = Contract(full) if any(x.name == 'Windows.Foundation.Metadata.ApiContractAttribute' for x in a) else Struct(full)
            elif extends == 'System.MulticastDelegate':
                t = Delegate(full, guid=guid, generics=gp)
            elif extends == 'System.Attribute':
                continue
            else:
                t = RuntimeClass(full)
                t.base = extends if extends != 'System.Object' else None
                t.sealed = bool(flags & 0x100)
                t.static = bool(flags & 0x80) and bool(flags & 0x100)
            t.attrs = a
            if isinstance(t, (Enum, Struct)):
                for fi in range(td[4], fend):
                    f = m.table(0x04)[fi - 1]
                    fname = m.string(f[1])
                    s = _Sig(m.blob(f[2]))
                    s.byte()
                    ft = self._sig_type(m, s)
                    if isinstance(t, Enum):
                        if fname == 'value__':
                            t.underlying = ft.name
                            continue
                        ctype, cval = consts[fi]
                        fmt = '<i' if ctype == 0x08 else '<I'
                        t.values.append((fname, struct.unpack(fmt, cval)[0]))
                    else:
                        t.fields.append(Field(fname, ft))
            if isinstance(t, (Interface, Delegate, RuntimeClass)):
                for mi in range(td[5], mend):
                    md = m.table(0x06)[mi - 1]
                    mname = m.string(md[3])
                    ret, ptypes = self._method_sig(m, md[4])
                    pend = m.table(0x06)[mi][5] if mi < nmethods else len(params) + 1
                    pnames = {}
                    pflags = {}
                    for pi in range(md[5], pend):
                        pr = params[pi - 1]
                        pnames[pr[1]] = m.string(pr[2])
                        pflags[pr[1]] = pr[0]
                    plist = []
                    for k, pt in enumerate(ptypes):
                        out = bool(pflags.get(k + 1, 0) & 2)
                        if out and pt.byref and not isinstance(pt, Array):
                            pt = pt.copy()
                            pt.byref = False
                        plist.append(Param(pnames.get(k + 1, 'p%d' % k), pt, out))
                    retname = pnames.get(0, 'result')
                    if mname == '.ctor' and isinstance(t, Delegate):
                        continue
                    meth = Method(mname, plist, ret, retname)
                    meth.attrs = attrs_of(0x06, mi)
                    meth.special = bool(md[2] & 0x800)
                    meth.ctor = mname == '.ctor'
                    meth.static = bool(md[2] & 0x10)
                    t.methods.append(meth)
                for idx, coded in impls.get(i + 1, []):
                    it = self._tdor_type(m, coded)
                    ia = attrs_of(0x09, idx)
                    t.interfaces.append((it, ia))
            self.types[full] = t
