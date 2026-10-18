/*
 * logon.h — the sign-in and lock screen (Windows' LogonUI)
 *
 * At boot, when an account has to be chosen or its password typed
 * (um_account_signin_required), the desktop starts behind this screen:
 * the accounts in the bottom-left corner, the chosen one's picture and
 * name, and its password box (or a Sign in button for an account without
 * a password).  Win+L, the Start menu's Lock and LockWorkStation bring it
 * back for the account signed in, which must type its password to go on.
 * While it shows, no program gets the keyboard or the mouse.  With more
 * than one account the others are listed on it too: choosing one and
 * signing in switches to it, the first account's programs still running.
 */

#pragma once

#include "../include/types.h"
#include "input.h"

/* At the desktop's start: show the sign-in screen if it is needed.  True
 * when an account is signed in already (no screen) */
bool LogonStart(void);
/* The boot's first sign-in happened (once: then false) */
bool LogonTakeFirstSignIn(void);
bool LogonActive(void);
/* Lock the screen (any thread; the desktop loop shows it, LogonPoll) */
void LogonLock(void);
void LogonPoll(void);
/* Fast user switching: the sign-in screen with account @rid chosen (the
 * Start menu's account list); the one signed in keeps its programs */
void LogonSwitchTo(UINT32 rid);
/* Sign @rid out (any thread: Start's Sign out, ExitWindowsEx(EWX_LOGOFF)):
 * its programs are ended; the sign-in screen follows if it was on it */
void LogonSignOut(UINT32 rid);
/* Drawn over everything (the shell's overlay) */
void LogonDraw(void);
/* Input while it shows (the desktop loop's) */
void LogonKey(const KeyEvent *k);
void LogonPress(int x, int y);
