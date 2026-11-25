// SPDX-License-Identifier: MIT
// Exercise selection using raw 25-pixel/32-byte groups; never open a device.
#define main surface_ir_program_main
#include "surface-ir.c"
#undef main
#include <assert.h>
#include <sys/wait.h>

static struct context ctx;
static uint8_t a[STRIDE * HEIGHT], b[STRIDE * HEIGHT];

static void uniform_raw(uint8_t *raw, unsigned v)
{
	for (unsigned y = 0; y < HEIGHT; y++) {
		uint8_t *row = raw + y * STRIDE;
		for (unsigned x = 0; x < WIDTH; x++) {
			unsigned bit = (x % 25) * 10, byte = (x / 25) * 32 + bit / 8;
			uint16_t word = v << (bit % 8);
			row[byte] |= word & 255;
			row[byte + 1] |= word >> 8;
		}
	}
}
// Use a real POSIX record lock on an ordinary file, in both process orders.
static void test_locks(void)
{
	char path[] = "/tmp/surface-ir-lock.XXXXXX";
	int tmp = mkstemp(path);
	assert(tmp >= 0);
	close(tmp);
	struct context c = {.media = path, .media_fd = -1, .fd = -1, .sub = -1};
	assert(claim_media(&c) == 0);
	pid_t pid = fork();
	assert(pid >= 0);
	if (pid == 0) {
		int fd = open(path, O_RDWR);
		if (fd < 0)
			_exit(1);
		int rc = lockf(fd, F_TLOCK, 0);
		_exit(rc < 0 && (errno == EAGAIN || errno == EACCES) ? 0 : 2);
	}
	int status;
	assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	release_media(&c);
	int fd = open(path, O_RDWR);
	assert(fd >= 0 && lockf(fd, F_TLOCK, 0) == 0);
	pid = fork();
	assert(pid >= 0);
	if (pid == 0) {
		int rc = claim_media(&c);
		_exit(rc == -1 && errno == EBUSY && c.media_fd == -1 ? 0 : 3);
	}
	assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	close(fd);
	assert(claim_media(&c) == 0);
	release_media(&c);
	// Errors before the video device opens cannot leak ownership.
	c.video = "/no-such-surface-ir-device";
	assert(open_sensor(&c) == -1 && c.media_fd == -1 && c.fd == -1);
	assert(unlink(path) == 0);
}
int main(void)
{
	test_locks();
	uniform_raw(a, 44);
	uniform_raw(b, 624);
	ctx.strobe = true;
	assert(!select_frame(&ctx, a, 0, 100));
	assert(select_frame(&ctx, b, 1, 200) && ctx.capture_ns == 200 && ctx.darkest == 624);
	assert(!select_frame(&ctx, b, 2, 300));
	assert(select_frame(&ctx, a, 3, 400) && ctx.capture_ns == 300 && ctx.brightest == 624);
	// Reacquire a consecutive pair after a dropped frame; don't emit old data.
	assert(!select_frame(&ctx, b, 4, 500));
	assert(!select_frame(&ctx, a, 6, 700));
	assert(select_frame(&ctx, b, 7, 800) && ctx.capture_ns == 800 && ctx.seq == 3);
	// Sequence wrap still denotes adjacent physical frames.
	assert(!select_frame(&ctx, b, UINT32_MAX, 900));
	assert(select_frame(&ctx, a, 0, 1000) && ctx.capture_ns == 900 && ctx.seq == 4);
	ctx.strobe = false;
	assert(select_frame(&ctx, a, 8, 1100) && ctx.capture_ns == 1100 && ctx.darkest == 44);
	assert(select_frame(&ctx, b, 9, 1200) && ctx.capture_ns == 1200 && ctx.brightest == 624);
	puts("Synthetic infrared pairs: bright-first, bright-second, missed frame, sequence wrap "
	     "and passive cadence passed; media lock excludes conflicting users in both "
	     "directions.");
	return 0;
}
