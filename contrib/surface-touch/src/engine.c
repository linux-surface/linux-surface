// SPDX-License-Identifier: GPL-2.0-or-later
/* Touch engine: contact tracking and gesture recognition. See engine.h. */

#include <math.h>
#include <string.h>

#include "engine.h"

void te_config_defaults(struct te_config *cfg)
{
	cfg->tap_ms = 250;
	cfg->tap_move = 0.02;
	cfg->double_tap_ms = 300;
	cfg->long_press_ms = 600;
	cfg->swipe_min = 0.12;
	cfg->swipe_ms = 800;
	cfg->edge_margin = 0.025;
	cfg->pinch_start = 0.10;
	cfg->rotate_start = 12.0;
	cfg->palm_major = 0.08;
}

void te_init(struct te_engine *e, const struct te_config *cfg, double aspect,
	     te_callback cb, void *data)
{
	memset(e, 0, sizeof(*e));
	e->cfg = *cfg;
	e->aspect = aspect > 0 ? aspect : 1.5;
	e->cb = cb;
	e->cb_data = data;
	for (int i = 0; i < TE_MAX_SLOTS; i++)
		e->slot[i].id = e->slot[i].pid = -1;
}

void te_set_rotation(struct te_engine *e, int rotation)
{
	rotation = ((rotation % 360) + 360) % 360;
	if (rotation % 90)
		return;
	e->rotation = rotation;
}

/* Panel coordinates -> what the user sees. */
static void rotate_point(const struct te_engine *e, double x, double y, double *ox, double *oy)
{
	switch (e->rotation) {
	case 90:  *ox = 1 - y; *oy = x; break;
	case 180: *ox = 1 - x; *oy = 1 - y; break;
	case 270: *ox = y; *oy = 1 - x; break;
	default:  *ox = x; *oy = y; break;
	}
}

/* Aspect ratio as the user sees it. */
static double view_aspect(const struct te_engine *e)
{
	return (e->rotation == 90 || e->rotation == 270) ? 1.0 / e->aspect : e->aspect;
}

static double dist(const struct te_engine *e, double dx, double dy)
{
	return hypot(dx * view_aspect(e), dy);
}

static void emit(struct te_engine *e, struct te_event *ev)
{
	if (e->cb)
		e->cb(ev, e->cb_data);
}

static void base_event(struct te_event *ev, enum te_event_type type, uint64_t now)
{
	memset(ev, 0, sizeof(*ev));
	ev->type = type;
	ev->time_ms = now;
	ev->scale = 1.0;
}

void te_slot_update(struct te_engine *e, int slot, int id, double x, double y,
		    double major, double minor, double orientation)
{
	struct te_contact *c;

	if (slot < 0 || slot >= TE_MAX_SLOTS)
		return;
	c = &e->slot[slot];
	c->dirty = true;
	c->pid = id < 0 ? -1 : id;
	if (id < 0)
		return;
	rotate_point(e, x, y, &c->px, &c->py);
	c->pmajor = major;
	c->pminor = minor;
	c->porientation = orientation;
}

int te_active_contacts(const struct te_engine *e)
{
	int n = 0;

	for (int i = 0; i < TE_MAX_SLOTS; i++)
		if (e->slot[i].active && !e->slot[i].palm)
			n++;
	return n;
}

static void centroid(const struct te_engine *e, double *cx, double *cy)
{
	double sx = 0, sy = 0;
	int n = 0;

	for (int i = 0; i < TE_MAX_SLOTS; i++) {
		const struct te_contact *c = &e->slot[i];

		if (!c->active || c->palm)
			continue;
		sx += c->x;
		sy += c->y;
		n++;
	}
	*cx = n ? sx / n : 0;
	*cy = n ? sy / n : 0;
}

/* Mean distance to the centroid, and the angle between the first two contacts. */
static void spread(const struct te_engine *e, double *d, double *a)
{
	const struct te_contact *first = NULL, *second = NULL;
	double cx, cy, sum = 0;
	int n = 0;

	centroid(e, &cx, &cy);
	for (int i = 0; i < TE_MAX_SLOTS; i++) {
		const struct te_contact *c = &e->slot[i];

		if (!c->active || c->palm)
			continue;
		sum += dist(e, c->x - cx, c->y - cy);
		n++;
		if (!first)
			first = c;
		else if (!second)
			second = c;
	}
	*d = n ? sum / n : 0;
	*a = (first && second) ?
		atan2(second->y - first->y, (second->x - first->x) * view_aspect(e)) * 180.0 / M_PI : 0;
}

