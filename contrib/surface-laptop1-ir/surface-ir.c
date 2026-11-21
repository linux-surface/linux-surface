// SPDX-License-Identifier: MIT
// Read an OV7251 IPU3 grayscale stream, using only the supported V4L2 interfaces.
// PipeWire format/stream API follows the structure of PipeWire's MIT video-src example.
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
extern char **environ;
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

enum { WIDTH = 640, HEIGHT = 480, STRIDE = 832, NBUF = 4, PIXELS = WIDTH * HEIGHT };
static const enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
struct context {
	const char *video, *sensor, *media, *receiver;
	int media_fd;
	unsigned sensor_id, receiver_id;
	bool requested, waiting, link_changed, format_changed;
	struct media_link_desc original_link;
	struct v4l2_subdev_format original_format;
	int fd, sub, old_gain, old_exposure, gain_changed, exposure_changed;
	struct {
		uint8_t *ptr;
		size_t len;
	} buf[NBUF];
	bool streaming, has_strobe, strobe, strobe_touched, pending;
	uint32_t first_sequence;
	uint64_t first_score;
	int64_t first_ns;
	uint8_t first_raw[STRIDE * HEIGHT];
	unsigned seq, low, high, darkest, brightest;
	int64_t capture_ns;
	uint8_t gray[PIXELS];
	struct pw_main_loop *main;
	struct pw_stream *stream;
	struct spa_source *io, *retry;
	int result;
};
static volatile sig_atomic_t stop;
static void stop_direct(int sig)
{
	(void)sig;
	stop = 1;
}
static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;
	do {
		r = ioctl(fd, req, arg);
	} while (r < 0 && errno == EINTR && !stop);
	return r;
}
// Query the kernel's standard boolean on the sensor, not a GPIO or a torch.
static int probe_strobe(const char *sensor)
{
	int fd = open(sensor, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
		return -1;
	struct v4l2_queryctrl q = {.id = V4L2_CID_FLASH_STROBE_OE};
	int r = xioctl(fd, VIDIOC_QUERYCTRL, &q), saved = errno;
	close(fd);
	if (r < 0) {
		if (saved == EINVAL)
			return 0;
		errno = saved;
		return -1;
	}
	if (q.flags & V4L2_CTRL_FLAG_DISABLED)
		return 0;
	if (q.type != V4L2_CTRL_TYPE_BOOLEAN || (q.flags & V4L2_CTRL_FLAG_READ_ONLY)) {
		errno = ENOTSUP;
		return -1;
	}
	return 1;
}
static int set_strobe(int fd, bool enabled)
{
	struct v4l2_control c = {.id = V4L2_CID_FLASH_STROBE_OE, .value = enabled};
	if (xioctl(fd, VIDIOC_S_CTRL, &c) < 0 || xioctl(fd, VIDIOC_G_CTRL, &c) < 0)
		return -1;
	if (c.value != (int)enabled) {
		errno = EIO;
		return -1;
	}
	return 0;
}
/* The node advertises only the tested native 30 Hz sensor mode (15 Hz lit).
 * Do not disturb an explicitly selected different timing or a test pattern. */
static int check_mode(int fd)
{
	struct v4l2_subdev_frame_interval fi = {.pad = 0, .which = V4L2_SUBDEV_FORMAT_ACTIVE};
	struct v4l2_control blanking = {.id = V4L2_CID_VBLANK};
	struct v4l2_control pattern = {.id = V4L2_CID_TEST_PATTERN};
	if (xioctl(fd, VIDIOC_SUBDEV_G_FRAME_INTERVAL, &fi) < 0 ||
	    xioctl(fd, VIDIOC_G_CTRL, &blanking) < 0 || xioctl(fd, VIDIOC_G_CTRL, &pattern) < 0)
		return -1;
	if (!fi.interval.numerator || 30ULL * fi.interval.numerator != fi.interval.denominator ||
	    blanking.value != 1244 || pattern.value) {
		errno = ENOTSUP;
		return -1;
	}
	return 0;
}
static int change_control(int fd, unsigned id, int target, int *old)
{
	struct v4l2_control c = {.id = id};
	if (xioctl(fd, VIDIOC_G_CTRL, &c) < 0)
		return 0;
	*old = c.value;
	c.value = target;
	return xioctl(fd, VIDIOC_S_CTRL, &c) == 0;
}
static void restore(int fd, unsigned id, int val)
{
	struct v4l2_control c = {.id = id, .value = val};
	if (xioctl(fd, VIDIOC_S_CTRL, &c) < 0)
		perror("restore infrared camera control");
}
// The IPU3 PipelineHandler locks the CIO2 *media* device with lockf
// (not BSD flock, and not the capture node). Reading the graph at startup
// is harmless; we change links/formats only after acquiring this lock.
static int claim_media(struct context *d)
{
	d->media_fd = open(d->media, O_RDWR | O_CLOEXEC);
	if (d->media_fd < 0)
		return -1;
	if (lockf(d->media_fd, F_TLOCK, 0) < 0) {
		int err = errno;
		close(d->media_fd);
		d->media_fd = -1;
		errno = (err == EAGAIN || err == EACCES) ? EBUSY : err;
		return -1;
	}
	return 0;
}
static void release_media(struct context *d)
{
	// The last close releases the POSIX lock, including on abnormal exit.
	if (d->media_fd >= 0) {
		close(d->media_fd);
		d->media_fd = -1;
	}
}
static int configure_graph(struct context *d)
{
	struct media_entity_desc info = {.id = d->sensor_id};
	struct media_pad_desc pads[8];
	struct media_link_desc links[32];
	struct media_links_enum list = {.entity = d->sensor_id, .pads = pads, .links = links};
	if (xioctl(d->media_fd, MEDIA_IOC_ENUM_ENTITIES, &info) < 0)
		return -1;
	if (info.id != d->sensor_id || info.pads > 8 || info.links > 32) {
		errno = EOVERFLOW;
		return -1;
	}
	if (xioctl(d->media_fd, MEDIA_IOC_ENUM_LINKS, &list) < 0)
		return -1;
	bool found = false;
	for (unsigned i = 0; i < info.links; i++) {
		struct media_link_desc link = links[i];
		if (link.source.entity != d->sensor_id || link.source.index ||
		    link.sink.entity != d->receiver_id || link.sink.index)
			continue;
		d->original_link = link;
		if (!(link.flags & MEDIA_LNK_FL_ENABLED)) {
			link.flags |= MEDIA_LNK_FL_ENABLED;
			if (xioctl(d->media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0)
				return -1;
			d->link_changed = true;
		}
		found = true;
		break;
	}
	if (!found) {
		errno = ENOLINK;
		return -1;
	}
	int fd = open(d->receiver, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -1;
	struct v4l2_subdev_format f = {.which = V4L2_SUBDEV_FORMAT_ACTIVE, .pad = 0};
	int rc = xioctl(fd, VIDIOC_SUBDEV_G_FMT, &f);
	if (rc == 0 && (f.format.width != WIDTH || f.format.height != HEIGHT ||
			f.format.code != MEDIA_BUS_FMT_Y10_1X10)) {
		d->original_format = f;
		f.format.width = WIDTH;
		f.format.height = HEIGHT;
		f.format.code = MEDIA_BUS_FMT_Y10_1X10;
		rc = xioctl(fd, VIDIOC_SUBDEV_S_FMT, &f);
		if (rc == 0) {
			d->format_changed = true;
			if (f.format.width != WIDTH || f.format.height != HEIGHT ||
			    f.format.code != MEDIA_BUS_FMT_Y10_1X10) {
				errno = EINVAL;
				rc = -1;
			}
		}
	}
	int err = errno;
	close(fd);
	errno = err;
	return rc;
}
static void restore_graph(struct context *d)
{
	if (d->format_changed) {
		int fd = open(d->receiver, O_RDWR | O_CLOEXEC);
		if (fd < 0 || xioctl(fd, VIDIOC_SUBDEV_S_FMT, &d->original_format) < 0)
			perror("restore infrared CSI receiver format");
		if (fd >= 0)
			close(fd);
		d->format_changed = false;
	}
	if (d->link_changed) {
		if (xioctl(d->media_fd, MEDIA_IOC_SETUP_LINK, &d->original_link) < 0)
			perror("restore infrared sensor link");
		d->link_changed = false;
	}
}
static void close_sensor(struct context *d)
{
	if (d->fd < 0) {
		release_media(d);
		return;
	}
	// Clear the request on normal disconnect and partial initialization.
	// STREAMOFF is an independent hardware shutdown path in the driver.
	if (d->strobe_touched && d->sub >= 0 && set_strobe(d->sub, false) < 0)
		perror("disable infrared strobe");
	d->strobe_touched = false;
	if (d->streaming) {
		enum v4l2_buf_type t = type;
		if (xioctl(d->fd, VIDIOC_STREAMOFF, &t) < 0)
			perror("stop infrared camera");
	}
	for (unsigned i = 0; i < NBUF; i++)
		if (d->buf[i].ptr) {
			munmap(d->buf[i].ptr, d->buf[i].len);
			d->buf[i].ptr = NULL;
		}
	if (d->sub >= 0) {
		if (d->exposure_changed)
			restore(d->sub, V4L2_CID_EXPOSURE, d->old_exposure);
		if (d->gain_changed)
			restore(d->sub, V4L2_CID_ANALOGUE_GAIN, d->old_gain);
		close(d->sub);
	}
	close(d->fd);
	d->fd = d->sub = -1;
	d->exposure_changed = d->gain_changed = 0;
	restore_graph(d);
	release_media(d);
	d->streaming = d->pending = false;
	d->seq = 0;
	memset(d->gray, 0, sizeof(d->gray));
	memset(d->first_raw, 0, sizeof(d->first_raw));
}
static int open_sensor(struct context *d)
{
	struct v4l2_format f = {.type = type};
	struct v4l2_requestbuffers req = {.count = NBUF, .type = type, .memory = V4L2_MEMORY_MMAP};
	enum v4l2_buf_type t = type;
	const char *stage = "claim shared IPU3 capture controller";
	if (claim_media(d) < 0)
		goto fail;
	stage = "open capture node";
	d->fd = open(d->video, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (d->fd < 0)
		goto fail;
	f.fmt.pix_mp.width = WIDTH;
	f.fmt.pix_mp.height = HEIGHT;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_IPU3_Y10;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	stage = "set infrared capture format";
	if (xioctl(d->fd, VIDIOC_S_FMT, &f) < 0)
		goto fail;
	if (f.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_IPU3_Y10 || f.fmt.pix_mp.width != WIDTH ||
	    f.fmt.pix_mp.height != HEIGHT || f.fmt.pix_mp.plane_fmt[0].bytesperline != STRIDE) {
		errno = EINVAL;
		goto fail;
	}
	stage = "allocate infrared capture buffers";
	if (xioctl(d->fd, VIDIOC_REQBUFS, &req) < 0)
		goto fail;
	if (req.count < 2 || req.count > NBUF) {
		errno = EINVAL;
		goto fail;
	}
	for (unsigned i = 0; i < req.count; i++) {
		struct v4l2_plane p = {0};
		struct v4l2_buffer b = {.type = type,
					.memory = V4L2_MEMORY_MMAP,
					.index = i,
					.length = 1,
					.m.planes = &p};
		if (xioctl(d->fd, VIDIOC_QUERYBUF, &b) < 0)
			goto fail;
		if (p.length < STRIDE * HEIGHT) {
			errno = EINVAL;
			goto fail;
		}
		void *v =
		    mmap(NULL, p.length, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, p.m.mem_offset);
		if (v == MAP_FAILED)
			goto fail;
		d->buf[i].ptr = v;
		d->buf[i].len = p.length;
		if (xioctl(d->fd, VIDIOC_QBUF, &b) < 0)
			goto fail;
	}
	stage = "configure locked infrared input link";
	if (configure_graph(d) < 0)
		goto fail;
	stage = "open infrared sensor";
	d->sub = open(d->sensor, O_RDWR | O_CLOEXEC);
	if (d->sub < 0)
		goto fail;
	stage = "verify the default 30 Hz sensor mode and disabled test pattern";
	if (check_mode(d->sub) < 0)
		goto fail;
	// We own the CIO2 capture queue now. Clear any stale request before
	// changing timing, then request *only* exposure-synchronized pulses.
	if (d->has_strobe) {
		stage = "clear infrared strobe";
		d->strobe_touched = true;
		if (set_strobe(d->sub, false) < 0)
			goto fail;
	}
	stage = "set infrared capture exposure and gain";
	d->gain_changed =
	    change_control(d->sub, V4L2_CID_ANALOGUE_GAIN, d->strobe ? 128 : 256, &d->old_gain);
	d->exposure_changed =
	    change_control(d->sub, V4L2_CID_EXPOSURE, d->strobe ? 504 : 1500, &d->old_exposure);
	if (d->strobe && (!d->gain_changed || !d->exposure_changed))
		goto fail;
	stage = "start infrared camera";
	if (xioctl(d->fd, VIDIOC_STREAMON, &t) < 0)
		goto fail;
	d->streaming = true;
	d->seq = 0;
	d->low = 0;
	d->high = 1023;
	if (d->strobe) {
		// Request pulses only while we hold a live stream. If the process
		// dies here, the driver's stream-off path clears the request.
		stage = "enable synchronized infrared strobe";
		if (set_strobe(d->sub, true) < 0)
			goto fail;
	}
	return 0;
fail: {
	int err = errno;
	if (err != EBUSY || !d->waiting)
		perror(stage);
	close_sensor(d);
	errno = err;
	return -1;
}
}
// IPU3 packs 25 little-endian 10-bit samples in each 32-byte group.
// Stretch the 2nd-98th percentile to make ambient infrared visible, even at dusk.
static inline int pixel(const uint8_t *row, unsigned x)
{
	unsigned bit = (x % 25) * 10;
	const uint8_t *p = row + (x / 25) * 32 + bit / 8;
	return ((p[0] | p[1] << 8) >> (bit % 8)) & 1023;
}
static void decode(struct context *d, const uint8_t *raw)
{
	unsigned h[1024] = {0}, n = 0, lo = 0, hi = 1023;
	for (unsigned y = 0; y < HEIGHT; y += 4)
		for (unsigned x = 0; x < WIDTH; x += 4) {
			h[pixel(raw + y * STRIDE, x)]++;
			n++;
		}
	unsigned sum = 0;
	while (lo < 1023 && sum + h[lo] < n / 50)
		sum += h[lo++];
	sum = 0;
	while (hi > 0 && sum + h[hi] < n / 50)
		sum += h[hi--];
	d->darkest = 0;
	while (d->darkest < 1023 && !h[d->darkest])
		d->darkest++;
	d->brightest = 1023;
	while (d->brightest > 0 && !h[d->brightest])
		d->brightest--;
	if (d->seq == 0) {
		d->low = lo;
		d->high = hi;
	} else {
		d->low = (3 * d->low + lo + 2) / 4;
		d->high = (3 * d->high + hi + 2) / 4;
	}
	int span = (int)d->high - (int)d->low;
	if (span < 16)
		span = 16;
	uint8_t lut[1024];
	for (int i = 0; i < 1024; i++) {
		int v = (i - (int)d->low) * 255 / span;
		lut[i] = v < 0 ? 0 : v > 255 ? 255 : v;
	}
	for (unsigned y = 0; y < HEIGHT; y++)
		for (unsigned x = 0; x < WIDTH; x++)
			d->gray[PIXELS - 1 - y * WIDTH - x] = lut[pixel(raw + y * STRIDE, x)];
	d->seq++;
}
// The kernel pulses on alternate sensor frames. Select on original 10-bit
// light levels BEFORE contrast stretching; operate on complete adjacent pairs
// so the output clock need not assume whether odd or even frames were lit.
static bool select_frame(struct context *d, const uint8_t *raw, uint32_t sequence, int64_t ns)
{
	if (d->strobe) {
		uint64_t score = 0;
		for (unsigned y = 0; y < HEIGHT; y += 8)
			for (unsigned x = 0; x < WIDTH; x += 8)
				score += pixel(raw + y * STRIDE, x);
		if (!d->pending || sequence != d->first_sequence + 1u) {
			memcpy(d->first_raw, raw, sizeof(d->first_raw));
			d->first_sequence = sequence;
			d->first_score = score;
			d->first_ns = ns;
			d->pending = true;
			return false;
		}
		if (d->first_score >= score) {
			raw = d->first_raw;
			ns = d->first_ns;
		}
		d->pending = false;
	}
	decode(d, raw);
	d->capture_ns = ns;
	return true;
}
static int read_frame(struct context *d)
{
	struct v4l2_plane p = {0};
	struct v4l2_buffer b = {
	    .type = type, .memory = V4L2_MEMORY_MMAP, .length = 1, .m.planes = &p};
	if (xioctl(d->fd, VIDIOC_DQBUF, &b) < 0)
		return errno == EAGAIN ? 0 : -1;
	if (b.index >= NBUF || p.data_offset > p.bytesused || p.bytesused > d->buf[b.index].len) {
		errno = EIO;
		return -1;
	}
	bool good =
	    !(b.flags & V4L2_BUF_FLAG_ERROR) && p.bytesused - p.data_offset >= STRIDE * HEIGHT;
	if (good) {
		int64_t ns;
		if ((b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC &&
		    (b.timestamp.tv_sec || b.timestamp.tv_usec))
			ns = (int64_t)b.timestamp.tv_sec * SPA_NSEC_PER_SEC +
			     b.timestamp.tv_usec * 1000;
		else {
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			ns = (int64_t)now.tv_sec * SPA_NSEC_PER_SEC + now.tv_nsec;
		}
		good = select_frame(d, d->buf[b.index].ptr + p.data_offset, b.sequence, ns);
	} else
		d->pending = false;
	if (xioctl(d->fd, VIDIOC_QBUF, &b) < 0)
		return -1;
	return good;
}
static void state_note(const char *message)
{
	fprintf(stderr, "%s\n", message);
	pid_t child;
	char *argv[] = {"notify-send",
			"--transient",
			"-t",
			"3500",
			"-a",
			"Surface infrared camera",
			"Surface infrared camera",
			(char *)message,
			NULL};
	(void)posix_spawnp(&child, argv[0], NULL, NULL, argv, environ);
}
static void camera_ready(void *arg, int fd, uint32_t mask)
{
	(void)fd;
	struct context *d = arg;
	if ((mask & (SPA_IO_ERR | SPA_IO_HUP)) != 0) {
		d->result = 1;
		pw_main_loop_quit(d->main);
		return;
	}
	int r = read_frame(d);
	if (r < 0) {
		perror("read infrared camera");
		d->result = 1;
		pw_main_loop_quit(d->main);
		return;
	}
	if (r && pw_stream_is_driving(d->stream))
		pw_stream_trigger_process(d->stream);
}
static void retry_pw(void *arg, uint64_t expirations)
{
	(void)expirations;
	struct context *d = arg;
	if (!d->requested || d->streaming)
		return;
	struct pw_loop *loop = pw_main_loop_get_loop(d->main);
	if (open_sensor(d) < 0) {
		if (errno == EBUSY) {
			if (!d->waiting)
				state_note("Infrared camera waiting: the shared camera controller "
					   "is busy.");
			d->waiting = true;
			struct timespec delay = {.tv_nsec = 350000000};
			if (pw_loop_update_timer(loop, d->retry, &delay, NULL, false) >= 0)
				return;
		}
		d->result = 1;
		pw_main_loop_quit(d->main);
		return;
	}
	d->waiting = false;
	d->io = pw_loop_add_io(loop, d->fd, SPA_IO_IN | SPA_IO_ERR | SPA_IO_HUP, false,
			       camera_ready, d);
	if (!d->io) {
		d->result = 1;
		pw_main_loop_quit(d->main);
		return;
	}
	state_note(d->strobe ? "Infrared camera on: synchronized illumination active."
			     : "Infrared camera on: using ambient light.");
}
static void pw_state(void *arg, enum pw_stream_state old, enum pw_stream_state state,
		     const char *msg)
{
	(void)old;
	struct context *d = arg;
	switch (state) {
	case PW_STREAM_STATE_STREAMING:
		d->requested = true;
		retry_pw(d, 0);
		break;
	case PW_STREAM_STATE_PAUSED:
		d->requested = d->waiting = false;
		if (d->retry)
			pw_loop_update_timer(pw_main_loop_get_loop(d->main), d->retry, NULL, NULL,
					     false);
		if (d->io) {
			pw_loop_destroy_source(pw_main_loop_get_loop(d->main), d->io);
			d->io = NULL;
		}
		if (d->streaming) {
			close_sensor(d);
			state_note("Infrared camera off.");
		}
		break;
	case PW_STREAM_STATE_ERROR:
		fprintf(stderr, "PipeWire: %s\n", msg ? msg : "error");
		d->result = 1;
		pw_main_loop_quit(d->main);
		break;
	case PW_STREAM_STATE_UNCONNECTED:
		pw_main_loop_quit(d->main);
		break;
	default:
		break;
	}
}
static void pw_param(void *arg, uint32_t id, const struct spa_pod *format)
{
	if (id != SPA_PARAM_Format || !format)
		return;
	struct context *d = arg;
	uint8_t mem[512];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(mem, sizeof(mem));
	const struct spa_pod *params[2];
	params[0] = spa_pod_builder_add_object(
	    &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers, SPA_PARAM_BUFFERS_buffers,
	    SPA_POD_CHOICE_RANGE_Int(4, 2, 8), SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
	    SPA_PARAM_BUFFERS_size, SPA_POD_Int(PIXELS * 2), SPA_PARAM_BUFFERS_stride,
	    SPA_POD_Int(WIDTH * 2));
	params[1] = spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
					       SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header),
					       SPA_PARAM_META_size,
					       SPA_POD_Int(sizeof(struct spa_meta_header)));
	pw_stream_update_params(d->stream, params, 2);
}
static void pw_process(void *arg)
{
	struct context *d = arg;
	struct pw_buffer *b = pw_stream_dequeue_buffer(d->stream);
	if (!b)
		return;
	struct spa_buffer *s = b->buffer;
	if (s->n_datas && s->datas[0].chunk) {
		struct spa_data *p = &s->datas[0];
		p->chunk->size = 0;
		if (d->seq && p->data && p->maxsize >= 2 * PIXELS) {
			uint8_t *out = p->data;
			for (unsigned i = 0; i < PIXELS; i++) {
				out[2 * i] = (uint8_t)(16 + d->gray[i] * 219 / 255);
				out[2 * i + 1] = 128;
			}
			p->chunk->offset = 0;
			p->chunk->size = 2 * PIXELS;
			p->chunk->stride = 2 * WIDTH;
			struct spa_meta_header *header =
			    spa_buffer_find_meta_data(s, SPA_META_Header, sizeof(*header));
			if (header) {
				header->flags = 0;
				header->seq = d->seq;
				header->pts = d->capture_ns;
				header->dts_offset = 0;
			}
		}
	}
	pw_stream_queue_buffer(d->stream, b);
}
static void quit_pw(void *arg, int sig)
{
	(void)sig;
	pw_main_loop_quit(((struct context *)arg)->main);
}
static const struct pw_stream_events events = {PW_VERSION_STREAM_EVENTS, .state_changed = pw_state,
					       .param_changed = pw_param, .process = pw_process};
static int publish(struct context *d)
{
	pw_init(NULL, NULL);
	signal(SIGCHLD, SIG_IGN);
	d->main = pw_main_loop_new(NULL);
	if (!d->main)
		return 1;
	struct pw_loop *loop = pw_main_loop_get_loop(d->main);
	d->retry = pw_loop_add_timer(loop, retry_pw, d);
	if (!d->retry) {
		d->result = 1;
		goto cleanup;
	}
	pw_loop_add_signal(loop, SIGTERM, quit_pw, d);
	pw_loop_add_signal(loop, SIGINT, quit_pw, d);
	d->stream = pw_stream_new_simple(
	    loop, "Surface infrared camera",
	    pw_properties_new(PW_KEY_MEDIA_CLASS, "Video/Source", PW_KEY_NODE_NAME,
			      "surface-laptop-ov7251-ir", PW_KEY_NODE_DESCRIPTION,
			      "Surface Infrared (monochrome)", PW_KEY_MEDIA_ROLE, "Camera",
			      "priority.session", "1", "priority.driver", "1", NULL),
	    &events, d);
	if (!d->stream) {
		perror("create PipeWire stream");
		d->result = 1;
		goto cleanup;
	}
	uint8_t mem[512];
	struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(mem, sizeof(mem));
	const struct spa_pod *fmt = spa_pod_builder_add_object(
	    &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType,
	    SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
	    SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
	    SPA_POD_Id(SPA_VIDEO_FORMAT_YUY2), SPA_FORMAT_VIDEO_size,
	    SPA_POD_Rectangle(&SPA_RECTANGLE(WIDTH, HEIGHT)), SPA_FORMAT_VIDEO_framerate,
	    SPA_POD_Fraction(&SPA_FRACTION(d->strobe ? 15 : 30, 1)));
	int r = pw_stream_connect(d->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
				  PW_STREAM_FLAG_DRIVER | PW_STREAM_FLAG_MAP_BUFFERS, &fmt, 1);
	if (r < 0) {
		fprintf(stderr, "PipeWire: %s\n", strerror(-r));
		d->result = 1;
		goto cleanup;
	}
	fprintf(
	    stderr,
	    "Select ‘Surface Infrared (monochrome)’ in a PipeWire camera app. Ctrl+C stops it.\n");
	pw_main_loop_run(d->main);
cleanup:
	d->requested = false;
	if (d->retry) {
		pw_loop_destroy_source(loop, d->retry);
		d->retry = NULL;
	}
	if (d->io) {
		pw_loop_destroy_source(loop, d->io);
		d->io = NULL;
	}
	if (d->streaming) {
		close_sensor(d);
		state_note("Infrared camera off.");
	}
	if (d->stream)
		pw_stream_destroy(d->stream);
	pw_main_loop_destroy(d->main);
	pw_deinit();
	return d->result;
}
int main(int argc, char **argv)
{
	static struct context d = {.fd = -1, .sub = -1, .media_fd = -1};
	if ((argc != 8 && argc != 9) || (argc == 9 && strcmp(argv[8], "--passive")) ||
	    (strcmp(argv[1], "pw") && strcmp(argv[1], "check") && strcmp(argv[1], "rate"))) {
		fprintf(stderr, "Use surface-laptop1-ir.\n");
		return 2;
	}
	d.video = argv[2];
	d.sensor = argv[3];
	d.media = argv[4];
	d.receiver = argv[5];
	char *end;
	d.sensor_id = strtoul(argv[6], &end, 10);
	if (*end || !d.sensor_id)
		return 2;
	d.receiver_id = strtoul(argv[7], &end, 10);
	if (*end || !d.receiver_id)
		return 2;
	int available = probe_strobe(d.sensor);
	if (available < 0) {
		perror("query synchronized IR strobe");
		return 1;
	}
	d.has_strobe = available;
	d.strobe = argc == 8;
	if (d.strobe && !available) {
		fprintf(
		    stderr,
		    "This driver does not expose synchronized strobe control; use --passive.\n");
		return 3;
	}
	if (!strcmp(argv[1], "rate")) {
		printf("%d\n", d.strobe ? 15 : 30);
		return 0;
	}
	fprintf(stderr, "Surface IR: %s\n",
		d.strobe       ? "on-demand synchronized strobe; 15 selected frames/s"
		: d.has_strobe ? "passive capture requested; 30 frames/s"
			       : "no kernel strobe control; passive capture at 30 frames/s");
	if (!strcmp(argv[1], "pw"))
		return publish(&d);
	int64_t first_ns = 0, last_ns = 0;
	signal(SIGINT, stop_direct);
	signal(SIGTERM, stop_direct);
	signal(SIGPIPE, SIG_IGN);
	if (open_sensor(&d) < 0)
		return 1;
	struct pollfd p = {.fd = d.fd, .events = POLLIN};
	int rc = 0;
	while (!stop && d.seq < 10) {
		int n = poll(&p, 1, 2000);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			fprintf(stderr, "Infrared camera stopped responding.\n");
			rc = 1;
			break;
		}
		int r = read_frame(&d);
		if (r < 0) {
			perror("get infrared frame");
			rc = 1;
			break;
		}
		if (!r)
			continue;
		if (!first_ns)
			first_ns = d.capture_ns;
		if (last_ns && d.capture_ns <= last_ns) {
			rc = 1;
			break;
		}
		last_ns = d.capture_ns;
	}
	if (d.seq != 10 || last_ns <= first_ns)
		rc = 1;
	if (!rc)
		printf("%u actual IR frames, 640x480 at %.1f fps; final 10-bit sample range %u..%u "
		       "(2nd–98th "
		       "percentile %u..%u)\n",
		       d.seq, (d.seq - 1) * 1.e9 / (last_ns - first_ns), d.darkest, d.brightest,
		       d.low, d.high);
	close_sensor(&d);
	return rc;
}
