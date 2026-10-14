/*
 * wallpaper.h — the desktop's picture wallpaper and the wallpaper folder
 *
 * Wallpapers are PNG files in C:\Windows\Web\Wallpaper, which NovaOS
 * ships beside the kernel (\EFI\NOVA\Files, fs/bootfiles.h), never inside
 * it; a user adds more from Settings > Personalization, which copies a
 * picture there.  The Aurora theme draws the chosen picture (by default
 * Aurora-Sunrise.png), filling the screen; HKCU\Control Panel\Desktop's
 * Wallpaper value remembers it, as on Windows.
 *
 * Decoding a picture takes a while, so it runs on a thread of its own
 * ("wallpaper"), which also makes the thumbnails Settings shows; the
 * desktop draws the new picture once it is ready.
 */

#pragma once

#include "../include/types.h"
#include "../gdi/gdi.h"
#include "../fs/ramfs.h"
#include "../um/um.h"

/* NovaOS's own personalization choices: Theme (a theme's name), IconSet
 * (a folder of C:\Windows\Web\Icons, "" for the built-in drawings) */
#define PERSONALIZE_KEY    "User\\" UM_USER_SID "\\Software\\NovaOS\\Personalization"

#define WALLPAPER_DIR      "\\Windows\\Web\\Wallpaper"
#define WALLPAPER_DEFAULT  WALLPAPER_DIR "\\Aurora-Sunrise.png"

/* At startup: the saved choice, decoded before the first frame */
void WallpaperInit(void);
/* Draw the picture to fill @r (cropped to keep its shape); false while
 * there is none (the caller draws the procedural sunrise).  Desktop thread. */
bool WallpaperDraw(GdiRect r);
/* A new picture finished decoding since the last call (the desktop then
 * redraws its background) */
bool WallpaperChanged(void);

/* The chosen picture's C: path ("\Windows\Web\Wallpaper\Beam.png") */
const char *WallpaperCurrent(void);
/* Choose the picture at C: path @path and remember it */
void WallpaperSet(const char *path);

/* The pictures in the wallpaper folder (PNG files), sorted by name; the
 * file-system lock held.  Returns how many (<= max). */
int  WallpaperList(RamNode **out, int max);
/* Pictures a user could add: PNG files in C:\Downloads, C:\Pictures,
 * C:\Documents and C:\Desktop (and their folders, but not icon sets)
 * whose names the wallpaper folder does not have yet.  File-system lock held. */
int  WallpaperCandidates(RamNode **out, int max);
/* Copy @src into the wallpaper folder; its C: path out (@cap bytes), or
 * false if it could not be copied.  File-system lock held. */
bool WallpaperInstall(RamNode *src, char *path, int cap);

/* Draw @f's thumbnail in @r (cropped), or false while it is being made
 * (it is queued).  File-system lock held. */
bool WallpaperDrawThumb(RamNode *f, GdiRect r);
/* Changes each time a thumbnail is ready (Settings repaints) */
UINT32 WallpaperThumbsGen(void);
