// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Tests for surface-camd's image processing on synthetic sensor frames:
 * Bayer order handling, white balance convergence, auto exposure software
 * gain, 180 degree rotation, and the IR (Y10) path. Run: make test
 */
#define SURFACE_CAMD_NO_MAIN
#include "surface-camd.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* Build a packed frame where each 2x2 block has the given R,G,B (10-bit, incl. black level). */
static uint8_t *make_bayer(struct cam *c, int (*rgb)(unsigned x, unsigned y, int ch))
{
	uint8_t *raw = calloc((size_t)c->bpl * c->h, 1);
	uint16_t *line = calloc(c->w, 2);

	for (unsigned y = 0; y < c->h; y++) {
		for (unsigned x = 0; x < c->w; x++) {
			int idx = (y & 1) * 2 + (x & 1);
			int ch = idx == c->r_idx ? 0 : idx == c->b_idx ? 2 : 1;
			line[x] = rgb(x / 2, y / 2, ch);
		}
		ipu3_pack_line(line, raw + (size_t)y * c->bpl, c->w);
	}
	free(line);
	return raw;
}

static int cast_grey(unsigned x, unsigned y, int ch) { (void)x; (void)y; return 64 + (ch == 0 ? 120 : ch == 1 ? 200 : 90); }
static int red_left(unsigned x, unsigned y, int ch) { (void)y; return 64 + (x < 8 ? (ch == 0 ? 600 : 40) : 200); }
static int dark(unsigned x, unsigned y, int ch) { (void)x; (void)y; (void)ch; return 64 + 10; }

static void init(struct cam *c, uint32_t code, unsigned w, unsigned h)
{
	memset(c, 0, sizeof(*c));
	c->black_level = 64; c->target = 0.18; c->saturation = 1.0;
	c->soft_gain = 1.0; c->wb_r = c->wb_b = 1.0;
	c->sensor_fd = -1;
	c->mbus_code = code; c->w = w; c->h = h;
	c->bpl = ipu3_bytesperline(w);
	if (code == MEDIA_BUS_FMT_Y10_1X10) { c->kind = KIND_MONO; c->ow = w; c->oh = h; }
	else { c->kind = KIND_BAYER; bayer_layout(code, &c->r_idx, &c->b_idx); c->ow = w / 2; c->oh = h / 2; }
	c->out_size = c->kind == KIND_BAYER ? c->ow * c->oh * 2 : c->ow * c->oh;
	c->out = calloc(c->out_size, 1); c->rgb_row = calloc(c->ow, 3);
	c->line0 = calloc(w + 32, 2); c->line1 = calloc(w + 32, 2);
	build_lut(c);
}

static void run(struct cam *c, const uint8_t *raw, int frames)
{
	for (int i = 0; i < frames; i++) {
		struct stats st = { 0 };
		if (c->kind == KIND_BAYER) process_bayer(c, raw, &st); else process_mono(c, raw, &st);
		update_3a(c, &st);
	}
}

int main(void)
{
	uint32_t codes[] = { MEDIA_BUS_FMT_SBGGR10_1X10, MEDIA_BUS_FMT_SGBRG10_1X10,
			     MEDIA_BUS_FMT_SGRBG10_1X10, MEDIA_BUS_FMT_SRGGB10_1X10 };
	struct cam c;

	for (unsigned k = 0; k < 4; k++) {
		/* White balance: a grey scene with a green cast should end up neutral. */
		init(&c, codes[k], 64, 32);
		uint8_t *raw = make_bayer(&c, cast_grey);
		run(&c, raw, 60);
		int u = c.out[1], v = c.out[3];
		CHECK(abs(u - 128) <= 3 && abs(v - 128) <= 3, "code %x: grey not neutral u=%d v=%d (wb %.2f/%.2f)", codes[k], u, v, c.wb_r, c.wb_b);
		free(raw);

		/* Bayer order: a red area must come out red (V >> 128), the rest not. */
		init(&c, codes[k], 64, 32);
		raw = make_bayer(&c, red_left);
		run(&c, raw, 1);
		CHECK(c.out[3] > 160, "code %x: red area V=%d", codes[k], c.out[3]);
		CHECK(c.out[(c.ow - 2) * 2 + 3] < 140, "code %x: right side V=%d", codes[k], c.out[(c.ow - 2) * 2 + 3]);

		/* 180 degree rotation moves the red area to the right edge. */
		c.rot180 = true;
		run(&c, raw, 1);
		CHECK(c.out[(c.ow - 2) * 2 + 3] > 160 && c.out[3] < 140, "code %x: rotation", codes[k]);
		free(raw);
	}

	/* Without sensor controls, auto exposure falls back to software gain on dark scenes. */
	init(&c, MEDIA_BUS_FMT_SBGGR10_1X10, 64, 32);
	uint8_t *raw = make_bayer(&c, dark);
	run(&c, raw, 30);
	CHECK(c.soft_gain > 3.5, "dark scene soft gain %.2f", c.soft_gain);
	free(raw);

	/* IR / Y10 path: brightness maps monotonically, rotation reverses. */
	init(&c, MEDIA_BUS_FMT_Y10_1X10, 50, 4);
	{
		uint8_t *r = calloc((size_t)c.bpl * c.h, 1);
		uint16_t line[50];
		for (unsigned y = 0; y < c.h; y++) {
			for (unsigned x = 0; x < 50; x++) line[x] = 64 + x * 19;
			ipu3_pack_line(line, r + (size_t)y * c.bpl, 50);
		}
		run(&c, r, 1);
		CHECK(c.out[0] == 0 && c.out[49] > 240 && c.out[10] < c.out[20], "IR ramp %d..%d", c.out[0], c.out[49]);
		c.rot180 = true;
		run(&c, r, 1);
		CHECK(c.out[49] == 0 && c.out[0] > 240, "IR rotation");
		free(r);
	}

	printf(fails ? "%d test(s) failed\n" : "all processing tests passed\n", fails);
	return fails != 0;
}
