/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Key names for gesture bindings: "LEFTCTRL+LEFTALT+T", "SUPER+D", ... */
#ifndef SURFACE_TOUCH_KEYS_H
#define SURFACE_TOUCH_KEYS_H

#define KEYS_MAX_COMBO 6

/* Name without the KEY_ prefix, case-insensitive; also CTRL, ALT, SHIFT,
 * SUPER/META/WIN as aliases for the left-hand keys. -1 if unknown. */
int key_code(const char *name);
/* Parse "A+B+C" into codes; returns the count, or -1 with *bad set. */
int key_combo(const char *spec, int *codes, const char **bad);

#endif
