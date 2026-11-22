// SPDX-License-Identifier: MIT
//
// Scoped LD_PRELOAD shim for gnome-shell (Mutter) that applies libinput's
// LIBINPUT_ACCEL_PROFILE_CUSTOM curve to touchpads. GNOME/Mutter only exposes
// flat and adaptive profiles via gsettings; libinput >= 1.23 supports custom
// curves natively.
//
// Load via a systemd user drop-in on org.gnome.Shell@.service rather than
// /etc/ld.so.preload so only the compositor is affected.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#ifndef DEFAULT_CONF_PATH
#define DEFAULT_CONF_PATH "/etc/touchpad-accel.conf"
#endif

#define MAX_POINTS 64
#define MAX_TRACKED_DEVICES 16

struct libinput_device;
struct libinput_config_accel;

struct speed_entry {
	struct libinput_device *dev;
	double speed;
};

static struct speed_entry g_speeds[MAX_TRACKED_DEVICES];

static void remember_speed(struct libinput_device *dev, double speed)
{
	for (size_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
		if (g_speeds[i].dev == dev || g_speeds[i].dev == NULL) {
			g_speeds[i].dev = dev;
			g_speeds[i].speed = speed;
			return;
		}
	}
}

static double recall_speed(struct libinput_device *dev)
{
	for (size_t i = 0; i < MAX_TRACKED_DEVICES; i++) {
		if (g_speeds[i].dev == dev)
			return g_speeds[i].speed;
	}
	return 0.0;
}

// Stable libinput ABI values (libinput >= 1.23)
enum {
	LIBINPUT_PROFILE_CUSTOM = 1 << 2,
};

enum {
	LIBINPUT_ACCEL_TYPE_FALLBACK = 0,
	LIBINPUT_ACCEL_TYPE_MOTION = 1,
	LIBINPUT_ACCEL_TYPE_SCROLL = 2,
};

typedef int (*set_speed_fn)(struct libinput_device *, double);
typedef int (*set_profile_fn)(struct libinput_device *, int);
typedef int (*tap_get_fingers_fn)(struct libinput_device *);
typedef const char *(*get_name_fn)(struct libinput_device *);
typedef struct libinput_config_accel *(*accel_create_fn)(int);
typedef void (*accel_destroy_fn)(struct libinput_config_accel *);
typedef int (*accel_set_points_fn)(struct libinput_config_accel *, int, double,
				   size_t, double *);
typedef int (*accel_apply_fn)(struct libinput_device *,
			      struct libinput_config_accel *);

struct curve {
	double step;
	size_t n;
	double points[MAX_POINTS];
};

struct accel_conf {
	struct curve motion;
	struct curve scroll;
};

static void log_msg(const char *fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	syslog(LOG_USER | LOG_INFO, "touchpad-accel: %s", buf);
	if (isatty(STDERR_FILENO))
		fprintf(stderr, "touchpad-accel: %s\n", buf);
}

static locale_t c_locale(void)
{
	static locale_t loc = (locale_t)0;
	if (!loc)
		loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	return loc;
}

static int parse_double(const char *tok, double *out)
{
	char *end = NULL;
	double v = strtod_l(tok, &end, c_locale());
	if (end == tok || *end != '\0')
		return -1;
	*out = v;
	return 0;
}

static int parse_points(char *rest, struct curve *c)
{
	char *save = NULL;
	char *tok = strtok_r(rest, " \t\r\n", &save);
	c->n = 0;
	while (tok) {
		if (c->n >= MAX_POINTS) {
			log_msg("too many points (max %d)", MAX_POINTS);
			return -1;
		}
		if (parse_double(tok, &c->points[c->n]) != 0) {
			log_msg("invalid point '%s'", tok);
			return -1;
		}
		c->n++;
		tok = strtok_r(NULL, " \t\r\n", &save);
	}
	return c->n >= 2 ? 0 : -1;
}

static int load_config(const char *dev_name, struct accel_conf *out)
{
	const char *path = getenv("TOUCHPAD_ACCEL_CONF");
	if (!path || !*path)
		path = DEFAULT_CONF_PATH;

	FILE *f = fopen(path, "r");
	if (!f)
		return -1;

	memset(out, 0, sizeof(*out));
	int active_section = 1; // 1 = global or matching [device "..."]
	char line[2048];
	while (fgets(line, sizeof(line), f)) {
		char *hash = strchr(line, '#');
		if (hash)
			*hash = '\0';

		char *p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		size_t len = strlen(p);
		while (len > 0 && (p[len - 1] == '\n' || p[len - 1] == '\r' ||
				   p[len - 1] == ' ' || p[len - 1] == '\t'))
			p[--len] = '\0';
		if (*p == '\0')
			continue;

		if (p[0] == '[') {
			char *q1 = strchr(p, '"');
			char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (strncmp(p, "[device", 7) == 0 && q1 && q2) {
				*q2 = '\0';
				active_section = (dev_name && strstr(dev_name, q1 + 1) != NULL);
			} else {
				active_section = 0;
			}
			continue;
		}

		if (!active_section)
			continue;

		char *sp = strpbrk(p, " \t");
		if (!sp)
			continue;
		*sp++ = '\0';
		while (*sp == ' ' || *sp == '\t')
			sp++;

		if (strcmp(p, "motion_step") == 0) {
			if (parse_double(sp, &out->motion.step) != 0) {
				fclose(f);
				return -1;
			}
		} else if (strcmp(p, "motion_points") == 0) {
			if (parse_points(sp, &out->motion) != 0) {
				fclose(f);
				return -1;
			}
		} else if (strcmp(p, "scroll_step") == 0) {
			if (parse_double(sp, &out->scroll.step) != 0) {
				fclose(f);
				return -1;
			}
		} else if (strcmp(p, "scroll_points") == 0) {
			if (parse_points(sp, &out->scroll) != 0) {
				fclose(f);
				return -1;
			}
		}
	}
	fclose(f);

	if (out->motion.step <= 0.0 || out->motion.n < 2)
		return -1;
	return 0;
}

