/*
 * iconset.h — icon sets: the desktop's app, folder and file icons as files
 *
 * An icon set is a folder of C:\Windows\Web\Icons holding .ico (or .png)
 * files named after the icons they replace (ICONSET_NAMES): terminal.ico,
 * folder-documents.ico, this-pc.ico, ...  NovaOS ships "Beam" beside the
 * kernel (\EFI\NOVA\Files, fs/bootfiles.h), never inside it; a user adds
 * more from Settings > Personalization, which copies a folder of such
 * files there.  An icon the chosen set has no file for, and every icon
 * under "Built-in", is drawn by apps.c as before.  The choice is the
 * IconSet value of PERSONALIZE_KEY (wm/wallpaper.h).
 */

#pragma once

#include "apps.h"

#define ICONSET_DIR "\\Windows\\Web\\Icons"
#define ICONSET_MAX 16                     /* sets Settings lists */

/* At startup: load the saved choice */
void IconSetInit(void);
/* The chosen set's folder name ("" for the built-in drawings) */
const char *IconSetCurrent(void);
/* Choose set @name ("" built in), load its icons and remember it.  The
 * file-system lock held. */
void IconSetChoose(const char *name);

/* The chosen set's icon for an app, a folder kind, a file and This PC;
 * NULL where it has none (the caller draws its own) */
GdiIcon *IconSetApp(AppId id);
GdiIcon *IconSetFolder(FolderKind k);
GdiIcon *IconSetFile(void);
GdiIcon *IconSetPc(void);

/* The sets in ICONSET_DIR, by name (folders with at least one icon named
 * as ICONSET_NAMES says).  File-system lock held.  How many (<= max). */
int  IconSetList(RamNode **out, int max);
/* Folders a user could add as a set: folders in C:\Downloads,
 * C:\Documents, C:\Desktop and C:\Pictures (and one level below) with at
 * least one such icon, whose names ICONSET_DIR does not have yet. */
int  IconSetCandidates(RamNode **out, int max);
/* Folder @d holds icons named as a set's are (its pictures are icons, not wallpapers) */
bool IconSetIsSet(const RamNode *d);
/* Copy folder @src's icons into ICONSET_DIR\<its name> and choose it */
bool IconSetInstall(RamNode *src);
/* Draw set @dir's icon for @id (or the built-in one) in a size x size box:
 * Settings' previews */
void IconSetDrawPreview(RamNode *dir, AppId id, int x, int y, int size);
