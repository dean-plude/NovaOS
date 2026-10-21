#!/usr/bin/env python3
"""Writes the C++ "ABI" headers of Windows Runtime types without MIDL.

    tools/winrt/abigen.py OUT_DIR [--winmd FILE]... [--idl FILE]...
                          [--iids FILE.winmd]... [--disable FEATURE]...

Reads Windows Runtime metadata (.winmd) and MIDL 3 IDL into one set of
types and writes, for every namespace N, the headers MIDL and the Windows
SDK give C++ code (lower-case names, as the SDK's):

  n.h          the namespace's interfaces (struct IFoo : IInspectable with
               its methods in vtable order, __declspec(uuid)), delegates
               (IFooHandler : IUnknown), runtime class names
               (RuntimeClass_N_Foo) and the generic instances its types use
               (template <> struct __declspec(uuid) IVector<T> ...,
               __FIVector_1_... typedefs)
  n.values.h   its enums, structs and generic interface templates
               (IVector_impl<T>), which other namespaces' headers include

Types MIDL would synthesize from IDL (a runtime class's IFoo, IFooStatics,
IFooFactory ...) get the IID the same name has in an --iids .winmd (the
published metadata), else a name-based UUID of NovaOS's (see iid_for).
Generic instances get the IID Windows gives them (pinterface signatures,
as in Windows' own metadata rules).  See docs/design/windows-ui-xaml.md.
"""
import argparse
import copy, hashlib
import os
import re
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import idl as idlmod
import winmd as winmdmod
from model import (Enum, Struct, Interface, Delegate, RuntimeClass, Contract,
                   TypeRef, GenericInst, GenericVar, Prim, Array)

PINTERFACE_NS = uuid.UUID('11f47ad5-7b73-42c0-abae-878b1e16adee')
NOVA_NS = uuid.UUID('6e6f7661-0032-4358-8000-61626967656e')   # "nova", 32, ..., "abigen"

PRIM_ABI = {'Boolean': '::boolean', 'Char16': 'WCHAR', 'Int8': 'INT8', 'UInt8': 'BYTE',
            'Int16': 'INT16', 'UInt16': 'UINT16', 'Int32': 'INT32', 'UInt32': 'UINT32',
            'Int64': 'INT64', 'UInt64': 'UINT64', 'Single': 'FLOAT', 'Double': 'DOUBLE',
            'String': 'HSTRING', 'Object': 'IInspectable*', 'Guid': 'GUID',
            'IntPtr': 'INT_PTR', 'UIntPtr': 'UINT_PTR'}
PRIM_ARG = dict(PRIM_ABI, Boolean='bool', Char16='wchar_t', Int32='int', Single='float',
                Double='double')
PRIM_SIG = {'Boolean': 'b1', 'Char16': 'c2', 'Int8': 'i1', 'UInt8': 'u1', 'Int16': 'i2',
            'UInt16': 'u2', 'Int32': 'i4', 'UInt32': 'u4', 'Int64': 'i8', 'UInt64': 'u8',
            'Single': 'f4', 'Double': 'f8', 'String': 'string',
            'Object': 'cinterface(IInspectable)', 'Guid': 'g16'}
PRIM_MANGLE = {'Boolean': 'boolean', 'Char16': 'wchar__zt', 'Int8': 'INT8', 'UInt8': 'BYTE',
               'Int16': 'INT16', 'UInt16': 'UINT16', 'Int32': 'int', 'UInt32': 'UINT32',
               'Int64': '__int64', 'UInt64': 'UINT64', 'Single': 'float', 'Double': 'double',
               'String': 'HSTRING', 'Object': 'IInspectable', 'Guid': 'GUID'}
# Windows.Foundation types the ABI spells as Win32 types
SPECIAL = {'Windows.Foundation.HResult': 'HRESULT',
           'Windows.Foundation.EventRegistrationToken': 'EventRegistrationToken'}


def name_uuid(ns, text):
    d = bytearray(hashlib.sha1(ns.bytes + text.encode('utf-8')).digest()[:16])
    d[6] = (d[6] & 0x0F) | 0x50
    d[8] = (d[8] & 0x3F) | 0x80
    return str(uuid.UUID(bytes=bytes(d)))


def cpp_ns(ns):
    return 'ABI::' + ns.replace('.', '::')


def strip_arity(name):
    return name.split('`')[0]


