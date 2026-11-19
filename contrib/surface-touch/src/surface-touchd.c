// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * surface-touchd - touchscreen and pen service for Surface devices.
 *
 * Reads the touchscreen and stylus input devices that iptsd creates (or any
 * direct multitouch screen / pen) and adds what raw evdev doesn't offer:
 *
 *   - a Unix socket API (JSON lines): live touch, pen and gesture streams,
 *     device info, contact snapshots and runtime settings
 *   - gesture recognition: taps, double taps, long presses, swipes, edge
 *     swipes, pinch and rotate, with any number of fingers
 *   - gesture bindings to key shortcuts or commands
 *   - turning touch off, or blocking touch while the pen is near the screen
 *   - screen rotation, so gestures match what the user sees
 *   - --record / --replay of raw input for debugging and bug reports
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <math.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/uinput.h>

#include "engine.h"
#include "json.h"
#include "keys.h"

#ifndef VERSION
#define VERSION "dev"
#endif

#define MAX_CLIENTS 16
#define MAX_BINDINGS 64
#define TICK_MS 50
#define RESCAN_MS 2000

#define SUB_TOUCH   1u
#define SUB_PEN     2u
#define SUB_GESTURE 4u
#define SUB_STATE   8u
#define SUB_ALL     15u

static volatile sig_atomic_t g_stop;

static void logmsg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static uint64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------- config */

enum act_type { ACT_KEY, ACT_EXEC };

struct binding {
	char spec[48];		/* as written, e.g. "swipe:3:left" */
	char gesture[16];	/* tap, double_tap, long_press, swipe, edge_swipe,
				   pinch_in, pinch_out, rotate_cw, rotate_ccw */
	int fingers;		/* 0 = any */
	enum te_dir dir;	/* swipe direction / edge, TE_DIR_NONE = any */
	enum act_type act;
	int keys[KEYS_MAX_COMBO], nkeys;
	char action[256];	/* as written */
};

struct config {
	char touch_dev[128], pen_dev[128];
	char socket_path[108], socket_group[32];
	char exec_user[32];
	int rotation;
	bool pen_blocks_touch;
	int pen_release_ms;
	bool bindings_enabled;
	struct te_config te;
	struct binding bind[MAX_BINDINGS];
	int nbind;
};

static enum te_dir parse_dir(const char *s)
{
	if (!strcmp(s, "left")) return TE_LEFT;
	if (!strcmp(s, "right")) return TE_RIGHT;
	if (!strcmp(s, "up") || !strcmp(s, "top")) return TE_UP;
	if (!strcmp(s, "down") || !strcmp(s, "bottom")) return TE_DOWN;
	return TE_DIR_NONE;
}

static char *trim(char *s)
{
	char *e;

	while (*s == ' ' || *s == '\t')
		s++;
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = 0;
	return s;
}

/* "swipe:3:left = key LEFTCTRL+LEFTALT+LEFT" */
static int parse_binding(struct binding *b, const char *spec, const char *action, char *err, size_t errlen)
{
	static const char *const gestures[] = {
		"tap", "double_tap", "long_press", "swipe", "edge_swipe",
		"pinch_in", "pinch_out", "rotate_cw", "rotate_ccw",
	};
	char tmp[48], *parts[3] = { 0 }, *save = NULL, *p;
	int n = 0;
	bool known = false;
	const char *bad;

	memset(b, 0, sizeof(*b));
	snprintf(b->spec, sizeof(b->spec), "%s", spec);
	snprintf(b->action, sizeof(b->action), "%s", action);
	snprintf(tmp, sizeof(tmp), "%s", spec);
	for (p = strtok_r(tmp, ":", &save); p && n < 3; p = strtok_r(NULL, ":", &save))
		parts[n++] = p;
	if (!n) {
		snprintf(err, errlen, "empty gesture");
		return -1;
	}
	snprintf(b->gesture, sizeof(b->gesture), "%s", parts[0]);
	for (size_t i = 0; i < sizeof(gestures) / sizeof(gestures[0]); i++)
		known |= !strcmp(gestures[i], b->gesture);
	if (!known) {
		snprintf(err, errlen, "unknown gesture '%s'", b->gesture);
		return -1;
	}
	for (int i = 1; i < n; i++) {
		if (parts[i][0] >= '1' && parts[i][0] <= '9')
			b->fingers = atoi(parts[i]);
		else if ((b->dir = parse_dir(parts[i])) == TE_DIR_NONE) {
			snprintf(err, errlen, "bad direction '%s'", parts[i]);
			return -1;
		}
	}

	if (!strncmp(action, "key ", 4)) {
		b->act = ACT_KEY;
		b->nkeys = key_combo(action + 4, b->keys, &bad);
		if (b->nkeys < 0) {
			snprintf(err, errlen, "unknown key '%s'", bad);
			return -1;
		}
	} else if (!strncmp(action, "exec ", 5)) {
		b->act = ACT_EXEC;
	} else {
		snprintf(err, errlen, "action must start with 'key ' or 'exec '");
		return -1;
	}
	return 0;
}

static void config_defaults(struct config *c)
{
	memset(c, 0, sizeof(*c));
	snprintf(c->socket_path, sizeof(c->socket_path), "/run/surface-touchd/socket");
	snprintf(c->socket_group, sizeof(c->socket_group), "input");
	c->pen_release_ms = 300;
	c->bindings_enabled = true;
	te_config_defaults(&c->te);
}

static bool parse_bool(const char *v)
{
	return !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcmp(v, "1") || !strcasecmp(v, "on");
}

/* Settings that can be changed at runtime too. Returns false if unknown. */
static bool set_number(struct config *c, const char *k, double v)
{
	struct { const char *name; int *i; double *d; } map[] = {
		{ "tap_ms", &c->te.tap_ms, NULL },
		{ "double_tap_ms", &c->te.double_tap_ms, NULL },
		{ "long_press_ms", &c->te.long_press_ms, NULL },
		{ "swipe_ms", &c->te.swipe_ms, NULL },
		{ "pen_release_ms", &c->pen_release_ms, NULL },
		{ "tap_move", NULL, &c->te.tap_move },
		{ "swipe_min", NULL, &c->te.swipe_min },
		{ "edge_margin", NULL, &c->te.edge_margin },
		{ "pinch_start", NULL, &c->te.pinch_start },
		{ "rotate_start", NULL, &c->te.rotate_start },
		{ "palm_major", NULL, &c->te.palm_major },
	};

	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
		if (strcmp(map[i].name, k))
			continue;
		if (v < 0 || v > 100000)
			return false;
		if (map[i].i)
			*map[i].i = (int)v;
		else
			*map[i].d = v;
		return true;
	}
	return false;
}

