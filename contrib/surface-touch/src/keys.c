// SPDX-License-Identifier: GPL-2.0-or-later
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <linux/input-event-codes.h>

#include "keys.h"

#define K(x) { #x, KEY_##x }

static const struct { const char *name; int code; } keys[] = {
	K(A), K(B), K(C), K(D), K(E), K(F), K(G), K(H), K(I), K(J), K(K), K(L), K(M),
	K(N), K(O), K(P), K(Q), K(R), K(S), K(T), K(U), K(V), K(W), K(X), K(Y), K(Z),
	K(0), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8), K(9),
	K(F1), K(F2), K(F3), K(F4), K(F5), K(F6), K(F7), K(F8), K(F9), K(F10), K(F11), K(F12),
	K(ESC), K(TAB), K(ENTER), K(SPACE), K(BACKSPACE), K(DELETE), K(INSERT),
	K(HOME), K(END), K(PAGEUP), K(PAGEDOWN), K(UP), K(DOWN), K(LEFT), K(RIGHT),
	K(LEFTCTRL), K(RIGHTCTRL), K(LEFTALT), K(RIGHTALT), K(LEFTSHIFT), K(RIGHTSHIFT),
	K(LEFTMETA), K(RIGHTMETA), K(COMPOSE), K(SYSRQ), K(PRINT),
	K(MINUS), K(EQUAL), K(LEFTBRACE), K(RIGHTBRACE), K(SEMICOLON), K(APOSTROPHE),
	K(GRAVE), K(BACKSLASH), K(COMMA), K(DOT), K(SLASH),
	K(VOLUMEUP), K(VOLUMEDOWN), K(MUTE), K(PLAYPAUSE), K(NEXTSONG), K(PREVIOUSSONG),
	K(BRIGHTNESSUP), K(BRIGHTNESSDOWN), K(BACK), K(FORWARD), K(ZOOMIN), K(ZOOMOUT),
	K(SCREENLOCK), K(COFFEE), K(SEARCH), K(MENU), K(KEYBOARD), K(CALC),
	{ "CTRL", KEY_LEFTCTRL }, { "ALT", KEY_LEFTALT }, { "SHIFT", KEY_LEFTSHIFT },
	{ "SUPER", KEY_LEFTMETA }, { "META", KEY_LEFTMETA }, { "WIN", KEY_LEFTMETA },
	{ "ESCAPE", KEY_ESC }, { "RETURN", KEY_ENTER }, { "DEL", KEY_DELETE },
};

int key_code(const char *name)
{
	if (!strncasecmp(name, "KEY_", 4))
		name += 4;
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		if (!strcasecmp(keys[i].name, name))
			return keys[i].code;
	return -1;
}

int key_combo(const char *spec, int *codes, const char **bad)
{
	static char badbuf[32];
	char part[32];
	int n = 0;

	while (*spec) {
		size_t len = strcspn(spec, "+");

		while (len && isspace((unsigned char)*spec)) {
			spec++;
			len--;
		}
		snprintf(part, sizeof(part), "%.*s", (int)len, spec);
		for (int i = (int)strlen(part) - 1; i >= 0 && isspace((unsigned char)part[i]); i--)
			part[i] = 0;
		if (!part[0] || n >= KEYS_MAX_COMBO || (codes[n] = key_code(part)) < 0) {
			snprintf(badbuf, sizeof(badbuf), "%s", part[0] ? part : "(empty)");
			if (bad)
				*bad = badbuf;
			return -1;
		}
		n++;
		spec += len;
		if (*spec == '+')
			spec++;
	}
	if (!n && bad)
		*bad = "(empty)";
	return n ? n : -1;
}
