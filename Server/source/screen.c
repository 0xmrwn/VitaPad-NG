#include <stdint.h>
#include <string.h>

#include "screen.h"
#include "padbits.h"

// Time since `then`, 0 if the clock went backwards
static uint64_t since(uint64_t now, uint64_t then){
	return now > then ? now - then : 0;
}

void screen_init(Screen *s, uint64_t now_us){
	memset(s, 0, sizeof(*s));
	s->mode = SCREEN_SETUP;
	s->stats = STATS_HIDDEN;
	s->stats_drawn = STATS_SUMMARY;
	s->last_us = now_us;
	s->last_activity_us = now_us;
	// "Never up": start one grace period in the past (wraps on a young clock; the grace test uses
	// unsigned subtraction, so a link that never rose can't hold PLAY)
	s->last_wifi_us = now_us - UI_WIFI_LOST_GRACE_US;
	s->last_usb_us = now_us - UI_USB_LOST_GRACE_US;
}

// Moves a level toward its target; the step rounds up so a fade always finishes within its duration
static uint16_t move_level(uint16_t level, int goal, uint64_t step_us){
	uint64_t up = (step_us * UI_LEVEL_MAX + UI_FADE_IN_US - 1) / UI_FADE_IN_US;
	uint64_t down = (step_us * UI_LEVEL_MAX + UI_FADE_OUT_US - 1) / UI_FADE_OUT_US;
	int v = level;
	if (v < goal){
		v += (int)up;
		if (v > goal) v = goal;
	}else if (v > goal){
		v -= (int)down;
		if (v < goal) v = goal;
	}
	return (uint16_t)v;
}

void screen_update(Screen *s, const ScreenInput *in){
	const uint64_t now = in->now_us;

	// 1. The remap menu and a manual screen off pause everything
	if (in->remap_open || in->screen_off){
		s->last_us = now;
		s->paused = 1;
		return;
	}
	int activity = in->activity;
	if (s->paused){
		// The user just closed the menu or turned the screen on
		activity = 1;
		s->paused = 0;
	}

	// 2. Time step, clamped so a stall or a sleep doesn't jump a fade; a clock going backwards gives 0
	uint64_t step = since(now, s->last_us);
	if (step > UI_MAX_STEP_US) step = UI_MAX_STEP_US;
	s->last_us = now;

	// 3. Links. PLAY holds through the grace period after the link drops
	if (in->wifi_link) s->last_wifi_us = now;
	if (in->usb_link) s->last_usb_us = now;
	int mode = s->mode;
	if (in->wifi_link || in->usb_link) mode = SCREEN_PLAY;
	else if (mode == SCREEN_PLAY
		&& now - s->last_wifi_us >= UI_WIFI_LOST_GRACE_US
		&& now - s->last_usb_us >= UI_USB_LOST_GRACE_US) mode = SCREEN_SETUP;
	int switched = mode != s->mode;
	if (switched && mode == SCREEN_PLAY) s->hint_until_us = now + UI_HINT_SHOW_US;
	s->mode = mode;

	// 4. Idle. A mode switch always starts awake
	if (activity || switched) s->last_activity_us = now;
	uint64_t timeout = mode == SCREEN_SETUP ? UI_IDLE_TIMEOUT_SETUP_US : UI_IDLE_TIMEOUT_US;
	int awake = since(now, s->last_activity_us) < timeout;

	// 5. Front panel: the hint comes back for a new contact after a quiet spell
	if (in->new_contacts > 0 && (!s->front_seen || since(now, s->last_front_us) >= UI_HINT_REARM_QUIET_US))
		s->hint_until_us = now + UI_HINT_SHOW_US;
	if (in->front_count > 0){
		s->last_front_us = now;
		s->front_seen = 1;
	}

	// 6. Stats: tap cycles hidden, summary, diagnostic
	if (in->stats_tap){
		if (s->stats == STATS_HIDDEN){
			s->stats = STATS_SUMMARY;
			s->stats_until_us = now + UI_STATS_SHOW_US;
		}else if (s->stats == STATS_SUMMARY){
			s->stats = STATS_DIAG;
			s->stats_until_us = now + UI_DIAG_SHOW_US;
		}else s->stats = STATS_HIDDEN;
	}
	if (s->stats != STATS_HIDDEN && now >= s->stats_until_us) s->stats = STATS_HIDDEN;
	if (s->stats != STATS_HIDDEN) s->stats_drawn = s->stats;

	// 7. Targets: the diagnostic view replaces everything else
	int diag = s->stats == STATS_DIAG;
	int body = !awake || diag ? 0 : mode == SCREEN_SETUP ? UI_LEVEL_MAX : UI_PLAY_BODY_LEVEL;
	int text = mode == SCREEN_SETUP && awake && !diag ? UI_LEVEL_MAX : 0;
	int lit = awake && !diag ? UI_LEVEL_MAX : 0;
	int hint = now < s->hint_until_us && !diag ? UI_LEVEL_MAX : 0;
	int stats_on = s->stats != STATS_HIDDEN ? UI_LEVEL_MAX : 0;

	// 8. Fades
	s->body = move_level(s->body, body, step);
	s->text = move_level(s->text, text, step);
	s->lit = move_level(s->lit, lit, step);
	s->hint = move_level(s->hint, hint, step);
	s->stats_level = move_level(s->stats_level, stats_on, step);
}

int screen_activity(ActivityRef *ref, const PadSnapshot *snap, int new_contacts){
	uint32_t buttons = snap->buttons & PAD_MIRROR_MASK;
	const uint8_t sticks[4] = { snap->lx, snap->ly, snap->rx, snap->ry };
	int active = 0;
	if (!ref->valid) active = 1;
	else{
		if (buttons != ref->buttons) active = 1;
		for (int i = 0; i < 4; i++){
			int d = (int)sticks[i] - (int)ref->sticks[i];
			if (d < 0) d = -d;
			if (d >= UI_STICK_ACTIVITY_DELTA) active = 1;
		}
		if (new_contacts > 0) active = 1;
	}
	// The reference only moves when activity fires, so slow rest noise below the delta never adds up
	if (active){
		ref->valid = 1;
		ref->buttons = buttons;
		memcpy(ref->sticks, sticks, sizeof(ref->sticks));
	}
	return active;
}
