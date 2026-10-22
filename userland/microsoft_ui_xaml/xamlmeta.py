# XamlMetadataProviderGenerated.cpp for Microsoft.UI.Xaml.dll 2.x: the
# table of XAML types WinUI 2's metadata provider (dll/XamlMetadataProvider)
# answers the XAML parser from, written from the model of WinUI 2's types as
# Microsoft's T4 template writes it from the .winmd
# (third_party/winui2/dev/dll/XamlMetadataProviderGenerated.tt, with
# CommonHelpers.tt and WinRtType.tt, MIT).  The template reads the metadata
# through .NET's reflection, so it names types as .NET projects them
# (Int32, Nullable`1<Double>, the collections as IList and IReadOnlyList);
# PROJECTED gives those names.  For each public type (not an automation
# peer, event arguments or a delegate) it registers an XamlType: its base
# type, an activator for a class with a default constructor, and its
# properties (dependency properties through its statics, the others with a
# getter and setter); each type a property or base names gets a primitive
# entry, or a bare one.  build.py calls generate().
from model import Enum, Struct, Interface, Delegate, RuntimeClass, Contract, TypeRef, GenericInst, Prim

DP = 'Windows.UI.Xaml.DependencyProperty'

# .NET's names for the Windows Runtime's fundamental and projected types
PRIM_NET = {'Boolean': 'Boolean', 'Char16': 'Char', 'Int8': 'SByte', 'UInt8': 'Byte', 'Int16': 'Int16',
            'UInt16': 'UInt16', 'Int32': 'Int32', 'UInt32': 'UInt32', 'Int64': 'Int64', 'UInt64': 'UInt64',
            'Single': 'Single', 'Double': 'Double', 'String': 'String', 'Object': 'Object', 'Guid': 'Guid'}
NET_PRIMITIVE = {'Boolean', 'Char', 'SByte', 'Byte', 'Int16', 'UInt16', 'Int32', 'UInt32', 'Int64', 'UInt64',
                 'Single', 'Double'}
PROJECTED = {  # Windows Runtime name -> (.NET's metadata name, C++/WinRT name)
    'Windows.Foundation.TimeSpan': ('TimeSpan', 'winrt::Windows::Foundation::TimeSpan'),
    'Windows.Foundation.DateTime': ('DateTimeOffset', 'winrt::Windows::Foundation::DateTime'),
    'Windows.Foundation.Uri': ('Uri', 'winrt::Windows::Foundation::Uri'),
    'Windows.Foundation.HResult': ('Exception', 'winrt::hresult'),
    'Windows.Foundation.IClosable': ('IDisposable', 'winrt::Windows::Foundation::IClosable'),
    'Windows.UI.Xaml.Interop.TypeName': ('Type', 'winrt::Windows::UI::Xaml::Interop::TypeName'),
    'Windows.UI.Xaml.Input.ICommand': ('Windows.UI.Xaml.Input.ICommand', 'winrt::Windows::UI::Xaml::Input::ICommand'),
    'Windows.UI.Xaml.Interop.IBindableVector': ('System.Collections.IList',
                                                'winrt::Windows::UI::Xaml::Interop::IBindableVector'),
    'Windows.UI.Xaml.Interop.IBindableIterable': ('System.Collections.IEnumerable',
                                                  'winrt::Windows::UI::Xaml::Interop::IBindableIterable'),
    'Windows.UI.Xaml.Data.INotifyPropertyChanged': ('System.ComponentModel.INotifyPropertyChanged',
                                                    'winrt::Windows::UI::Xaml::Data::INotifyPropertyChanged'),
    'Windows.Foundation.Numerics.Vector2': ('System.Numerics.Vector2', 'winrt::Windows::Foundation::Numerics::float2'),
    'Windows.Foundation.Numerics.Vector3': ('System.Numerics.Vector3', 'winrt::Windows::Foundation::Numerics::float3'),
    'Windows.Foundation.Numerics.Vector4': ('System.Numerics.Vector4', 'winrt::Windows::Foundation::Numerics::float4'),
    'Windows.Foundation.Numerics.Matrix3x2': ('System.Numerics.Matrix3x2',
                                              'winrt::Windows::Foundation::Numerics::float3x2'),
    'Windows.Foundation.Numerics.Matrix4x4': ('System.Numerics.Matrix4x4',
                                              'winrt::Windows::Foundation::Numerics::float4x4'),
}
GENERIC_NET = {  # generic Windows Runtime interface -> .NET's metadata name (WinRtType's mapping back)
    'Windows.Foundation.IReference`1': 'Nullable`1',
    'Windows.Foundation.Collections.IVector`1': 'Windows.Foundation.Collections.IVector`1',
    'Windows.Foundation.Collections.IObservableVector`1': 'Windows.Foundation.Collections.IObservableVector`1',
    'Windows.Foundation.Collections.IVectorView`1': 'Windows.Foundation.Collections.IReadOnlyList`1',
    'Windows.Foundation.Collections.IIterable`1': 'Windows.Foundation.Collections.IEnumerable`1',
    'Windows.Foundation.Collections.IMap`2': 'Windows.Foundation.Collections.IMap`2',
    'Windows.Foundation.Collections.IMapView`2': 'Windows.Foundation.Collections.IReadOnlyDictionary`2',
}
PRIM_CPP = {'Single': 'float', 'Int32': 'int', 'Boolean': 'bool'}


