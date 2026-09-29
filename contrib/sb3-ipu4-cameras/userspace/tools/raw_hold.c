/* STREAMON and hold. IPU4 = MPLANE. DQBUF error ≠ STREAMOFF.
 *
 * Frame output:
 *   default: stdout pipe (slow when frame > fs.pipe-max-size, often 1 MiB)
 *   SURFACE_RAW_SHM=/dev/shm/foo — double-buffer to foo.0 / foo.1 and write
 *   only an 8-byte header (seq:u32 LE, len:u32 LE) to stdout (slot = seq & 1).
 */
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

static int write_full(int fd, const void *buf, size_t n)
{
	const unsigned char *p = buf;
	size_t off = 0;
	while (off < n) {
		ssize_t w = write(fd, p + off, n - off);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (w == 0)
			return -1;
		off += (size_t)w;
	}
	return 0;
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
	const char *shm = getenv("SURFACE_RAW_SHM");
	unsigned seq = 0;
	char path0[256], path1[256], pathc[256];

	if (argc < 2) {
		fprintf(stderr, "usage: raw_hold DEV [W H FOURCC]\n");
		return 2;
	}
	signal(SIGTERM, on_stop);
	signal(SIGINT, on_stop);

	if (shm && shm[0]) {
		snprintf(path0, sizeof(path0), "%s.0", shm);
		snprintf(path1, sizeof(path1), "%s.1", shm);
		snprintf(pathc, sizeof(pathc), "%s.ctl", shm);
		fprintf(stderr, "raw_hold SHM %s.{0,1,ctl}\n", shm);
	}

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
	fprintf(stderr, "STREAMON ok type=%u\n", type);
	fflush(stderr);

	{
		unsigned bpl = (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ?
				       fmt.fmt.pix_mp.plane_fmt[0].bytesperline :
				       fmt.fmt.pix.bytesperline;
		unsigned hh = (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) ?
				      fmt.fmt.pix_mp.height :
				      fmt.fmt.pix.height;
		unsigned logged = 0;
		unsigned expect = bpl * hh;
		static unsigned char z[4096];
		void *shm_map[2] = { NULL, NULL };
		int shm_fd[2] = { -1, -1 };
		void *ctl_map = NULL;
		int ctl_fd = -1;

		if (shm && shm[0] && expect > 0) {
			const char *paths[2] = { path0, path1 };
			unsigned si;
			for (si = 0; si < 2; si++) {
				shm_fd[si] = open(paths[si], O_RDWR | O_CREAT, 0666);
				if (shm_fd[si] < 0) {
					perror("shm open");
					shm = NULL;
					break;
				}
				if (ftruncate(shm_fd[si], (off_t)expect) < 0) {
					perror("shm ftruncate");
					shm = NULL;
					break;
				}
				shm_map[si] = mmap(NULL, expect, PROT_WRITE | PROT_READ,
						   MAP_SHARED, shm_fd[si], 0);
				if (shm_map[si] == MAP_FAILED) {
					perror("shm mmap");
					shm_map[si] = NULL;
					shm = NULL;
					break;
				}
			}
			if (shm && shm[0]) {
				ctl_fd = open(pathc, O_RDWR | O_CREAT, 0666);
				if (ctl_fd >= 0 && ftruncate(ctl_fd, 8) == 0) {
					ctl_map = mmap(NULL, 8, PROT_WRITE | PROT_READ, MAP_SHARED, ctl_fd, 0);
					if (ctl_map == MAP_FAILED)
						ctl_map = NULL;
				}
				fprintf(stderr, "raw_hold SHM mmap %u B ×2 ctl=%s\n", expect,
					ctl_map ? "yes" : "no");
			}
		}

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

			if (shm && shm[0] && shm_map[seq & 1u]) {
				unsigned char *dst = (unsigned char *)shm_map[seq & 1u];
				unsigned hdr[2];
				memcpy(dst, p, used);
				if (used < expect)
					memset(dst + used, 0, expect - used);
				/* Ensure reader sees complete frame before publishing seq. */
				__sync_synchronize();
				hdr[0] = seq;
				hdr[1] = expect;
				if (ctl_map) {
					memcpy(ctl_map, hdr, sizeof(hdr));
					__sync_synchronize();
				}
				/* Optional pipe header (compat); never block DQBUF. */
				{
					ssize_t w;
					const unsigned char *hp = (const unsigned char *)hdr;
					size_t left = sizeof(hdr);
					int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
					if (flags >= 0)
						(void)fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
					while (left) {
						w = write(STDOUT_FILENO, hp, left);
						if (w < 0) {
							if (errno == EINTR)
								continue;
							break;
						}
						if (w == 0)
							break;
						hp += (size_t)w;
						left -= (size_t)w;
					}
					if (flags >= 0)
						(void)fcntl(STDOUT_FILENO, F_SETFL, flags);
				}
				seq++;
			} else {
				if (used && write_full(STDOUT_FILENO, p, used) < 0)
					break;
				while (used < expect) {
					unsigned n = expect - used;
					if (n > sizeof(z))
						n = sizeof(z);
					if (write_full(STDOUT_FILENO, z, n) < 0)
						goto stop;
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
		{
			unsigned si;
			for (si = 0; si < 2; si++) {
				if (shm_map[si])
					munmap(shm_map[si], expect);
				if (shm_fd[si] >= 0)
					close(shm_fd[si]);
			}
			if (ctl_map)
				munmap(ctl_map, 8);
			if (ctl_fd >= 0)
				close(ctl_fd);
		}
	}
stop:
	(void)ioctl(fd, VIDIOC_STREAMOFF, &type);
	close(fd);
	return 0;
}