class Generator:
    def __init__(self, types, iids):
        self.types = types
        self.iids = iids
        self.instances = {}            # mangled -> GenericInst

    # -- lookups --------------------------------------------------------------
    def get(self, name):
        t = self.types.get(name)
        if t is None:
            raise KeyError('unknown type ' + name)
        return t

    def iid_for(self, t):
        if t.guid:
            return t.guid.lower()
        if t.name in self.iids:
            t.guid = self.iids[t.name]
        else:
            t.guid = name_uuid(NOVA_NS, t.name)
        return t.guid

    def default_iface(self, rc):
        return rc.default_interface()

    def iface_cpp_name(self, t):
        """The C++ name of an interface or delegate type definition."""
        short = strip_arity(t.short)
        # (the collections' two delegates keep their own names, as the
        # Windows SDK's windows.foundation.collections.h spells them:
        # VectorChangedEventHandler<T>, MapChangedEventHandler<K, V>)
        if isinstance(t, Delegate) and t.name.startswith('Windows.Foundation.Collections.'):
            return short
        if isinstance(t, Delegate):
            short = 'I' + short
        return short

    # -- signatures for generic IIDs -------------------------------------------
    def sig(self, ty):
        if isinstance(ty, Prim):
            return PRIM_SIG[ty.name]
        if isinstance(ty, GenericInst):
            g = self.get(ty.name)
            return 'pinterface({%s};%s)' % (self.iid_for(g), ';'.join(self.sig(a) for a in ty.args))
        t = self.get(ty.name)
        if isinstance(t, Enum):
            return 'enum(%s;%s)' % (t.name, 'u4' if t.underlying == 'UInt32' else 'i4')
        if isinstance(t, Struct):
            return 'struct(%s;%s)' % (t.name, ';'.join(self.sig(f.type) for f in t.fields))
        if isinstance(t, Delegate):
            return 'delegate({%s})' % self.iid_for(t)
        if isinstance(t, Interface):
            return '{%s}' % self.iid_for(t)
        if isinstance(t, RuntimeClass):
            d = self.default_iface(t)
            return 'rc(%s;%s)' % (t.name, self.sig(d))
        raise ValueError(ty)

    def generic_iid(self, inst):
        return name_uuid(PINTERFACE_NS, self.sig(inst))

    # -- C++ spellings ----------------------------------------------------------
    def mangle(self, ty):
        if isinstance(ty, Prim):
            return PRIM_MANGLE[ty.name]
        if isinstance(ty, GenericInst):
            g = self.get(ty.name)
            return '__F%s_%d_%s' % (self.iface_cpp_name(g), len(ty.args),
                                    '_'.join(self.mangle(a) for a in ty.args))
        t = self.get(ty.name)
        if isinstance(t, (Interface, Delegate)):
            return t.namespace.replace('.', '__C') + '__C' + self.iface_cpp_name(t)
        return t.name.replace('.', '__C')

    def qual(self, t):
        if isinstance(t, (Interface, Delegate)):
            return '%s::%s' % (cpp_ns(t.namespace), self.iface_cpp_name(t))
        return '%s::%s' % (cpp_ns(t.namespace), t.short)

    def generic_cpp(self, inst):
        g = self.get(inst.name)
        return '%s::%s<%s>' % (cpp_ns(g.namespace), self.iface_cpp_name(g),
                               ', '.join(self.arg_cpp(a) for a in inst.args))

    def arg_cpp(self, ty, gen=None):
        """A type as a generic argument: the logical type (a class pointer,
        bool, int ...)."""
        if isinstance(ty, GenericVar):
            return gen[ty.index] + '_logical'
        if isinstance(ty, Prim):
            return PRIM_ARG[ty.name]
        if isinstance(ty, GenericInst):
            if gen:
                g = self.get(ty.name)
                return '%s::%s<%s>*' % (cpp_ns(g.namespace), self.iface_cpp_name(g),
                                        ', '.join(self.arg_cpp(a, gen) for a in ty.args))
            return self.generic_cpp(ty) + '*'
        if ty.name in SPECIAL:
            return SPECIAL[ty.name]
        t = self.get(ty.name)
        if isinstance(t, (Interface, Delegate, RuntimeClass)):
            return self.qual(t) + '*'
        return self.qual(t)

    def agg_cpp(self, ty):
        """A generic argument as the template specialization takes it:
        AggregateType<logical, abi> where the two differ."""
        if isinstance(ty, Prim) and ty.name == 'Boolean':
            return 'ABI::Windows::Foundation::Internal::AggregateType<bool, ::boolean>'
        if isinstance(ty, TypeRef) and ty.name not in SPECIAL:
            t = self.get(ty.name)
            if isinstance(t, RuntimeClass):
                d = self.default_iface(t)
                return 'ABI::Windows::Foundation::Internal::AggregateType<%s*, %s>' % (
                    self.qual(t), self.abi_cpp(d))
        return self.arg_cpp(ty)

    def abi_cpp(self, ty, gen=None):
        """A type as a method parameter or field (the ABI type)."""
        if isinstance(ty, GenericVar):
            return gen[ty.index] + '_abi'
        if isinstance(ty, Prim):
            return PRIM_ABI[ty.name]
        if isinstance(ty, GenericInst):
            return self.arg_cpp(ty, gen)
        if ty.name in SPECIAL:
            return SPECIAL[ty.name]
        t = self.get(ty.name)
        if isinstance(t, RuntimeClass):
            d = self.default_iface(t)
            if d is None:
                return 'IInspectable*'
            return self.abi_cpp(d, gen)
        if isinstance(t, (Interface, Delegate)):
            return self.qual(t) + '*'
        return self.qual(t)

    def params_cpp(self, m, gen=None):
        out = []
        for p in m.params:
            ty = p.type
            if isinstance(ty, Array):
                el = self.abi_cpp(ty.elem, gen)
                if ty.byref:                                 # receive array
                    out.append('UINT32* %sLength' % p.name)
                    out.append('%s** %s' % (el, p.name))
                else:                                        # pass / fill array
                    out.append('UINT32 %sLength' % p.name)
                    out.append('%s* %s' % (el, p.name))
                continue
            c = self.abi_cpp(ty, gen)
            if p.out or ty.byref:
                c += '*'
            out.append('%s %s' % (c, _ident(p.name)))
        if m.ret is not None:
            ty = m.ret
            rn = m.retname
            if rn in [p.name for p in m.params]:
                rn = 'returnValue'
            if isinstance(ty, Array):
                out.append('UINT32* %sLength' % rn)
                out.append('%s** %s' % (self.abi_cpp(ty.elem, gen), rn))
            else:
                out.append('%s* %s' % (self.abi_cpp(ty, gen), _ident(rn)))
        return ', '.join(out)

    # -- references -------------------------------------------------------------
    def refs(self, ty, out):
        """Collect named types and generic instances a type mentions."""
        if isinstance(ty, Array):
            self.refs(ty.elem, out)
        elif isinstance(ty, GenericVar):
            pass
        elif isinstance(ty, GenericInst):
            for a in ty.args:
                self.refs(a, out)
            if not _open_generic(ty):
                out.add(('inst', self._key(ty), ty))
            g = self.get(ty.name)
            out.add(('type', g.name, None))
        elif isinstance(ty, TypeRef) and ty.name not in SPECIAL:
            t = self.get(ty.name)
            out.add(('type', t.name, None))
            if isinstance(t, RuntimeClass):
                d = self.default_iface(t)
                if d is not None:
                    self.refs(d, out)

    def _key(self, inst):
        return self.mangle(inst)

    def type_refs(self, t):
        out = set()
        if isinstance(t, Struct):
            for f in t.fields:
                self.refs(f.type, out)
        if isinstance(t, (Interface, Delegate)):
            for m in t.methods:
                for p in m.params:
                    self.refs(p.type, out)
                if m.ret is not None:
                    self.refs(m.ret, out)
            # (and the generic instances it requires: IPropertySet's
            # IObservableMap<String, Object>)
            for ty, _ in t.interfaces:
                if isinstance(ty, GenericInst):
                    self.refs(ty, out)
        if isinstance(t, RuntimeClass):
            d = self.default_iface(t)
            if d is not None:
                self.refs(d, out)
            # (and the generic instances it implements, as MIDL makes them:
            # SetterBaseCollection's IVector<SetterBase*>, which XAML's
            # framework class derives from)
            for ty, _ in t.interfaces:
                if isinstance(ty, GenericInst):
                    self.refs(ty, out)
        return out

    # -- writing ---------------------------------------------------------------
    def write(self, outdir):
        os.makedirs(outdir, exist_ok=True)
        byns = {}
        for t in self.types.values():
            if isinstance(t, Contract) or not t.namespace:
                continue
            byns.setdefault(t.namespace, []).append(t)
        self.tail = self.idl_file_headers(outdir, byns)
        for ns in sorted(byns):
            self.write_ns(outdir, ns, sorted(byns[ns], key=lambda t: t.name))
        for path, items in getattr(self, 'classic', {}).items():
            self.write_classic(outdir, path, items)
        return sorted(byns)

    def idl_file_headers(self, outdir, byns):
        """MIDL names its header after the IDL file (microsoft.ui.xaml.h,
        microsoft.ui.xaml.controls.controls.h) and puts in it every
        namespace the file declares types in; code includes them by that
        name.  An IDL file's header includes those namespaces' headers and
        the headers of the IDL files it imports.
        Where the file's name is also a namespace's header or a classic COM
        header's (microsoft.ui.xaml.h), that header ends with the includes:
        they are returned as {header name: lines}."""
        tails = {}
        classic = {os.path.basename(p)[:-4].lower() + '.h' for p in getattr(self, 'classic', {})}
        file_ns, imports = getattr(self, 'file_ns', {}), getattr(self, 'imports', {})
        names = {os.path.basename(p)[:-4].lower() + '.h' for p in set(file_ns) | set(imports)} | \
                {ns.lower() + '.h' for ns in byns}     # an imported SDK IDL file's header is its namespace's
        for path in sorted(set(file_ns) | set(imports)):
            name = os.path.basename(path)[:-4].lower() + '.h'
            incs = ['#include "%s"' % i for i in imports.get(path, []) if i in names and i != name]
            incs += ['#include "%s.h"' % ns.lower() for ns in sorted(file_ns.get(path, ()))
                     if ns in byns and ns.lower() + '.h' != name]
            incs = list(dict.fromkeys(incs))
            if not incs:
                continue
            if name in classic or any(ns.lower() + '.h' == name for ns in byns):
                tails[name] = ['/* the rest of %s */' % os.path.basename(path)] + incs
            else:
                _write(os.path.join(outdir, name),
                       ['/* Generated by tools/winrt/abigen.py from %s: do not edit. */' % os.path.basename(path),
                        '#pragma once'] + incs)
        return tails

    def write_classic(self, outdir, path, items):
        """A classic COM IDL file's header, under the IDL file's name."""
        h = ['/* Generated by tools/winrt/abigen.py from %s: do not edit. */' % os.path.basename(path),
             '#pragma once', '#include <winrt_abi.h>']
        for inc in self.imports.get(path, []):
            h += ['#if __has_include(<%s>)' % inc, '#include <%s>' % inc, '#endif']
        used = set()
        defined = {x for k, x in items if k == 'iface'}
        for k, x in items:
            if k == 'iface':
                for r in self.types[x].methods:
                    used |= set(re.findall(r'\bI[A-Z]+[a-z]\w*',' '.join(r.raw)))
        for n in sorted(used - defined - {'IUnknown', 'IInspectable'}):
            h.append('struct %s;' % n)
        for k, x in items:
            h.append(x if k == 'quote' else self.iface_text(self.types[x]))
        name = os.path.basename(path)[:-4].lower() + '.h'
        _write(os.path.join(outdir, name), h + self.tail.get(name, []))

    def forward(self, t, lines):
        ns = cpp_ns(t.namespace)
        if isinstance(t, (Interface, Delegate)):
            if t.generics:
                decl = 'template <%s> struct %s;' % (
                    ', '.join('class ' + g for g in t.generics), self.iface_cpp_name(t))
            else:
                decl = 'struct %s;' % self.iface_cpp_name(t)
        elif isinstance(t, RuntimeClass):
            decl = 'class %s;' % t.short
        elif isinstance(t, Enum):
            decl = 'enum %s%s : %s;' % ('class ' if t.name in SCOPED_ENUMS else '', t.short,
                                        'unsigned int' if t.underlying == 'UInt32' else 'int')
        elif isinstance(t, Struct) and t.name == 'Windows.Foundation.EventRegistrationToken':
            # (the Win32 type itself, as the Windows SDK's windows.foundation.h
            # has it, so the ABI's name is no second, incomplete struct)
            decl = 'typedef ::EventRegistrationToken EventRegistrationToken;'
        elif isinstance(t, Struct):
            decl = 'struct %s;' % t.short
        else:
            return
        lines.append('%s %s %s' % (_open(ns), decl, '}' * (ns.count('::') + 1)))

    def write_ns(self, outdir, ns, types):
        lower = ns.lower()
        guard = re.sub(r'\W', '_', ns.upper())
        # values header: enums, structs, generic templates
        v = ['/* Generated by tools/winrt/abigen.py from Windows Runtime metadata: do not edit. */',
             '#pragma once', '#include <winrt_abi.h>']
        deps = set()
        fwd = set()
        for t in types:
            if isinstance(t, Struct):
                for f in t.fields:
                    r = set()
                    self.refs(f.type, r)
                    for k, n, _ in r:
                        if k == 'type':
                            o = self.get(n)
                            if o.namespace != ns and isinstance(o, (Struct, Enum)):
                                deps.add(o.namespace)
                            elif o.namespace != ns:
                                fwd.add(n)
            if isinstance(t, (Interface, Delegate)) and t.generics:
                for k, n, _ in self.type_refs(t):
                    if k == 'type' and self.get(n).namespace != ns:
                        fwd.add(n)
        for d in sorted(deps):
            v.append('#include "%s.values.h"' % d.lower())
        for n in sorted(fwd):
            self.forward(self.get(n), v)
        for t in types:
            if isinstance(t, (Interface, Delegate, RuntimeClass, Enum, Struct)):
                self.forward(t, v)
        body = []
        for t in self.sorted_structs(types):
            if isinstance(t, Enum):
                body.append(self.enum_text(t))
            elif isinstance(t, Struct) and t.name not in SPECIAL:
                body.append(self.struct_text(t))
        for t in types:
            if isinstance(t, (Interface, Delegate)) and t.generics:
                body.append(self.template_text(t))
        # flag operators go in the enum's namespace, as midl puts them, so
        # argument-dependent lookup finds them from code inside a namespace
        # that declares an operator| of its own (XAML's DirectUI)
        for t in types:
            if isinstance(t, Enum) and t.underlying == 'UInt32':
                body.append('DEFINE_ENUM_FLAG_OPERATORS(%s)' % t.short)
        if body:
            v.append(_open(cpp_ns(ns)))
            v.extend(body)
            v.append('}' * (cpp_ns(ns).count('::') + 1))
        _write(os.path.join(outdir, lower + '.values.h'), v)

        # main header
        # (each with the API contract version the Windows SDK's headers
        # declare, ahead of its guard as MIDL puts it, so a source that
        # undefines it to light up newer types gets it back from the next
        # header it includes: XAML's DXamlCoreLightup.cpp)
        h = ['/* Generated by tools/winrt/abigen.py from Windows Runtime metadata: do not edit. */',
             '#if !defined(WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION)',
             '#define WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION 0x130000',
             '#endif',
             '#pragma once', '#include <winrt_abi.h>', '#include "%s.values.h"' % lower]
        if ns == 'Windows.Foundation.Collections':
            # (the Windows SDK's own collections header says it has been
            # included so, which WRL-style collections check for)
            h.append('#define %s_H' % guard)
        refs = set()
        for t in types:
            if isinstance(t, (Interface, Delegate)) and t.generics:
                continue
            refs |= self.type_refs(t)
        extra = getattr(self, 'declared_by_ns', {}).get(ns, [])
        for inst in extra:
            self.refs(inst, refs)
        vals = sorted({self.get(n).namespace for k, n, _ in refs if k == 'type'} - {ns})
        for d in vals:
            h.append('#include "%s.values.h"' % d.lower())
        for n in sorted(n for k, n, _ in refs if k == 'type'):
            o = self.get(n)
            if o.namespace != ns:
                self.forward(o, h)
        # generic instances, innermost first, with the instances theirs
        # require and their methods name (IVector<T>'s IIterable<T>, whose
        # First gives an IIterator<T>), as MIDL makes them
        insts = self.close_instances([x for k, n, x in refs if k == 'inst'])
        for inst in self.order_instances(insts):
            h.append(self.instance_text(inst))
        h.append(_open(cpp_ns(ns)))
        for t in self.sorted_ifaces(types):
            h.append(self.iface_text(t))
        h.append('}' * (cpp_ns(ns).count('::') + 1))
        for t in self.sorted_ifaces(types):
            if not isinstance(t, Delegate) and not getattr(t, 'classic', False):
                h.append('extern const __declspec(selectany) wchar_t InterfaceName_%s_%s[] = L"%s";' % (
                    t.namespace.replace('.', '_'), self.iface_cpp_name(t), t.name))
        for t in types:
            if isinstance(t, RuntimeClass):
                cname = t.name.replace('.', '_')
                h.append('#ifndef RUNTIMECLASS_%s_DEFINED' % cname)
                h.append('#define RUNTIMECLASS_%s_DEFINED' % cname)
                h.append('extern const __declspec(selectany) wchar_t RuntimeClass_%s[] = L"%s";'
                         % (cname, t.name))
                h.append('#endif')
        _write(os.path.join(outdir, lower + '.h'), h + self.tail.get(lower + '.h', []))

    def sorted_structs(self, types):
        """Enums first, then structs after the structs they contain."""
        out = [t for t in types if isinstance(t, Enum)]
        structs = [t for t in types if isinstance(t, Struct)]
        done = set()

        def visit(s):
            if s.name in done:
                return
            done.add(s.name)
            for f in s.fields:
                if isinstance(f.type, TypeRef) and f.type.name in self.types:
                    o = self.types[f.type.name]
                    if isinstance(o, Struct) and o.namespace == s.namespace:
                        visit(o)
            out.append(s)
        for s in structs:
            visit(s)
        return out

    def sorted_ifaces(self, types):
        out = []
        done = set()
        bynames = {t.name: t for t in types}

        def visit(t):
            if t.name in done:
                return
            done.add(t.name)
            base = getattr(t, 'base', None)
            if getattr(t, 'classic', False) and base:
                b = bynames.get(t.namespace + '.' + base) or bynames.get(base)
                if b:
                    visit(b)
            out.append(t)
        for t in types:
            if isinstance(t, (Interface, Delegate)) and not t.generics:
                visit(t)
        return out

    def close_instances(self, insts):
        out = {self.mangle(i): i for i in insts}
        todo = list(insts)
        while todo:
            inst = todo.pop()
            g = self.get(inst.name)
            found = set()
            for ty in [t for t, _ in getattr(g, 'interfaces', [])] + \
                    [p.type for m in g.methods for p in m.params] + [m.ret for m in g.methods if m.ret is not None]:
                self.refs(_substitute(ty, inst.args), found)
            for k, _, x in found:
                if k == 'inst' and self.mangle(x) not in out:
                    out[self.mangle(x)] = x
                    todo.append(x)
        return list(out.values())

    def order_instances(self, insts):
        seen = {}

        def visit(i):
            k = self.mangle(i)
            if k in seen:
                return
            for a in i.args:
                if isinstance(a, GenericInst):
                    visit(a)
                elif isinstance(a, TypeRef) and a.name in self.types:
                    t = self.types[a.name]
                    if isinstance(t, RuntimeClass):
                        d = self.default_iface(t)
                        if isinstance(d, GenericInst):
                            visit(d)
            seen[k] = i
        for i in sorted(insts, key=self.mangle):
            visit(i)
        return list(seen.values())

    def enum_text(self, t):
        u = 'unsigned int' if t.underlying == 'UInt32' else 'int'
        scoped = t.name in SCOPED_ENUMS
        lines = ['enum %s%s : %s' % ('class ' if scoped else '', t.short, u), '{']
        for n, val in t.values:
            lines.append('    %s%s = %s,' % ('' if scoped else t.short + '_', n, ('%#x' % val) if u != 'int' else val))
        lines.append('};')
        return '\n'.join(lines)

    def struct_text(self, t):
        lines = ['struct %s' % t.short, '{']
        for f in t.fields:
            lines.append('    %s %s;' % (self.abi_cpp(f.type), f.name))
        lines.append('};')
        return '\n'.join(lines)

    def template_text(self, t):
        gen = t.generics
        cname = self.iface_cpp_name(t)
        base = 'IUnknown' if isinstance(t, Delegate) else 'IInspectable'
        tparams = ', '.join('class %s' % g for g in gen)
        lines = ['template <%s>' % tparams, 'struct %s_impl : %s' % (cname, base), '{', 'private:']
        for g in gen:
            lines.append('    typedef typename ABI::Windows::Foundation::Internal::GetAbiType<%s>::type %s_abi;' % (g, g))
            lines.append('    typedef typename ABI::Windows::Foundation::Internal::GetLogicalType<%s>::type %s_logical;' % (g, g))
        lines.append('public:')
        if len(gen) == 1:
            lines.append('    typedef %s T_complex;' % gen[0])
        else:
            for g in gen:
                lines.append('    typedef %s %s_complex;' % (g, g))
        for m in t.methods:
            # (the collections' bulk methods, added in Windows 8.1, have a
            # body in the Windows SDK's templates, so an implementation
            # written before them, XAML's TrackerCollection, still builds)
            body = '{ return E_NOTIMPL; }' if t.namespace == 'Windows.Foundation.Collections' and \
                m.overload() in ('GetMany', 'ReplaceAll') else '= 0;'
            lines.append('    virtual HRESULT STDMETHODCALLTYPE %s(%s) %s' % (
                m.overload(), self.params_cpp(m, gen), body))
        lines.append('};')
        lines.append('template <%s> struct %s;' % (tparams, cname))
        return '\n'.join(lines)

    def instance_text(self, inst):
        g = self.get(inst.name)
        m = self.mangle(inst)
        ns = cpp_ns(g.namespace)
        cname = self.iface_cpp_name(g)
        args = ', '.join(self.arg_cpp(a) for a in inst.args)
        aggs = ', '.join(self.agg_cpp(a) for a in inst.args)
        rc = '%s<%s>' % (strip_arity(g.name) + '`%d' % len(inst.args),
                         ', '.join(self.rc_name(a) for a in inst.args))
        lines = ['#ifndef DEF_%s_USE' % m, '#define DEF_%s_USE' % m, _open(ns),
                 'template <>',
                 'struct __declspec(uuid("%s")) %s<%s> : %s_impl<%s>' % (
                     self.generic_iid(inst), cname, args, cname, aggs),
                 '{',
                 '    static const wchar_t* z_get_rc_name_impl() { return L"%s"; }' % rc,
                 '};',
                 'typedef %s<%s> %s_t;' % (cname, args, m),
                 '}' * (ns.count('::') + 1),
                 '#define %s %s::%s_t' % (m, ns, m),
                 '#endif']
        return '\n'.join(lines)

    def rc_name(self, ty):
        if isinstance(ty, Prim):
            return ty.name
        if isinstance(ty, GenericInst):
            return '%s<%s>' % (ty.name, ', '.join(self.rc_name(a) for a in ty.args))
        return ty.name

    def iface_text(self, t):
        cname = self.iface_cpp_name(t)
        if getattr(t, 'classic', False):
            lines = ['struct __declspec(uuid("%s")) __declspec(novtable) %s : public %s' % (
                self.iid_for(t), cname, t.base), '{', 'public:']
            for m in t.methods:
                lines.append('    virtual %s STDMETHODCALLTYPE %s(%s) = 0;' % (m.raw_ret, m.name, ', '.join(m.raw)))
            lines.append('};')
            return '\n'.join(lines)
        base = 'IUnknown' if isinstance(t, Delegate) else 'IInspectable'
        lines = ['struct __declspec(uuid("%s")) __declspec(novtable) %s : public %s' % (
            self.iid_for(t), cname, base), '{', 'public:']
        for m in t.methods:
            lines.append('    virtual HRESULT STDMETHODCALLTYPE %s(%s) = 0;' % (
                m.overload(), self.params_cpp(m)))
        lines.append('};')
        return '\n'.join(lines)