static double wrap_angle(double a)
{
	while (a > 180)
		a -= 360;
	while (a < -180)
		a += 360;
	return a;
}

static void transform_end(struct te_engine *e, uint64_t now)
{
	struct te_event ev;
	double cx, cy;

	centroid(e, &cx, &cy);
	if (e->pinching) {
		base_event(&ev, TE_PINCH, now);
		ev.phase = TE_END;
		ev.fingers = e->pinch_n;
		ev.scale = e->last_scale;
		ev.x = e->last_cx;
		ev.y = e->last_cy;
		emit(e, &ev);
	}
	if (e->rotating) {
		base_event(&ev, TE_ROTATE, now);
		ev.phase = TE_END;
		ev.fingers = e->pinch_n;
		ev.angle = e->last_angle;
		ev.x = e->last_cx;
		ev.y = e->last_cy;
		emit(e, &ev);
	}
	e->pinching = e->rotating = false;
	e->pinch_n = 0;
}

static void transform_update(struct te_engine *e, uint64_t now)
{
	int n = te_active_contacts(e);
	struct te_event ev;
	double d, a, scale, angle, cx, cy;

	if (n != e->pinch_n)
		transform_end(e, now);
	if (n < 2)
		return;

	spread(e, &d, &a);
	if (!e->pinch_n) {
		/* new baseline for this finger count */
		e->pinch_n = n;
		e->pinch_d0 = d > 1e-6 ? d : 1e-6;
		e->pinch_a0 = a;
		e->last_scale = 1.0;
		e->last_angle = 0;
		return;
	}

	scale = d / e->pinch_d0;
	angle = wrap_angle(a - e->pinch_a0);
	centroid(e, &cx, &cy);
	e->last_cx = cx;
	e->last_cy = cy;

	if (!e->pinching && fabs(scale - 1.0) >= e->cfg.pinch_start) {
		e->pinching = e->transformed = true;
		base_event(&ev, TE_PINCH, now);
		ev.phase = TE_BEGIN;
		ev.fingers = n;
		ev.scale = scale;
		ev.x = cx;
		ev.y = cy;
		emit(e, &ev);
	} else if (e->pinching && scale != e->last_scale) {
		base_event(&ev, TE_PINCH, now);
		ev.phase = TE_UPDATE;
		ev.fingers = n;
		ev.scale = scale;
		ev.x = cx;
		ev.y = cy;
		emit(e, &ev);
	}
	e->last_scale = scale;

	if (!e->rotating && fabs(angle) >= e->cfg.rotate_start) {
		e->rotating = e->transformed = true;
		base_event(&ev, TE_ROTATE, now);
		ev.phase = TE_BEGIN;
		ev.fingers = n;
		ev.angle = angle;
		ev.x = cx;
		ev.y = cy;
		emit(e, &ev);
	} else if (e->rotating && angle != e->last_angle) {
		base_event(&ev, TE_ROTATE, now);
		ev.phase = TE_UPDATE;
		ev.fingers = n;
		ev.angle = angle;
		ev.x = cx;
		ev.y = cy;
		emit(e, &ev);
	}
	e->last_angle = angle;
}

/* Mean movement of the active contacts from where they started. */
static void mean_move(const struct te_engine *e, double *dx, double *dy)
{
	double sx = 0, sy = 0;
	int n = 0;

	for (int i = 0; i < TE_MAX_SLOTS; i++) {
		const struct te_contact *c = &e->slot[i];

		if (!c->active || c->palm)
			continue;
		sx += c->x - c->sx;
		sy += c->y - c->sy;
		n++;
	}
	*dx = n ? sx / n : 0;
	*dy = n ? sy / n : 0;
}

static enum te_dir edge_of(const struct te_engine *e, double x, double y)
{
	double m = e->cfg.edge_margin;
	double mx = m / view_aspect(e);	/* same physical margin on x */