static void apply_custom_accel(struct libinput_device *dev)
{
	static tap_get_fingers_fn p_tap_fingers = NULL;
	static get_name_fn p_get_name = NULL;
	static accel_create_fn p_accel_create = NULL;
	static accel_destroy_fn p_accel_destroy = NULL;
	static accel_set_points_fn p_accel_set_points = NULL;
	static accel_apply_fn p_accel_apply = NULL;

	if (!p_tap_fingers)
		p_tap_fingers = (tap_get_fingers_fn)dlsym(RTLD_NEXT, "libinput_device_config_tap_get_finger_count");
	if (!p_get_name)
		p_get_name = (get_name_fn)dlsym(RTLD_NEXT, "libinput_device_get_name");
	if (!p_accel_create)
		p_accel_create = (accel_create_fn)dlsym(RTLD_NEXT, "libinput_config_accel_create");
	if (!p_accel_destroy)
		p_accel_destroy = (accel_destroy_fn)dlsym(RTLD_NEXT, "libinput_config_accel_destroy");
	if (!p_accel_set_points)
		p_accel_set_points = (accel_set_points_fn)dlsym(RTLD_NEXT, "libinput_config_accel_set_points");
	if (!p_accel_apply)
		p_accel_apply = (accel_apply_fn)dlsym(RTLD_NEXT, "libinput_device_config_accel_apply");

	if (!p_tap_fingers || !p_accel_create || !p_accel_destroy ||
	    !p_accel_set_points || !p_accel_apply)
		return;

	if (p_tap_fingers(dev) <= 0)
		return;

	const char *name = p_get_name ? p_get_name(dev) : NULL;
	struct accel_conf conf;
	if (load_config(name, &conf) != 0) {
		log_msg("no valid curve config for '%s'; keeping compositor profile",
			name ? name : "unknown");
		return;
	}

	struct libinput_config_accel *cfg = p_accel_create(LIBINPUT_PROFILE_CUSTOM);
	if (!cfg) {
		log_msg("libinput_config_accel_create failed for '%s'",
			name ? name : "unknown");
		return;
	}

	// Scale motion curve by the GNOME/compositor speed slider ([-1, 1] -> [0.5x, 2.0x])
	double speed = recall_speed(dev);
	double speed_mul = pow(2.0, speed);
	for (size_t i = 0; i < conf.motion.n; i++)
		conf.motion.points[i] *= speed_mul;

	int rc = p_accel_set_points(cfg, LIBINPUT_ACCEL_TYPE_MOTION,
				    conf.motion.step, conf.motion.n, conf.motion.points);
	if (rc != 0) {
		log_msg("set_points(MOTION) failed (%d) for '%s'",
			rc, name ? name : "unknown");
		p_accel_destroy(cfg);
		return;
	}

	if (conf.scroll.step > 0.0 && conf.scroll.n >= 2) {
		rc = p_accel_set_points(cfg, LIBINPUT_ACCEL_TYPE_SCROLL,
					conf.scroll.step, conf.scroll.n, conf.scroll.points);
		if (rc != 0) {
			log_msg("set_points(SCROLL) failed (%d) for '%s'",
				rc, name ? name : "unknown");
			p_accel_destroy(cfg);
			return;
		}
	}

	rc = p_accel_apply(dev, cfg);
	p_accel_destroy(cfg);
	if (rc == 0)
		log_msg("applied custom accel curve to '%s' (step=%.3f, n=%zu, speed=%.2f, mul=%.2f)",
			name ? name : "unknown", conf.motion.step, conf.motion.n, speed, speed_mul);
	else
		log_msg("accel_apply failed (%d) for '%s'",
			rc, name ? name : "unknown");
}

int libinput_device_config_accel_set_speed(struct libinput_device *dev, double speed)
{
	static set_speed_fn real_fn = NULL;
	if (!real_fn)
		real_fn = (set_speed_fn)dlsym(RTLD_NEXT, "libinput_device_config_accel_set_speed");
	remember_speed(dev, speed);
	int rc = real_fn(dev, speed);
	apply_custom_accel(dev);
	return rc;
}

int libinput_device_config_accel_set_profile(struct libinput_device *dev, int profile)
{
	static set_profile_fn real_fn = NULL;
	if (!real_fn)
		real_fn = (set_profile_fn)dlsym(RTLD_NEXT, "libinput_device_config_accel_set_profile");
	int rc = real_fn(dev, profile);
	apply_custom_accel(dev);
	return rc;
}
