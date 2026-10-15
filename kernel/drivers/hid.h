/*
 * hid.h — HID devices on buses other than USB
 *
 * usbhid.c's report-descriptor parser and report decoding (keys, mice,
 * touchpads, touch screens, pens) serve I2C-HID devices (i2chid.c) and
 * the keyboard and mouse class drivers on HID collections (nt/kbdhid.c)
 * too.
 */

#pragma once

#include "../include/types.h"
#include "../wm/input.h"

/* Read report descriptor @desc (@len bytes); NULL when the device has
 * nothing NovaOS uses.  @kind says what it is ("touchpad (mouse mode)",
 * "keyboard", ...).  Nothing reaches the desktop until HidStart(). */
void *HidAttach(const UINT8 *desc, int len, const char **kind);
void  HidStart(void *hid);
/* One input report (starting with its report ID if the device uses IDs) */
void  HidInput(void *hid, const UINT8 *report, int len);
/* ... that came at @tick (sched_ticks(): touchpad taps are timed) */
void  HidInputAt(void *hid, const UINT8 *report, int len, UINT64 tick);
/* The device went: the keys and buttons it held are let go; no more input */
void  HidStop(void *hid);
/* Whether its reports start with a report ID; whether it has keys */
bool  HidReportIds(void *hid);
bool  HidIsKeyboard(void *hid);
/* Where its output reports go (the keyboard LEDs, kept in step with the
 * lock keys): @out gets each with its report ID byte first (0 when the
 * device has no IDs), from the usb thread, and must not wait */
void  HidSetOutput(void *hid, void (*out)(void *ctx, const UINT8 *report, int len), void *ctx);

/* Precision touchpads: the feature report (@id, @out with the ID byte
 * first if the device uses IDs) that switches one to touchpad mode
 * (Input Mode 3, surface and button switches on); 0 for other devices.
 * Once the device took it, HidTouchpadMode(true) makes its finger reports
 * drive the pointer, taps, clicks and two-finger scrolling */
int   HidTouchpadModeReport(void *hid, UINT8 *id, UINT8 *out, int cap);
void  HidTouchpadMode(void *hid, bool on);

/* Checks: @hid's events are collected instead of posted, until
 * HidCaptureEnd() hands them over (at most @max) */
void  HidCaptureBegin(void *hid);
int   HidCaptureEnd(InputEvent *out, int max);
