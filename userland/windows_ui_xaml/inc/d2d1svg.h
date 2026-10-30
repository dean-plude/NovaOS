/* NovaOS: the Windows SDK's d2d1svg.h, as far as XAML names it.
 * SVG documents are Direct2D 1.4 (ID2D1DeviceContext5), which NovaOS's
 * d2d1 does not have (d2d1_3.h).  XAML's SvgImageSource holds them in
 * ComPtrs, and the imaging component's EncodedImageData reads an SVG
 * document's root size (when a device context 5 makes one), so the document
 * and element interfaces carry the SDK's methods in vtable order (the
 * attribute types they take are named only); the glyph style's join when
 * the code that calls them builds. */
#pragma once
#include <d2d1_3.h>

typedef enum D2D1_SVG_LENGTH_UNITS
{
    D2D1_SVG_LENGTH_UNITS_NUMBER = 0,
    D2D1_SVG_LENGTH_UNITS_PERCENTAGE = 1,
    D2D1_SVG_LENGTH_UNITS_FORCE_DWORD = 0xffffffff
} D2D1_SVG_LENGTH_UNITS;

typedef enum D2D1_SVG_ATTRIBUTE_STRING_TYPE
{
    D2D1_SVG_ATTRIBUTE_STRING_TYPE_SVG = 0,
    D2D1_SVG_ATTRIBUTE_STRING_TYPE_ID = 1,
    D2D1_SVG_ATTRIBUTE_STRING_TYPE_FORCE_DWORD = 0xffffffff
} D2D1_SVG_ATTRIBUTE_STRING_TYPE;

typedef enum D2D1_SVG_ATTRIBUTE_POD_TYPE
{
    D2D1_SVG_ATTRIBUTE_POD_TYPE_FLOAT = 0,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_COLOR = 1,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_FILL_MODE = 2,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_DISPLAY = 3,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_OVERFLOW = 4,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_LINE_CAP = 5,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_LINE_JOIN = 6,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_VISIBILITY = 7,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_MATRIX = 8,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_UNIT_TYPE = 9,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_EXTEND_MODE = 10,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_PRESERVE_ASPECT_RATIO = 11,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX = 12,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH = 13,
    D2D1_SVG_ATTRIBUTE_POD_TYPE_FORCE_DWORD = 0xffffffff
} D2D1_SVG_ATTRIBUTE_POD_TYPE;

typedef struct D2D1_SVG_LENGTH
{
    FLOAT value;
    D2D1_SVG_LENGTH_UNITS units;
} D2D1_SVG_LENGTH;

typedef struct D2D1_SVG_VIEWBOX
{
    FLOAT x;
    FLOAT y;
    FLOAT width;
    FLOAT height;
} D2D1_SVG_VIEWBOX;

interface ID2D1SvgAttribute;
interface ID2D1SvgPaint;
interface ID2D1SvgStrokeDashArray;
interface ID2D1SvgPointCollection;
interface ID2D1SvgPathData;
struct ID2D1SvgDocument;