static int load_config(struct config *c, const char *path, bool must_exist)
{
	FILE *f = fopen(path, "r");
	char line[512];
	int lineno = 0, errors = 0;

	if (!f) {
		if (must_exist) {
			logmsg("cannot open %s: %s", path, strerror(errno));
			return -1;
		}
		return 0;
	}
	while (fgets(line, sizeof(line), f)) {
		char *s = trim(line), *eq, *k, *v, err[128];

		lineno++;
		if (!*s || *s == '#')
			continue;
		eq = strchr(s, '=');
		if (!eq) {
			logmsg("%s:%d: expected 'key = value'", path, lineno);
			errors++;
			continue;
		}
		*eq = 0;
		k = trim(s);
		v = trim(eq + 1);

		if (!strncmp(k, "bind ", 5)) {
			if (c->nbind >= MAX_BINDINGS) {
				logmsg("%s:%d: too many bindings", path, lineno);
				errors++;
			} else if (parse_binding(&c->bind[c->nbind], trim(k + 5), v, err, sizeof(err)) < 0) {
				logmsg("%s:%d: %s", path, lineno, err);
				errors++;
			} else {
				c->nbind++;
			}
		} else if (!strcmp(k, "touch_device")) {
			snprintf(c->touch_dev, sizeof(c->touch_dev), "%s", v);
		} else if (!strcmp(k, "pen_device")) {
			snprintf(c->pen_dev, sizeof(c->pen_dev), "%s", v);
		} else if (!strcmp(k, "socket")) {
			snprintf(c->socket_path, sizeof(c->socket_path), "%s", v);
		} else if (!strcmp(k, "socket_group")) {
			snprintf(c->socket_group, sizeof(c->socket_group), "%s", v);
		} else if (!strcmp(k, "exec_user")) {
			snprintf(c->exec_user, sizeof(c->exec_user), "%s", v);
		} else if (!strcmp(k, "rotation")) {
			c->rotation = atoi(v);
		} else if (!strcmp(k, "pen_blocks_touch")) {
			c->pen_blocks_touch = parse_bool(v);
		} else if (!strcmp(k, "bindings")) {
			c->bindings_enabled = parse_bool(v);
		} else if (!set_number(c, k, atof(v))) {
			logmsg("%s:%d: unknown setting '%s'", path, lineno, k);
			errors++;
		}
	}
	fclose(f);
	return errors ? -1 : 0;
}

/* -------------------------------------------------------------- devices */

struct axis {
	bool present;
	int min, max, res;
};

struct device {
	int fd;
	char path[64];
	char name[128];
	struct axis x, y, major, minor, ori, pressure, tilt_x, tilt_y;
};

static void axis_read(int fd, int code, struct axis *a)
{
	struct input_absinfo ai;

	memset(a, 0, sizeof(*a));
	if (ioctl(fd, EVIOCGABS(code), &ai) < 0 || ai.maximum <= ai.minimum)
		return;
	a->present = true;
	a->min = ai.minimum;
	a->max = ai.maximum;
	a->res = ai.resolution;
}

static double axis_norm(const struct axis *a, int v)
{
	if (!a->present)
		return 0;
	return (double)(v - a->min) / (a->max - a->min);
}

static double axis_mm(const struct axis *a)
{
	return a->present && a->res > 0 ? (double)(a->max - a->min) / a->res : 0;
}

#define BITS_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_LONG - 1) / BITS_LONG)
static bool test_bit(const unsigned long *b, int n)
{
	return (b[n / BITS_LONG] >> (n % BITS_LONG)) & 1;
}

enum dev_kind { DEV_NONE, DEV_TOUCH, DEV_PEN };

static enum dev_kind classify(int fd, const char *name)
{
	unsigned long props[NLONGS(INPUT_PROP_CNT)] = { 0 };
	unsigned long absb[NLONGS(ABS_CNT)] = { 0 };
	unsigned long keyb[NLONGS(KEY_CNT)] = { 0 };

	if (strstr(name, "IPTSD Virtual Touchscreen"))
		return DEV_TOUCH;
	if (strstr(name, "IPTSD Virtual Stylus"))
		return DEV_PEN;
	ioctl(fd, EVIOCGPROP(sizeof(props)), props);
	ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absb)), absb);
	ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyb)), keyb);
	if (!test_bit(props, INPUT_PROP_DIRECT))
		return DEV_NONE;
	if (test_bit(keyb, BTN_TOOL_PEN))
		return DEV_PEN;
	if (test_bit(absb, ABS_MT_POSITION_X) && test_bit(absb, ABS_MT_SLOT))
		return DEV_TOUCH;
	return DEV_NONE;
}

static int open_device(struct device *d, const char *path)
{
	memset(d, 0, sizeof(*d));
	d->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (d->fd < 0)
		return -1;
	snprintf(d->path, sizeof(d->path), "%s", path);
	if (ioctl(d->fd, EVIOCGNAME(sizeof(d->name) - 1), d->name) < 0)
		snprintf(d->name, sizeof(d->name), "?");
	return 0;
}

static void read_touch_axes(struct device *d)
{
	axis_read(d->fd, ABS_MT_POSITION_X, &d->x);
	axis_read(d->fd, ABS_MT_POSITION_Y, &d->y);
	axis_read(d->fd, ABS_MT_TOUCH_MAJOR, &d->major);
	axis_read(d->fd, ABS_MT_TOUCH_MINOR, &d->minor);
	axis_read(d->fd, ABS_MT_ORIENTATION, &d->ori);
}

static void read_pen_axes(struct device *d)
{
	axis_read(d->fd, ABS_X, &d->x);
	axis_read(d->fd, ABS_Y, &d->y);
	axis_read(d->fd, ABS_PRESSURE, &d->pressure);
	axis_read(d->fd, ABS_TILT_X, &d->tilt_x);
	axis_read(d->fd, ABS_TILT_Y, &d->tilt_y);
}

