/* NovaOS: the Windows SDK's rometadataresolution.h, which MinGW-w64 lacks:
 * the Windows Runtime's type resolution (api-ms-win-ro-typeresolution,
 * in wintypes.dll), as Microsoft documents it.  XAML's reflection splits a
 * generic type name into its parts with RoParseTypeName
 * (core/metadata/ReflectionAPI.cpp). */
#pragma once
#include <windows.h>
#include <winstring.h>
#include <cor.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the .winmd file that defines type @name, its metadata and the type's token */
HRESULT WINAPI RoGetMetaDataFile(HSTRING name, IMetaDataDispenserEx *metaDataDispenser, HSTRING *metaDataFilePath,
                                 IMetaDataImport2 **metaDataImport, mdTypeDef *typeDefToken);
/* "A`1<B, C>" -> "A`1", "B", "C" (an array of HSTRINGs the caller frees) */
HRESULT WINAPI RoParseTypeName(HSTRING typeName, DWORD *partsCount, HSTRING **typeNameParts);
/* the .winmd files and sub-namespaces of namespace @name */
HRESULT WINAPI RoResolveNamespace(HSTRING name, HSTRING windowsMetaDataDir, DWORD packageGraphDirsCount,
                                  const HSTRING *packageGraphDirs, DWORD *metaDataFilePathsCount,
                                  HSTRING **metaDataFilePaths, DWORD *subNamespacesCount, HSTRING **subNamespaces);
HRESULT WINAPI RoIsApiContractPresent(PCWSTR name, UINT16 majorVersion, UINT16 minorVersion, BOOL *present);
HRESULT WINAPI RoIsApiContractMajorVersionPresent(PCWSTR name, UINT16 majorVersion, BOOL *present);

#ifdef __cplusplus
}
#endif
