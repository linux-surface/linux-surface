// SPDX-License-Identifier: GPL-2.0-or-later
/* Gesture engine tests with synthetic multitouch frames. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/engine.h"

#define MAXEV 512
static struct te_event evs[MAXEV];
static int nev;
static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void cb(const struct te_event *ev, void *d)
{
	(void)d;
	if (nev < MAXEV)
		evs[nev++] = *ev;
}

static int count(enum te_event_type t)
{
	int n = 0;

	for (int i = 0; i < nev; i++)
		n += evs[i].type == t;
	return n;
}

static const struct te_event *find(enum te_event_type t)
{
	for (int i = 0; i < nev; i++)
		if (evs[i].type == t)
			return &evs[i];
	return NULL;
}

static const struct te_event *find_phase(enum te_event_type t, enum te_phase p)
{
	for (int i = 0; i < nev; i++)
		if (evs[i].type == t && evs[i].phase == p)
			return &evs[i];
	return NULL;
}

static struct te_engine eng;
static uint64_t now;

static void setup(void)
{
	struct te_config cfg;

	te_config_defaults(&cfg);
	te_init(&eng, &cfg, 1.5, cb, NULL);
	nev = 0;
	now = 10000;
}

/* Move n fingers (slots 0..n-1) from (x0,y0)+offsets to (x1,y1)+offsets over ms. */
static void fingers(int n, double x0, double y0, double x1, double y1, int ms, int steps, bool lift)
{
	for (int s = 0; s <= steps; s++) {
		double t = steps ? (double)s / steps : 1;

		for (int f = 0; f < n; f++)
			te_slot_update(&eng, f, 100 + f, x0 + (x1 - x0) * t + f * 0.05, y0 + (y1 - y0) * t,
				       0.02, 0.015, 90);
		te_frame(&eng, now);
		if (s < steps)
			now += ms / (steps ? steps : 1);
	}
	if (lift) {
		now += 10;
		for (int f = 0; f < n; f++)
			te_slot_update(&eng, f, -1, 0, 0, 0, 0, 0);
		te_frame(&eng, now);
	}
}

static void test_touch_stream(void)
{
	setup();
	fingers(1, 0.5, 0.5, 0.6, 0.5, 100, 5, true);
	CHECK(count(TE_TOUCH_DOWN) == 1, "down %d", count(TE_TOUCH_DOWN));
	CHECK(count(TE_TOUCH_MOVE) == 5, "move %d", count(TE_TOUCH_MOVE));
	CHECK(count(TE_TOUCH_UP) == 1, "up %d", count(TE_TOUCH_UP));
	CHECK(find(TE_TOUCH_UP)->id == 100, "up keeps the tracking id, got %d", find(TE_TOUCH_UP)->id);
	CHECK(find(TE_TOUCH_DOWN)->major == 0.02, "major passed through");
}

static void test_taps(void)
{
	setup();
	fingers(1, 0.5, 0.5, 0.5, 0.5, 80, 2, true);
	CHECK(count(TE_TAP) == 1 && find(TE_TAP)->fingers == 1, "1-finger tap");
	CHECK(count(TE_DOUBLE_TAP) == 0, "no double tap yet");

	now += 150;
	fingers(1, 0.505, 0.5, 0.505, 0.5, 80, 2, true);
	CHECK(count(TE_TAP) == 2 && count(TE_DOUBLE_TAP) == 1, "double tap: taps %d double %d",
	      count(TE_TAP), count(TE_DOUBLE_TAP));

	setup();
	fingers(3, 0.3, 0.5, 0.3, 0.5, 100, 2, true);
	CHECK(count(TE_TAP) == 1 && find(TE_TAP)->fingers == 3, "3-finger tap");

	/* too slow for a tap, too short for a long press: nothing */
	setup();
	fingers(1, 0.5, 0.5, 0.5, 0.5, 400, 4, true);
	CHECK(count(TE_TAP) == 0 && count(TE_LONG_PRESS) == 0, "slow touch is no tap");

	/* double tap needs the taps close together */
	setup();
	fingers(1, 0.2, 0.5, 0.2, 0.5, 80, 2, true);
	now += 100;
	fingers(1, 0.8, 0.5, 0.8, 0.5, 80, 2, true);
	CHECK(count(TE_DOUBLE_TAP) == 0, "far apart taps are not a double tap");
}

static void test_long_press(void)
{
	setup();
	fingers(2, 0.5, 0.5, 0.5, 0.5, 200, 2, false);
	now += 500;
	te_tick(&eng, now);
	CHECK(count(TE_LONG_PRESS) == 1 && find(TE_LONG_PRESS)->fingers == 2, "2-finger long press");
	te_tick(&eng, now + 500);
	CHECK(count(TE_LONG_PRESS) == 1, "long press fires once");
	fingers(2, 0.5, 0.5, 0.5, 0.5, 0, 0, true);
	CHECK(count(TE_TAP) == 0, "no tap after long press");

	/* moving cancels a long press */
	setup();
	fingers(1, 0.5, 0.5, 0.6, 0.5, 300, 6, false);
	now += 600;
	te_tick(&eng, now);
	CHECK(count(TE_LONG_PRESS) == 0, "moved finger is no long press");
}