/* Find the touchscreen and pen (config paths win over autodetection). */
static void scan_devices(const struct config *cfg, struct device *touch, struct device *pen)
{
	char path[64];

	if (touch->fd < 0 && cfg->touch_dev[0] && open_device(touch, cfg->touch_dev) == 0) {
		read_touch_axes(touch);
		logmsg("touchscreen: %s (%s)", touch->path, touch->name);
	}
	if (pen->fd < 0 && cfg->pen_dev[0] && open_device(pen, cfg->pen_dev) == 0) {
		read_pen_axes(pen);
		logmsg("pen: %s (%s)", pen->path, pen->name);
	}
	for (int i = 0; i < 64 && (touch->fd < 0 || pen->fd < 0); i++) {
		struct device d;
		enum dev_kind k;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		if (open_device(&d, path) < 0)
			continue;
		k = classify(d.fd, d.name);
		if (k == DEV_TOUCH && touch->fd < 0 && !cfg->touch_dev[0]) {
			*touch = d;
			read_touch_axes(touch);
			logmsg("touchscreen: %s (%s), %.0fx%.0f mm", touch->path, touch->name,
			       axis_mm(&touch->x), axis_mm(&touch->y));
		} else if (k == DEV_PEN && pen->fd < 0 && !cfg->pen_dev[0]) {
			*pen = d;
			read_pen_axes(pen);
			logmsg("pen: %s (%s)", pen->path, pen->name);
		} else {
			close(d.fd);
		}
	}
}

/* ---------------------------------------------------------------- state */

struct client {
	int fd;
	unsigned int subs;
	char in[2048];
	size_t inlen;
	bool dead;
};

struct mt_slot {
	int id;
	int x, y, major, minor, ori;
	bool dirty;
};

struct pen_state {
	bool near, tool_pen, rubber, touching, button, button2;
	int x, y, pressure, tilt_x, tilt_y;
	bool changed;
};

struct daemon {
	struct config cfg;
	struct te_engine eng;
	struct device touch, pen;
	struct mt_slot mt[TE_MAX_SLOTS];
	int cur_slot;
	int raw_active;		/* contacts the compositor currently sees */
	struct pen_state ps;

	bool touch_enabled;
	bool grabbed;
	uint64_t pen_left_ms;

	int listen_fd;
	struct client cl[MAX_CLIENTS];
	int ncl;

	int uinput_fd;
	bool dry_run;		/* replay: print actions instead of running them */
	bool print_events;	/* replay: write all events to stdout */
	uint64_t now;
};

static struct daemon D;

static void broadcast(unsigned int sub, const struct jw *w)
{
	if (w->overflow)
		return;
	if (D.print_events)
		fwrite(w->buf, 1, w->len, stdout);
	for (int i = 0; i < D.ncl; i++) {
		struct client *c = &D.cl[i];
		ssize_t n;

		if (c->dead || !(c->subs & sub))
			continue;
		n = send(c->fd, w->buf, w->len, MSG_NOSIGNAL | MSG_DONTWAIT);
		if (n != (ssize_t)w->len)
			c->dead = true;	/* gone, or too slow to keep up */
	}
}

static void reply_to(struct client *c, const struct jw *w)
{
	if (!c) {
		fwrite(w->buf, 1, w->len, stdout);
		return;
	}
	if (send(c->fd, w->buf, w->len, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)w->len)
		c->dead = true;
}

static bool touch_blocked(void)
{
	if (!D.touch_enabled)
		return true;
	if (!D.cfg.pen_blocks_touch)
		return false;
	return D.ps.near || (D.pen_left_ms && D.now - D.pen_left_ms < (uint64_t)D.cfg.pen_release_ms);
}

static void send_state(void)
{
	struct jw w;

	jw_begin(&w);
	jw_str(&w, "event", "state");
	jw_bool(&w, "touch_enabled", D.touch_enabled);
	jw_bool(&w, "touch_blocked", touch_blocked());
	jw_bool(&w, "pen_near", D.ps.near);
	jw_int(&w, "rotation", D.cfg.rotation);
	jw_bool(&w, "touchscreen", D.touch.fd >= 0);
	jw_bool(&w, "pen", D.pen.fd >= 0);
	jw_int(&w, "t", (long long)D.now);
	jw_end(&w);
	broadcast(SUB_STATE, &w);
}

/*
 * Grab the touchscreen (EVIOCGRAB) while touch is off or blocked by the pen:
 * the compositor then gets no touch input. Only switch while no finger is
 * down, so no contact gets stuck in the compositor.
 */
static void update_grab(void)
{
	bool want = touch_blocked();

	if (want == D.grabbed || D.touch.fd < 0 || D.raw_active > 0)
		return;
	if (ioctl(D.touch.fd, EVIOCGRAB, want ? 1 : 0) < 0 && !D.dry_run) {
		logmsg("EVIOCGRAB: %s", strerror(errno));
		return;
	}
	D.grabbed = want;
	if (want)
		te_reset(&D.eng, D.now);
	send_state();
}

/* --------------------------------------------------------------- actions */