_KEYWORDS = {'default', 'operator', 'delete', 'new', 'class', 'template', 'this', 'register',
             'union', 'struct', 'enum', 'namespace', 'interface', 'boolean'}


def _substitute(ty, args):
    """@ty with a generic's parameters given as @args"""
    if isinstance(ty, GenericVar):
        return args[ty.index]
    if isinstance(ty, GenericInst):
        return GenericInst(ty.name, [_substitute(a, args) for a in ty.args])
    if isinstance(ty, Array):
        a = copy.copy(ty)
        a.elem = _substitute(ty.elem, args)
        return a
    return ty


def _open_generic(ty):
    if isinstance(ty, GenericVar):
        return True
    if isinstance(ty, GenericInst):
        return any(_open_generic(a) for a in ty.args)
    if isinstance(ty, Array):
        return _open_generic(ty.elem)
    return False


def _ident(n):
    return n + '_' if n in _KEYWORDS else n


def _open(ns):
    return ' '.join('namespace %s {' % p for p in ns.split('::'))


# enums the Windows SDK declares in classic COM IDL (asyncinfo.idl) as a
# scoped enum, AsyncStatus::Completed, not MIDL's AsyncStatus_Completed
SCOPED_ENUMS = {'Windows.Foundation.AsyncStatus'}

