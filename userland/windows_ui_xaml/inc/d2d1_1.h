/* NovaOS: MinGW-w64's d2d1_1.h (Direct2D 1.1) gives its interfaces their
 * IIDs for __uuidof through __CRT_UUID_DECL, which is empty where the
 * compiler has __uuidof of its own (clang in MSVC mode, as XAML's sources
 * are compiled): here each is given its IID as the Windows SDK's header
 * does, __declspec(uuid) (the same IIDs, from MinGW-w64's header). */
#pragma once
#include_next <d2d1_1.h>

#ifdef __cplusplus
struct __declspec(uuid("483473d7-cd46-4f9d-9d3a-3112aa80159d")) ID2D1Properties;
struct __declspec(uuid("82237326-8111-4f7c-bcf4-b5c1175564fe")) ID2D1GdiMetafileSink;
struct __declspec(uuid("2f543dc3-cfc1-4211-864f-cfd91c6f3395")) ID2D1GdiMetafile;
struct __declspec(uuid("10a72a66-e91c-43f4-993f-ddf4b82b0b4a")) ID2D1StrokeStyle1;
struct __declspec(uuid("54d7898a-a061-40a7-bec7-e465bcba2c4f")) ID2D1CommandSink;
struct __declspec(uuid("b4f34a19-2383-4d76-94f6-ec343657c3dc")) ID2D1CommandList;
struct __declspec(uuid("2c1d867d-c290-41c8-ae7e-34a98702e9a5")) ID2D1PrintControl;
struct __declspec(uuid("28211a43-7d89-476f-8181-2d6159b220ad")) ID2D1Effect;
struct __declspec(uuid("a898a84c-3873-4588-b08b-ebbf978df041")) ID2D1Bitmap1;
struct __declspec(uuid("fe9e984d-3f95-407c-b5db-cb94d4e8f87c")) ID2D1ImageBrush;
struct __declspec(uuid("41343a53-e41a-49a2-91cd-21793bbb62e5")) ID2D1BitmapBrush1;
struct __declspec(uuid("ae1572f4-5dd0-4777-998b-9279472ae63b")) ID2D1GradientStopCollection1;
struct __declspec(uuid("1c4820bb-5771-4518-a581-2fe4dd0ec657")) ID2D1ColorContext;
struct __declspec(uuid("e8f7fe7a-191c-466d-ad95-975678bda998")) ID2D1DeviceContext;
struct __declspec(uuid("47dd575d-ac05-4cdd-8049-9b02cd16f44c")) ID2D1Device;
struct __declspec(uuid("689f1f85-c72e-4e33-8f19-85754efd5ace")) ID2D1DrawingStateBlock1;
struct __declspec(uuid("62baa2d2-ab54-41b7-b872-787e0106a421")) ID2D1PathGeometry1;
struct __declspec(uuid("bb12d362-daee-4b9a-aa1d-14ba401cfa1f")) ID2D1Factory1;
struct __declspec(uuid("31e6e7bc-e0ff-4d46-8c64-a0a8c41c15d3")) ID2D1Multithread;
#endif

/* and D2D1::Matrix4x4F, which the Windows SDK's d2d1_1helper.h (included by
 * its d2d1_1.h) gives and MinGW-w64's lacks: a 4x4 matrix over
 * D2D1_MATRIX_4X4_F with the product XAML's Direct2D helpers use
 * (plat/win/desktop/D2DUtils.h) */
#ifdef __cplusplus
namespace D2D1 {
class Matrix4x4F : public D2D1_MATRIX_4X4_F
{
public:
    Matrix4x4F() { for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) m[i][j] = i == j ? 1.0f : 0.0f; }
    static const Matrix4x4F *ReinterpretBaseType(const D2D1_MATRIX_4X4_F *p) { return static_cast<const Matrix4x4F *>(p); }
    static Matrix4x4F *ReinterpretBaseType(D2D1_MATRIX_4X4_F *p) { return static_cast<Matrix4x4F *>(p); }
    Matrix4x4F operator*(const Matrix4x4F &o) const
    {
        Matrix4x4F r;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j] + m[i][3] * o.m[3][j];
        return r;
    }
};
}
#endif
