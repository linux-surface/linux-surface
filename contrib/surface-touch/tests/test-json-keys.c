// SPDX-License-Identifier: GPL-2.0-or-later
/* Tests for the JSON helpers and key-combo parsing. */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "../src/json.h"
#include "../src/keys.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void test_writer(void)
{
	struct jw w;

	jw_begin(&w);
	jw_str(&w, "event", "ge\"st\\ure\n");
	jw_int(&w, "n", 3);
	jw_num(&w, "x", 0.25);
	jw_bool(&w, "ok", true);
	jw_raw(&w, "a", "[1,2]");
	jw_end(&w);
	CHECK(!strcmp(w.buf, "{\"event\":\"ge\\\"st\\\\ure\\n\",\"n\":3,\"x\":0.25,\"ok\":true,\"a\":[1,2]}\n"),
	      "writer output: %s", w.buf);

	/* overflow is detected, not truncated silently */
	char big[5000];

	memset(big, 'x', sizeof(big) - 1);
	big[sizeof(big) - 1] = 0;
	jw_begin(&w);
	jw_str(&w, "s", big);
	jw_end(&w);
	CHECK(w.overflow, "overflow flagged");
}

static void test_parser(void)
{
	struct jp_object o;
	const char *err;

	CHECK(jp_parse("{\"cmd\":\"set\",\"key\":\"rotation\",\"value\":90}", &o, &err) == 0, "parse ok");
	CHECK(!strcmp(jp_get(&o, "cmd")->str, "set"), "cmd");
	CHECK(jp_get(&o, "value")->type == JP_NUMBER && jp_get(&o, "value")->num == 90, "number");

	CHECK(jp_parse(" { \"cmd\" : \"subscribe\" , \"events\" : [ \"touch\" , \"pen\" ] } ", &o, &err) == 0,
	      "spaces and arrays: %s", err);
	const struct jp_field *f = jp_get(&o, "events");

	CHECK(f && f->type == JP_ARRAY && f->n_items == 2 && !strcmp(f->items[1], "pen"), "array items");

	CHECK(jp_parse("{\"v\":false,\"n\":null,\"s\":\"a\\\"b\\u0041\"}", &o, &err) == 0, "bool/null/escapes");
	CHECK(jp_get(&o, "v")->type == JP_BOOL && !jp_get(&o, "v")->b, "false");
	CHECK(jp_get(&o, "n")->type == JP_NULL, "null");
	CHECK(!strcmp(jp_get(&o, "s")->str, "a\"bA"), "escapes: %s", jp_get(&o, "s")->str);
	CHECK(jp_parse("{}", &o, &err) == 0 && o.n == 0, "empty object");

	static const char *const bad[] = {
		"", "[]", "{\"a\"}", "{\"a\":}", "{\"a\":1", "{\"a\":\"x}", "{\"a\":1} x",
		"{\"a\":[1]}", "{a:1}", "{\"a\":1,}",
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
		CHECK(jp_parse(bad[i], &o, &err) < 0 && err, "rejects %s", bad[i]);
}

static void test_keys(void)
{
	int k[KEYS_MAX_COMBO];
	const char *bad = NULL;

	CHECK(key_combo("LEFTCTRL+LEFTALT+T", k, &bad) == 3 && k[0] == KEY_LEFTCTRL &&
	      k[1] == KEY_LEFTALT && k[2] == KEY_T, "ctrl+alt+t");
	CHECK(key_combo("super + d", k, &bad) == 2 && k[0] == KEY_LEFTMETA && k[1] == KEY_D,
	      "aliases, case and spaces");
	CHECK(key_combo("KEY_VOLUMEUP", k, &bad) == 1 && k[0] == KEY_VOLUMEUP, "KEY_ prefix");
	CHECK(key_combo("CTRL+NOPE", k, &bad) < 0 && !strcmp(bad, "NOPE"), "unknown key reported: %s", bad);
	CHECK(key_combo("", k, &bad) < 0, "empty");
	CHECK(key_combo("CTRL++A", k, &bad) < 0, "empty part");
	CHECK(key_combo("A+B+C+D+E+F+G", k, &bad) < 0, "too many keys");
}

int main(void)
{
	test_writer();
	test_parser();
	test_keys();
	printf(fails ? "%d json/keys test(s) failed\n" : "all json/keys tests passed\n", fails);
	return fails != 0;
}
