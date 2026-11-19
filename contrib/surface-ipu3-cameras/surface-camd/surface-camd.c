// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * surface-camd - userspace camera driver for the IPU3 cameras on
 * Surface Pro 4/5/6, Surface Book 1/2 and other IPU3 devices.
 *
 * It captures raw sensor data straight from the IPU3 CIO2 (CSI-2 receiver),
 * does the image processing in software, and publishes the result as a
 * normal webcam through a v4l2loopback device. It does not use libcamera
 * or the IPU3 ImgU, so it works where those are broken or missing, and it
 * supports the OV7251 IR camera, which the libcamera IPU3 pipeline cannot
 * handle (the ImgU only processes Bayer sensors).
 *
 * Processing (colour sensors):
 *   IPU3 10-bit packed Bayer -> black level -> 2x2 demosaic (half
 *   resolution, no zipper artifacts) -> grey-world white balance ->
 *   gamma -> YUYV
 * Processing (IR sensor):
 *   IPU3 10-bit packed Y10 -> gamma -> GREY (8-bit)
 * Both use auto exposure, which sets the sensor's exposure and analogue gain
 * controls, plus a software gain once those are at their limits.
 *
 * The sensor only streams while an application is reading the loopback
 * device (v4l2loopback "client usage" events), so the camera and its privacy
 * LED are off when nothing uses them.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>

#include "ipu3-unpack.h"

/* v4l2loopback private event: number of capture clients changed. */
#ifndef V4L2_EVENT_PRI_CLIENT_USAGE
#define V4L2_EVENT_PRI_CLIENT_USAGE (V4L2_EVENT_PRIVATE_START + 0x08E00000 + 1)
#endif

#define NUM_BUFS 4
#define MAX_ENTITIES 128

static volatile sig_atomic_t g_stop;
static int g_verbose;

static void logmsg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

#define dbg(...) do { if (g_verbose) logmsg(__VA_ARGS__); } while (0)
#define die(...) do { logmsg(__VA_ARGS__); exit(1); } while (0)

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r < 0 && errno == EINTR);
	return r;
}

/* ------------------------------------------------------------------ media */

struct entity {
	struct media_entity_desc desc;
};

struct media {
	int fd;
	struct entity ent[MAX_ENTITIES];
	int n;
};

static int open_cio2_media(struct media *m)
{
	char path[32];

	for (int i = 0; i < 32; i++) {
		struct media_device_info info;
		int fd;

		snprintf(path, sizeof(path), "/dev/media%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(&info, 0, sizeof(info));
		if (!xioctl(fd, MEDIA_IOC_DEVICE_INFO, &info) &&
		    !strcmp(info.driver, "ipu3-cio2")) {
			dbg("using %s (%s)", path, info.model);
			m->fd = fd;
			return 0;
		}
		close(fd);
	}
	return -1;
}

static void enum_entities(struct media *m)
{
	struct media_entity_desc d;

	m->n = 0;
	memset(&d, 0, sizeof(d));
	d.id = MEDIA_ENT_ID_FLAG_NEXT;
	while (m->n < MAX_ENTITIES && !xioctl(m->fd, MEDIA_IOC_ENUM_ENTITIES, &d)) {
		m->ent[m->n++].desc = d;
		d.id |= MEDIA_ENT_ID_FLAG_NEXT;
	}
}

static struct entity *find_entity_by_id(struct media *m, uint32_t id)
{
	for (int i = 0; i < m->n; i++)
		if (m->ent[i].desc.id == id)
			return &m->ent[i];
	return NULL;
}

static struct entity *find_entity_by_prefix(struct media *m, const char *prefix)
{
	for (int i = 0; i < m->n; i++)
		if (!strncmp(m->ent[i].desc.name, prefix, strlen(prefix)))
			return &m->ent[i];
	return NULL;
}

/* Find the first link from @src's source pads; return its descriptor. */
static int find_link_from(struct media *m, struct entity *src,
			  const char *sink_prefix, struct media_link_desc *out)
{
	struct media_links_enum le;
	struct media_pad_desc *pads;
	struct media_link_desc *links;
	int ret = -1;

	pads = calloc(src->desc.pads ? src->desc.pads : 1, sizeof(*pads));
	links = calloc(src->desc.links ? src->desc.links : 1, sizeof(*links));
	memset(&le, 0, sizeof(le));
	le.entity = src->desc.id;
	le.pads = pads;
	le.links = links;
	if (xioctl(m->fd, MEDIA_IOC_ENUM_LINKS, &le) < 0)
		goto out;

	for (unsigned int i = 0; i < src->desc.links; i++) {
		struct entity *sink;

		if (links[i].source.entity != src->desc.id)
			continue;
		sink = find_entity_by_id(m, links[i].sink.entity);
		if (sink && !strncmp(sink->desc.name, sink_prefix, strlen(sink_prefix))) {
			*out = links[i];
			ret = 0;
			break;
		}
	}
out:
	free(pads);
	free(links);
	return ret;
}

static int devnode_path(const struct entity *e, char *buf, size_t len)
{
	char sys[64], line[256];
	FILE *f;
	int ret = -1;

	snprintf(sys, sizeof(sys), "/sys/dev/char/%u:%u/uevent",
		 e->desc.dev.major, e->desc.dev.minor);
	f = fopen(sys, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "DEVNAME=", 8)) {
			line[strcspn(line, "\n")] = 0;
			snprintf(buf, len, "/dev/%s", line + 8);
			ret = 0;
			break;
		}
	}
	fclose(f);
	return ret;
}