def _write(path, lines):
    text = '\n'.join(lines) + '\n'
    if os.path.exists(path) and open(path).read() == text:
        return
    with open(path, 'w') as f:
        f.write(text)


SEARCH = ['Windows.Foundation.Collections', 'Windows.Foundation']


def resolve_names(ir, types):
    """Qualify the IDL's unqualified type names (IVector<...>, a class of
    the declaring namespace)."""
    def fix(ty):
        if ty is None:
            return
        if isinstance(ty, Array):
            fix(ty.elem)
            return
        if isinstance(ty, GenericInst):
            for a in ty.args:
                fix(a)
        if isinstance(ty, (TypeRef, GenericInst)) and ty.name not in types:
            ctx = getattr(ty, 'ctx_ns', None)
            for ns in ([ctx] if ctx else []) + SEARCH:
                if ns + '.' + ty.name in types:
                    ty.name = ns + '.' + ty.name
                    return
            if ty.name != 'void':
                raise KeyError('IDL type %s not found' % ty.name)
    allt = types
    for t in ir.types.values():
        for f in getattr(t, 'fields', []):
            fix(f.type)
        for m in getattr(t, 'methods', []):
            for p in m.params:
                fix(p.type)
            fix(m.ret)
        for it, _ in getattr(t, 'interfaces', []):
            fix(it)
        for b in getattr(t, 'pending_bases', []):
            fix(b)
        for g in getattr(t, 'groups', {}).values():
            for m in g.methods:
                for p in m.params:
                    fix(p.type)
                fix(m.ret)
        for _, bg in getattr(t, 'blocks', []):
            for g in bg.values():
                for m in g.methods:
                    for p in m.params:
                        fix(p.type)
                    fix(m.ret)
    kept = []
    for ns, inst in ir.declared:
        try:
            fix(inst)
            kept.append((ns, inst))
        except KeyError:                  # an instance of a type a feature removed
            pass
    ir.declared = kept


