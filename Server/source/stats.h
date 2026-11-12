#pragma once
#include <stdint.h>
#include "inputrate.h"
#include "fastbuttons.h"

#define FRAME_WINDOW_US (1000 * 1000)
#define FRAME_LATE_US 17500   // a period this long means at least one vblank was missed
#define STATS_LINE_MAX 120
#define STATS_SUMMARY_LINES 4
#define STATS_DIAG_LINES 19

typedef struct {
	uint64_t window_start_us, last_swap_us;
	uint32_t frames, late, sum_period_us, max_period_us, sum_work_us, max_work_us;
	int valid;                                  // one full window done
	uint32_t avg_period_us, peak_period_us, avg_work_us, peak_work_us, late_frames; // last full window
} FrameTime;

typedef struct {
	InputRates rates;
	int fast_ok;            // fast_stats() returned nonzero
	int fast_enabled;       // fast_enabled()
	FastButtonsStats fast;
	int battery;            // 0..100
	int wifi_link, usb_link;
	int zones_owned;        // FrontTouch=Zones
	int zones_source;       // 0 built-in, 1 zones.txt, -1 zones.txt invalid (built-in used)
	int zones_count;
	int rear_owned;         // RearTouch=Zones
	int rear_source;        // 0 built-in, 1 rear-zones.txt, -1 rear-zones.txt invalid (built-in used)
	int rear_count;
	int mirror_enabled;
	int art_loaded;
} StatsInput;

void frametime_init(FrameTime *f, uint64_t now_us);
void frametime_rendered(FrameTime *f, uint64_t now_us); // after vita2d_wait_rendering_done
void frametime_swapped(FrameTime *f, uint64_t now_us);  // after vita2d_swap_buffers
int stats_summary(const StatsInput *in, const FrameTime *f, char lines[STATS_SUMMARY_LINES][STATS_LINE_MAX]);
int stats_diag(const StatsInput *in, const FrameTime *f, char lines[STATS_DIAG_LINES][STATS_LINE_MAX]);