/* --------------------------------------------------------------- controls */

struct ctrl {
	uint32_t id;
	bool present;
	int32_t min, max, step, def, cur;
};

static void ctrl_query(int fd, struct ctrl *c, uint32_t id)
{
	struct v4l2_queryctrl q = { .id = id };
	struct v4l2_control v = { .id = id };

	c->id = id;
	c->present = false;
	if (xioctl(fd, VIDIOC_QUERYCTRL, &q) < 0 ||
	    (q.flags & V4L2_CTRL_FLAG_DISABLED))
		return;
	c->present = true;
	c->min = q.minimum;
	c->max = q.maximum;
	c->step = q.step ? q.step : 1;
	c->def = q.default_value;
	c->cur = !xioctl(fd, VIDIOC_G_CTRL, &v) ? v.value : q.default_value;
}

static void ctrl_set(int fd, struct ctrl *c, int32_t val)
{
	struct v4l2_control v = { .id = c->id };

	if (!c->present)
		return;
	if (val < c->min)
		val = c->min;
	if (val > c->max)
		val = c->max;
	val = c->min + (val - c->min) / c->step * c->step;
	if (val == c->cur)
		return;
	v.value = val;
	if (!xioctl(fd, VIDIOC_S_CTRL, &v))
		c->cur = val;
}

/* ------------------------------------------------------------------ state */

enum kind { KIND_BAYER, KIND_MONO };

struct cam {
	/* configuration */
	const char *sensor_prefix;
	const char *output;
	const char *led_path;
	unsigned int req_w, req_h;
	int black_level;
	bool always_on;
	int wait_s;		/* wait for the sensor to appear */
	double target;		/* AE target, fraction of full scale (linear) */
	double saturation;

	/* devices */
	struct media media;
	int sensor_fd, csi2_fd, video_fd, out_fd;
	struct media_link_desc link;	/* sensor -> CSI-2 receiver */
	char sensor_name[32], csi2_name[32];

	/* formats */
	enum kind kind;
	uint32_t mbus_code;
	unsigned int w, h, bpl;		/* raw */
	unsigned int ow, oh;		/* output */
	int r_idx, b_idx;		/* positions in 2x2 Bayer block */
	bool rot180;

	/* buffers (only allocated while streaming) */
	void *buf[NUM_BUFS];
	size_t buflen[NUM_BUFS];
	unsigned int nbufs;
	bool streaming;
	bool wanted;			/* an application is reading the output */
	time_t last_try;
	unsigned long frames, dropped;
	uint16_t *line0, *line1;
	uint8_t *rgb_row, *out;
	size_t out_size;

	/* 3A */
	struct ctrl exposure, again;
	double soft_gain;
	double wb_r, wb_b;
	unsigned int frame;
	uint8_t lut[1024];
};

static void build_lut(struct cam *c)
{
	/* sRGB-like transfer curve from 10-bit linear to 8-bit. */
	for (int i = 0; i < 1024; i++) {
		double x = i / 1023.0;
		double y = x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1 / 2.4) - 0.055;

		c->lut[i] = (uint8_t)lrint(y * 255.0);
	}
}

static int bayer_layout(uint32_t code, int *r, int *b)
{
	/* Index in the 2x2 block: 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right. */
	switch (code) {
	case MEDIA_BUS_FMT_SBGGR10_1X10: *b = 0; *r = 3; return 0;
	case MEDIA_BUS_FMT_SGBRG10_1X10: *b = 1; *r = 2; return 0;
	case MEDIA_BUS_FMT_SGRBG10_1X10: *r = 1; *b = 2; return 0;
	case MEDIA_BUS_FMT_SRGGB10_1X10: *r = 0; *b = 3; return 0;
	}
	return -1;
}

static uint32_t cio2_fourcc(uint32_t code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SBGGR10_1X10: return V4L2_PIX_FMT_IPU3_SBGGR10;
	case MEDIA_BUS_FMT_SGBRG10_1X10: return V4L2_PIX_FMT_IPU3_SGBRG10;
	case MEDIA_BUS_FMT_SGRBG10_1X10: return V4L2_PIX_FMT_IPU3_SGRBG10;
	case MEDIA_BUS_FMT_SRGGB10_1X10: return V4L2_PIX_FMT_IPU3_SRGGB10;
	case MEDIA_BUS_FMT_Y10_1X10:     return V4L2_PIX_FMT_IPU3_Y10;
	}
	return 0;
}

/* -------------------------------------------------------------- IR LED */

static void led_set(const struct cam *c, bool on)
{
	char path[512], val[16] = "1";
	FILE *f;

	if (!c->led_path)
		return;
	snprintf(path, sizeof(path), "%s/max_brightness", c->led_path);
	f = fopen(path, "r");
	if (f) {
		if (!fgets(val, sizeof(val), f))
			strcpy(val, "1");
		fclose(f);
	}
	snprintf(path, sizeof(path), "%s/brightness", c->led_path);
	f = fopen(path, "w");
	if (!f) {
		dbg("cannot write %s: %s", path, strerror(errno));
		return;
	}
	fputs(on ? val : "0", f);
	fclose(f);
}

