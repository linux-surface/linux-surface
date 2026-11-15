// SPDX-License-Identifier: GPL-2.0-or-later
/* Tests for the IPU3 10-bit packing helpers. Run: make test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipu3-unpack.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
	/* Stride matches the kernel's cio2_bytesperline() for sensor widths. */
	CHECK(ipu3_bytesperline(640) == 832, "640 stride %u", ipu3_bytesperline(640));
	CHECK(ipu3_bytesperline(2592) == 3328, "2592 stride %u", ipu3_bytesperline(2592));
	CHECK(ipu3_bytesperline(3264) == 4224, "3264 stride %u", ipu3_bytesperline(3264));
	CHECK(ipu3_packed_bytes(25) == 32 && ipu3_packed_bytes(26) == 64, "packed bytes");

	/* Known bit layout: pixel 0 = low 10 bits of bytes 0-1, pixel 1 starts at bit 10. */
	{
		uint8_t b[32] = { 0 };
		uint16_t o[25];
		b[0] = 0xff; b[1] = 0x03;	/* pixel 0 = 0x3ff */
		b[1] |= 0x04;			/* pixel 1 bit 0 -> value 1 */
		b[31] = 0x03 << 0;		/* pixel 24 occupies bits 240-249: bytes 30-31 */
		b[30] = 0x00;
		ipu3_unpack_line(b, o, 25);
		CHECK(o[0] == 0x3ff, "p0 %x", o[0]);
		CHECK(o[1] == 1, "p1 %x", o[1]);
		CHECK(o[24] == 0x300, "p24 %x", o[24]);
	}

	/* Round trip random data for widths that do and don't fill the last block. */
	unsigned int widths[] = { 1, 24, 25, 26, 640, 1296, 2592, 3264 };
	srand(1);
	for (unsigned int w = 0; w < sizeof(widths) / sizeof(widths[0]); w++) {
		unsigned int W = widths[w];
		uint16_t *in = calloc(W, 2), *out = calloc(W, 2);
		uint8_t *pk = calloc(ipu3_bytesperline(W), 1);
		for (unsigned int i = 0; i < W; i++)
			in[i] = rand() & 0x3ff;
		ipu3_pack_line(in, pk, W);
		ipu3_unpack_line(pk, out, W);
		CHECK(!memcmp(in, out, W * 2), "round trip width %u", W);
		free(in); free(out); free(pk);
	}

	printf(fails ? "%d test(s) failed\n" : "all tests passed\n", fails);
	return fails != 0;
}