	if (x <= mx)
		return TE_LEFT;
	if (x >= 1 - mx)
		return TE_RIGHT;
	if (y <= m)
		return TE_UP;
	if (y >= 1 - m)
		return TE_DOWN;
	return TE_DIR_NONE;
}

static void session_end(struct te_engine *e, uint64_t now)
{
	uint64_t duration = now - e->t_start;
	double move = dist(e, e->last_dx, e->last_dy);
	double max_move = e->session_move;
	struct te_event ev;

	transform_end(e, now);

	if (e->long_fired || e->transformed || !e->max_fingers) {
		/* nothing more to report */
	} else if (duration <= (uint64_t)e->cfg.tap_ms && max_move <= e->cfg.tap_move) {
		base_event(&ev, TE_TAP, now);
		ev.fingers = e->max_fingers;
		ev.x = e->last_cx;
		ev.y = e->last_cy;
		emit(e, &ev);

		if (e->last_tap_fingers == e->max_fingers &&
		    now - e->last_tap_t <= (uint64_t)e->cfg.double_tap_ms &&
		    dist(e, e->last_cx - e->last_tap_x, e->last_cy - e->last_tap_y) <= 4 * e->cfg.tap_move) {
			ev.type = TE_DOUBLE_TAP;
			emit(e, &ev);
			e->last_tap_t = 0;
			e->last_tap_fingers = 0;
		} else {
			e->last_tap_t = now;
			e->last_tap_fingers = e->max_fingers;
			e->last_tap_x = e->last_cx;
			e->last_tap_y = e->last_cy;
		}
	} else if (duration <= (uint64_t)e->cfg.swipe_ms && move >= e->cfg.swipe_min) {
		double ax = fabs(e->last_dx * view_aspect(e)), ay = fabs(e->last_dy);
		enum te_dir dir = ax >= ay ? (e->last_dx < 0 ? TE_LEFT : TE_RIGHT)
					   : (e->last_dy < 0 ? TE_UP : TE_DOWN);
		/* An edge swipe moves away from the edge it started at. */
		bool inward = (e->edge == TE_LEFT && dir == TE_RIGHT) ||
			      (e->edge == TE_RIGHT && dir == TE_LEFT) ||
			      (e->edge == TE_UP && dir == TE_DOWN) ||
			      (e->edge == TE_DOWN && dir == TE_UP);

		if (e->edge_start && e->max_fingers == 1 && inward) {
			base_event(&ev, TE_EDGE_SWIPE, now);
			ev.direction = e->edge;
		} else {
			base_event(&ev, TE_SWIPE, now);
			ev.direction = dir;
		}
		ev.fingers = e->max_fingers;
		ev.dx = e->last_dx;
		ev.dy = e->last_dy;
		ev.x = e->last_cx;
		ev.y = e->last_cy;
		emit(e, &ev);
	}

	e->in_session = false;
}