static int setup_uinput(void)
{
	struct uinput_setup us;
	bool any = false;
	int fd;

	for (int i = 0; i < D.cfg.nbind; i++)
		any |= D.cfg.bind[i].act == ACT_KEY;
	if (!any || D.dry_run)
		return -1;

	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) {
		logmsg("key bindings disabled: /dev/uinput: %s", strerror(errno));
		return -1;
	}
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	for (int i = 0; i < D.cfg.nbind; i++)
		for (int k = 0; k < D.cfg.bind[i].nkeys; k++)
			ioctl(fd, UI_SET_KEYBIT, D.cfg.bind[i].keys[k]);
	memset(&us, 0, sizeof(us));
	us.id.bustype = BUS_VIRTUAL;
	us.id.vendor = 0x045e;	/* Microsoft */
	us.id.product = 0x7a7d;
	snprintf(us.name, sizeof(us.name), "surface-touchd gesture keys");
	if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
		logmsg("key bindings disabled: uinput setup: %s", strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

static void uinput_emit(int type, int code, int value)
{
	struct input_event ie;

	memset(&ie, 0, sizeof(ie));
	ie.type = type;
	ie.code = code;
	ie.value = value;
	if (write(D.uinput_fd, &ie, sizeof(ie)) < 0)
		logmsg("uinput write: %s", strerror(errno));
}

static void press_keys(const struct binding *b)
{
	if (D.uinput_fd < 0)
		return;
	for (int i = 0; i < b->nkeys; i++)
		uinput_emit(EV_KEY, b->keys[i], 1);
	uinput_emit(EV_SYN, SYN_REPORT, 0);
	for (int i = b->nkeys - 1; i >= 0; i--)
		uinput_emit(EV_KEY, b->keys[i], 0);
	uinput_emit(EV_SYN, SYN_REPORT, 0);
}

/* Run the command as exec_user, never as root. */
static void run_command(const char *cmd)
{
	struct passwd *pw;
	pid_t pid;

	if (!D.cfg.exec_user[0]) {
		logmsg("exec binding ignored: set exec_user in the config");
		return;
	}
	pw = getpwnam(D.cfg.exec_user);
	if (!pw || pw->pw_uid == 0) {
		logmsg("exec binding ignored: exec_user '%s' is unknown or root", D.cfg.exec_user);
		return;
	}
	pid = fork();
	if (pid != 0)
		return;

	/* child */
	char rt[64], bus[96];

	setsid();
	if (initgroups(pw->pw_name, pw->pw_gid) < 0 || setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0)
		_exit(127);
	snprintf(rt, sizeof(rt), "/run/user/%u", (unsigned int)pw->pw_uid);
	snprintf(bus, sizeof(bus), "unix:path=%s/bus", rt);
	clearenv();
	setenv("HOME", pw->pw_dir, 1);
	setenv("USER", pw->pw_name, 1);
	setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
	setenv("XDG_RUNTIME_DIR", rt, 1);
	setenv("DBUS_SESSION_BUS_ADDRESS", bus, 1);
	if (chdir(pw->pw_dir) < 0 && chdir("/") < 0)
		_exit(127);
	execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
	_exit(127);
}

static bool binding_matches(const struct binding *b, const char *gesture, int fingers, enum te_dir dir)
{
	return !strcmp(b->gesture, gesture) &&
	       (!b->fingers || b->fingers == fingers) &&
	       (b->dir == TE_DIR_NONE || b->dir == dir);
}

static void run_bindings(const char *gesture, int fingers, enum te_dir dir)
{
	if (!D.cfg.bindings_enabled)
		return;
	for (int i = 0; i < D.cfg.nbind; i++) {
		const struct binding *b = &D.cfg.bind[i];
		struct jw w;

		if (!binding_matches(b, gesture, fingers, dir))
			continue;
		jw_begin(&w);
		jw_str(&w, "event", "action");
		jw_str(&w, "binding", b->spec);
		jw_str(&w, "action", b->action);
		jw_int(&w, "t", (long long)D.now);
		jw_end(&w);
		broadcast(SUB_GESTURE, &w);
		if (D.dry_run)
			continue;
		if (b->act == ACT_KEY)
			press_keys(b);
		else
			run_command(b->action + 5);
	}
}

/* --------------------------------------------------------- engine events */

static void on_engine_event(const struct te_event *ev, void *data)
{
	struct jw w;
	bool is_touch = ev->type <= TE_TOUCH_UP || ev->type == TE_PALM;

	(void)data;
	jw_begin(&w);
	jw_str(&w, "event", is_touch ? "touch" : "gesture");
	jw_str(&w, is_touch ? "phase" : "gesture", te_type_name(ev->type));
	if (is_touch) {
		if (ev->type == TE_PALM)
			jw_str(&w, "palm", te_phase_name(ev->phase));
		jw_int(&w, "slot", ev->slot);
		jw_int(&w, "id", ev->id);
		jw_num(&w, "x", ev->x);
		jw_num(&w, "y", ev->y);
		if (ev->type != TE_TOUCH_UP) {
			jw_num(&w, "major", ev->major);
			jw_num(&w, "minor", ev->minor);
			jw_num(&w, "orientation", ev->orientation);
		}
	} else {
		jw_int(&w, "fingers", ev->fingers);
		jw_num(&w, "x", ev->x);
		jw_num(&w, "y", ev->y);
		if (ev->type == TE_SWIPE) {
			jw_str(&w, "direction", te_dir_name(ev->direction));
			jw_num(&w, "dx", ev->dx);
			jw_num(&w, "dy", ev->dy);
		} else if (ev->type == TE_EDGE_SWIPE) {
			jw_str(&w, "edge", te_dir_name(ev->direction));
		} else if (ev->type == TE_PINCH) {
			jw_str(&w, "phase", te_phase_name(ev->phase));
			jw_num(&w, "scale", ev->scale);
		} else if (ev->type == TE_ROTATE) {
			jw_str(&w, "phase", te_phase_name(ev->phase));
			jw_num(&w, "angle", ev->angle);
		}
	}
	jw_int(&w, "t", (long long)ev->time_ms);
	jw_end(&w);
	broadcast(is_touch ? SUB_TOUCH : SUB_GESTURE, &w);

	switch (ev->type) {
	case TE_TAP: case TE_DOUBLE_TAP: case TE_LONG_PRESS:
		run_bindings(te_type_name(ev->type), ev->fingers, TE_DIR_NONE);
		break;
	case TE_SWIPE: case TE_EDGE_SWIPE:
		run_bindings(te_type_name(ev->type), ev->fingers, ev->direction);
		break;
	case TE_PINCH:
		if (ev->phase == TE_END && ev->scale <= 0.75)
			run_bindings("pinch_in", ev->fingers, TE_DIR_NONE);
		else if (ev->phase == TE_END && ev->scale >= 1.33)
			run_bindings("pinch_out", ev->fingers, TE_DIR_NONE);
		break;
	case TE_ROTATE:
		if (ev->phase == TE_END && ev->angle >= 30)
			run_bindings("rotate_cw", ev->fingers, TE_DIR_NONE);
		else if (ev->phase == TE_END && ev->angle <= -30)
			run_bindings("rotate_ccw", ev->fingers, TE_DIR_NONE);
		break;
	default:
		break;
	}
}

static double touch_aspect(void)
{
	double w = axis_mm(&D.touch.x), h = axis_mm(&D.touch.y);

	return w > 0 && h > 0 ? w / h : 1.5;	/* Surface Pro screens are 3:2 */
}

static void engine_init(void)
{
	te_init(&D.eng, &D.cfg.te, touch_aspect(), on_engine_event, NULL);
	te_set_rotation(&D.eng, D.cfg.rotation);
	for (int i = 0; i < TE_MAX_SLOTS; i++)
		D.mt[i].id = -1;
	D.raw_active = 0;
	D.cur_slot = 0;
}

/* ------------------------------------------------------------- input */

static void touch_event(const struct input_event *ie)
{
	struct mt_slot *s;

	if (ie->type == EV_ABS) {
		if (ie->code == ABS_MT_SLOT) {
			D.cur_slot = ie->value;
			return;
		}
		if (D.cur_slot < 0 || D.cur_slot >= TE_MAX_SLOTS)
			return;
		s = &D.mt[D.cur_slot];
		switch (ie->code) {
		case ABS_MT_TRACKING_ID: s->id = ie->value; break;
		case ABS_MT_POSITION_X: s->x = ie->value; break;
		case ABS_MT_POSITION_Y: s->y = ie->value; break;
		case ABS_MT_TOUCH_MAJOR: s->major = ie->value; break;
		case ABS_MT_TOUCH_MINOR: s->minor = ie->value; break;
		case ABS_MT_ORIENTATION: s->ori = ie->value; break;
		default: return;
		}
		s->dirty = true;
		return;
	}
	if (ie->type != EV_SYN || ie->code != SYN_REPORT)
		return;

	D.raw_active = 0;
	for (int i = 0; i < TE_MAX_SLOTS; i++) {
		s = &D.mt[i];
		if (s->id >= 0)
			D.raw_active++;
		if (!s->dirty)
			continue;
		s->dirty = false;
		if (D.grabbed)
			continue;	/* touch is off: no events, no gestures */
		te_slot_update(&D.eng, i, s->id,
			       axis_norm(&D.touch.x, s->x), axis_norm(&D.touch.y, s->y),
			       D.touch.major.present ? (double)s->major / D.touch.major.max : 0,
			       D.touch.minor.present ? (double)s->minor / D.touch.minor.max : 0,
			       D.touch.ori.present ? axis_norm(&D.touch.ori, s->ori) * 180.0 : 0);
	}
	if (!D.grabbed)
		te_frame(&D.eng, D.now);
	update_grab();
}

static double tilt_deg(const struct axis *a, int v)
{
	if (!a->present)
		return 0;
	if (a->res > 0)
		return (double)v / a->res * 180.0 / M_PI;	/* units per radian */
	return (double)v / 100.0;
}

static void pen_event(const struct input_event *ie)
{
	struct pen_state *p = &D.ps;
	struct jw w;

	if (ie->type == EV_KEY) {
		switch (ie->code) {
		case BTN_TOOL_PEN: p->tool_pen = ie->value; break;
		case BTN_TOOL_RUBBER: p->rubber = ie->value; break;
		case BTN_TOUCH: p->touching = ie->value; break;
		case BTN_STYLUS: p->button = ie->value; break;
		case BTN_STYLUS2: p->button2 = ie->value; break;
		default: return;
		}
		if (ie->code == BTN_TOOL_PEN || ie->code == BTN_TOOL_RUBBER) {
			bool near = p->tool_pen || p->rubber;

			if (near != p->near) {
				p->near = near;
				if (!near)
					D.pen_left_ms = D.now;
				send_state();
			}
		}
		p->changed = true;
		return;
	}
	if (ie->type == EV_ABS) {
		switch (ie->code) {
		case ABS_X: p->x = ie->value; break;
		case ABS_Y: p->y = ie->value; break;
		case ABS_PRESSURE: p->pressure = ie->value; break;
		case ABS_TILT_X: p->tilt_x = ie->value; break;
		case ABS_TILT_Y: p->tilt_y = ie->value; break;
		default: return;
		}
		p->changed = true;
		return;
	}
	if (ie->type != EV_SYN || ie->code != SYN_REPORT || !p->changed)
		return;
	p->changed = false;

	double x = axis_norm(&D.pen.x, p->x), y = axis_norm(&D.pen.y, p->y), vx, vy;

	switch (D.eng.rotation) {
	case 90:  vx = 1 - y; vy = x; break;
	case 180: vx = 1 - x; vy = 1 - y; break;
	case 270: vx = y; vy = 1 - x; break;
	default:  vx = x; vy = y; break;
	}
	jw_begin(&w);
	jw_str(&w, "event", "pen");
	jw_str(&w, "tool", p->rubber ? "eraser" : "pen");
	jw_bool(&w, "near", p->near);
	jw_bool(&w, "touching", p->touching);
	jw_num(&w, "x", vx);
	jw_num(&w, "y", vy);
	jw_num(&w, "pressure", axis_norm(&D.pen.pressure, p->pressure));
	jw_num(&w, "tilt_x", tilt_deg(&D.pen.tilt_x, p->tilt_x));
	jw_num(&w, "tilt_y", tilt_deg(&D.pen.tilt_y, p->tilt_y));
	jw_bool(&w, "button", p->button);
	jw_bool(&w, "button2", p->button2);
	jw_int(&w, "t", (long long)D.now);
	jw_end(&w);
	broadcast(SUB_PEN, &w);
	update_grab();
}

/* returns -1 when the device went away */
static int drain(struct device *d, void (*handler)(const struct input_event *))
{
	struct input_event ev[64];

	for (;;) {
		ssize_t n = read(d->fd, ev, sizeof(ev));

		if (n < 0) {
			if (errno == EAGAIN || errno == EINTR)
				return 0;
			return -1;
		}
		if (n == 0)
			return -1;
		D.now = now_ms();
		for (size_t i = 0; i < (size_t)n / sizeof(ev[0]); i++)
			handler(&ev[i]);
	}
}

static void drop_device(struct device *d, const char *what)
{
	logmsg("%s %s disappeared (iptsd restarted?), waiting for it", what, d->path);
	close(d->fd);
	d->fd = -1;
	if (d == &D.touch) {
		te_reset(&D.eng, D.now);
		D.grabbed = false;
		D.raw_active = 0;
	} else {
		memset(&D.ps, 0, sizeof(D.ps));
	}
	send_state();
}

/* ------------------------------------------------------------ commands */

static void contacts_json(char *buf, size_t len)
{
	size_t n = 0;
	bool first = true;

	n += snprintf(buf + n, len - n, "[");
	for (int i = 0; i < TE_MAX_SLOTS && n < len; i++) {
		const struct te_contact *c = &D.eng.slot[i];

		if (!c->active)
			continue;
		n += snprintf(buf + n, len - n,
			      "%s{\"slot\":%d,\"id\":%d,\"x\":%.5g,\"y\":%.5g,\"major\":%.5g,\"minor\":%.5g,\"orientation\":%.5g,\"palm\":%s}",
			      first ? "" : ",", i, c->id, c->x, c->y, c->major, c->minor, c->orientation,
			      c->palm ? "true" : "false");
		first = false;
	}
	if (n < len)
		snprintf(buf + n, len - n, "]");
}

static void device_json(char *buf, size_t len, const struct device *d)
{
	if (d->fd < 0) {
		snprintf(buf, len, "null");
		return;
	}
	snprintf(buf, len, "{\"path\":\"%s\",\"name\":\"%s\",\"width_mm\":%.1f,\"height_mm\":%.1f}",
		 d->path, d->name, axis_mm(&d->x), axis_mm(&d->y));
	for (char *p = buf; *p; p++)	/* device names are ASCII; keep the JSON valid */
		if ((unsigned char)*p < 0x20 || *p == '\\')
			*p = ' ';
}

static void settings_json(struct jw *w)
{
	jw_int(w, "rotation", D.cfg.rotation);
	jw_bool(w, "touch_enabled", D.touch_enabled);
	jw_bool(w, "touch_blocked", touch_blocked());
	jw_bool(w, "pen_blocks_touch", D.cfg.pen_blocks_touch);
	jw_bool(w, "bindings", D.cfg.bindings_enabled);
	jw_int(w, "tap_ms", D.cfg.te.tap_ms);
	jw_int(w, "double_tap_ms", D.cfg.te.double_tap_ms);
	jw_int(w, "long_press_ms", D.cfg.te.long_press_ms);
	jw_int(w, "swipe_ms", D.cfg.te.swipe_ms);
	jw_int(w, "pen_release_ms", D.cfg.pen_release_ms);
	jw_num(w, "tap_move", D.cfg.te.tap_move);
	jw_num(w, "swipe_min", D.cfg.te.swipe_min);
	jw_num(w, "edge_margin", D.cfg.te.edge_margin);
	jw_num(w, "pinch_start", D.cfg.te.pinch_start);
	jw_num(w, "rotate_start", D.cfg.te.rotate_start);
	jw_num(w, "palm_major", D.cfg.te.palm_major);
}

static unsigned int sub_bit(const char *s)
{
	if (!strcmp(s, "touch")) return SUB_TOUCH;
	if (!strcmp(s, "pen")) return SUB_PEN;
	if (!strcmp(s, "gesture")) return SUB_GESTURE;
	if (!strcmp(s, "state")) return SUB_STATE;
	if (!strcmp(s, "all")) return SUB_ALL;
	return 0;
}

static void reply_error(struct client *c, const char *cmd, const char *msg)
{
	struct jw w;

	jw_begin(&w);
	jw_str(&w, "reply", cmd ? cmd : "");
	jw_bool(&w, "ok", false);
	jw_str(&w, "error", msg);
	jw_end(&w);
	reply_to(c, &w);
}

static void handle_command(struct client *c, const char *line)
{
	struct jp_object o;
	const struct jp_field *f, *k, *v;
	const char *err, *cmd;
	char buf[2048];
	struct jw w;

	if (jp_parse(line, &o, &err) < 0) {
		reply_error(c, NULL, err);
		return;
	}
	f = jp_get(&o, "cmd");
	if (!f || f->type != JP_STRING) {
		reply_error(c, NULL, "missing \"cmd\"");
		return;
	}
	cmd = f->str;

	jw_begin(&w);
	jw_str(&w, "reply", cmd);

	if (!strcmp(cmd, "ping")) {
		jw_bool(&w, "ok", true);
		jw_int(&w, "t", (long long)D.now);
	} else if (!strcmp(cmd, "info")) {
		jw_bool(&w, "ok", true);
		jw_str(&w, "version", VERSION);
		device_json(buf, sizeof(buf), &D.touch);
		jw_raw(&w, "touchscreen", buf);
		device_json(buf, sizeof(buf), &D.pen);
		jw_raw(&w, "pen", buf);
		jw_num(&w, "aspect", D.eng.aspect);
		jw_raw(&w, "events", "[\"touch\",\"pen\",\"gesture\",\"state\"]");
		jw_raw(&w, "gestures", "[\"tap\",\"double_tap\",\"long_press\",\"swipe\",\"edge_swipe\",\"pinch\",\"rotate\"]");
	} else if (!strcmp(cmd, "get")) {
		jw_bool(&w, "ok", true);
		settings_json(&w);
		jw_bool(&w, "pen_near", D.ps.near);
		jw_int(&w, "contacts", te_active_contacts(&D.eng));
	} else if (!strcmp(cmd, "contacts")) {
		jw_bool(&w, "ok", true);
		contacts_json(buf, sizeof(buf));
		jw_raw(&w, "contacts", buf);
	} else if (!strcmp(cmd, "bindings")) {
		size_t n = 0;

		n += snprintf(buf + n, sizeof(buf) - n, "[");
		for (int i = 0; i < D.cfg.nbind && n < sizeof(buf) - 1; i++) {
			struct jw item;

			jw_begin(&item);
			jw_str(&item, "gesture", D.cfg.bind[i].spec);
			jw_str(&item, "action", D.cfg.bind[i].action);
			jw_end(&item);
			item.buf[item.len - 1] = 0;	/* no newline inside the array */
			n += snprintf(buf + n, sizeof(buf) - n, "%s%s", i ? "," : "", item.buf);
		}
		if (n < sizeof(buf) - 1)
			snprintf(buf + n, sizeof(buf) - n, "]");
		jw_bool(&w, "ok", true);
		jw_bool(&w, "enabled", D.cfg.bindings_enabled);
		jw_raw(&w, "bindings", buf);
	} else if (!strcmp(cmd, "subscribe") || !strcmp(cmd, "unsubscribe")) {
		unsigned int bits = 0;

		f = jp_get(&o, "events");
		if (!f || f->type != JP_ARRAY) {
			reply_error(c, cmd, "\"events\" must be an array, e.g. [\"gesture\",\"pen\"]");
			return;
		}
		for (int i = 0; i < f->n_items; i++) {
			unsigned int b = sub_bit(f->items[i]);

			if (!b) {
				reply_error(c, cmd, "unknown event type (touch, pen, gesture, state, all)");
				return;
			}
			bits |= b;
		}
		if (c) {
			if (!strcmp(cmd, "subscribe"))
				c->subs |= bits;
			else
				c->subs &= ~bits;
		}
		jw_bool(&w, "ok", true);
	} else if (!strcmp(cmd, "set")) {
		k = jp_get(&o, "key");
		v = jp_get(&o, "value");
		if (!k || k->type != JP_STRING || !v) {
			reply_error(c, cmd, "need \"key\" and \"value\"");
			return;
		}
		if (!strcmp(k->str, "rotation")) {
			int r = (int)v->num;

			if (v->type != JP_NUMBER || (r != 0 && r != 90 && r != 180 && r != 270)) {
				reply_error(c, cmd, "rotation must be 0, 90, 180 or 270");
				return;
			}
			D.cfg.rotation = r;
			te_reset(&D.eng, D.now);
			te_set_rotation(&D.eng, r);
		} else if (!strcmp(k->str, "touch_enabled") || !strcmp(k->str, "pen_blocks_touch") ||
			   !strcmp(k->str, "bindings")) {
			if (v->type != JP_BOOL) {
				reply_error(c, cmd, "value must be true or false");
				return;
			}
			if (!strcmp(k->str, "touch_enabled"))
				D.touch_enabled = v->b;
			else if (!strcmp(k->str, "pen_blocks_touch"))
				D.cfg.pen_blocks_touch = v->b;
			else
				D.cfg.bindings_enabled = v->b;
		} else if (v->type != JP_NUMBER || !set_number(&D.cfg, k->str, v->num)) {
			reply_error(c, cmd, "unknown key or bad value");
			return;
		} else {
			D.eng.cfg = D.cfg.te;
		}
		update_grab();
		send_state();
		jw_bool(&w, "ok", true);
		settings_json(&w);
	} else {
		reply_error(c, cmd, "unknown command (ping, info, get, set, contacts, bindings, subscribe, unsubscribe)");
		return;
	}
	jw_end(&w);
	reply_to(c, &w);
}

/* --------------------------------------------------------------- socket */

static int setup_socket(const char *path, const char *group)
{
	struct sockaddr_un sa;
	struct group *gr;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 || listen(fd, 8) < 0) {
		logmsg("socket %s: %s", path, strerror(errno));
		close(fd);
		return -1;
	}
	/* Touch and pen data is sensitive (it can reveal what is typed on an
	 * on-screen keyboard): only root and socket_group may connect. */
	gr = group[0] ? getgrnam(group) : NULL;
	if (gr && chown(path, 0, gr->gr_gid) == 0)
		chmod(path, 0660);
	else
		chmod(path, 0600);
	logmsg("API socket: %s (group %s)", path, gr ? group : "root only");
	return fd;
}