def load(winmds, idls, iid_winmds, disabled, keep_disabled_types=True):
    r = winmdmod.Reader()
    for w in winmds:
        r.load(w)
    types = dict(r.types)
    ir = idlmod.IdlReader(disabled_features=disabled, keep_disabled_types=keep_disabled_types)
    for f in idls:
        ir.load(f)
    for n, t in ir.types.items():
        types[n] = t                              # IDL wins over metadata

    resolve_names(ir, types)
    idlmod.finish_classes(ir.types, lambda n: ir.types.get(n) or types.get(n))
    types.update(ir.types)
    iids = {}
    for w in iid_winmds:
        rr = winmdmod.Reader()
        rr.load(w)
        for n, t in rr.types.items():
            if isinstance(t, (Interface, Delegate)) and t.guid:
                iids[n] = t.guid
    # the namespaces' headers carry the IDL's `declare` instances too
    return types, iids, ir.declared, ir.classic_out, ir.imports, ir.file_ns


TOP = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WINMD = os.path.join(TOP, 'third_party', 'winmd')
XAML_IDL = os.path.join(TOP, 'third_party', 'microsoft-ui-xaml', 'dxaml', 'xcp', 'dxaml', 'idl')
# NovaOS's own IDL: the generic instances the Windows SDK's Foundation IDL
# declares, and the Windows App SDK types the XAML sources use that the
# published metadata predates
NOVA_OWN_IDL = [os.path.join(TOP, 'userland', 'windows_ui_xaml', 'idl', f)
                for f in ('windows.foundation.declares.idl', 'microsoft.ui.lifted.idl')]
