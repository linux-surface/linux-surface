// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * surface-touchctl - command line client for the surface-touchd API.
 *
 *   surface-touchctl info | get | contacts | bindings | ping
 *   surface-touchctl set KEY VALUE        e.g. set touch_enabled false
 *   surface-touchctl monitor [touch] [pen] [gesture] [state]
 *   surface-touchctl raw '{"cmd":"get"}'
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int connect_socket(void)
{
	const char *path = getenv("SURFACE_TOUCHD_SOCKET");
	struct sockaddr_un sa;
	int fd;

	if (!path)
		path = "/run/surface-touchd/socket";
	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(stderr, "cannot connect to %s: %s\n", path, strerror(errno));
		if (errno == EACCES)
			fprintf(stderr, "(add yourself to the socket group, see surface-touchd.conf)\n");
		exit(1);
	}
	return fd;
}

static void send_line(int fd, const char *s)
{
	size_t len = strlen(s);

	if (write(fd, s, len) != (ssize_t)len || write(fd, "\n", 1) != 1) {
		perror("write");
		exit(1);
	}
}

/* Print lines from fd; stop after the first "reply" line unless follow. */
static int read_lines(int fd, int follow)
{
	char buf[8192];
	size_t len = 0;
	int rc = 0;

	for (;;) {
		ssize_t n = read(fd, buf + len, sizeof(buf) - 1 - len);
		char *nl;

		if (n <= 0)
			return n == 0 ? rc : 1;
		len += n;
		buf[len] = 0;
		while ((nl = memchr(buf, '\n', len))) {
			*nl = 0;
			if (!strncmp(buf, "{\"reply\":", 9)) {
				if (strstr(buf, "\"ok\":false"))
					rc = 2;
				if (!follow || rc) {
					puts(buf);
					return rc;
				}
			} else {
				puts(buf);
			}
			fflush(stdout);
			len -= nl + 1 - buf;
			memmove(buf, nl + 1, len);
		}
		if (len >= sizeof(buf) - 1)
			len = 0;	/* overlong line: drop */
	}
}

/* JSON value for a command-line word: true/false/numbers stay bare. */
static void json_value(char *out, size_t len, const char *v)
{
	char *end;

	if (!strcmp(v, "true") || !strcmp(v, "false") || !strcmp(v, "null")) {
		snprintf(out, len, "%s", v);
		return;
	}
	strtod(v, &end);
	if (*v && !*end) {
		snprintf(out, len, "%s", v);
		return;
	}
	size_t n = snprintf(out, len, "\"");
	for (; *v && n + 3 < len; v++) {
		if (*v == '"' || *v == '\\')
			out[n++] = '\\';
		out[n++] = *v;
	}
	snprintf(out + n, len - n, "\"");
}

static void usage(void)
{
	fprintf(stderr,
		"Usage: surface-touchctl COMMAND\n"
		"  info                     devices, version, supported events\n"
		"  get                      current settings and state\n"
		"  set KEY VALUE            change a setting, e.g.\n"
		"                             set touch_enabled false\n"
		"                             set pen_blocks_touch true\n"
		"                             set rotation 90\n"
		"                             set long_press_ms 500\n"
		"  contacts                 fingers on the screen right now\n"
		"  bindings                 configured gesture bindings\n"
		"  monitor [TYPE...]        stream events: touch pen gesture state\n"
		"                           (default: gesture state)\n"
		"  ping\n"
		"  raw JSON                 send a raw request\n"
		"Socket: $SURFACE_TOUCHD_SOCKET or /run/surface-touchd/socket\n");
	exit(1);
}

int main(int argc, char **argv)
{
	char req[1024], val[512];
	int fd;

	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help"))
		usage();

	if (!strcmp(argv[1], "monitor")) {
		size_t n = snprintf(req, sizeof(req), "{\"cmd\":\"subscribe\",\"events\":[");

		if (argc == 2) {
			n += snprintf(req + n, sizeof(req) - n, "\"gesture\",\"state\"");
		}
		for (int i = 2; i < argc && n < sizeof(req); i++)
			n += snprintf(req + n, sizeof(req) - n, "%s\"%s\"", i > 2 ? "," : "", argv[i]);
		if (n < sizeof(req))
			snprintf(req + n, sizeof(req) - n, "]}");
		fd = connect_socket();
		send_line(fd, req);
		return read_lines(fd, 1);
	}

	if (!strcmp(argv[1], "set")) {
		if (argc != 4)
			usage();
		json_value(val, sizeof(val), argv[3]);
		snprintf(req, sizeof(req), "{\"cmd\":\"set\",\"key\":\"%s\",\"value\":%s}", argv[2], val);
	} else if (!strcmp(argv[1], "raw")) {
		if (argc != 3)
			usage();
		snprintf(req, sizeof(req), "%s", argv[2]);
	} else if (argc == 2) {
		snprintf(req, sizeof(req), "{\"cmd\":\"%s\"}", argv[1]);
	} else {
		usage();
	}
	fd = connect_socket();
	send_line(fd, req);
	return read_lines(fd, 0);
}
