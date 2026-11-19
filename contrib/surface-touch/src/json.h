/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Minimal JSON for the surface-touchd socket API: a writer for flat objects
 * and a parser for flat request objects (strings, numbers, booleans, null
 * and arrays of strings). */
#ifndef SURFACE_TOUCH_JSON_H
#define SURFACE_TOUCH_JSON_H

#include <stdbool.h>
#include <stddef.h>

/* --- writer ------------------------------------------------------------ */

struct jw {
	char buf[4096];
	size_t len;
	bool first;
	bool overflow;
};

void jw_begin(struct jw *w);		/* "{" */
void jw_end(struct jw *w);		/* "}\n" */
void jw_str(struct jw *w, const char *key, const char *val);
void jw_num(struct jw *w, const char *key, double val);
void jw_int(struct jw *w, const char *key, long long val);
void jw_bool(struct jw *w, const char *key, bool val);
/* Insert pre-formatted JSON (an object or array) as the value of key. */
void jw_raw(struct jw *w, const char *key, const char *json);

/* --- parser ------------------------------------------------------------ */

#define JP_MAX_FIELDS 16
#define JP_MAX_ARRAY 16

enum jp_type { JP_STRING, JP_NUMBER, JP_BOOL, JP_NULL, JP_ARRAY };

struct jp_field {
	char key[32];
	enum jp_type type;
	char str[256];
	double num;
	bool b;
	int n_items;
	char items[JP_MAX_ARRAY][32];
};

struct jp_object {
	int n;
	struct jp_field f[JP_MAX_FIELDS];
};

/* Returns 0 on success, -1 with *err set on malformed input. */
int jp_parse(const char *s, struct jp_object *o, const char **err);
const struct jp_field *jp_get(const struct jp_object *o, const char *key);

#endif