# NovaOS's inputs: Windows' metadata, the lifted Microsoft.UI types XAML
# names, and the XAML framework's IDL (third_party/*/NOVA-VENDOR.txt)
NOVA_WINMD = ['Windows', 'Microsoft.UI', 'Microsoft.UI.Text', 'Microsoft.Graphics',
              'Microsoft.Foundation', 'Microsoft.Windows.ApplicationModel.Resources']
NOVA_IIDS = ['Microsoft.UI.Xaml']
NOVA_IDL = ['winrt/core/microsoft.ui.xaml.coretypes.idl', 'winrt/core/microsoft.ui.xaml.coretypes2.idl',
            'winrt/controls/microsoft.ui.xaml.controls.controls.idl',
            'winrt/controls/microsoft.ui.xaml.controls.controls2.idl',
            'winrt/main/microsoft.ui.xaml.idl', 'winrt/main/microsoft.ui.xaml.private.idl',
            'com/microsoft.ui.xaml.compositiontarget-private.idl',
            'com/microsoft.ui.xaml.hosting.referencetracker.idl',
            'com/microsoft.ui.xaml.media.dxinterop.idl', 'com/microsoft.ui.xaml.window.idl',
            'com/microsoft.ui.xaml.xamlroot.idl',
            '../../components/xamlDiagnostics/idl/XamlOM.WinUI.idl',
            '../../components/xamlDiagnostics/idl/XamlOM.WinUI.Private.idl']