static void accept_clients(void)
{
	for (;;) {
		int fd = accept4(D.listen_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

		if (fd < 0)
			return;
		if (D.ncl >= MAX_CLIENTS) {
			close(fd);
			continue;
		}
		memset(&D.cl[D.ncl], 0, sizeof(D.cl[0]));
		D.cl[D.ncl].fd = fd;
		D.cl[D.ncl].subs = 0;	/* events only after "subscribe" */
		D.ncl++;
	}
}

static void client_read(struct client *c)
{
	for (;;) {
		ssize_t n = recv(c->fd, c->in + c->inlen, sizeof(c->in) - 1 - c->inlen, 0);
		char *nl;

		if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
			c->dead = true;
			return;
		}
		if (n < 0)
			return;
		c->inlen += n;
		c->in[c->inlen] = 0;
		while ((nl = memchr(c->in, '\n', c->inlen))) {
			*nl = 0;
			if (nl > c->in)
				handle_command(c, c->in);
			c->inlen -= nl + 1 - c->in;
			memmove(c->in, nl + 1, c->inlen);
			c->in[c->inlen] = 0;
		}
		if (c->inlen >= sizeof(c->in) - 1) {
			reply_error(c, NULL, "line too long");
			c->dead = true;
			return;
		}
	}
}