void te_frame(struct te_engine *e, uint64_t now)
{
	struct te_event ev;
	int n;

	for (int i = 0; i < TE_MAX_SLOTS; i++) {
		struct te_contact *c = &e->slot[i];
		bool down = false;

		if (!c->dirty)
			continue;
		c->dirty = false;

		/* lifted, or the slot was reused for a new contact */
		if (c->active && c->pid != c->id) {
			base_event(&ev, c->palm ? TE_PALM : TE_TOUCH_UP, now);
			if (c->palm)
				ev.phase = TE_END;
			ev.slot = i;
			ev.id = c->id;
			ev.x = c->x;
			ev.y = c->y;
			emit(e, &ev);
			c->active = false;
			c->palm = false;
			c->id = -1;
		}
		if (c->pid < 0)
			continue;

		if (!c->active) {
			down = true;
			c->active = true;
			c->palm = false;
			c->id = c->pid;
			c->sx = c->px;
			c->sy = c->py;
			c->max_move = 0;
			c->t_down = now;
		} else if (c->x == c->px && c->y == c->py && c->major == c->pmajor &&
			   c->minor == c->pminor && c->orientation == c->porientation) {
			continue;	/* nothing changed */
		}
		c->x = c->px;
		c->y = c->py;
		c->major = c->pmajor;
		c->minor = c->pminor;
		c->orientation = c->porientation;

		if (!c->palm && c->major > e->cfg.palm_major) {
			/* Palms are reported once and left out of gestures. */
			c->palm = true;
			base_event(&ev, TE_PALM, now);
			ev.phase = TE_BEGIN;
		} else if (c->palm) {
			continue;
		} else {
			double m = dist(e, c->x - c->sx, c->y - c->sy);

			if (m > c->max_move)
				c->max_move = m;
			if (m > e->session_move)
				e->session_move = m;
			base_event(&ev, down ? TE_TOUCH_DOWN : TE_TOUCH_MOVE, now);
		}
		ev.slot = i;
		ev.id = c->id;
		ev.x = c->x;
		ev.y = c->y;
		ev.major = c->major;
		ev.minor = c->minor;
		ev.orientation = c->orientation;
		emit(e, &ev);
	}

	n = te_active_contacts(e);
	if (n > 0 && !e->in_session) {
		double cx, cy;

		e->in_session = true;
		e->t_start = now;
		e->max_fingers = 0;
		e->long_fired = false;
		e->session_move = 0;
		e->transformed = false;
		centroid(e, &cx, &cy);
		e->edge = edge_of(e, cx, cy);
		e->edge_start = e->edge != TE_DIR_NONE;
	}
	if (!e->in_session)
		return;

	if (n > e->max_fingers)
		e->max_fingers = n;
	if (n > 1)
		e->edge_start = false;

	if (n > 0) {
		mean_move(e, &e->last_dx, &e->last_dy);
		centroid(e, &e->last_cx, &e->last_cy);
	}
	transform_update(e, now);
	te_tick(e, now);

	if (n == 0)
		session_end(e, now);
}

void te_tick(struct te_engine *e, uint64_t now)
{
	struct te_event ev;
	double max_move = 0;
	int n;

	if (!e->in_session || e->long_fired || e->transformed)
		return;
	n = te_active_contacts(e);
	if (n == 0 || n != e->max_fingers)
		return;
	if (now - e->t_start < (uint64_t)e->cfg.long_press_ms)
		return;
	for (int i = 0; i < TE_MAX_SLOTS; i++)
		if (e->slot[i].active && !e->slot[i].palm && e->slot[i].max_move > max_move)
			max_move = e->slot[i].max_move;
	if (max_move > e->cfg.tap_move)
		return;

	e->long_fired = true;
	base_event(&ev, TE_LONG_PRESS, now);
	ev.fingers = n;
	ev.x = e->last_cx;
	ev.y = e->last_cy;
	emit(e, &ev);
}

void te_reset(struct te_engine *e, uint64_t now)
{
	struct te_event ev;

	for (int i = 0; i < TE_MAX_SLOTS; i++) {
		struct te_contact *c = &e->slot[i];

		if (c->active) {
			base_event(&ev, TE_TOUCH_UP, now);
			ev.slot = i;
			ev.id = c->id;
			ev.x = c->x;
			ev.y = c->y;
			emit(e, &ev);
		}
		memset(c, 0, sizeof(*c));
		c->id = c->pid = -1;
	}
	e->pinching = e->rotating = false;
	e->pinch_n = 0;
	e->in_session = false;
}

const char *te_type_name(enum te_event_type t)
{
	static const char *const names[] = {
		[TE_TOUCH_DOWN] = "down", [TE_TOUCH_MOVE] = "move", [TE_TOUCH_UP] = "up",
		[TE_PALM] = "palm", [TE_TAP] = "tap", [TE_DOUBLE_TAP] = "double_tap",
		[TE_LONG_PRESS] = "long_press", [TE_SWIPE] = "swipe",
		[TE_EDGE_SWIPE] = "edge_swipe", [TE_PINCH] = "pinch", [TE_ROTATE] = "rotate",
	};

	return (unsigned int)t < sizeof(names) / sizeof(names[0]) ? names[t] : "?";
}

const char *te_dir_name(enum te_dir d)
{
	switch (d) {
	case TE_LEFT: return "left";
	case TE_RIGHT: return "right";
	case TE_UP: return "up";
	case TE_DOWN: return "down";
	default: return "none";
	}
}

const char *te_phase_name(enum te_phase p)
{
	switch (p) {
	case TE_BEGIN: return "begin";
	case TE_UPDATE: return "update";
	case TE_END: return "end";
	default: return "none";
	}
}
