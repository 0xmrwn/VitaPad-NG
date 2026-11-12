#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stats.h"

// Longest period or work time kept: a sleep or a stall must not overflow the 32-bit sums
#define FRAME_CLAMP_US (60u * 1000 * 1000)

void frametime_init(FrameTime *f, uint64_t now_us){
	memset(f, 0, sizeof(*f));
	f->window_start_us = now_us;
	f->last_swap_us = now_us;
}

static uint32_t span(uint64_t now_us, uint64_t then_us){
	if (now_us <= then_us) return 0;
	uint64_t d = now_us - then_us;
	return d > FRAME_CLAMP_US ? FRAME_CLAMP_US : (uint32_t)d;
}

void frametime_rendered(FrameTime *f, uint64_t now_us){
	uint32_t work = span(now_us, f->last_swap_us);
	f->sum_work_us += work;
	if (work > f->max_work_us) f->max_work_us = work;
}

void frametime_swapped(FrameTime *f, uint64_t now_us){
	uint32_t period = span(now_us, f->last_swap_us);
	f->last_swap_us = now_us;
	f->frames++;
	f->sum_period_us += period;
	if (period > f->max_period_us) f->max_period_us = period;
	if (period > FRAME_LATE_US) f->late++;

	if (now_us - f->window_start_us >= FRAME_WINDOW_US){
		f->avg_period_us = f->sum_period_us / f->frames;
		f->peak_period_us = f->max_period_us;
		f->avg_work_us = f->sum_work_us / f->frames;
		f->peak_work_us = f->max_work_us;
		f->late_frames = f->late;
		f->valid = 1;
		f->window_start_us = now_us;
		f->frames = f->late = 0;
		f->sum_period_us = f->max_period_us = f->sum_work_us = f->max_work_us = 0;
	}
}

// The credits of the v1.9.2 main screen (main.c:396, main.c:412-419), kept word for word
static const char *const credits[9] = {
	"VitaPad v.1.9 by Rinnegatamante",
	"Thanks to MakiseKurisu & yuntiancherry for the ViGEm client support",
	"Thanks to Evengard for the vJoy client support",
	"Thanks to nyorem for the Linux client port",
	"Thanks to TheOfficialFloW (Adrenaline) for the PS button technique",
	"Thanks to my distinguished Patroners for their awesome support:",
	"@Sarkies_Proxy - ArkSource - Freddy Parra",
	"RaveHeart - Tain Sueiras - drd7of14 - psymu",
	"The Vita3K project - nullobject - polytoad",
};

int stats_summary(const StatsInput *in, const FrameTime *f, char lines[STATS_SUMMARY_LINES][STATS_LINE_MAX]){
	if (in->rates.streamed > 0) snprintf(lines[0], STATS_LINE_MAX, "Stream: %d pkt/s", in->rates.streamed);
	else if (in->wifi_link) snprintf(lines[0], STATS_LINE_MAX, "Polls: %d/s, %d with new data", in->rates.polls, in->rates.fresh);
	else if (in->usb_link) snprintf(lines[0], STATS_LINE_MAX, "USB: connected");
	else snprintf(lines[0], STATS_LINE_MAX, "PC: not connected");

	// Same order of tests as the v1.9.2 main screen (main.c:426-432)
	if (!in->fast_ok) snprintf(lines[1], STATS_LINE_MAX, "Fast buttons: unavailable");
	else if (!in->fast_enabled) snprintf(lines[1], STATS_LINE_MAX, "Fast buttons: off");
	else if (!in->fast.available) snprintf(lines[1], STATS_LINE_MAX, "Fast buttons: unavailable");
	else if (!in->fast.running || in->fast.learned < 12) snprintf(lines[1], STATS_LINE_MAX, "Fast buttons: starting");
	else snprintf(lines[1], STATS_LINE_MAX, "Fast buttons: %.1f ms sooner, %d fallbacks", in->fast.avg_us / 1000.0f, in->fast.fallbacks);

	if (!f->valid) snprintf(lines[2], STATS_LINE_MAX, "Frame: measuring");
	else snprintf(lines[2], STATS_LINE_MAX, "Frame: %.1f ms, max %.1f, %d late", f->avg_period_us / 1000.0f, f->peak_period_us / 1000.0f, (int)f->late_frames);

	snprintf(lines[3], STATS_LINE_MAX, "Battery: %d%%", in->battery);
	return STATS_SUMMARY_LINES;
}

