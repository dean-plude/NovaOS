/* NovaOS: the Windows SDK's propkey.h is MinGW-w64's, with the values of
 * System.Photo.Orientation it lacks (the EXIF orientations, which the
 * imaging component's WicService maps to WIC transforms). */
#pragma once
#include_next <propkey.h>

#ifndef PHOTO_ORIENTATION_NORMAL
#define PHOTO_ORIENTATION_NORMAL 1UL
#define PHOTO_ORIENTATION_FLIPHORIZONTAL 2UL
#define PHOTO_ORIENTATION_ROTATE180 3UL
#define PHOTO_ORIENTATION_FLIPVERTICAL 4UL
#define PHOTO_ORIENTATION_TRANSPOSE 5UL
#define PHOTO_ORIENTATION_ROTATE270 6UL
#define PHOTO_ORIENTATION_TRANSVERSE 7UL
#define PHOTO_ORIENTATION_ROTATE90 8UL
#endif