static void reap_clients(void)
{
	for (int i = 0; i < D.ncl;) {
		if (D.cl[i].dead) {
			close(D.cl[i].fd);
			D.cl[i] = D.cl[--D.ncl];
		} else {
			i++;
		}
	}
}

/* ------------------------------------------------------- record/replay */

static void record(void)
{
	struct pollfd p[2];
	static const int touch_codes[] = { ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TOUCH_MAJOR,
					   ABS_MT_TOUCH_MINOR, ABS_MT_ORIENTATION };
	static const int pen_codes[] = { ABS_X, ABS_Y, ABS_PRESSURE, ABS_TILT_X, ABS_TILT_Y };
	uint64_t t0 = now_ms();

	scan_devices(&D.cfg, &D.touch, &D.pen);
	if (D.touch.fd < 0 && D.pen.fd < 0) {
		logmsg("no touchscreen or pen found");
		exit(1);
	}
	printf("# surface-touchd recording %s\n", VERSION);
	for (int i = 0; D.touch.fd >= 0 && i < 5; i++) {
		struct axis a;

		axis_read(D.touch.fd, touch_codes[i], &a);
		if (a.present)
			printf("touch_abs %d %d %d %d\n", touch_codes[i], a.min, a.max, a.res);
	}
	for (int i = 0; D.pen.fd >= 0 && i < 5; i++) {
		struct axis a;

		axis_read(D.pen.fd, pen_codes[i], &a);
		if (a.present)
			printf("pen_abs %d %d %d %d\n", pen_codes[i], a.min, a.max, a.res);
	}
	fflush(stdout);
	logmsg("recording, Ctrl-C to stop");
	while (!g_stop) {
		p[0].fd = D.touch.fd;
		p[1].fd = D.pen.fd;
		p[0].events = p[1].events = POLLIN;
		if (poll(p, 2, 500) <= 0)
			continue;
		for (int k = 0; k < 2; k++) {
			struct input_event ev[64];
			ssize_t n;

			if (!(p[k].revents & POLLIN))
				continue;
			n = read(p[k].fd, ev, sizeof(ev));
			for (size_t i = 0; n > 0 && i < (size_t)n / sizeof(ev[0]); i++)
				printf("%c %llu %u %u %d\n", k ? 'P' : 'T',
				       (unsigned long long)(now_ms() - t0), ev[i].type, ev[i].code, ev[i].value);
		}
		fflush(stdout);
	}
}