int stats_diag(const StatsInput *in, const FrameTime *f, char lines[STATS_DIAG_LINES][STATS_LINE_MAX]){
	for (int i = 0; i < STATS_DIAG_LINES; i++) lines[i][0] = 0;
	snprintf(lines[0], STATS_LINE_MAX, "VitaPad NG 2.0 diagnostics - tap the centre to close");
	snprintf(lines[1], STATS_LINE_MAX, "Input updates/s: buttons & sticks %d, touch %d, motion %d", in->rates.ctrl, in->rates.touch, in->rates.motion);

	if (in->rates.streamed > 0) snprintf(lines[2], STATS_LINE_MAX, "Streaming to the PC: %d packets/s (UDP)", in->rates.streamed);
	else if (in->wifi_link) snprintf(lines[2], STATS_LINE_MAX, "PC polls/s: %d (%d with new button & stick data)", in->rates.polls, in->rates.fresh);
	else if (in->usb_link) snprintf(lines[2], STATS_LINE_MAX, "USB: connected");
	else snprintf(lines[2], STATS_LINE_MAX, "PC: not connected");

	if (!in->fast_ok) snprintf(lines[3], STATS_LINE_MAX, "Fast buttons: need Unsafe Homebrew");
	else if (!in->fast_enabled) snprintf(lines[3], STATS_LINE_MAX, "Fast buttons: off (change it in the remap menu)");
	else if (!in->fast.available) snprintf(lines[3], STATS_LINE_MAX, "Fast buttons: unavailable on this firmware");
	else if (!in->fast.running || in->fast.learned < 12) snprintf(lines[3], STATS_LINE_MAX, "Fast buttons: starting...");
	else snprintf(lines[3], STATS_LINE_MAX, "Fast buttons: %.1f ms sooner (%d changes), %d reads/s of %d us (max %d), %d fallbacks",
		in->fast.avg_us / 1000.0f, in->fast.matched, in->fast.reads_per_s, in->fast.read_us, in->fast.read_max_us, in->fast.fallbacks);

	if (!f->valid) snprintf(lines[4], STATS_LINE_MAX, "Frame: measuring");
	else snprintf(lines[4], STATS_LINE_MAX, "Frame: %.2f ms avg, %.2f ms max, work %.2f ms avg, %.2f ms max, %d late",
		f->avg_period_us / 1000.0f, f->peak_period_us / 1000.0f, f->avg_work_us / 1000.0f, f->peak_work_us / 1000.0f, (int)f->late_frames);

	snprintf(lines[5], STATS_LINE_MAX, "Battery: %d%%", in->battery);

	if (!in->zones_owned) snprintf(lines[6], STATS_LINE_MAX, "Front touch: forwarded to the PC (FrontTouch=Forward)");
	else if (in->zones_source > 0) snprintf(lines[6], STATS_LINE_MAX, "Front touch: %d zones from zones.txt", in->zones_count);
	else if (in->zones_source < 0) snprintf(lines[6], STATS_LINE_MAX, "Front touch: %d zones from built-in defaults (zones.txt is invalid)", in->zones_count);
	else snprintf(lines[6], STATS_LINE_MAX, "Front touch: %d zones from built-in defaults", in->zones_count);

	snprintf(lines[7], STATS_LINE_MAX, "Mirror: %s%s", in->mirror_enabled ? "on" : "off (Mirror=Off)", in->art_loaded ? "" : " - controller art missing");

	// lines[8] stays empty
	for (int i = 0; i < 9; i++) snprintf(lines[9 + i], STATS_LINE_MAX, "%s", credits[i]);
	return STATS_DIAG_LINES;
}
