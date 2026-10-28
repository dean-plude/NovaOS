#!/usr/bin/env python3
"""Writes the Windows.UI.Xaml faces of NovaOS's XAML framework.

    tools/winrt/winfaces.py OUT.cpp [--report]

NovaOS's XAML framework is Microsoft's WinUI 3 source (dxaml), whose
classes and interfaces are the lifted Microsoft.UI.Xaml ones.  The system
XAML a UWP app and WinUI 2 are compiled against is Windows.UI.Xaml: the
same classes, with the names, interface IDs and interface split
(IUIElement, IUIElement2 ... IUIElement10) Windows' own metadata gives
them.  This writes, from that metadata (third_party/winmd/Windows.winmd)
and the framework's IDL (as tools/winrt/abigen.py reads them), a table
of faces:

  forward faces  a Windows.UI.Xaml interface over the framework's: a
                 framework object asked for IUIElement5 answers with a
                 tear-off whose methods call the same methods of the lifted
                 IUIElement (userland/windows_ui_xaml/src/windows_faces.cpp
                 holds the tear-offs, the query hook and activation by the
                 Windows names)
  reverse faces  a lifted interface over a Windows one: what the framework
                 calls on an app's object (a control's overrides, an event
                 handler, an ICommand, a collection)

Each method of a face is a thunk that converts what it is handed between
the two sets of types (an interface pointer is asked for the other
world's interface, a Windows-boxed enum or struct is boxed again as the
framework's) and calls the other interface's method by its vtable slot.
Methods are matched by name; one whose types have no counterpart (a
Windows.UI.Composition visual where the framework takes
Microsoft.UI.Composition's) answers E_NOTIMPL, naming itself once.
--report prints what is matched.  See docs/design/windows-ui-xaml.md.
"""
import re
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abigen
from model import (Enum, Struct, Interface, Delegate, RuntimeClass, TypeRef,
                   GenericInst, GenericVar, Prim, Array)

WIN = 'Windows.UI.Xaml.'
MUX = 'Microsoft.UI.Xaml.'
# (the compositor's objects, dcomp.dll's, answer both namespaces' IIDs)
COMP_WIN = 'Windows.UI.Composition.'
COMP_MUX = 'Microsoft.UI.Composition.'
PRIM_C = {'Boolean': 'BYTE', 'Char16': 'WCHAR', 'Int8': 'INT8', 'UInt8': 'BYTE', 'Int16': 'INT16',
          'UInt16': 'UINT16', 'Int32': 'INT32', 'UInt32': 'UINT32', 'Int64': 'INT64', 'UInt64': 'UINT64',
          'Single': 'FLOAT', 'Double': 'DOUBLE', 'Guid': 'GUID', 'IntPtr': 'INT_PTR', 'UIntPtr': 'UINT_PTR'}
TYPENAME = 'Windows.UI.Xaml.Interop.TypeName'
SPECIAL_C = {'Windows.Foundation.HResult': 'INT32', 'Windows.Foundation.EventRegistrationToken': 'INT64'}


# Windows.UI.Xaml events of a UWP app's lifetime, which the framework
# builds without (its Feature_UwpSupportApi is off) and NovaOS never raises
# (it suspends no process, and a desktop thread's Application is never
# suspended on Windows either): WinUI 2's AnimatedVisualPlayer, for one,
# pauses on Suspending
NEVER_RAISED = {WIN + 'IApplication': ('Suspending', 'Resuming'),
                WIN + 'IApplication2': ('LeavingBackground', 'EnteredBackground')}


def attr_args(t, name):
    return [a.args for a in t.attrs if a.name.endswith(name)]