class Gen:
    def __init__(self, types, own):
        self.types = types
        self.out = []
        self.indent = 0
        self.discovered = {}            # metadata name -> type
        self.registered = {}
        self.own = own

    # -- names --------------------------------------------------------------------------
    def meta(self, ty):
        """the metadata name WinRtType gives a type"""
        if ty is None:
            return 'Object'
        if isinstance(ty, Prim):
            return PRIM_NET[ty.name]
        if isinstance(ty, GenericInst):
            return '%s<%s>' % (GENERIC_NET.get(ty.name, ty.name), ','.join(self.meta(a) for a in ty.args))
        return PROJECTED.get(ty.name, (ty.name,))[0]

    def cpp(self, ty):
        """the C++/WinRT name WinRtType gives a type"""
        if isinstance(ty, Prim):
            return {'String': 'winrt::hstring', 'Object': 'winrt::IInspectable', 'Guid': 'winrt::guid'}.get(
                ty.name, PRIM_NET[ty.name].lower() if ty.name not in PRIM_CPP else PRIM_CPP[ty.name])
        if isinstance(ty, GenericInst):
            return 'winrt::%s<%s>' % (ty.name.split('`')[0].replace('.', '::'), ', '.join(self.cpp(a) for a in ty.args))
        if ty.name in PROJECTED:
            return PROJECTED[ty.name][1]
        return 'winrt::' + ty.name.replace('.', '::')

    def kind(self, ty):
        if isinstance(ty, Prim):
            if PRIM_NET[ty.name] in NET_PRIMITIVE:
                return 'primitive'
            return {'String': 'string', 'Object': 'object', 'Guid': 'value'}[ty.name]
        if isinstance(ty, GenericInst):
            return 'nullable' if ty.name == 'Windows.Foundation.IReference`1' else 'class'
        t = self.types.get(ty.name)
        if isinstance(t, Enum):
            return 'enum'
        if isinstance(t, Struct) or ty.name in ('Windows.Foundation.TimeSpan', 'Windows.Foundation.DateTime'):
            return 'value'
        return 'class'

    def system(self, ty):
        """WinRtType.IsSystemType: a .NET primitive, a nullable, a string or a Windows.UI.Xaml type"""
        k = self.kind(ty)
        return k in ('primitive', 'nullable', 'string') or (not isinstance(ty, Prim) and
                                                            self.meta(ty).startswith('Windows.UI.Xaml'))

    def short(self, ty):
        return self.meta(ty).rsplit('.', 1)[-1] if not isinstance(ty, GenericInst) else ty.name

    # -- the types' members ---------------------------------------------------------------
    def class_ifaces(self, rc, kinds=('instance',)):
        synth = dict((n, k) for k, n in getattr(rc, 'synth', []))
        out = []
        for it, attrs in rc.interfaces:
            if any(a.name.endswith(('OverridableAttribute', 'ProtectedAttribute')) for a in attrs):
                continue
            k = synth.get(it.name, 'instance')
            if k in kinds:
                out.append(it)
        return out

    def props(self, iface_names):
        """[(name, type, can_write)] of the properties of interfaces @iface_names, in order"""
        out, seen = [], {}
        for n in iface_names:
            it = self.types.get(n.name if hasattr(n, 'name') else n)
            if not isinstance(it, Interface):
                continue
            for m in it.methods:
                if m.name.startswith('get_') and not m.params:
                    seen[m.name[4:]] = len(out)
                    out.append([m.name[4:], m.ret, False])
                elif m.name.startswith('put_') and m.name[4:] in seen:
                    out[seen[m.name[4:]]][2] = True
        return out

    def instance_props(self, t):
        if isinstance(t, RuntimeClass):
            return self.props(self.class_ifaces(t))
        return []

    def static_ifaces(self, t):
        return sorted((n for k, n in getattr(t, 'synth', []) if k == 'static'), key=lambda n: n.rsplit('.', 1)[-1])

    def find_property(self, t, name):
        """.NET's Type.GetProperty(name): the type's public property of that name, its bases' too"""
        while isinstance(t, RuntimeClass):
            for p in self.instance_props(t):
                if p[0] == name:
                    return p[1]
            t = self.types.get(t.base) if t.base else None
        return None

    def find_getter(self, t, name):
        """a Get<name> method the class declares (an attached property's getter), its type"""
        ifaces = [n for k, n in getattr(t, 'synth', []) if k in ('static', 'instance')]
        for n in ifaces:
            it = self.types.get(n)
            for m in getattr(it, 'methods', []):
                if m.name == 'Get' + name:
                    return m.ret
        return None

    def has_default_ctor(self, t):
        if not isinstance(t, RuntimeClass):
            return False
        if getattr(t, 'activatable_default', False):
            return True
        for k, n in getattr(t, 'synth', []):
            if k == 'factory' and not t.sealed:
                for m in self.types[n].methods:
                    if [p.name for p in m.params] == ['baseInterface', 'innerInterface']:
                        return True
        return False

    def content_property(self, t):
        for a in t.attrs:
            if a.name == 'contentproperty' and a.args:
                return a.args[0]
        return ''

    def collection(self, t, generic):
        """the type arguments of IVector (or IMap) the class implements itself"""
        for it, _ in getattr(t, 'interfaces', []):
            if isinstance(it, GenericInst) and it.name == generic:
                return it.args
        return None

    # -- writing ---------------------------------------------------------------------------
    def w(self, text=''):
        self.out.append('    ' * self.indent + text if text else '')

    def discover(self, ty):
        name = self.meta(ty)
        if name not in self.discovered:
            self.discovered[name] = ty

    def register(self, ty, explore):
        t = self.types.get(ty.name) if not isinstance(ty, (Prim, GenericInst)) else None
        name = self.meta(ty)
        self.w('XamlMetadataProvider::RegisterXamlType(')
        self.indent += 1
        self.w('/* Arg1 TypeName */ ')
        self.w('L"%s",' % name)
        self.w('/* Arg2 CreateXamlTypeCallback */ ')
        if isinstance(t, Enum):
            self.w('[]()')
            self.w('{')
            self.indent += 1
            self.w('auto xamlType = winrt::make<EnumXamlType>(')
            self.indent += 1
            self.w('/* Arg 1 - TypeName */ ')
            self.w('(PCWSTR)L"%s",' % name)
            self.w('/* Arg 2 - CreateFromString func */ ')
            self.w('(std::function<winrt::IInspectable(hstring)>)[](hstring fromString)')
            self.w('{')
            self.indent += 1
            for v, _ in t.values:
                self.w('if (fromString == L"%s") return box_value(winrt::%s::%s);' % (v, t.short, v))
            self.w('throw winrt::hresult_invalid_argument();')
            self.indent -= 1
            self.w('});')
            self.indent -= 1
            self.w('')
            self.w('return xamlType;')
            self.indent -= 1
            self.w('});')
            self.indent -= 1
            self.w('')
            return
        self.w('[]()')
        self.w('{')
        self.indent += 1
        if explore:
            self.w('auto xamlType = winrt::make_self<XamlType>(')
            self.indent += 1
            self.w('/* Arg 1 - TypeName */ ')
            self.w('(PCWSTR)L"%s",' % name)
            self.w('/* Arg 2 - BaseTypeName */ ')
            base = self.base_of(t)
            if base is not None:
                self.discover(base)
                self.w('(PCWSTR)L"%s",' % self.meta(base))
            else:
                self.w('(PCWSTR)L"",')
            self.w('/* Arg 3 - Activator func */ ')
            if self.has_default_ctor(t):
                if t.sealed:
                    self.w('(std::function<winrt::IInspectable()>)[]()'
                           '{ return XamlMetadataProviderGenerated::ActivateInstance(L"%s"); },' % t.name)
                else:
                    self.w('(std::function<winrt::IInspectable()>)[]()'
                           '{ return XamlMetadataProviderGenerated::ActivateInstanceWithFactory<winrt::I%sFactory>(L"%s"); },'
                           % (t.short, t.name))
            else:
                self.w('nullptr,')
            self.w('/* Arg 4 - Populate properties func */ ')
            self.populate(t)
            self.indent -= 1
            self.w(');')
            self.w('')
        else:
            if isinstance(t, Struct) or self.kind(ty) == 'value':
                basename = 'ValueType'
            else:
                basename = 'Object'
            self.w('auto xamlType = winrt::make_self<XamlType>((PCWSTR)L"%s", (PCWSTR)L"%s" /* BaseTypeName */ , '
                   'nullptr /* Activator Func */, nullptr /* PopulatePropertiesFunc */ );' % (name, basename))
        items = self.collection(t, 'Windows.Foundation.Collections.IVector`1') if t is not None else None
        pairs = self.collection(t, 'Windows.Foundation.Collections.IMap`2') if t is not None else None
        if items:
            self.w('xamlType->SetCollectionAddFunc((std::function<void(winrt::IInspectable const&, winrt::IInspectable const&)>)'
                   '[](winrt::IInspectable const& collection, winrt::IInspectable const& value)')
            self.w('{')
            self.w('    collection.as<%s>().Append(unbox_value<%s>(value));' % (self.cpp(ty), self.cpp(items[0])))
            self.w('});')
            self.w('')
        elif pairs:
            self.w('xamlType->SetAddToMapFunc((std::function<void(winrt::IInspectable const&, winrt::IInspectable const&, '
                   'winrt::IInspectable const&)>)[](winrt::IInspectable const& map, winrt::IInspectable const& key, '
                   'winrt::IInspectable const& value)')
            self.w('{')
            self.w('    map.as<%s>().Insert(unbox_value<%s>(key), unbox_value<%s>(value));'
                   % (self.cpp(ty), self.cpp(pairs[0]), self.cpp(pairs[1])))
            self.w('});')
            self.w('')
        self.w('return static_cast<winrt::IXamlType>(*xamlType);')
        self.indent -= 1
        self.w('});')
        self.indent -= 1
        self.w('')

    def base_of(self, t):
        """the base type .NET gives: a class's base class (or Object), a struct's ValueType, an interface none"""
        if isinstance(t, RuntimeClass):
            return TypeRef(t.base) if t.base else Prim('Object')
        if isinstance(t, Struct):
            return TypeRef('System.ValueType')
        return None

    def populate(self, t):
        statics = self.static_ifaces(t) if isinstance(t, RuntimeClass) else []
        sprops = [p for n in statics for p in self.props([n])]
        iprops = self.instance_props(t)
        if isinstance(t, Interface) or not (sprops or iprops):
            self.w('nullptr')
            return
        content = self.content_property(t)
        dps = []
        self.w('(std::function<void(XamlTypeBase&)>)[](XamlTypeBase& xamlType)')
        self.w('{')
        self.indent += 1
        for i, n in enumerate(statics):
            suffix = str(i + 1) if i else ''
            short = n.rsplit('.', 1)[-1]
            self.w('winrt::%s statics%s = XamlMetadataProviderGenerated::GetFactory<winrt::%s>(L"%s");'
                   % (short, suffix, short, t.name))
            self.w('{')
            self.indent += 1
            for pname, ptype, _ in self.props([n]):
                if isinstance(ptype, TypeRef) and ptype.name == DP:
                    name = pname.replace('Property', '')
                    dps.append(name)
                    ty = self.find_property(t, name)
                    is_content = ty is not None and name == content
                    if ty is None:
                        ty = self.find_getter(t, name)
                    tname = self.meta(ty) if ty is not None else 'Object'
                    if tname not in self.discovered:
                        self.discovered[tname] = ty
                    self.w('xamlType.AddDPMember(L"%s", L"%s", statics%s.%s(), %s /* isContent */);'
                           % (name, tname, suffix, pname, 'true' if is_content else 'false'))
                else:
                    self.w('// Ignoring static non-dependency property %s()' % pname)
            self.indent -= 1
            self.w('}')
            self.w('')
            self.w('')
        for pname, ptype, can_write in iprops:
            if pname in dps:
                continue
            self.discover(ptype)
            self.w('xamlType.AddMember(')
            self.w('    L"%s", /* propertyName */' % pname)
            self.w('    L"%s", /* propertyType */' % self.meta(ptype))
            self.w('    [](winrt::IInspectable instance) { return %s; },' % self.get_snippet(t, pname, ptype))
            if can_write:
                self.w('    [](winrt::IInspectable instance, winrt::IInspectable value) { %s; },'
                       % self.set_snippet(t, pname, ptype))
            else:
                self.w('    nullptr, /* setter */')
            self.w('    %s, /* isContent */' % ('true' if content == pname else 'false'))
            self.w('    false, /* isDependencyProperty */')
            self.w('    false /* isAttachable */);')
        self.indent -= 1
        self.w('}')

    def get_snippet(self, t, pname, ty):
        k = self.kind(ty)
        call = 'instance.as<winrt::%s>().%s()' % (t.short, pname)
        if k == 'enum':
            return 'box_value<int>((int)%s)' % call
        if k == 'nullable':
            return call
        if k in ('primitive', 'value', 'string'):
            return 'box_value(%s)' % call
        return call

    def set_snippet(self, t, pname, ty):
        k = self.kind(ty)
        target = 'instance.as<winrt::%s>().%s' % (t.short, pname)
        if k == 'primitive':
            n = PRIM_NET[ty.name]
            return '%s(unbox_value<%s>(value))' % (target, PRIM_CPP.get(ty.name, n.lower()))
        if k == 'enum':
            return '%s(unbox_value<winrt::%s>(value))' % (target, self.short(ty))
        if k == 'nullable':
            inner = ty.args[0]
            name = PRIM_CPP.get(inner.name, PRIM_NET[inner.name].lower()) if isinstance(inner, Prim) else \
                'winrt::' + {'Vector2': 'float2', 'Vector3': 'float3', 'Vector4': 'float4'}.get(self.short(inner), self.short(inner))
            return '%s(value.as<winrt::IReference<%s>>())' % (target, name)
        if k == 'value':
            name = {'Vector2': 'float2', 'Vector3': 'float3', 'Vector4': 'float4'}.get(self.short(ty), self.short(ty))
            return '%s(unbox_value<winrt::%s>(value))' % (target, name)
        if k == 'string':
            return '%s(unbox_value<winrt::hstring>(value))' % target
        if k == 'object':
            return '%s(value)' % target
        return '%s(value.as<%s>())' % (target, self.cpp(ty))

    def generate(self):
        self.w('#include "pch.h"')
        self.w('#include "common.h"')
        self.w('')
        self.w('')
        self.w('#include "XamlMetadataProvider.h"')
        self.w('#include "XamlMetadataProviderGenerated.h"')
        self.w('')
        self.w('')
        self.w('#ifndef MUX_EXPERIMENTAL')
        self.w('#include "XamlControlsResources.h"')
        self.w('#else')
        self.w('#include "ExperimentalXamlControlsResources.h"')
        self.w('#endif')
        self.w('')
        self.w('//////////////////////////////////////////////////////////////')
        self.w('/// WARNING: Generated File: Please do not modify manually ///')
        self.w('//////////////////////////////////////////////////////////////')
        self.w('// (by userland/microsoft_ui_xaml/xamlmeta.py, as XamlMetadataProviderGenerated.tt writes it)')
        self.w('void XamlMetadataProviderGenerated::RegisterTypes()')
        self.w('{')
        self.indent += 1
        for n in self.own:
            t = self.types[n]
            if n.startswith('Microsoft.UI.Private') or isinstance(t, (Delegate, Contract)) or \
                    getattr(t, 'exclusive_to', None) or getattr(t, 'classic', False):
                continue
            if t.short.endswith(('AutomationPeer', 'EventArgs')):
                continue
            self.registered[self.meta(TypeRef(n))] = t
            self.register(TypeRef(n), True)
        self.w('// Register types encountered ')
        for name in sorted(self.discovered):
            if name.startswith('Nullable`1') or name in self.registered:
                continue
            ty = self.discovered[name]
            if ty is None or self.system(ty):
                self.w('XamlMetadataProvider::RegisterXamlType(')
                self.w('    L"%s",' % name)
                self.w('    []() { return winrt::make<PrimitiveXamlType>((PCWSTR)L"%s"); });' % name)
            else:
                self.register(ty, False)
        self.indent -= 1
        self.w('}')
        self.w('')
        return '\n'.join(self.out) + '\n'


def generate(types, own):
    return Gen(types, own).generate()