static void replay_axis(struct device *d, int code, int min, int max, int res)
{
	struct axis a = { true, min, max, res };

	switch (code) {
	case ABS_MT_POSITION_X: case ABS_X: d->x = a; break;
	case ABS_MT_POSITION_Y: case ABS_Y: d->y = a; break;
	case ABS_MT_TOUCH_MAJOR: d->major = a; break;
	case ABS_MT_TOUCH_MINOR: d->minor = a; break;
	case ABS_MT_ORIENTATION: d->ori = a; break;
	case ABS_PRESSURE: d->pressure = a; break;
	case ABS_TILT_X: d->tilt_x = a; break;
	case ABS_TILT_Y: d->tilt_y = a; break;
	}
}

/* Feed a recording through the engine; print events and (not run) actions. */
static int replay(const char *path)
{
	FILE *f = strcmp(path, "-") ? fopen(path, "r") : stdin;
	char line[256];
	bool engine_ready = false;

	if (!f) {
		logmsg("cannot open %s: %s", path, strerror(errno));
		return 1;
	}
	D.touch.fd = D.pen.fd = -1;
	while (fgets(line, sizeof(line), f)) {
		int code, min, max, res, type, value;
		unsigned long long t;
		char dev;

		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (sscanf(line, "touch_abs %d %d %d %d", &code, &min, &max, &res) == 4) {
			replay_axis(&D.touch, code, min, max, res);
			continue;
		}
		if (sscanf(line, "pen_abs %d %d %d %d", &code, &min, &max, &res) == 4) {
			replay_axis(&D.pen, code, min, max, res);
			continue;
		}
		if (sscanf(line, "%c %llu %d %d %d", &dev, &t, &type, &code, &value) != 5) {
			logmsg("bad line: %s", line);
			continue;
		}
		if (!engine_ready) {
			engine_init();
			engine_ready = true;
		}
		D.now = t;
		te_tick(&D.eng, D.now);

		struct input_event ie;

		memset(&ie, 0, sizeof(ie));
		ie.type = type;
		ie.code = code;
		ie.value = value;
		if (dev == 'T')
			touch_event(&ie);
		else if (dev == 'P')
			pen_event(&ie);
	}
	if (engine_ready)
		te_tick(&D.eng, D.now + 10000);
	if (f != stdin)
		fclose(f);
	return 0;
}