class Faces:
    def __init__(self, types, iids):
        self.types = types
        self.g = abigen.Generator(types, iids)
        self.guids = {}                 # iid -> index
        self.structs = {}               # struct name -> C name
        self.struct_order = []
        self.thunks = []                # C text
        self.faces = []                 # (impl iid, target iid, vtable name, flags, name)
        self.done = set()               # impl names faced
        self.queue = []                 # generic instance pairs to face
        self.stats = {'matched': 0, 'missing': 0}
        self.missing = []
        self.boxes = []                 # (windows IReference iid, lifted IReference iid, C type)
        self.delegate_boxes = []        # (windows IReference iid, lifted IReference iid, lifted delegate iid)
        self.exclusive = {}
        for t in types.values():
            if isinstance(t, Interface):
                ex = getattr(t, 'exclusive_to', None) or next((a[0] for a in attr_args(t, 'ExclusiveToAttribute')), None)
                if ex:
                    self.exclusive.setdefault(ex, []).append(t)

    # -- the two worlds ---------------------------------------------------------
    def lifted_name(self, n):
        if n.startswith(WIN):
            c = MUX + n[len(WIN):]
            if c in self.types:
                return c
        return n

    def lift(self, ty):
        if isinstance(ty, Array):
            a = Array(self.lift(ty.elem))
            a.byref = getattr(ty, 'byref', False)
            return a
        if isinstance(ty, GenericInst):
            return GenericInst(ty.name, [self.lift(a) for a in ty.args])
        if isinstance(ty, TypeRef):
            return TypeRef(self.lifted_name(ty.name))
        return ty

    def key(self, ty):
        if ty is None:
            return 'void'
        if isinstance(ty, Array):
            return self.key(ty.elem) + '[]'
        if isinstance(ty, GenericInst):
            return '%s<%s>' % (ty.name, ','.join(self.key(a) for a in ty.args))
        return getattr(ty, 'name', repr(ty))

    def is_windows(self, ty):
        """does @ty name a Windows.UI.Xaml type the framework has under its
        lifted name"""
        if isinstance(ty, GenericInst):
            return any(self.is_windows(a) for a in ty.args)
        if isinstance(ty, Array):
            return self.is_windows(ty.elem)
        return isinstance(ty, TypeRef) and self.lifted_name(ty.name) != ty.name

    # -- C spellings ------------------------------------------------------------
    def guid(self, iid):
        iid = str(uuid.UUID(iid))
        if iid not in self.guids:
            self.guids[iid] = len(self.guids)
        return 'G[%d]' % self.guids[iid]

    def struct_c(self, t):
        # (one C struct for a Windows struct and the framework's: the
        # framework's name)
        canon = self.lifted_name(t.name)
        if canon != t.name:
            t = self.types[canon]
        if t.name not in self.structs:
            fields = []
            for f in t.fields:
                k = self.kind(f.type)
                if k is None or k[0] not in ('val', 'string'):
                    raise ValueError(t.name)
                fields.append('%s %s;' % (k[1] if k[0] == 'val' else 'void*', f.name))
            c = 'S_' + t.name.replace('.', '_')
            self.structs[t.name] = c
            self.struct_order.append('struct %s { %s };' % (c, ' '.join(fields)))
        return self.structs[t.name]

    def kind(self, ty):
        """('val', C type), ('string',), ('object',), ('ref', iid),
        ('array', element kind) or None"""
        try:
            if isinstance(ty, Array):
                k = self.kind(ty.elem)
                return ('array', k) if k and k[0] in ('val', 'string', 'object') else None
            if isinstance(ty, Prim):
                if ty.name == 'String':
                    return ('string',)
                if ty.name == 'Object':
                    return ('object',)
                return ('val', PRIM_C[ty.name])
            if isinstance(ty, GenericInst):
                return ('ref', self.g.generic_iid(ty))
            if not isinstance(ty, TypeRef):
                return None
            if ty.name in SPECIAL_C:
                return ('val', SPECIAL_C[ty.name])
            t = self.types.get(ty.name)
            if isinstance(t, Enum):
                return ('val', 'UINT32' if t.underlying == 'UInt32' else 'INT32')
            if isinstance(t, Struct):
                return ('val', self.struct_c(t))
            if isinstance(t, (Interface, Delegate)):
                return ('ref', self.g.iid_for(t))
            if isinstance(t, RuntimeClass):
                d = t.default_interface()
                return ('object',) if d is None else self.kind(d)
        except (KeyError, ValueError):
            return None
        return None

    def cparams(self, k, out):
        """the C parameter types of a parameter of kind @k"""
        base = 'void*' if k[0] in ('string', 'object', 'ref') else k[1] if k[0] == 'val' else None
        if k[0] == 'array':
            el = 'void*' if k[1][0] != 'val' else k[1][1]
            return ['UINT32*', el + '**'] if out == 'receive' else ['UINT32', el + '*']
        return [base + '*'] if out else [base]

    # -- signatures ---------------------------------------------------------------
    def params(self, m, args=None):
        """a method's parameters as (type, direction): 'in', 'out', 'fill'
        (an array the caller allocates) or 'receive'"""
        out = []
        for p in m.params:
            ty = abigen._substitute(p.type, args) if args else p.type
            if isinstance(ty, Array):
                out.append((ty, 'receive' if getattr(ty, 'byref', False) else 'fill' if p.out else 'in'))
            else:
                out.append((ty, 'out' if p.out or getattr(ty, 'byref', False) else 'in'))
        if m.ret is not None:
            ty = abigen._substitute(m.ret, args) if args else m.ret
            out.append((ty, 'receive' if isinstance(ty, Array) else 'out'))
        return out

    def compatible(self, a, b):
        if len(a) != len(b):
            return False
        for (ta, da), (tb, db) in zip(a, b):
            if da != db:
                return False
            if self.key(self.lift(ta)) == self.key(self.lift(tb)):
                continue
            if self.same_composition(ta, tb):
                continue
            ka, kb = self.kind(ta), self.kind(tb)
            if ka and kb and {ka[0], kb[0]} == {'ref', 'object'} and da in ('in', 'out'):
                continue
            return False
        return True

    def same_composition(self, a, b):
        """a Windows.UI.Composition class or interface where the framework
        has Microsoft.UI.Composition's of the same name: one object answers
        both (dcomp.dll), so the thunk asks it for the other's interface"""
        if not (isinstance(a, TypeRef) and isinstance(b, TypeRef)):
            return False
        n = {a.name, b.name}
        w = [x for x in n if x.startswith(COMP_WIN)]
        m = [x for x in n if x.startswith(COMP_MUX)]
        return (len(w) == 1 and len(m) == 1 and w[0][len(COMP_WIN):] == m[0][len(COMP_MUX):]
                and all(self.kind(TypeRef(x)) and self.kind(TypeRef(x))[0] == 'ref' for x in n))

    # -- thunks -------------------------------------------------------------------
    def thunk(self, name, impl, target, slot, tgt_iid, to_framework_in, queue_side, optional=False):
        """a thunk implementing @impl's parameters by calling @target's at
        vtable @slot of the interface @tgt_iid; @to_framework_in: whether
        what goes in flows from Windows' types to the framework's (a forward
        face) and what comes out the other way; @optional: an interface the
        object may lack, a method of a later version of it"""
        cp, args, pre, post, clean, zero = ['NovaTear* t'], ['p'], [], [], [], []
        outer, inner = False, None  # (a composable factory's outer object and inner out)
        tcp = ['void*']
        for i, ((ti, d), (tt, _)) in enumerate(zip(impl, target)):
            ki, kt = self.kind(ti), self.kind(tt)
            if ki is None or kt is None:
                return None
            a = 'a%d' % i
            ic = self.cparams(ki, d if d != 'in' else None) if d != 'fill' else self.cparams(ki, None)
            tc = self.cparams(kt, d if d != 'in' else None) if d != 'fill' else self.cparams(kt, None)
            if d == 'out':
                zero += [a + 'n', a] if len(ic) == 2 else [a]
            if len(ic) == 2:
                cp += ['%s %sn' % (ic[0], a), '%s %s' % (ic[1], a)]
                tcp += tc
                args += [a + 'n', a]
                continue
            cp.append('%s %s' % (ic[0], a))
            tcp.append(tc[0])
            if ki[0] == 'ref' and kt[0] == 'ref' and ki[1] == kt[1]:
                args.append(a)
            elif ki[0] in ('ref', 'object') and kt[0] in ('ref', 'object'):
                for x in (ti, tt):
                    # (a class whose default interface is a generic one, as
                    # Grid.RowDefinitions' RowDefinitionCollection is an
                    # IVector<RowDefinition>: that interface's face)
                    c = self.types.get(x.name) if isinstance(x, TypeRef) else None
                    if isinstance(c, RuntimeClass) and isinstance(c.default_interface(), GenericInst):
                        x = c.default_interface()
                    if isinstance(x, GenericInst) and self.is_windows(x):
                        self.queue.append(x)
                if d == 'in':
                    if kt[0] == 'ref':
                        pre.append('void* c%d = 0; if (SUCCEEDED(hr)) hr = nova_cross(%s, &%s, &c%d);'
                                   % (i, a, self.guid(kt[1]), i))
                    elif to_framework_in:
                        pre.append('void* c%d = 0; if (SUCCEEDED(hr)) hr = nova_object_in(%s, &c%d);' % (i, a, i))
                        outer = outer or ki[0] == 'object'
                    elif ki[0] == 'object':
                        # (an object the framework hands the app's code: a
                        # box of one of its Windows-named enums or structs
                        # as Windows boxes it)
                        pre.append('void* c%d = 0; if (SUCCEEDED(hr)) hr = nova_object_to_app(%s, &c%d);' % (i, a, i))
                    else:
                        pre.append('void* c%d = %s; nova_addref(c%d);' % (i, a, i))
                    args.append('c%d' % i)
                    clean.append('nova_release(c%d);' % i)
                else:
                    pre.append('if (%s) *%s = 0; void* o%d = 0;' % (a, a, i))
                    args.append('&o%d' % i)
                    if ki[0] == 'ref' and inner is not None:
                        # (a composable class's instance, made over an app's
                        # outer object: crossed through the inner object it
                        # was handed with, as the outer cannot answer for
                        # the class's interfaces until it holds the inner;
                        # .NET Native's outer does not yet while
                        # CreateInstance runs)
                        post.append('hr = nova_out_inner(hr, o%d, &%s, %s, %s ? *%s : 0);'
                                    % (i, self.guid(ki[1]), a, inner, inner))
                    elif ki[0] == 'ref':
                        post.append('hr = nova_out(hr, o%d, &%s, %s);' % (i, self.guid(ki[1]), a))
                    elif not to_framework_in:
                        post.append('hr = nova_object_out(hr, o%d, %s);' % (i, a))
                    else:
                        post.append('hr = nova_out(hr, o%d, 0, %s);' % (i, a))
                        if outer and to_framework_in and ki[0] == 'object':
                            inner = '((void**)%s)' % a
            elif self.key(ti) == TYPENAME and d in ('in', 'out'):
                # (a type's name: Windows.UI.Xaml.Controls.Control as the
                # framework's Microsoft.UI.Xaml.Controls.Control, and back)
                if d == 'in':
                    pre.append('%s c%d = %s; void* h%d = 0; nova_typename(&c%d, &h%d, %d);'
                               % (kt[1], i, a, i, i, i, 1 if to_framework_in else 0))
                    args.append('c%d' % i)
                    clean.append('WindowsDeleteString((HSTRING)h%d);' % i)
                else:
                    args.append(a)
                    post.append('if (SUCCEEDED(hr)) nova_typename_out(%s, %d);' % (a, 0 if to_framework_in else 1))
            elif ki[0] == 'object' and kt[0] == 'object' and d == 'in' and to_framework_in:
                pre.append('void* c%d = 0; if (SUCCEEDED(hr)) hr = nova_object_in(%s, &c%d);' % (i, a, i))
                args.append('c%d' % i)
                clean.append('nova_release(c%d);' % i)
            elif ki[0] == 'object' and kt[0] == 'object' and d == 'out' and not to_framework_in:
                pre.append('if (%s) *%s = 0; void* o%d = 0;' % (a, a, i))
                args.append('&o%d' % i)
                post.append('hr = nova_object_out(hr, o%d, %s);' % (i, a))
            else:
                args.append(a)
        iid, cls = tgt_iid if isinstance(tgt_iid, tuple) else (tgt_iid, None)
        get = 'nova_target(t, &%s, &p)' % self.guid(iid) if cls is None else \
            'nova_target_static(t, L"%s", &%s, &p)' % (cls, self.guid(iid))
        body = ['static HRESULT STDMETHODCALLTYPE %s(%s)' % (name, ', '.join(cp)), '{',
                '    void* p; HRESULT hr = %s;' % get]
        if optional:
            # (an object without the later interface: what it would answer is
            # nothing, zeroed outputs)
            body.append('    if (hr == E_NOINTERFACE) { %s return S_OK; }'
                        % ' '.join('if (%s) __builtin_memset(%s, 0, sizeof(*%s));' % (z, z, z) for z in zero))
        body.append('    if (FAILED(hr)) return hr;')
        body += ['    ' + x for x in pre]
        body.append('    if (SUCCEEDED(hr)) hr = ((HRESULT (STDMETHODCALLTYPE*)(%s))(*(void***)p)[%d])(%s);'
                    % (', '.join(tcp), slot, ', '.join(args)))
        body += ['    ' + x for x in post + clean]
        body += ['    nova_untarget(t, p);', '    return hr;', '}']
        self.thunks.append('\n'.join(body))
        return name

    def missing_thunk(self, name, what):
        iface, _, method = what.rpartition('.')
        if iface in NEVER_RAISED and method.startswith(('add_', 'remove_')) and method[method.index('_') + 1:] in NEVER_RAISED[iface]:
            # (an event the framework has no counterpart of because NovaOS
            # never raises it: the handler is kept until it is removed)
            self.thunks.append('static HRESULT STDMETHODCALLTYPE %s(void*, IUnknown* h, INT64* token) '
                               '{ return nova_face_event_kept(h, token); }' % name if method.startswith('add_') else
                               'static HRESULT STDMETHODCALLTYPE %s(void*, INT64 token) '
                               '{ return nova_face_event_removed(token); }' % name)
            return name
        self.thunks.append('static HRESULT STDMETHODCALLTYPE %s() { return nova_face_missing("%s"); }' % (name, what))
        self.missing.append(what)
        return name

    def face(self, impl_name, impl_iid, delegate, methods, flags, default_target):
        """a face: @methods are (impl method name, impl params, target or
        None) with target (params, slot, interface iid[, 'optional'])"""
        n = len(self.faces)
        slots = ['nova_tear_qi', 'nova_tear_addref', 'nova_tear_release']
        if not delegate:
            slots += ['nova_tear_iids', 'nova_tear_name', 'nova_tear_trust']
        to_fw = 'reverse' not in flags
        for j, (mname, ip, tgt) in enumerate(methods):
            fname = 'f%d_%d' % (n, j)
            r = None
            if tgt is not None:
                r = self.thunk(fname, ip, tgt[0], tgt[1], tgt[2], to_fw, None, len(tgt) > 3)
            if r is None:
                r = self.missing_thunk(fname, '%s.%s' % (impl_name, mname))
                self.stats['missing'] += 1
            else:
                self.stats['matched'] += 1
            slots.append(r)
        self.thunks.append('static void* const V%d[] = { %s };' % (n, ', '.join('(void*)&' + s for s in slots)))
        self.faces.append((impl_iid, default_target, 'V%d' % n, flags, impl_name))

    def base_slots(self, t):
        return 3 if isinstance(t, Delegate) or t.base == 'IUnknown' else 6

    # -- the faces ----------------------------------------------------------------
    def pair_face(self, win, lif, args_w=None, args_l=None, name=None):
        """forward and reverse faces of a Windows interface or delegate and
        its lifted counterpart with the same methods (a non-exclusive
        interface, a delegate, a generic instance)"""
        name = name or win.name
        iw = self.g.iid_for(win) if args_w is None else self.g.generic_iid(GenericInst(win.name, args_w))
        il = self.g.iid_for(lif) if args_l is None else self.g.generic_iid(GenericInst(lif.name, args_l))
        if iw == il or name in self.done:
            return
        self.done.add(name)
        # (by ABI name: an overload's, IXamlMetadataProvider's
        # GetXamlTypeByFullName, is its Overload attribute's in Windows'
        # metadata, its method_name in the framework's IDL)
        lm = {m.overload(): (k, m) for k, m in enumerate(lif.methods)}
        wm = {m.overload(): (k, m) for k, m in enumerate(win.methods)}
        delegate = isinstance(win, Delegate)
        for impl, other, ia, oa, ii, oi, flags, label in ((win, lm, args_w, args_l, iw, il, (), name),
                                                           (lif, wm, args_l, args_w, il, iw, ('reverse',),
                                                            name + ' (framework)')):
            methods = []
            for m in impl.methods:
                ip = self.params(m, ia)
                tgt = None
                if m.overload() in other:
                    k, om = other[m.overload()]
                    tp = self.params(om, oa)
                    if self.compatible(ip, tp):
                        tgt = (tp, self.base_slots(lif if 'reverse' not in flags else win) + k, oi)
                if tgt is None and 'reverse' in flags and args_w is None:
                    tgt = self.sibling_target(win, m, ip)
                methods.append((m.overload(), ip, tgt))
            self.face(label, ii, delegate, methods,
                      flags + (('delegate',) if delegate else ()), oi)

    def sibling_target(self, win, m, ip):
        """a lifted method that Windows moved to a later version of the
        interface (IXamlType's BoxedType is IXamlType2's): the target is
        that version's method, the thunk asking the object for it and, if
        the object has only the first version, answering nothing"""
        for v in range(2, 10):
            w = self.types.get('%s%d' % (win.name, v))
            if not isinstance(w, Interface) or w.generics:
                continue
            for k, om in enumerate(w.methods):
                if om.overload() == m.overload():
                    tp = self.params(om, None)
                    if self.compatible(ip, tp):
                        return (tp, self.base_slots(w) + k, self.g.iid_for(w), 'optional')
        return None

    def class_faces(self, wc):
        """the faces of a Windows class's exclusive interfaces over its
        framework class's"""
        lc = self.types.get(self.lifted_name(wc.name))
        if not isinstance(lc, RuntimeClass) or lc is wc:
            return
        w_static = {str(a[0]) for n in ('StaticAttribute', 'ActivatableAttribute', 'ComposableAttribute')
                    for a in attr_args(wc, n) if a and isinstance(a[0], str)}
        # (Windows' metadata as read keeps no attributes on a class's
        # interfaces: its overridable ones are known by their names)
        w_over = {repr(i) for i, at in wc.interfaces if any(a.name.endswith('OverridableAttribute') for a in at)
                  or re.search(r'Overrides\d*$', repr(i))}
        l_inst = {repr(i) for i, _ in lc.interfaces}
        l_over = {repr(i) for i, at in lc.interfaces if any(a.name.endswith('OverridableAttribute') for a in at)}
        lifted = [t for t in self.exclusive.get(lc.name, []) if t.base != 'IUnknown']
        # (and the framework's base classes' interfaces: WinUI 3 moved
        # Control's IsTabStop, TabIndex and focus to UIElement; a base
        # class's statics are its own factory's)
        bases = []
        b = self.types.get(lc.base) if lc.base else None
        while isinstance(b, RuntimeClass):
            bases.append(b)
            b = self.types.get(b.base) if b.base else None
        for w in self.exclusive.get(wc.name, []):
            if w.name in self.done or isinstance(w, Delegate) or w.generics:
                continue
            self.done.add(w.name)
            static = w.name in w_static
            over = w.name in w_over
            cands = [t for t in lifted if (t.name not in l_inst) == static and (t.name in l_over) == over]
            if not static and not over:
                cands += [self.types[n] for n in l_inst - l_over
                          if isinstance(self.types.get(n), Interface) and self.types[n] not in cands
                          and not getattr(self.types[n], 'generics', None)]
            owner = {}
            if not over:
                for bc in bases:
                    b_inst = {repr(i) for i, _ in bc.interfaces}
                    b_over = {repr(i) for i, at in bc.interfaces
                              if any(a.name.endswith('OverridableAttribute') for a in at)}
                    for t in self.exclusive.get(bc.name, []):
                        if t.base != 'IUnknown' and (t.name not in b_inst) == static and t.name not in b_over:
                            cands.append(t)
                            if static:
                                owner[t.name] = bc.name
            prim = self.primary(w, cands)
            if prim is None and not w.methods and not static and not over:
                # (an interface with no methods whose framework class has
                # no counterpart of it, IUIElementWeakCollection, where
                # the framework's class is only its collection: a face of
                # nothing over the class's default interface, so the
                # object can be handed out as the Windows class)
                k = self.kind(TypeRef(lc.name))
                if k and k[0] == 'ref':
                    self.face(w.name, self.g.iid_for(w), False, [], (), k[1])
                continue
            if prim is None:
                continue
            if over:
                cands = [prim]
            methods = []
            for m in w.methods:
                ip = self.params(m)
                tgt = None
                for c in [prim] + [x for x in cands if x is not prim]:
                    for k, lm in enumerate(c.methods):
                        if lm.name == m.name and self.compatible(ip, self.params(lm)):
                            iid = self.g.iid_for(c)
                            tgt = (self.params(lm), self.base_slots(c) + k,
                                   (iid, owner[c.name]) if c.name in owner else iid)
                            break
                    if tgt:
                        break
                methods.append((m.name, ip, tgt))
            # (an interface with no methods, IButton, is still asked for)
            if not methods or any(t for _, _, t in methods):
                self.face(w.name, self.g.iid_for(w), False, methods, (), self.g.iid_for(prim))
        # the framework's overridable interfaces over the Windows class's
        # (what the framework calls on an app's class composed over its own)
        for lo in [self.types[n] for n in sorted(l_over) if isinstance(self.types.get(n), Interface)]:
            if lo.name in self.done:
                continue
            self.done.add(lo.name)
            wos = [self.types[n] for n in sorted(w_over) if isinstance(self.types.get(n), Interface)]
            methods = []
            for m in lo.methods:
                ip = self.params(m)
                tgt = None
                for wo in wos:
                    for k, wm in enumerate(wo.methods):
                        if wm.name == m.name and self.compatible(ip, self.params(wm)):
                            tgt = (self.params(wm), 6 + k, self.g.iid_for(wo))
                    if tgt:
                        break
                methods.append((m.name, ip, tgt))
            if wos and any(t for _, _, t in methods):
                self.face(lo.name + ' (overrides)', self.g.iid_for(lo), False, methods, ('reverse', 'override'),
                          self.g.iid_for(wos[0]))

    def primary(self, w, cands):
        """the framework interface a Windows one mostly is: the same name
        less its version number (IUIElement5: IUIElement), else the one
        holding most of its methods"""
        short = w.short.rstrip('0123456789')
        for suffix in ('Statics', 'Factory', 'Overrides', 'Protected'):
            if short.endswith(suffix):
                short = short[:-len(suffix)].rstrip('0123456789') + suffix
        want = {self.lifted_name(w.namespace + '.' + short), MUX + (w.namespace + '.' + short)[len(WIN):]}
        for c in cands:
            if c.name in want:
                return c
        names = {m.name for m in w.methods}
        best = max(cands, key=lambda c: len(names & {m.name for m in c.methods}), default=None)
        if best is not None and names & {m.name for m in best.methods}:
            return best
        return None

    def generic_faces(self):
        while self.queue:
            gw = self.queue.pop()
            gl = self.lift(gw)
            d = self.types.get(gw.name)
            if not isinstance(d, Interface):
                continue
            self.pair_face(d, d, gw.args, gl.args, self.key(gw))

    def box_faces(self):
        """IReference<T> of each Windows.UI.Xaml enum and struct the
        framework has: faces, and the boxes nova_object_in() boxes again"""
        for n, t in sorted(self.types.items()):
            if n.startswith(WIN) and isinstance(t, (Enum, Struct)) and self.lifted_name(n) != n:
                gw = GenericInst('Windows.Foundation.IReference`1', [TypeRef(n)])
                gl = self.lift(gw)
                k = self.kind(TypeRef(n))
                if k is None:
                    continue
                self.queue.append(gw)
                self.boxes.append((self.g.generic_iid(gw), self.g.generic_iid(gl), k[1], n))
        # (and of each Windows.UI.Xaml delegate the framework has: a handler an
        # app's code boxes, UIElement.AddHandler's, boxed again as the
        # framework's, over the framework's face of the delegate)
        for n, t in sorted(self.types.items()):
            lt = self.types.get(self.lifted_name(n))
            if n.startswith(WIN) and isinstance(t, Delegate) and not t.generics and isinstance(lt, Delegate) \
                    and lt is not t:
                gw = GenericInst('Windows.Foundation.IReference`1', [TypeRef(n)])
                self.delegate_boxes.append((self.g.generic_iid(gw), self.g.generic_iid(self.lift(gw)),
                                            self.g.iid_for(lt), n))

    def run(self):
        for n, t in sorted(self.types.items()):
            if n.startswith(WIN) and isinstance(t, RuntimeClass):
                self.class_faces(t)
        for n, t in sorted(self.types.items()):
            if not n.startswith(WIN) or not isinstance(t, Interface) or t.generics or n in self.done:
                continue
            if getattr(t, 'exclusive_to', None) or attr_args(t, 'ExclusiveToAttribute'):
                continue
            lt = self.types.get(self.lifted_name(n))
            if isinstance(lt, Interface) and lt is not t and type(lt) is type(t):
                self.pair_face(t, lt)
        self.box_faces()
        self.generic_faces()

    def write(self, path):
        faces = sorted(self.faces, key=lambda f: uuid.UUID(f[0]).bytes_le)
        body = list(self.thunks)
        body.append('extern "C" const NovaFace nova_faces[] = {')
        for impl, tgt, v, flags, name in faces:
            fl = ' | '.join('NOVA_FACE_' + f.upper() for f in flags) or '0'
            body.append('    {&%s, &%s, %s, %s, "%s"},' % (self.guid(impl), self.guid(tgt), v, fl, name))
        body.append('};')
        body.append('extern "C" const unsigned nova_face_count = %d;' % len(faces))
        body.append('extern "C" const NovaBox nova_boxes[] = {')
        for w, l, c, n in self.boxes:
            body.append('    {&%s, &%s, sizeof(%s), "%s", L"Windows.Foundation.IReference`1<%s>"},'
                        % (self.guid(w), self.guid(l), c, n, self.lifted_name(n)))
        body.append('};')
        body.append('extern "C" const unsigned nova_box_count = %d;' % len(self.boxes))
        body.append('extern "C" const NovaDelegateBox nova_delegate_boxes[] = {')
        for w, l, d, n in self.delegate_boxes:
            body.append('    {&%s, &%s, &%s, L"Windows.Foundation.IReference`1<%s>"},'
                        % (self.guid(w), self.guid(l), self.guid(d), self.lifted_name(n)))
        body.append('};')
        body.append('extern "C" const unsigned nova_delegate_box_count = %d;' % len(self.delegate_boxes))
        # (the framework's type names that are Windows.UI.Xaml ones, sorted:
        # the type names nova_typename() renames)
        names = sorted(n[len(MUX):] for n, t in self.types.items()
                       if n.startswith(MUX) and isinstance(t, (RuntimeClass, Interface, Enum, Struct))
                       and not getattr(t, 'generics', None) and (WIN + n[len(MUX):]) in self.types)
        body.append('extern "C" const wchar_t *const nova_xaml_names[] = {')
        body += ['    L"%s",' % n for n in names]
        body.append('};')
        body.append('extern "C" const unsigned nova_xaml_name_count = %d;' % len(names))
        lines = ['// Generated by tools/winrt/winfaces.py: the Windows.UI.Xaml faces of the',
                 "// XAML framework (Windows' metadata over the framework's IDL).  Do not edit.",
                 '#include "windows_faces.h"', '']
        lines += self.struct_order
        lines.append('static const GUID G[] = {')
        for iid, _ in sorted(self.guids.items(), key=lambda x: x[1]):
            b = uuid.UUID(iid).bytes
            lines.append('    {0x%s, 0x%s, 0x%s, {%s}},' % (b[0:4].hex(), b[4:6].hex(), b[6:8].hex(),
                                                            ', '.join('0x%02x' % x for x in b[8:])))
        lines.append('};')
        text = '\n'.join(lines + body) + '\n'
        if not os.path.exists(path) or open(path).read() != text:
            with open(path, 'w') as f:
                f.write(text)


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    winmds, idls, iids, disabled = abigen.nova_inputs()
    types, iidmap, _, _, _, _ = abigen.load(winmds, idls, iids, disabled)
    f = Faces(types, iidmap)
    f.run()
    f.write(argv[0])
    print('winfaces: %d faces, %d methods matched, %d not (E_NOTIMPL), %d boxes, %d delegate boxes -> %s'
          % (len(f.faces), f.stats['matched'], f.stats['missing'], len(f.boxes), len(f.delegate_boxes), argv[0]))
    if '--report' in argv:
        for m in f.missing:
            print('  not matched: ' + m)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
