/* STREAMON and hold. IPU4 = MPLANE. DQBUF error ≠ STREAMOFF. */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;

static void on_stop(int sig)
{
	(void)sig;
	g_stop = 1;
}

int main(int argc, char **argv)
{
	struct v4l2_format fmt;
	struct v4l2_requestbuffers req;
	struct v4l2_buffer buf;
	struct v4l2_plane planes[VIDEO_MAX_PLANES];
	enum v4l2_buf_type type;
	void *maps[8];
	unsigned nbufs, i;
	int fd;

	if (argc < 2) {
		fprintf(stderr, "usage: raw_hold DEV [W H FOURCC]\n");
		return 2;
	}
	signal(SIGTERM, on_stop);
	signal(SIGINT, on_stop);

	fd = open(argv[1], O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
			perror("G_FMT");
			return 1;
		}
	}
	type = fmt.type;
	{
		unsigned bpl0 = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
		unsigned hh0 = fmt.fmt.pix_mp.height;
		unsigned ww0 = fmt.fmt.pix_mp.width;

		if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			bpl0 = fmt.fmt.pix.bytesperline;
			hh0 = fmt.fmt.pix.height;
			ww0 = fmt.fmt.pix.width;
		}
		fprintf(stderr, "fmt type=%u %ux%u bpl=%u\n", type, ww0, hh0, bpl0);
	}

	memset(&req, 0, sizeof(req));
	req.count = 3;
	req.type = type;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
		perror("REQBUFS");
		return 1;
	}
	nbufs = req.count;
	if (nbufs > 8)
		nbufs = 8;
	for (i = 0; i < nbufs; i++) {
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));
		buf.type = type;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			buf.length = 1;
			buf.m.planes = planes;
		}
		if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
			perror("QUERYBUF");
			return 1;
		}
		if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			maps[i] = mmap(NULL, planes[0].length, PROT_READ, MAP_SHARED,
				       fd, planes[0].m.mem_offset);
		} else {
			maps[i] = mmap(NULL, buf.length, PROT_READ, MAP_SHARED,
				       fd, buf.m.offset);
		}
		if (maps[i] == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
			perror("QBUF");
			return 1;
		}
	}
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		perror("STREAMON");
		return 1;
	}

	{
		unsigned bpl = (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ?
				       fmt.fmt.pix_mp.plane_fmt[0].bytesperline :
				       fmt.fmt.pix.bytesperline;
		unsigned hh = (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ?
				      fmt.fmt.pix_mp.height :
				      fmt.fmt.pix.height;
		unsigned logged = 0;

	while (!g_stop) {
		fd_set rfds;
		struct timeval tv;
		int s;
		unsigned used, off;
		const unsigned char *p;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 20000;
		s = select(fd + 1, &rfds, NULL, NULL, &tv);
		if (s <= 0)
			continue;
		memset(&buf, 0, sizeof(buf));
		memset(planes, 0, sizeof(planes));
		buf.type = type;
		buf.memory = V4L2_MEMORY_MMAP;
		if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			buf.length = 1;
			buf.m.planes = planes;
		}
		if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0)
			continue;
		if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			used = planes[0].bytesused;
			off = planes[0].data_offset;
		} else {
			used = buf.bytesused;
			off = 0;
		}
		if (used <= off)
			goto requeue;
		p = (const unsigned char *)maps[buf.index] + off;
		used -= off;
		/* Fixed size bpl*h — variable bytesused shifted Bayer by 4 B
		   (red-black maze / stripe). */
		{
			unsigned expect = bpl * hh;
			static unsigned char z[4096];

			if (!logged) {
				fprintf(stderr,
					"dq used=%u off=%u out=%u expect=%u bpl=%u h=%u\n",
					(type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ?
						planes[0].bytesused :
						buf.bytesused,
					off, used, expect, bpl, hh);
				logged = 1;
			}
			if (used > expect)
				used = expect;
			if (used)
				(void)write(STDOUT_FILENO, p, used);
			while (used < expect) {
				unsigned n = expect - used;
				if (n > sizeof(z))
					n = sizeof(z);
				if (write(STDOUT_FILENO, z, n) != (ssize_t)n)
					break;
				used += n;
			}
		}
	requeue:
		if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			buf.length = 1;
			buf.m.planes = planes;
		}
		(void)ioctl(fd, VIDIOC_QBUF, &buf);
	}
	}
	(void)ioctl(fd, VIDIOC_STREAMOFF, &type);
	close(fd);
	return 0;
}