static void test_swipes(void)
{
	static const struct { double x0, y0, x1, y1; enum te_dir d; } cases[] = {
		{ 0.6, 0.5, 0.3, 0.5, TE_LEFT }, { 0.3, 0.5, 0.6, 0.5, TE_RIGHT },
		{ 0.4, 0.7, 0.4, 0.4, TE_UP }, { 0.4, 0.3, 0.4, 0.6, TE_DOWN },
	};

	for (unsigned i = 0; i < 4; i++) {
		setup();
		fingers(3, cases[i].x0, cases[i].y0, cases[i].x1, cases[i].y1, 300, 10, true);
		const struct te_event *ev = find(TE_SWIPE);

		CHECK(ev && ev->fingers == 3 && ev->direction == cases[i].d, "3-finger swipe %s: %s",
		      te_dir_name(cases[i].d), ev ? te_dir_name(ev->direction) : "none");
		CHECK(count(TE_TAP) == 0 && count(TE_PINCH) == 0, "swipe is not a tap/pinch");
	}

	/* too slow */
	setup();
	fingers(3, 0.6, 0.5, 0.3, 0.5, 2000, 20, true);
	CHECK(count(TE_SWIPE) == 0, "slow drag is no swipe");
	/* too short */
	setup();
	fingers(3, 0.5, 0.5, 0.45, 0.5, 200, 5, true);
	CHECK(count(TE_SWIPE) == 0, "short move is no swipe");
}

static void test_edge_swipe(void)
{
	setup();
	fingers(1, 0.995, 0.5, 0.8, 0.5, 200, 8, true);
	const struct te_event *ev = find(TE_EDGE_SWIPE);

	CHECK(ev && ev->direction == TE_RIGHT, "swipe in from the right edge: %s",
	      ev ? te_dir_name(ev->direction) : "none");
	CHECK(count(TE_SWIPE) == 0, "edge swipe is not also a swipe");

	setup();
	fingers(1, 0.5, 0.002, 0.5, 0.3, 200, 8, true);
	ev = find(TE_EDGE_SWIPE);
	CHECK(ev && ev->direction == TE_UP, "swipe down from the top edge: %s",
	      ev ? te_dir_name(ev->direction) : "none");

	/* moving along the edge is a normal swipe */
	setup();
	fingers(1, 0.995, 0.7, 0.995, 0.3, 200, 8, true);
	CHECK(count(TE_EDGE_SWIPE) == 0 && count(TE_SWIPE) == 1, "along the edge = swipe");
}

static void test_pinch_rotate(void)
{
	/* two fingers moving apart */
	setup();
	for (int s = 0; s <= 10; s++) {
		double d = 0.05 + 0.01 * s;

		te_slot_update(&eng, 0, 1, 0.5 - d / 1.5, 0.5, 0.02, 0.02, 0);
		te_slot_update(&eng, 1, 2, 0.5 + d / 1.5, 0.5, 0.02, 0.02, 0);
		te_frame(&eng, now);
		now += 20;
	}
	te_slot_update(&eng, 0, -1, 0, 0, 0, 0, 0);
	te_slot_update(&eng, 1, -1, 0, 0, 0, 0, 0);
	te_frame(&eng, now);
	const struct te_event *b = find_phase(TE_PINCH, TE_BEGIN), *e = find_phase(TE_PINCH, TE_END);

	CHECK(b && e, "pinch begin and end");
	CHECK(e && fabs(e->scale - 3.0) < 0.05, "pinch out scale ~3, got %.3f", e ? e->scale : 0);
	CHECK(count(TE_SWIPE) == 0 && count(TE_TAP) == 0, "pinch is not a swipe or tap");
	CHECK(count(TE_ROTATE) == 0, "no rotation in a straight pinch");

	/* two fingers turning 45 degrees clockwise around the centre */
	setup();
	for (int s = 0; s <= 9; s++) {
		double a = s * 5.0 * M_PI / 180.0, r = 0.1;

		te_slot_update(&eng, 0, 1, 0.5 - r * cos(a) / 1.5, 0.5 - r * sin(a), 0.02, 0.02, 0);
		te_slot_update(&eng, 1, 2, 0.5 + r * cos(a) / 1.5, 0.5 + r * sin(a), 0.02, 0.02, 0);
		te_frame(&eng, now);
		now += 20;
	}
	te_slot_update(&eng, 0, -1, 0, 0, 0, 0, 0);
	te_slot_update(&eng, 1, -1, 0, 0, 0, 0, 0);
	te_frame(&eng, now);
	e = find_phase(TE_ROTATE, TE_END);
	CHECK(e && fabs(e->angle - 45.0) < 1.0, "rotate 45 cw, got %.2f", e ? e->angle : 0);
	CHECK(count(TE_PINCH) == 0, "pure rotation is no pinch");
}

