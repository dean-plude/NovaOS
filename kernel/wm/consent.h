/*
 * consent.h — User Account Control's prompt on the secure desktop
 * (Windows' consent.exe)
 *
 * When a program is to run as administrator (ShellExecute's "runas"
 * verb, or a manifest asking for requireAdministrator or
 * highestAvailable), the screen dims and this dialog takes over the
 * keyboard and the mouse: "Do you want to allow this app to make changes
 * to your device?" with the program's name and where it is.  An
 * administrator answers Yes or No; a standard user (or an administrator
 * whose policy says so) types an administrator's user name and password,
 * and a wrong one is refused.  Programs cannot draw over it or send it
 * keys: it reads only the keyboard and mouse themselves, as the secure
 * desktop does.
 */

#pragma once

#include "../include/types.h"
#include "input.h"

/* Ask, blocking the calling thread until the dialog is answered (never
 * call it from the desktop's thread).  @path: the program; @credentials:
 * ask for an administrator's name and password rather than Yes or No.
 * Returns the account that agreed: @self for Yes, the administrator whose
 * password was typed, or 0 for No (or no desktop to ask on). */
UINT32 ConsentAsk(const char *path, bool credentials, UINT32 self);

bool ConsentActive(void);
/* Counts the prompts shown, so the desktop can tell one prompt from the next */
UINT32 ConsentSerial(void);
/* The desktop loop: shows a waiting request once nothing else (the
 * sign-in or lock screen) has the screen */
void ConsentPoll(void);
/* Drawn over everything (the shell's overlay) */
void ConsentDraw(void);
/* Input while it shows (the desktop loop's) */
void ConsentKey(const KeyEvent *k);
void ConsentPress(int x, int y);
