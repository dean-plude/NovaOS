/*
 * strmap.h — a Windows Runtime map with string keys, as the Windows
 * Runtime's own string-keyed collections are (Windows.Foundation
 * .Collections' StringMap and PropertySet): IObservableMap<String, V>,
 * IMap<String, V>, IMapView<String, V> and IIterable<IKeyValuePair<String,
 * V>>, V a string or (a property set's) an object; and a read-only list of
 * strings (IVectorView<String>).  Compiled into the DLLs
 * that hand one out ("sources" in their dll.json).
 */
#ifndef NOVA_STRMAP_H
#define NOVA_STRMAP_H
#include <windows.h>
#include <roapi.h>

/* a new empty map (an IInspectable: the map's IObservableMap face asked
 * of it gives the rest): @objects, values are objects (IInspectable*) and
 * the map is a PropertySet (IPropertySet); else strings (a StringMap) */
HRESULT nova_strmap_new(BOOL objects, void **out);
/* (a string map's) @key set to @value, without raising MapChanged */
HRESULT nova_strmap_put(void *map, const WCHAR *key, const WCHAR *value);
/* a read-only list of copies of @items (strvec.c): its IVectorView<String>
 * (IIterable<String> asked of it) */
HRESULT nova_strvec_new(const WCHAR *const *items, UINT32 count, void **out);

#endif
