// SPDX-License-Identifier: GPL-2.0-or-later
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static void put(struct jw *w, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (w->overflow)
		return;
	va_start(ap, fmt);
	n = vsnprintf(w->buf + w->len, sizeof(w->buf) - w->len, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof(w->buf) - w->len) {
		w->overflow = true;
		return;
	}
	w->len += n;
}

static void put_escaped(struct jw *w, const char *s)
{
	put(w, "\"");
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;

		if (ch == '"' || ch == '\\')
			put(w, "\\%c", ch);
		else if (ch == '\n')
			put(w, "\\n");
		else if (ch == '\t')
			put(w, "\\t");
		else if (ch < 0x20)
			put(w, "\\u%04x", ch);
		else
			put(w, "%c", ch);
	}
	put(w, "\"");
}

static void key(struct jw *w, const char *k)
{
	if (!w->first)
		put(w, ",");
	w->first = false;
	put_escaped(w, k);
	put(w, ":");
}

void jw_begin(struct jw *w)
{
	w->len = 0;
	w->buf[0] = 0;
	w->first = true;
	w->overflow = false;
	put(w, "{");
}

void jw_end(struct jw *w)
{
	put(w, "}\n");
}

void jw_str(struct jw *w, const char *k, const char *val)
{
	key(w, k);
	put_escaped(w, val ? val : "");
}

void jw_num(struct jw *w, const char *k, double val)
{
	key(w, k);
	if (!isfinite(val))
		put(w, "null");
	else
		put(w, "%.5g", val);
}

void jw_int(struct jw *w, const char *k, long long val)
{
	key(w, k);
	put(w, "%lld", val);
}

void jw_bool(struct jw *w, const char *k, bool val)
{
	key(w, k);
	put(w, val ? "true" : "false");
}

void jw_raw(struct jw *w, const char *k, const char *json)
{
	key(w, k);
	put(w, "%s", json);
}

/* --- parser ------------------------------------------------------------ */

static const char *ws(const char *s)
{
	while (*s && isspace((unsigned char)*s))
		s++;
	return s;
}

/* Parse a JSON string at s (pointing at the opening quote) into out. */
static const char *pstring(const char *s, char *out, size_t outlen, const char **err)
{
	size_t n = 0;

	if (*s != '"') {
		*err = "expected string";
		return NULL;
	}
	s++;
	while (*s && *s != '"') {
		char ch = *s++;

		if (ch == '\\') {
			switch (*s) {
			case 'n': ch = '\n'; break;
			case 't': ch = '\t'; break;
			case 'r': ch = '\r'; break;
			case 'b': ch = '\b'; break;
			case 'f': ch = '\f'; break;
			case '"': case '\\': case '/': ch = *s; break;
			case 'u': {
				unsigned int cp;

				if (sscanf(s + 1, "%4x", &cp) != 1) {
					*err = "bad \\u escape";
					return NULL;
				}
				ch = cp < 0x80 ? (char)cp : '?';
				s += 4;
				break;
			}
			default:
				*err = "bad escape";
				return NULL;
			}
			s++;
		}
		if (n + 1 < outlen)
			out[n++] = ch;
	}
	if (*s != '"') {
		*err = "unterminated string";
		return NULL;
	}
	out[n] = 0;
	return s + 1;
}

int jp_parse(const char *s, struct jp_object *o, const char **err)
{
	const char *dummy;

	if (!err)
		err = &dummy;
	*err = NULL;
	memset(o, 0, sizeof(*o));
	s = ws(s);
	if (*s != '{') {
		*err = "expected object";
		return -1;
	}
	s = ws(s + 1);
	if (*s == '}')
		goto done;

	for (;;) {
		struct jp_field *f;

		if (o->n >= JP_MAX_FIELDS) {
			*err = "too many fields";
			return -1;
		}
		f = &o->f[o->n];
		s = pstring(ws(s), f->key, sizeof(f->key), err);
		if (!s)
			return -1;
		s = ws(s);
		if (*s != ':') {
			*err = "expected ':'";
			return -1;
		}
		s = ws(s + 1);

		if (*s == '"') {
			f->type = JP_STRING;
			s = pstring(s, f->str, sizeof(f->str), err);
			if (!s)
				return -1;
		} else if (!strncmp(s, "true", 4) || !strncmp(s, "false", 5)) {
			f->type = JP_BOOL;
			f->b = *s == 't';
			f->num = f->b;
			s += f->b ? 4 : 5;
		} else if (!strncmp(s, "null", 4)) {
			f->type = JP_NULL;
			s += 4;
		} else if (*s == '[') {
			f->type = JP_ARRAY;
			s = ws(s + 1);
			while (*s != ']') {
				char tmp[32];

				s = pstring(s, tmp, sizeof(tmp), err);
				if (!s)
					return -1;
				if (f->n_items < JP_MAX_ARRAY)
					strcpy(f->items[f->n_items++], tmp);
				s = ws(s);
				if (*s == ',')
					s = ws(s + 1);
				else if (*s != ']') {
					*err = "expected ',' or ']'";
					return -1;
				}
			}
			s++;
		} else {
			char *end;

			f->type = JP_NUMBER;
			f->num = strtod(s, &end);
			if (end == s) {
				*err = "bad value";
				return -1;
			}
			s = end;
		}
		o->n++;

		s = ws(s);
		if (*s == ',') {
			s = ws(s + 1);
			continue;
		}
		if (*s == '}')
			break;
		*err = "expected ',' or '}'";
		return -1;
	}
done:
	s = ws(s + 1);
	if (*s) {
		*err = "trailing data";
		return -1;
	}
	return 0;
}

const struct jp_field *jp_get(const struct jp_object *o, const char *k)
{
	for (int i = 0; i < o->n; i++)
		if (!strcmp(o->f[i].key, k))
			return &o->f[i];
	return NULL;
}
