/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Touch engine: tracks multitouch contacts frame by frame and recognises
 * gestures. Pure logic (no I/O), so it can be unit-tested with synthetic
 * frames.
 *
 * Coordinates are normalised to 0..1 in the orientation the user sees
 * (after applying the display rotation). Distances use "screen heights":
 * an x distance is multiplied by the aspect ratio, so a 0.1 move is the same
 * physical length horizontally and vertically.
 */
#ifndef SURFACE_TOUCH_ENGINE_H
#define SURFACE_TOUCH_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#define TE_MAX_SLOTS 16

enum te_event_type {
	TE_TOUCH_DOWN,
	TE_TOUCH_MOVE,
	TE_TOUCH_UP,
	TE_PALM,
	TE_TAP,
	TE_DOUBLE_TAP,
	TE_LONG_PRESS,
	TE_SWIPE,
	TE_EDGE_SWIPE,
	TE_PINCH,
	TE_ROTATE,
};

enum te_dir { TE_DIR_NONE, TE_LEFT, TE_RIGHT, TE_UP, TE_DOWN };
enum te_phase { TE_PHASE_NONE, TE_BEGIN, TE_UPDATE, TE_END };

struct te_event {
	enum te_event_type type;
	uint64_t time_ms;
	int fingers;
	enum te_dir direction;	/* swipe: movement; edge swipe: the edge it started from */
	enum te_phase phase;	/* pinch / rotate */
	double x, y;		/* touch: contact; gesture: centroid */
	double dx, dy;		/* swipe: total movement */
	double scale;		/* pinch: current distance / start distance */
	double angle;		/* rotate: degrees, positive = clockwise */
	/* touch events only */
	int slot, id;
	double major, minor, orientation;
};

typedef void (*te_callback)(const struct te_event *ev, void *data);

struct te_config {
	int tap_ms;			/* max duration of a tap */
	double tap_move;		/* max movement of a tap / long press */
	int double_tap_ms;		/* max gap between the taps of a double tap */
	int long_press_ms;		/* hold time for a long press */
	double swipe_min;		/* min movement of a swipe */
	int swipe_ms;			/* max duration of a swipe */
	double edge_margin;		/* start zone for edge swipes */
	double pinch_start;		/* scale change that starts a pinch */
	double rotate_start;		/* degrees that start a rotation */
	double palm_major;		/* contacts larger than this (0..1 of the diagonal) are palms */
};

struct te_contact {
	/* current state */
	bool active, palm;
	int id;
	double x, y, major, minor, orientation;
	double sx, sy;			/* start position */
	double max_move;
	uint64_t t_down;
	/* pending state from te_slot_update, applied by te_frame */
	bool dirty;
	int pid;
	double px, py, pmajor, pminor, porientation;
};

struct te_engine {
	struct te_config cfg;
	double aspect;			/* physical width / height of the panel */
	int rotation;			/* 0, 90, 180, 270 */
	te_callback cb;
	void *cb_data;

	/* raw slot state, in panel coordinates, filled by te_set_* */
	struct te_contact slot[TE_MAX_SLOTS];

	/* current gesture session (first finger down .. last finger up) */
	bool in_session;
	uint64_t t_start;
	int max_fingers;
	bool long_fired;
	double session_move;		/* max movement of any finger this session */
	bool transformed;		/* a pinch or rotation happened */
	bool edge_start;
	enum te_dir edge;
	double last_dx, last_dy, last_cx, last_cy;

	/* pinch / rotate */
	int pinch_n;			/* finger count the baseline belongs to; 0 = none */
	double pinch_d0, pinch_a0;
	bool pinching, rotating;
	double last_scale, last_angle;

	/* double tap */
	uint64_t last_tap_t;
	int last_tap_fingers;
	double last_tap_x, last_tap_y;
};

void te_config_defaults(struct te_config *cfg);
void te_init(struct te_engine *e, const struct te_config *cfg, double aspect,
	     te_callback cb, void *data);
void te_set_rotation(struct te_engine *e, int rotation);

/* Update a slot (panel coordinates normalised to 0..1, major/minor as a
 * fraction of the diagonal, orientation in degrees). id < 0 lifts it. */
void te_slot_update(struct te_engine *e, int slot, int id, double x, double y,
		    double major, double minor, double orientation);
/* Process the frame built by te_slot_update calls (on SYN_REPORT). */
void te_frame(struct te_engine *e, uint64_t now_ms);
/* Call periodically while fingers are down, for long-press detection. */
void te_tick(struct te_engine *e, uint64_t now_ms);
/* Lift everything without generating gestures (e.g. touch got disabled). */
void te_reset(struct te_engine *e, uint64_t now_ms);
int te_active_contacts(const struct te_engine *e);

const char *te_type_name(enum te_event_type t);
const char *te_dir_name(enum te_dir d);
const char *te_phase_name(enum te_phase p);

#endif