static char *find_ir_led(void)
{
	DIR *d = opendir("/sys/class/leds");
	struct dirent *e;
	char *found = NULL;

	if (!d)
		return NULL;
	while ((e = readdir(d))) {
		if (strstr(e->d_name, "ir_flood") ||
		    (strstr(e->d_name, "INT347E") && !strstr(e->d_name, "privacy"))) {
			if (asprintf(&found, "/sys/class/leds/%s", e->d_name) < 0)
				found = NULL;
			break;
		}
	}
	closedir(d);
	return found;
}

/* ------------------------------------------------------------ pipeline */

/*
 * Enable the sensor -> CSI-2 link and set the formats on the sensor, the
 * CSI-2 receiver and the CIO2 video node. Done before every stream start:
 * other programs (libcamera through PipeWire, a suspend/resume) can change
 * any of it while surface-camd is idle.
 */
static int configure_pipeline(struct cam *c, bool first)
{
	struct media_link_desc link = c->link;
	struct v4l2_subdev_format sf;
	struct v4l2_format vf;

	link.flags |= MEDIA_LNK_FL_ENABLED;
	if (xioctl(c->media.fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
		logmsg("Enabling link %s -> %s failed: %s%s", c->sensor_name, c->csi2_name,
		       strerror(errno), errno == EBUSY ? " (another program is streaming from the camera)" : "");
		return -1;
	}

	memset(&sf, 0, sizeof(sf));
	sf.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sf.pad = c->link.source.index;
	sf.format.code = c->mbus_code;
	sf.format.width = c->w;
	sf.format.height = c->h;
	sf.format.field = V4L2_FIELD_NONE;
	if (xioctl(c->sensor_fd, VIDIOC_SUBDEV_S_FMT, &sf) < 0) {
		logmsg("Setting sensor format failed: %s", strerror(errno));
		return -1;
	}
	if (sf.format.code != c->mbus_code || (!first && (sf.format.width != c->w || sf.format.height != c->h))) {
		logmsg("Sensor changed the format to %ux%u code 0x%04x", sf.format.width,
		       sf.format.height, sf.format.code);
		return -1;
	}
	c->w = sf.format.width;		/* the sensor may round the requested size */
	c->h = sf.format.height;

	/* CSI-2 receiver sink pad gets the same format. */
	sf.pad = c->link.sink.index;
	if (xioctl(c->csi2_fd, VIDIOC_SUBDEV_S_FMT, &sf) < 0) {
		logmsg("Setting CSI-2 receiver format failed: %s", strerror(errno));
		return -1;
	}

	memset(&vf, 0, sizeof(vf));
	vf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	vf.fmt.pix_mp.width = c->w;
	vf.fmt.pix_mp.height = c->h;
	vf.fmt.pix_mp.pixelformat = cio2_fourcc(c->mbus_code);
	vf.fmt.pix_mp.field = V4L2_FIELD_NONE;
	vf.fmt.pix_mp.num_planes = 1;
	if (xioctl(c->video_fd, VIDIOC_S_FMT, &vf) < 0) {
		logmsg("Setting CIO2 capture format failed: %s%s", strerror(errno),
		       errno == EBUSY ? " (another program is using the camera)" : "");
		return -1;
	}
	if (vf.fmt.pix_mp.width != c->w || vf.fmt.pix_mp.height != c->h) {
		logmsg("CIO2 adjusted size to %ux%u (sensor %ux%u)",
		       vf.fmt.pix_mp.width, vf.fmt.pix_mp.height, c->w, c->h);
		return -1;
	}
	c->bpl = vf.fmt.pix_mp.plane_fmt[0].bytesperline;
	if (c->bpl < ipu3_packed_bytes(c->w)) {
		logmsg("Unexpected CIO2 line stride %u for width %u", c->bpl, c->w);
		return -1;
	}
	return 0;
}

static void setup_pipeline(struct cam *c)
{
	struct entity *sensor, *csi2, *video;
	struct media_link_desc link, vlink;
	struct v4l2_subdev_format sf;
	char path[300];

	/* Sensors bind asynchronously after boot; optionally wait for them. */
	for (int t = 0;; t++) {
		sensor = NULL;
		if (open_cio2_media(&c->media) == 0) {
			enum_entities(&c->media);
			sensor = find_entity_by_prefix(&c->media, c->sensor_prefix);
			if (sensor)
				break;
			close(c->media.fd);
			c->media.fd = -1;
		}
		if (t >= c->wait_s || g_stop)
			break;
		sleep(1);
	}
	if (c->media.fd < 0)
		die("No IPU3 CIO2 media device found. Is ipu3_cio2 loaded?");
	if (!sensor) {
		logmsg("Sensor '%s' is not registered with the IPU3. Entities found:",
		       c->sensor_prefix);
		for (int i = 0; i < c->media.n; i++)
			logmsg("  %s", c->media.ent[i].desc.name);
		die("The sensor driver did not probe. Check: sudo dmesg | grep -iE '%s|int3472'",
		    c->sensor_prefix);
	}
	if (find_link_from(&c->media, sensor, "ipu3-csi2", &link) < 0)
		die("No link from %s to an ipu3-csi2 receiver", sensor->desc.name);
	csi2 = find_entity_by_id(&c->media, link.sink.entity);
	if (find_link_from(&c->media, csi2, "ipu3-cio2", &vlink) < 0)
		die("No video node behind %s", csi2->desc.name);
	video = find_entity_by_id(&c->media, vlink.sink.entity);
	logmsg("pipeline: %s -> %s -> %s", sensor->desc.name, csi2->desc.name,
	       video->desc.name);
	c->link = link;
	snprintf(c->sensor_name, sizeof(c->sensor_name), "%s", sensor->desc.name);
	snprintf(c->csi2_name, sizeof(c->csi2_name), "%s", csi2->desc.name);

	if (devnode_path(sensor, path, sizeof(path)) < 0 ||
	    (c->sensor_fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
		die("Cannot open sensor subdev for %s", sensor->desc.name);
	if (devnode_path(csi2, path, sizeof(path)) < 0 ||
	    (c->csi2_fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
		die("Cannot open %s subdev", csi2->desc.name);
	if (devnode_path(video, path, sizeof(path)) < 0 ||
	    (c->video_fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0)
		die("Cannot open %s video node", video->desc.name);

	/* Read the sensor's media bus code; the size comes from --size. */
	memset(&sf, 0, sizeof(sf));
	sf.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sf.pad = link.source.index;
	if (xioctl(c->sensor_fd, VIDIOC_SUBDEV_G_FMT, &sf) < 0)
		die("VIDIOC_SUBDEV_G_FMT on sensor failed: %s", strerror(errno));
	c->mbus_code = sf.format.code;
	c->w = c->req_w ? c->req_w : sf.format.width;
	c->h = c->req_h ? c->req_h : sf.format.height;

	if (c->mbus_code == MEDIA_BUS_FMT_Y10_1X10) {
		c->kind = KIND_MONO;
	} else if (!bayer_layout(c->mbus_code, &c->r_idx, &c->b_idx)) {
		c->kind = KIND_BAYER;
	} else {
		die("Unsupported sensor media bus code 0x%04x", c->mbus_code);
	}

	/* Check the whole configuration once now, so problems show at startup. */
	if (configure_pipeline(c, true) < 0)
		die("Configuring the camera pipeline failed");

	/* Controls used by auto exposure. */
	ctrl_query(c->sensor_fd, &c->exposure, V4L2_CID_EXPOSURE);
	ctrl_query(c->sensor_fd, &c->again, V4L2_CID_ANALOGUE_GAIN);
	if (!c->again.present)
		ctrl_query(c->sensor_fd, &c->again, V4L2_CID_GAIN);
	{
		struct ctrl rot;

		ctrl_query(c->sensor_fd, &rot, V4L2_CID_CAMERA_SENSOR_ROTATION);
		c->rot180 = rot.present && rot.cur == 180;
	}

	logmsg("sensor %ux%u code 0x%04x, stride %u, exposure %s, gain %s%s",
	       c->w, c->h, c->mbus_code, c->bpl,
	       c->exposure.present ? "yes" : "no", c->again.present ? "yes" : "no",
	       c->rot180 ? ", rotated 180" : "");
}

static void free_buffers(struct cam *c)
{
	struct v4l2_requestbuffers rb = {
		.count = 0,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP,
	};

	for (unsigned int i = 0; i < c->nbufs; i++)
		if (c->buf[i] && c->buf[i] != MAP_FAILED)
			munmap(c->buf[i], c->buflen[i]);
	memset(c->buf, 0, sizeof(c->buf));
	c->nbufs = 0;
	/* Releasing the buffers lets other programs use the camera again. */
	xioctl(c->video_fd, VIDIOC_REQBUFS, &rb);
}

static int alloc_buffers(struct cam *c)
{
	struct v4l2_requestbuffers rb = {
		.count = NUM_BUFS,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP,
	};

	if (xioctl(c->video_fd, VIDIOC_REQBUFS, &rb) < 0 || rb.count < 2) {
		logmsg("VIDIOC_REQBUFS failed: %s%s", strerror(errno),
		       errno == EBUSY ? " - another program is using this camera (libcamera/PipeWire, "
					"or another surface-camd: check systemctl status 'surface-camd@*')" : "");
		return -1;
	}
	c->nbufs = rb.count < NUM_BUFS ? rb.count : NUM_BUFS;
	for (unsigned int i = 0; i < c->nbufs; i++) {
		struct v4l2_plane plane;
		struct v4l2_buffer b;

		memset(&b, 0, sizeof(b));
		memset(&plane, 0, sizeof(plane));
		b.type = rb.type;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		b.m.planes = &plane;
		b.length = 1;
		if (xioctl(c->video_fd, VIDIOC_QUERYBUF, &b) < 0) {
			logmsg("VIDIOC_QUERYBUF failed: %s", strerror(errno));
			free_buffers(c);
			return -1;
		}
		c->buflen[i] = plane.length;
		c->buf[i] = mmap(NULL, plane.length, PROT_READ | PROT_WRITE,
				 MAP_SHARED, c->video_fd, plane.m.mem_offset);
		if (c->buf[i] == MAP_FAILED) {
			logmsg("mmap failed: %s", strerror(errno));
			free_buffers(c);
			return -1;
		}
	}
	return 0;
}

static int queue_buf(struct cam *c, unsigned int i)
{
	struct v4l2_plane plane;
	struct v4l2_buffer b;

	memset(&b, 0, sizeof(b));
	memset(&plane, 0, sizeof(plane));
	b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	b.memory = V4L2_MEMORY_MMAP;
	b.index = i;
	b.m.planes = &plane;
	b.length = 1;
	if (xioctl(c->video_fd, VIDIOC_QBUF, &b) < 0) {
		logmsg("VIDIOC_QBUF failed: %s", strerror(errno));
		return -1;
	}
	return 0;
}

/* Log the formats along the pipeline, to explain a failed stream start. */
static void log_pipeline_state(struct cam *c)
{
	struct v4l2_subdev_format sf;

	for (int k = 0; k < 2; k++) {
		memset(&sf, 0, sizeof(sf));
		sf.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		sf.pad = k ? c->link.sink.index : c->link.source.index;
		if (xioctl(k ? c->csi2_fd : c->sensor_fd, VIDIOC_SUBDEV_G_FMT, &sf) == 0)
			logmsg("  %s pad %u: %ux%u code 0x%04x", k ? c->csi2_name : c->sensor_name,
			       sf.pad, sf.format.width, sf.format.height, sf.format.code);
	}
	logmsg("  expected: %ux%u code 0x%04x; kernel log: sudo dmesg | tail -20", c->w, c->h, c->mbus_code);
}

static int stream_on(struct cam *c)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (c->streaming)
		return 0;
	if (configure_pipeline(c, false) < 0 || alloc_buffers(c) < 0)
		return -1;
	for (unsigned int i = 0; i < c->nbufs; i++) {
		if (queue_buf(c, i) < 0) {
			free_buffers(c);
			return -1;
		}
	}
	led_set(c, true);
	if (xioctl(c->video_fd, VIDIOC_STREAMON, &type) < 0) {
		logmsg("VIDIOC_STREAMON failed: %s", strerror(errno));
		log_pipeline_state(c);
		led_set(c, false);
		xioctl(c->video_fd, VIDIOC_STREAMOFF, &type);	/* returns queued buffers */
		free_buffers(c);
		return -1;
	}
	c->streaming = true;
	c->frame = 0;
	c->frames = c->dropped = 0;
	logmsg("streaming started");
	return 0;
}

static void stream_off(struct cam *c)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (!c->streaming)
		return;
	xioctl(c->video_fd, VIDIOC_STREAMOFF, &type);
	free_buffers(c);
	led_set(c, false);
	c->streaming = false;
	logmsg("streaming stopped (%lu frames, %lu with errors)", c->frames, c->dropped);
}

/* -------------------------------------------------------------- output */

static void setup_output(struct cam *c)
{
	struct v4l2_format f;
	struct v4l2_event_subscription sub;

	if (c->kind == KIND_BAYER) {
		c->ow = (c->w / 2) & ~1u;
		c->oh = c->h / 2;
	} else {
		c->ow = c->w & ~1u;
		c->oh = c->h;
	}

	/* After boot the v4l2loopback devices can appear a bit later. */
	for (int t = 0;; t++) {
		c->out_fd = open(c->output, O_RDWR | O_NONBLOCK | O_CLOEXEC);
		if (c->out_fd >= 0 || errno != ENOENT || t >= c->wait_s || g_stop)
			break;
		sleep(1);
	}
	if (c->out_fd < 0)
		die("Cannot open output %s: %s (is v4l2loopback loaded with the surface-camd "
		    "options? sudo modprobe -r v4l2loopback; sudo modprobe v4l2loopback)",
		    c->output, strerror(errno));

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	f.fmt.pix.width = c->ow;
	f.fmt.pix.height = c->oh;
	f.fmt.pix.field = V4L2_FIELD_NONE;
	f.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
	if (c->kind == KIND_BAYER) {
		f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
		f.fmt.pix.bytesperline = c->ow * 2;
	} else {
		f.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
		f.fmt.pix.bytesperline = c->ow;
	}
	f.fmt.pix.sizeimage = f.fmt.pix.bytesperline * c->oh;
	if (xioctl(c->out_fd, VIDIOC_S_FMT, &f) < 0)
		die("Setting output format on %s failed: %s", c->output, strerror(errno));
	c->out_size = f.fmt.pix.sizeimage;
	c->out = calloc(1, c->out_size);
	c->rgb_row = calloc(c->ow, 3);
	c->line0 = calloc(c->w + 32, sizeof(uint16_t));
	c->line1 = calloc(c->w + 32, sizeof(uint16_t));
	if (!c->out || !c->rgb_row || !c->line0 || !c->line1)
		die("out of memory");

	/* One black frame so applications can see and open the device. */
	if (c->kind == KIND_BAYER)
		for (size_t i = 0; i < c->out_size; i += 2) {
			c->out[i] = 16;
			c->out[i + 1] = 128;
		}
	if (write(c->out_fd, c->out, c->out_size) < 0)
		logmsg("initial write to %s failed: %s", c->output, strerror(errno));

	if (!c->always_on) {
		memset(&sub, 0, sizeof(sub));
		sub.type = V4L2_EVENT_PRI_CLIENT_USAGE;
		sub.flags = V4L2_EVENT_SUB_FL_SEND_INITIAL;
		if (xioctl(c->out_fd, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0) {
			logmsg("v4l2loopback has no client-usage events (needs 0.12.6+); streaming always");
			c->always_on = true;
		}
	}
	logmsg("output %s: %ux%u %s", c->output, c->ow, c->oh,
	       c->kind == KIND_BAYER ? "YUYV" : "GREY");
}

/* ---------------------------------------------------------- processing */

static inline uint8_t clamp8(int v)
{
	return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v;
}

struct stats {
	uint64_t sum_r, sum_g, sum_b, sum_y;
	uint64_t n;
};

static void process_bayer(struct cam *c, const uint8_t *raw, struct stats *st)
{
	const int bl = c->black_level;
	const double norm = 1023.0 / (1023 - bl);
	const int gr = (int)lrint(c->wb_r * c->soft_gain * norm * 256);
	const int gg = (int)lrint(c->soft_gain * norm * 256);
	const int gb = (int)lrint(c->wb_b * c->soft_gain * norm * 256);
	const int sat = (int)lrint(c->saturation * 256);
	int g1_idx = -1, g2_idx = -1;

	for (int i = 0; i < 4; i++) {
		if (i == c->r_idx || i == c->b_idx)
			continue;
		if (g1_idx < 0)
			g1_idx = i;
		else
			g2_idx = i;
	}

	for (unsigned int oy = 0; oy < c->oh; oy++) {
		const uint16_t *rows[2] = { c->line0, c->line1 };
		unsigned int dy = c->rot180 ? c->oh - 1 - oy : oy;
		uint8_t *dst = c->out + (size_t)dy * c->ow * 2;
		bool sample_row = (oy & 7) == 0;

		ipu3_unpack_line(raw + (size_t)(2 * oy) * c->bpl, c->line0, c->w);
		ipu3_unpack_line(raw + (size_t)(2 * oy + 1) * c->bpl, c->line1, c->w);

		for (unsigned int ox = 0; ox < c->ow; ox++) {
			int px[4], r, g, b;

			px[0] = rows[0][2 * ox];
			px[1] = rows[0][2 * ox + 1];
			px[2] = rows[1][2 * ox];
			px[3] = rows[1][2 * ox + 1];
			r = px[c->r_idx] - bl;
			b = px[c->b_idx] - bl;
			g = ((px[g1_idx] + px[g2_idx]) >> 1) - bl;
			if (r < 0) r = 0;
			if (g < 0) g = 0;
			if (b < 0) b = 0;

			if (sample_row && (ox & 7) == 0) {
				st->sum_r += r;
				st->sum_g += g;
				st->sum_b += b;
				st->n++;
			}

			r = (r * gr) >> 8;
			g = (g * gg) >> 8;
			b = (b * gb) >> 8;
			if (sample_row && (ox & 7) == 0)
				st->sum_y += (r * 77 + g * 150 + b * 29) >> 8;
			uint8_t *p = c->rgb_row + 3 * ox;

			p[0] = c->lut[r > 1023 ? 1023 : r];
			p[1] = c->lut[g > 1023 ? 1023 : g];
			p[2] = c->lut[b > 1023 ? 1023 : b];
		}

		/* RGB -> YUYV (BT.601 limited range), with saturation. */
		for (unsigned int ox = 0; ox < c->ow; ox += 2) {
			unsigned int sx0 = c->rot180 ? c->ow - 1 - ox : ox;
			unsigned int sx1 = c->rot180 ? sx0 - 1 : sx0 + 1;
			const uint8_t *p0 = c->rgb_row + 3 * sx0;
			const uint8_t *p1 = c->rgb_row + 3 * sx1;
			int y0 = (66 * p0[0] + 129 * p0[1] + 25 * p0[2] + 128) >> 8;
			int y1 = (66 * p1[0] + 129 * p1[1] + 25 * p1[2] + 128) >> 8;
			int rr = (p0[0] + p1[0]) >> 1, gg2 = (p0[1] + p1[1]) >> 1,
			    bb = (p0[2] + p1[2]) >> 1;
			int u = (-38 * rr - 74 * gg2 + 112 * bb + 128) >> 8;
			int v = (112 * rr - 94 * gg2 - 18 * bb + 128) >> 8;

			u = (u * sat) >> 8;
			v = (v * sat) >> 8;
			dst[2 * ox + 0] = clamp8(y0 + 16);
			dst[2 * ox + 1] = clamp8(u + 128);
			dst[2 * ox + 2] = clamp8(y1 + 16);
			dst[2 * ox + 3] = clamp8(v + 128);
		}
	}
}

static void process_mono(struct cam *c, const uint8_t *raw, struct stats *st)
{
	const int bl = c->black_level;
	const int gain = (int)lrint(c->soft_gain * 1023.0 / (1023 - bl) * 256);

	for (unsigned int y = 0; y < c->oh; y++) {
		unsigned int dy = c->rot180 ? c->oh - 1 - y : y;
		uint8_t *dst = c->out + (size_t)dy * c->ow;

		ipu3_unpack_line(raw + (size_t)y * c->bpl, c->line0, c->w);
		for (unsigned int x = 0; x < c->ow; x++) {
			int v = c->line0[x] - bl;

			if (v < 0)
				v = 0;
			v = (v * gain) >> 8;
			if (v > 1023)
				v = 1023;
			if ((y & 7) == 0 && (x & 7) == 0) {
				st->sum_y += v;
				st->n++;
			}
			dst[c->rot180 ? c->ow - 1 - x : x] = c->lut[v];
		}
	}
}

/*
 * Auto exposure: total = exposure * analogue gain * software gain. Raise
 * exposure first, then analogue gain, then software gain; lower them in
 * reverse order. Sensor settings take effect after ~2 frames, so update
 * every third frame.
 */
static void update_3a(struct cam *c, const struct stats *st)
{
	double mean, ratio, total, exp_min, exp_max, g_min, g_max, exp, gain, sg;

	if (!st->n)
		return;

	if (c->kind == KIND_BAYER && st->sum_r && st->sum_b) {
		double wr = (double)st->sum_g / st->sum_r;
		double wb = (double)st->sum_g / st->sum_b;

		wr = fmin(fmax(wr, 0.5), 4.0);
		wb = fmin(fmax(wb, 0.5), 4.0);
		c->wb_r += (wr - c->wb_r) * 0.15;
		c->wb_b += (wb - c->wb_b) * 0.15;
	}

	if (++c->frame % 3)
		return;

	mean = (double)st->sum_y / st->n / 1023.0;
	if (mean < 0.002)
		mean = 0.002;
	ratio = c->target / mean;
	if (ratio > 0.92 && ratio < 1.08)
		return;			/* close enough, avoid hunting */
	ratio = sqrt(fmin(fmax(ratio, 0.25), 4.0));	/* damped step */

	exp_min = c->exposure.present ? c->exposure.min : 1;
	exp_max = c->exposure.present ? c->exposure.max : 1;
	g_min = c->again.present ? (c->again.min > 0 ? c->again.min : 1) : 1;
	g_max = c->again.present ? c->again.max : 1;
	exp = c->exposure.present ? c->exposure.cur : 1;
	gain = c->again.present ? c->again.cur : g_min;
	if (gain < g_min)
		gain = g_min;

	total = exp * (gain / g_min) * c->soft_gain * ratio;

	/* Distribute the total over exposure, analogue gain, software gain. */
	exp = fmin(fmax(total, exp_min), exp_max);
	total /= exp;
	gain = fmin(fmax(total * g_min, g_min), g_max);
	total /= gain / g_min;
	sg = fmin(fmax(total, 1.0), 4.0);

	ctrl_set(c->sensor_fd, &c->exposure, (int32_t)lrint(exp));
	ctrl_set(c->sensor_fd, &c->again, (int32_t)lrint(gain));
	c->soft_gain = sg;
	dbg("AE mean %.3f -> exposure %d gain %d soft %.2f wb %.2f/%.2f",
	    mean, c->exposure.cur, c->again.cur, sg, c->wb_r, c->wb_b);
}

static void handle_frame(struct cam *c)
{
	struct v4l2_plane plane;
	struct v4l2_buffer b;
	struct stats st;

	memset(&b, 0, sizeof(b));
	memset(&plane, 0, sizeof(plane));
	b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	b.memory = V4L2_MEMORY_MMAP;
	b.m.planes = &plane;
	b.length = 1;
	if (xioctl(c->video_fd, VIDIOC_DQBUF, &b) < 0) {
		if (errno != EAGAIN)
			logmsg("VIDIOC_DQBUF failed: %s", strerror(errno));
		return;
	}

	if (b.flags & V4L2_BUF_FLAG_ERROR || plane.bytesused < (size_t)c->bpl * c->h) {
		if (c->dropped++ < 5 || g_verbose)
			logmsg("frame with errors (flags 0x%x, %u of %zu bytes)", b.flags,
			       plane.bytesused, (size_t)c->bpl * c->h);
	} else {
		c->frames++;
		if (c->frames == 1)
			logmsg("first frame received");
		memset(&st, 0, sizeof(st));
		if (c->kind == KIND_BAYER)
			process_bayer(c, c->buf[b.index], &st);
		else
			process_mono(c, c->buf[b.index], &st);
		update_3a(c, &st);
		if (write(c->out_fd, c->out, c->out_size) < 0 && errno != EAGAIN)
			logmsg("write to %s failed: %s", c->output, strerror(errno));
	}
	queue_buf(c, b.index);
}

static void handle_event(struct cam *c)
{
	struct v4l2_event ev;

	/* out_fd is non-blocking: this drains the pending events and returns. */
	while (!xioctl(c->out_fd, VIDIOC_DQEVENT, &ev)) {
		uint32_t count;

		if (ev.type != V4L2_EVENT_PRI_CLIENT_USAGE)
			continue;
		memcpy(&count, &ev.u, sizeof(count));
		dbg("client usage: %u", count);
		c->wanted = count > 0;
		if (c->wanted) {
			c->last_try = time(NULL);
			stream_on(c);
		} else {
			stream_off(c);
		}
	}
}

/* ---------------------------------------------------------------- main */

#ifndef SURFACE_CAMD_NO_MAIN
static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s --camera front|rear|ir --output /dev/videoN [options]\n"
		"  --camera NAME     front (ov5693), rear (ov8865), ir (ov7251)\n"
		"  --sensor PREFIX   media entity name prefix instead of --camera\n"
		"  --output DEV      v4l2loopback device (default /dev/video50 front,\n"
		"                    /dev/video51 rear, /dev/video52 ir)\n"
		"  --size WxH        sensor mode; colour output is half of this\n"
		"                    (default 2592x1944 front, 3264x2448 rear)\n"
		"  --black-level N   10-bit black level (default 64)\n"
		"  --target F        auto-exposure target, 0.05-0.5 (default 0.18)\n"
		"  --saturation F    colour saturation (default 1.25)\n"
		"  --ir-led PATH     IR flood LED sysfs dir (default: auto for --camera ir)\n"
		"  --no-ir-led       never touch the IR LED\n"
		"  --always-on       stream even with no application reading\n"
		"  --wait SECONDS    wait this long for the sensor to appear (default 0)\n"
		"  -v                verbose\n", argv0);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "camera", required_argument, NULL, 'c' },
		{ "sensor", required_argument, NULL, 's' },
		{ "output", required_argument, NULL, 'o' },
		{ "size", required_argument, NULL, 'S' },
		{ "black-level", required_argument, NULL, 'b' },
		{ "target", required_argument, NULL, 't' },
		{ "saturation", required_argument, NULL, 'u' },
		{ "ir-led", required_argument, NULL, 'l' },
		{ "no-ir-led", no_argument, NULL, 'L' },
		{ "always-on", no_argument, NULL, 'a' },
		{ "wait", required_argument, NULL, 'w' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct cam c;
	const char *camera = NULL, *default_out = NULL;
	unsigned int def_w = 0, def_h = 0;
	bool no_led = false;
	int opt;

	memset(&c, 0, sizeof(c));
	c.black_level = 64;
	c.target = 0.18;
	c.saturation = 1.25;
	c.soft_gain = 1.0;
	c.wb_r = c.wb_b = 1.0;
	c.sensor_fd = c.csi2_fd = c.video_fd = c.out_fd = c.media.fd = -1;

	while ((opt = getopt_long(argc, argv, "vh", opts, NULL)) != -1) {
		switch (opt) {
		case 'c': camera = optarg; break;
		case 's': c.sensor_prefix = optarg; break;
		case 'o': c.output = optarg; break;
		case 'S':
			if (sscanf(optarg, "%ux%u", &c.req_w, &c.req_h) != 2)
				die("bad --size '%s'", optarg);
			break;
		case 'b': c.black_level = atoi(optarg); break;
		case 't': c.target = atof(optarg); break;
		case 'u': c.saturation = atof(optarg); break;
		case 'l': c.led_path = optarg; break;
		case 'L': no_led = true; break;
		case 'a': c.always_on = true; break;
		case 'w': c.wait_s = atoi(optarg); break;
		case 'v': g_verbose = 1; break;
		default: usage(argv[0]); return opt == 'h' ? 0 : 1;
		}
	}

	if (camera && !c.sensor_prefix) {
		if (!strcmp(camera, "front")) {
			c.sensor_prefix = "ov5693";
			default_out = "/dev/video50";
			def_w = 2592, def_h = 1944;	/* -> 1296x972 output */
		} else if (!strcmp(camera, "rear")) {
			c.sensor_prefix = "ov8865";
			default_out = "/dev/video51";
			def_w = 3264, def_h = 2448;	/* -> 1632x1224 output */
		} else if (!strcmp(camera, "ir")) {
			c.sensor_prefix = "ov7251";
			default_out = "/dev/video52";
		} else
			die("unknown camera '%s'", camera);
	}
	if (!c.output)
		c.output = default_out;
	if (!c.req_w && !c.req_h) {
		c.req_w = def_w;
		c.req_h = def_h;
	}
	if (!c.sensor_prefix || !c.output) {
		usage(argv[0]);
		return 1;
	}
	if (c.black_level < 0 || c.black_level > 512)
		die("--black-level out of range");
	c.target = fmin(fmax(c.target, 0.05), 0.5);
	if (no_led)
		c.led_path = NULL;
	else if (!c.led_path && camera && !strcmp(camera, "ir")) {
		c.led_path = find_ir_led();
		logmsg("IR flood LED: %s", c.led_path ? c.led_path : "not found (image will be dark without IR light)");
	}

	{
		/* No SA_RESTART: a signal must wake up poll() so we exit promptly. */
		struct sigaction sa;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = on_signal;
		sigaction(SIGINT, &sa, NULL);
		sigaction(SIGTERM, &sa, NULL);
		signal(SIGPIPE, SIG_IGN);
	}

	build_lut(&c);
	setup_pipeline(&c);
	setup_output(&c);

	if (c.always_on && stream_on(&c) < 0)
		return 1;

	while (!g_stop) {
		struct pollfd p[2] = {
			{ .fd = c.out_fd, .events = POLLPRI },
			{ .fd = c.video_fd, .events = POLLIN },
		};
		int n = poll(p, c.streaming ? 2 : 1, 1000);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			die("poll: %s", strerror(errno));
		}
		if (p[0].revents & POLLPRI)
			handle_event(&c);
		if (c.streaming && (p[1].revents & POLLIN))
			handle_frame(&c);
		if (c.streaming && (p[1].revents & POLLERR)) {
			logmsg("capture error, restarting stream");
			stream_off(&c);
			stream_on(&c);
		}
		/* An app is waiting but the start failed (camera busy?): retry. */
		if ((c.wanted || c.always_on) && !c.streaming && time(NULL) - c.last_try >= 3) {
			c.last_try = time(NULL);
			stream_on(&c);
		}
	}

	stream_off(&c);
	logmsg("exiting");
	return 0;
}
#endif /* SURFACE_CAMD_NO_MAIN */
