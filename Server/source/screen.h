#pragma once
#include <stdint.h>
#include "mirror.h"

// All timing in microseconds of process time (sceKernelGetProcessTimeWide). Section 7.2.
#define UI_IDLE_TIMEOUT_US        (120 * 1000 * 1000)  // PLAY
// In PLAY the controller stays on screen, dimmed, with the pressed parts lit on top of it.
// (A black screen showing only the pressed parts was tried on the Vita and looked wrong.)
#define UI_PLAY_BODY_LEVEL        600
#define UI_IDLE_TIMEOUT_SETUP_US  (120 * 1000 * 1000)  // SETUP: long enough to walk to the PC and start the client
#define UI_FADE_IN_US             (150 * 1000)
#define UI_FADE_OUT_US            (600 * 1000)
#define UI_HINT_SHOW_US           (2500 * 1000)
#define UI_HINT_REARM_QUIET_US    (30 * 1000 * 1000)
#define UI_STATS_SHOW_US          (5 * 1000 * 1000)
#define UI_DIAG_SHOW_US           (60 * 1000 * 1000)
#define UI_WIFI_LOST_GRACE_US     (5 * 1000 * 1000)
#define UI_USB_LOST_GRACE_US      (1 * 1000 * 1000)
#define UI_MAX_STEP_US            (100 * 1000)
#define UI_STICK_ACTIVITY_DELTA   24   // tune after capture 3

#define UI_LEVEL_MAX 1000

enum { SCREEN_SETUP = 0, SCREEN_PLAY };
enum { STATS_HIDDEN = 0, STATS_SUMMARY, STATS_DIAG };

typedef struct {
	uint64_t now_us;
	int wifi_link;     // main.c `connected` (main.c:36)
	int usb_link;      // usb_state() == USB_STATE_CONNECTED (usbmode.c:255-258)
	int remap_open;    // remap_menu_open (remap.c:45)
	int screen_off;    // main.c screen_off (main.c:351)
	int activity;      // from screen_activity()
	int new_contacts;  // from the UI's zones_resolve()
	int front_count;   // front contacts this frame
	int stats_tap;     // from zones_tap_update()
} ScreenInput;

typedef struct {
	int mode;            // SCREEN_*
	int stats;           // STATS_* currently selected
	int stats_drawn;     // view to draw while the stats level fades out (STATS_SUMMARY or STATS_DIAG)
	int front_seen;      // a front contact was ever seen
	int paused;          // remap menu or screen off last frame
	uint64_t last_us, last_activity_us, last_wifi_us, last_usb_us, last_front_us;
	uint64_t hint_until_us, stats_until_us;
	uint16_t body, lit, hint, stats_level; // levels 0..UI_LEVEL_MAX
	uint16_t text;       // SETUP text level; the body has its own because it stays (dimmed) in PLAY
} Screen;

typedef struct {
	int valid;
	uint32_t buttons;
	uint8_t sticks[4];
} ActivityRef;

void screen_init(Screen *s, uint64_t now_us);
void screen_update(Screen *s, const ScreenInput *in);
// 1 if the snapshot or the front panel counts as activity (section 7.4); updates ref
int screen_activity(ActivityRef *ref, const PadSnapshot *snap, int new_contacts);