def nova_disabled():
    """The IDL features to leave out: those FeatureFlags.h (release
    branch) marks AlwaysDisabled, and Feature_HeaderPlacement, which the
    published metadata leaves out too."""
    text = open(os.path.join(TOP, 'third_party', 'microsoft-ui-xaml', 'dxaml', 'xcp', 'inc',
                             'FeatureFlags.h')).read()
    text = text.split('#else', 2)
    rel = text[0].split('#ifdef MUX_PRERELEASE')[0] + text[1].split('#endif')[0] + text[1].split('#endif', 1)[1]
    off = set(re.findall(r'feature_name\s+(\w+)\s*=\s*\{\s*AlwaysDisabled', rel))
    return sorted(off | {'Feature_HeaderPlacement'})


def nova_inputs():
    return ([os.path.join(WINMD, n + '.winmd') for n in NOVA_WINMD],
            [os.path.join(XAML_IDL, f) for f in NOVA_IDL] + NOVA_OWN_IDL,
            [os.path.join(WINMD, n + '.winmd') for n in NOVA_IIDS], nova_disabled())


def check():
    """Compare the interfaces and classes the XAML IDL gives with the
    published Microsoft.UI.Xaml.winmd: the same names, member order and
    signatures (what makes the generated headers binary-compatible)."""
    winmds, idls, iids, disabled = nova_inputs()
    ref = winmdmod.Reader()
    ref.load(iids[0])
    types, _, _, _, _, _ = load(winmds, idls, [], disabled, keep_disabled_types=False)

    def sig(t):
        if t is None:
            return 'void'
        return repr(t) + ('&' if getattr(t, 'byref', False) else '')
    ok = bad = 0
    for n, t in sorted(types.items()):
        r = ref.types.get(n)
        if r is None or not n.startswith('Microsoft.UI.Xaml'):
            continue
        if isinstance(t, Interface):
            a = [(m.overload(), [(sig(p.type), p.out) for p in m.params], sig(m.ret)) for m in t.methods]
            b = [(m.overload(), [(sig(p.type), p.out) for p in m.params], sig(m.ret)) for m in r.methods]
            same = a == b and type(t) is type(r)
        elif isinstance(t, RuntimeClass):
            ia = sorted(repr(i) for i, _ in t.interfaces if '`' not in repr(i))
            ib = sorted(repr(i) for i, _ in r.interfaces if '`' not in repr(i))
            same = ia == ib and t.base == r.base
        elif isinstance(t, Enum):                  # newer sources may add values
            same = set(r.values) <= set(t.values)
        elif isinstance(t, Struct):
            same = [(f.name, repr(f.type)) for f in t.fields] == [(f.name, repr(f.type)) for f in r.fields]
        else:
            same = True
        if same:
            ok += 1
        else:
            bad += 1
            print('abigen --check: %s differs from the published metadata' % n)
    print('abigen --check: %d types match Microsoft.UI.Xaml.winmd, %d differ' % (ok, bad))
    return bad == 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out', nargs='?')
    ap.add_argument('--winmd', action='append', default=[])
    ap.add_argument('--idl', action='append', default=[])
    ap.add_argument('--iids', action='append', default=[])
    ap.add_argument('--disable', action='append', default=[])
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args(argv)
    if a.check:
        return 0 if check() else 1
    if not a.out:
        ap.error('OUT_DIR is needed')
    if not a.winmd and not a.idl:                # NovaOS's own inputs
        a.winmd, a.idl, a.iids, a.disable = nova_inputs()
    types, iids, declared, classic, imports, file_ns = load(a.winmd, a.idl, a.iids, a.disable)
    g = Generator(types, iids)
    g.classic = classic
    g.imports = imports
    g.file_ns = file_ns
    g.declared_by_ns = {}
    for ns, inst in declared:
        g.declared_by_ns.setdefault(ns, []).append(inst)
    nss = g.write(a.out)
    print('abigen: %d types, %d namespaces -> %s' % (len(types), len(nss), a.out))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