static void test_palm(void)
{
	setup();
	te_slot_update(&eng, 0, 7, 0.5, 0.5, 0.2, 0.15, 0);	/* huge contact */
	te_frame(&eng, now);
	now += 50;
	te_slot_update(&eng, 0, -1, 0, 0, 0, 0, 0);
	te_frame(&eng, now);
	CHECK(find_phase(TE_PALM, TE_BEGIN) && find_phase(TE_PALM, TE_END), "palm begin/end");
	CHECK(count(TE_TAP) == 0 && count(TE_TOUCH_DOWN) == 0, "palm makes no tap/touch");

	/* a palm doesn't change a 1-finger tap into a 2-finger tap */
	setup();
	te_slot_update(&eng, 0, 7, 0.2, 0.8, 0.2, 0.15, 0);
	te_slot_update(&eng, 1, 8, 0.6, 0.4, 0.02, 0.02, 0);
	te_frame(&eng, now);
	now += 80;
	te_slot_update(&eng, 1, -1, 0, 0, 0, 0, 0);
	te_frame(&eng, now);
	CHECK(count(TE_TAP) == 1 && find(TE_TAP)->fingers == 1, "tap next to a palm");
}

static void test_rotation(void)
{
	/* Display rotated 90: a swipe along panel +y is a swipe left on screen. */
	setup();
	te_set_rotation(&eng, 90);
	fingers(3, 0.4, 0.3, 0.4, 0.7, 300, 10, true);
	const struct te_event *ev = find(TE_SWIPE);

	CHECK(ev && ev->direction == TE_LEFT, "rotated swipe: %s", ev ? te_dir_name(ev->direction) : "none");

	setup();
	te_set_rotation(&eng, 180);
	fingers(1, 0.2, 0.3, 0.2, 0.3, 50, 1, true);
	ev = find(TE_TAP);
	CHECK(ev && fabs(ev->x - 0.8) < 1e-9 && fabs(ev->y - 0.7) < 1e-9, "rotated coordinates");
}

static void test_slot_reuse_and_reset(void)
{
	/* new tracking id in the same slot within one frame = up + down */
	setup();
	te_slot_update(&eng, 0, 1, 0.5, 0.5, 0.02, 0.02, 0);
	te_frame(&eng, now);
	now += 30;
	te_slot_update(&eng, 0, -1, 0, 0, 0, 0, 0);
	te_slot_update(&eng, 0, 2, 0.6, 0.6, 0.02, 0.02, 0);
	te_frame(&eng, now);
	CHECK(count(TE_TOUCH_UP) == 1 && count(TE_TOUCH_DOWN) == 2, "slot reuse: up %d down %d",
	      count(TE_TOUCH_UP), count(TE_TOUCH_DOWN));
	CHECK(te_active_contacts(&eng) == 1, "one contact active");

	te_reset(&eng, now);
	CHECK(te_active_contacts(&eng) == 0 && count(TE_TOUCH_UP) == 2, "reset lifts all");
	nev = 0;
	now += 100;
	te_slot_update(&eng, 0, -1, 0, 0, 0, 0, 0);
	te_frame(&eng, now);
	CHECK(nev == 0, "no stray events after reset");
}

static void test_session_isolation(void)
{
	/* A 3-finger swipe followed by a 2-finger tap: slot 2's old movement
	 * must not turn the tap into "moved too far". */
	setup();
	fingers(3, 0.6, 0.5, 0.3, 0.5, 300, 10, true);
	now += 400;
	fingers(2, 0.4, 0.4, 0.4, 0.4, 100, 2, true);
	CHECK(count(TE_SWIPE) == 1 && count(TE_TAP) == 1 && find(TE_TAP)->fingers == 2,
	      "tap after swipe: swipes %d taps %d", count(TE_SWIPE), count(TE_TAP));

	/* same for long press after a swipe */
	setup();
	fingers(3, 0.6, 0.5, 0.3, 0.5, 300, 10, true);
	now += 400;
	fingers(1, 0.5, 0.5, 0.5, 0.5, 100, 1, false);
	now += 700;
	te_tick(&eng, now);
	CHECK(count(TE_LONG_PRESS) == 1, "long press after swipe");
}

int main(void)
{
	test_session_isolation();
	test_touch_stream();
	test_taps();
	test_long_press();
	test_swipes();
	test_edge_swipe();
	test_pinch_rotate();
	test_palm();
	test_rotation();
	test_slot_reuse_and_reset();
	printf(fails ? "%d engine test(s) failed\n" : "all engine tests passed\n", fails);
	return fails != 0;
}
