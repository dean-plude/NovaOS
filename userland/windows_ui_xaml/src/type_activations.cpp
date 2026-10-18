// NovaOS: the type table's constructors (c_aTypeActivations), which the
// XAML core generates with every type's (core/metadata
// DynamicMetadata.g.cpp, with the element tree), empty for programs that
// test the libraries without the core: the object writer creates nothing.
// A program that creates objects (xamlloadtest) defines its own; this one
// is alone in its object file so it is linked only when a program has none.
#include <TypeTableStructsPrivate.h>

extern const MetaDataTypeActivation c_aTypeActivations[KnownTypeCount] = {};
