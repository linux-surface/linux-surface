/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Unpacking of the IPU3 CIO2 10-bit packed formats (ip3b/ip3g/ip3G/ip3r/ip3y).
 *
 * The CIO2 stores 25 pixels in every 32 bytes: 250 bits of pixel data,
 * little-endian, pixel 0 in the lowest bits, followed by 6 bits of padding.
 * Lines are padded to a multiple of 64 bytes (see cio2_bytesperline()).
 */
#ifndef IPU3_UNPACK_H
#define IPU3_UNPACK_H

#include <stdint.h>

/* Bytes the packed pixels of one line occupy (without line-end padding). */
static inline unsigned int ipu3_packed_bytes(unsigned int width)
{
	return (width + 24) / 25 * 32;
}

/* The CIO2's line stride: 64 bytes per 50 pixels. */
static inline unsigned int ipu3_bytesperline(unsigned int width)
{
	return (width + 49) / 50 * 64;
}

static inline void ipu3_unpack_line(const uint8_t *src, uint16_t *dst,
				    unsigned int width)
{
	unsigned int x = 0;

	while (x < width) {
		unsigned int n = width - x < 25 ? width - x : 25;

		for (unsigned int i = 0; i < n; i++) {
			unsigned int bit = i * 10;
			unsigned int byte = bit >> 3;
			unsigned int v = src[byte] | (unsigned int)src[byte + 1] << 8;

			dst[x + i] = (v >> (bit & 7)) & 0x3ff;
		}
		src += 32;
		x += n;
	}
}

/* Inverse operation, used by the tests. */
static inline void ipu3_pack_line(const uint16_t *src, uint8_t *dst,
				  unsigned int width)
{
	unsigned int x = 0;

	while (x < width) {
		unsigned int n = width - x < 25 ? width - x : 25;

		for (unsigned int i = 0; i < 32; i++)
			dst[i] = 0;
		for (unsigned int i = 0; i < n; i++) {
			unsigned int bit = i * 10;
			unsigned int byte = bit >> 3;
			unsigned int v = (src[x + i] & 0x3ffu) << (bit & 7);

			dst[byte] |= v & 0xff;
			dst[byte + 1] |= (v >> 8) & 0xff;
		}
		dst += 32;
		x += n;
	}
}

#endif