struct __declspec(uuid("ac7b67a6-183e-49c1-a823-0ebe40b0db29")) __declspec(novtable) ID2D1SvgElement : public ID2D1Resource
{
    virtual void STDMETHODCALLTYPE GetDocument(ID2D1SvgDocument **document) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTagName(PWSTR name, UINT32 nameCount) = 0;
    virtual UINT32 STDMETHODCALLTYPE GetTagNameLength() = 0;
    virtual BOOL STDMETHODCALLTYPE IsTextContent() = 0;
    virtual void STDMETHODCALLTYPE GetParent(ID2D1SvgElement **parent) = 0;
    virtual BOOL STDMETHODCALLTYPE HasChildren() = 0;
    virtual void STDMETHODCALLTYPE GetFirstChild(ID2D1SvgElement **child) = 0;
    virtual void STDMETHODCALLTYPE GetLastChild(ID2D1SvgElement **child) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPreviousChild(ID2D1SvgElement *referenceChild, ID2D1SvgElement **previousChild) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNextChild(ID2D1SvgElement *referenceChild, ID2D1SvgElement **nextChild) = 0;
    virtual HRESULT STDMETHODCALLTYPE InsertChildBefore(ID2D1SvgElement *newChild, ID2D1SvgElement *referenceChild) = 0;
    virtual HRESULT STDMETHODCALLTYPE AppendChild(ID2D1SvgElement *newChild) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReplaceChild(ID2D1SvgElement *newChild, ID2D1SvgElement *oldChild) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveChild(ID2D1SvgElement *oldChild) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateChild(PCWSTR tagName, ID2D1SvgElement **newChild) = 0;
    virtual BOOL STDMETHODCALLTYPE IsAttributeSpecified(PCWSTR name, BOOL *inherited = NULL) = 0;
    virtual UINT32 STDMETHODCALLTYPE GetSpecifiedAttributeCount() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSpecifiedAttributeName(UINT32 index, PWSTR name, UINT32 nameCount,
                                                                BOOL *inherited = NULL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSpecifiedAttributeNameLength(UINT32 index, UINT32 *nameLength,
                                                                      BOOL *inherited = NULL) = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveAttribute(PCWSTR name) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetTextValue(const WCHAR *name, UINT32 nameCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTextValue(PWSTR name, UINT32 nameCount) = 0;
    virtual UINT32 STDMETHODCALLTYPE GetTextValueLength() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAttributeValue(PCWSTR name, ID2D1SvgAttribute *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAttributeValue(PCWSTR name, D2D1_SVG_ATTRIBUTE_POD_TYPE type, const void *value,
                                                        UINT32 valueSizeInBytes) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetAttributeValue(PCWSTR name, D2D1_SVG_ATTRIBUTE_STRING_TYPE type, PCWSTR value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttributeValue(PCWSTR name, REFIID riid, void **value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttributeValue(PCWSTR name, D2D1_SVG_ATTRIBUTE_POD_TYPE type, void *value,
                                                        UINT32 valueSizeInBytes) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttributeValue(PCWSTR name, D2D1_SVG_ATTRIBUTE_STRING_TYPE type, PWSTR value,
                                                        UINT32 valueCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAttributeValueLength(PCWSTR name, D2D1_SVG_ATTRIBUTE_STRING_TYPE type,
                                                              UINT32 *valueLength) = 0;

    HRESULT GetAttributeValue(PCWSTR name, FLOAT *value)
    {
        return GetAttributeValue(name, D2D1_SVG_ATTRIBUTE_POD_TYPE_FLOAT, value, sizeof(*value));
    }
    HRESULT GetAttributeValue(PCWSTR name, D2D1_SVG_LENGTH *value)
    {
        return GetAttributeValue(name, D2D1_SVG_ATTRIBUTE_POD_TYPE_LENGTH, value, sizeof(*value));
    }
    HRESULT GetAttributeValue(PCWSTR name, D2D1_SVG_VIEWBOX *value)
    {
        return GetAttributeValue(name, D2D1_SVG_ATTRIBUTE_POD_TYPE_VIEWBOX, value, sizeof(*value));
    }
};

struct __declspec(uuid("86b88e4d-afa4-4d7b-88e4-68a51c4a0aec")) __declspec(novtable) ID2D1SvgDocument : public ID2D1Resource
{
    virtual HRESULT STDMETHODCALLTYPE SetViewportSize(D2D1_SIZE_F viewportSize) = 0;
    virtual D2D1_SIZE_F STDMETHODCALLTYPE GetViewportSize() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetRoot(ID2D1SvgElement *root) = 0;
    virtual void STDMETHODCALLTYPE GetRoot(ID2D1SvgElement **root) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindElementById(PCWSTR id, ID2D1SvgElement **svgElement) = 0;
    virtual HRESULT STDMETHODCALLTYPE Serialize(IStream *outputXmlStream, ID2D1SvgElement *subtree = NULL) = 0;
    virtual HRESULT STDMETHODCALLTYPE Deserialize(IStream *inputXmlStream, ID2D1SvgElement **subtree) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePaint(int paintType, const D2D1_COLOR_F *color, PCWSTR id,
                                                  ID2D1SvgPaint **paint) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateStrokeDashArray(const void *dashes, UINT32 dashesCount,
                                                            ID2D1SvgStrokeDashArray **strokeDashArray) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePointCollection(const D2D1_POINT_2F *points, UINT32 pointsCount,
                                                            ID2D1SvgPointCollection **pointCollection) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePathData(const FLOAT *segmentData, UINT32 segmentDataCount,
                                                     const void *commands, UINT32 commandsCount,
                                                     ID2D1SvgPathData **pathData) = 0;
};

struct __declspec(uuid("af671749-d241-4db8-8e41-dcc2e5c1a438")) __declspec(novtable) ID2D1SvgGlyphStyle : public ID2D1Resource {};