/* ----------------------------------------------------------------- main */

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"  -c, --config FILE   config file (default /etc/surface-touchd.conf)\n"
		"  -s, --socket PATH   API socket (default /run/surface-touchd/socket)\n"
		"      --check         check the config file and exit\n"
		"      --record        print raw touch/pen input in replay format\n"
		"      --replay FILE   run a recording through the gesture engine; prints\n"
		"                      all events and the actions that would run\n"
		"  -v, --version\n", argv0);
}

int main(int argc, char **argv)
{
	static const struct option opts[] = {
		{ "config", required_argument, NULL, 'c' },
		{ "socket", required_argument, NULL, 's' },
		{ "check", no_argument, NULL, 'C' },
		{ "record", no_argument, NULL, 'R' },
		{ "replay", required_argument, NULL, 'P' },
		{ "version", no_argument, NULL, 'v' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	const char *config_path = "/etc/surface-touchd.conf", *socket_path = NULL, *replay_path = NULL;
	bool config_given = false, check = false, rec = false;
	uint64_t last_scan = 0;
	int opt;

	while ((opt = getopt_long(argc, argv, "c:s:vh", opts, NULL)) != -1) {
		switch (opt) {
		case 'c': config_path = optarg; config_given = true; break;
		case 's': socket_path = optarg; break;
		case 'C': check = true; break;
		case 'R': rec = true; break;
		case 'P': replay_path = optarg; break;
		case 'v': printf("surface-touchd %s\n", VERSION); return 0;
		default: usage(argv[0]); return opt == 'h' ? 0 : 1;
		}
	}

	config_defaults(&D.cfg);
	if (load_config(&D.cfg, config_path, config_given || check) < 0)
		return 1;
	if (check) {
		printf("%s: OK, %d binding(s)\n", config_path, D.cfg.nbind);
		return 0;
	}
	if (socket_path)
		snprintf(D.cfg.socket_path, sizeof(D.cfg.socket_path), "%s", socket_path);

	D.touch.fd = D.pen.fd = D.listen_fd = D.uinput_fd = -1;
	D.touch_enabled = true;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);	/* exec bindings: no zombies */

	if (rec) {
		record();
		return 0;
	}
	if (replay_path) {
		D.dry_run = true;
		D.print_events = true;
		return replay(replay_path);
	}

	D.uinput_fd = setup_uinput();
	D.listen_fd = setup_socket(D.cfg.socket_path, D.cfg.socket_group);
	if (D.listen_fd < 0)
		return 1;
	D.now = now_ms();
	engine_init();
	logmsg("surface-touchd %s: %d binding(s)%s", VERSION, D.cfg.nbind,
	       D.cfg.pen_blocks_touch ? ", pen blocks touch" : "");

	while (!g_stop) {
		struct pollfd p[3 + MAX_CLIENTS];
		int np = 0, timeout = -1, ti = -1, pi = -1, li, ci, ncl;

		D.now = now_ms();
		if ((D.touch.fd < 0 || D.pen.fd < 0) && D.now - last_scan >= RESCAN_MS) {
			bool had_touch = D.touch.fd >= 0;

			last_scan = D.now;
			scan_devices(&D.cfg, &D.touch, &D.pen);
			if (!had_touch && D.touch.fd >= 0) {
				engine_init();
				D.grabbed = false;
				update_grab();
			}
			send_state();
		}
		if (D.touch.fd < 0 || D.pen.fd < 0)
			timeout = RESCAN_MS;

		if (D.touch.fd >= 0) {
			ti = np;
			p[np++] = (struct pollfd){ .fd = D.touch.fd, .events = POLLIN };
		}
		if (D.pen.fd >= 0) {
			pi = np;
			p[np++] = (struct pollfd){ .fd = D.pen.fd, .events = POLLIN };
		}
		li = np;
		p[np++] = (struct pollfd){ .fd = D.listen_fd, .events = POLLIN };
		ci = np;
		ncl = D.ncl;
		for (int i = 0; i < D.ncl; i++)
			p[np++] = (struct pollfd){ .fd = D.cl[i].fd, .events = POLLIN };

		/* long-press detection and the pen-release delay need timers */
		if (D.eng.in_session && (timeout < 0 || timeout > TICK_MS))
			timeout = TICK_MS;
		if (D.cfg.pen_blocks_touch && D.pen_left_ms && !D.ps.near &&
		    D.now - D.pen_left_ms < (uint64_t)D.cfg.pen_release_ms + TICK_MS &&
		    (timeout < 0 || timeout > TICK_MS))
			timeout = TICK_MS;

		if (poll(p, np, timeout) < 0) {
			if (errno == EINTR)
				continue;
			logmsg("poll: %s", strerror(errno));
			break;
		}
		D.now = now_ms();

		if (ti >= 0 && (p[ti].revents & (POLLIN | POLLERR | POLLHUP)) &&
		    drain(&D.touch, touch_event) < 0)
			drop_device(&D.touch, "touchscreen");
		if (pi >= 0 && (p[pi].revents & (POLLIN | POLLERR | POLLHUP)) &&
		    drain(&D.pen, pen_event) < 0)
			drop_device(&D.pen, "pen");
		for (int i = 0; i < ncl; i++)
			if (p[ci + i].revents & (POLLIN | POLLHUP | POLLERR))
				client_read(&D.cl[i]);
		if (p[li].revents & POLLIN)
			accept_clients();
		reap_clients();

		te_tick(&D.eng, D.now);
		update_grab();		/* pen-release delay may have passed */
	}

	if (D.grabbed && D.touch.fd >= 0)
		ioctl(D.touch.fd, EVIOCGRAB, 0);
	if (D.uinput_fd >= 0) {
		ioctl(D.uinput_fd, UI_DEV_DESTROY);
		close(D.uinput_fd);
	}
	unlink(D.cfg.socket_path);
	logmsg("exiting");
	return 0;
}
