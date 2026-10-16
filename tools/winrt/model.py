"""The type model shared by the .winmd reader, the IDL reader and the ABI
header writer."""
import copy


class TypeBase:
    byref = False
    const = False

    def copy(self):
        return copy.copy(self)


class Prim(TypeBase):
    """A fundamental type: Boolean, Char16, (U)Int8..64, Single, Double,
    String, Object, Guid."""
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return self.name


class TypeRef(TypeBase):
    """A named type (enum, struct, interface, delegate or class)."""
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return self.name


class GenericInst(TypeBase):
    """An instance of a generic interface or delegate (name has its `n)."""
    def __init__(self, name, args):
        self.name = name
        self.args = args

    def __repr__(self):
        return '%s<%s>' % (self.name, ', '.join(map(repr, self.args)))


class GenericVar(TypeBase):
    def __init__(self, index):
        self.index = index

    def __repr__(self):
        return '!%d' % self.index


class Array(TypeBase):
    def __init__(self, elem):
        self.elem = elem

    def __repr__(self):
        return '%r[]' % self.elem


class Attribute:
    def __init__(self, name, args):
        self.name = name
        self.args = args

    def __repr__(self):
        return '[%s(%s)]' % (self.name, self.args)


class Param:
    def __init__(self, name, type, out):
        self.name = name
        self.type = type
        self.out = out


class Method:
    def __init__(self, name, params, ret, retname='result'):
        self.name = name
        self.params = params
        self.ret = ret
        self.retname = retname
        self.attrs = []
        self.special = False
        self.ctor = False
        self.static = False

    def overload(self):
        for a in self.attrs:
            if a.name == 'Windows.Foundation.Metadata.OverloadAttribute':
                return a.args[0]
        return self.name


class Field:
    def __init__(self, name, type):
        self.name = name
        self.type = type


class Type:
    kind = '?'

    def __init__(self, name):
        self.name = name
        self.attrs = []
        self.cpp_quote = []

    @property
    def namespace(self):
        return self.name.rsplit('.', 1)[0] if '.' in self.name else ''

    @property
    def short(self):
        return self.name.rsplit('.', 1)[-1]

    def attr(self, name):
        for a in self.attrs:
            if a.name == name or a.name.endswith('.' + name):
                return a
        return None


class Enum(Type):
    kind = 'enum'

    def __init__(self, name):
        super().__init__(name)
        self.values = []
        self.underlying = 'Int32'


class Struct(Type):
    kind = 'struct'

    def __init__(self, name):
        super().__init__(name)
        self.fields = []


class Interface(Type):
    kind = 'interface'

    def __init__(self, name, guid=None, generics=()):
        super().__init__(name)
        self.guid = guid
        self.generics = list(generics)
        self.methods = []
        self.interfaces = []          # (TypeBase, [Attribute])
        self.base = 'IInspectable'    # COM interfaces from IDL may name IUnknown


class Delegate(Interface):
    kind = 'delegate'


class RuntimeClass(Type):
    kind = 'class'

    def __init__(self, name):
        super().__init__(name)
        self.base = None
        self.sealed = True
        self.static = False
        self.methods = []
        self.interfaces = []

    def default_interface(self):
        for it, attrs in self.interfaces:
            if any(a.name.endswith('DefaultAttribute') for a in attrs):
                return it
        return None


class Contract(Type):
    kind = 'contract'
