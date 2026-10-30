/* NovaOS: the Windows App SDK's closed OrientationSupportedLegacy.h: whether
 * a WIC decoder before Windows 10 19H1 applies its frames' EXIF orientation.
 * The imaging component's WicService asks it only below 19H1, and NovaOS
 * answers the 19H1 contract and later, so no decoder here is that old. */
#pragma once
#include <wincodec.h>

inline bool IsOrientationSupportedLegacy(IWICBitmapDecoder *)
{
    return false;
}
