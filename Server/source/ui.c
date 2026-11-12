#include <stdio.h>
#include <string.h>
#include <vita2d.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>

#include "protocol.h"
#include "padbits.h"
#include "sprites.h"
#include "mirror.h"
#include "screen.h"
#include "stats.h"
#include "uiplan.h"
#include "zones.h"
#include "touchzones.h"
#include "ui.h"
#include "remap.h"
#include "psbutton.h"
#include "usbmode.h"
#include "inputrate.h"
#include "fastinput.h"

// Smallest text scale the shrink-to-fit loop goes down to
#define TEXT_MIN_SCALE 0.7f
#define TEXT_SCALE_STEP 0.05f

static vita2d_pgf *font = NULL;
static vita2d_texture *atlas = NULL;
static int art_loaded = 0;
static volatile int mirror_enabled = 1;

static Screen screen;
static MirrorState mirror;
static PadSnapshot snap;
static ActivityRef activity_ref;
static ZoneLatch latch;
static TapTracker tap;
static FrameTime frame_time;
static SetupInfo setup;
static StatsInput stats_input;
// About 70 KB, so it lives in static memory, never on the stack
static DrawList list;

void ui_init(vita2d_pgf *pgf){
	font = pgf;
	uint64_t now = sceKernelGetProcessTimeWide();
	screen_init(&screen, now);
	frametime_init(&frame_time, now);
	memset(&mirror, 0, sizeof(mirror));
	memset(&snap, 0, sizeof(snap));
	mirror_neutral(&snap);

	atlas = vita2d_load_PNG_file(SPRITES_ATLAS_PATH);
	if (atlas != NULL){
		// A wrong-sized atlas means the PNG and sprites.h disagree: draw no art rather than garbage
		if (vita2d_texture_get_width(atlas) != SPRITES_ATLAS_W || vita2d_texture_get_height(atlas) != SPRITES_ATLAS_H){
			vita2d_free_texture(atlas);
			atlas = NULL;
		}else{
			// Sprites are only ever drawn at whole pixels and scale 1
			vita2d_texture_set_filters(atlas, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
		}
	}
	art_loaded = atlas != NULL;
}

void ui_update(const SceCtrlData *pad, const char *ip, int wifi_link, int battery, int screen_off){
	uint64_t now = sceKernelGetProcessTimeWide();

	SceTouchData front;
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) < 0) front.reportNum = 0;
	ZonePoint points[ZONE_MAX_CONTACTS];
	int num = touchzones_read(&front, points);
	int new_contacts = 0;
	uint32_t zone_bits = zones_resolve(touchzones_table(), points, num, &latch, &new_contacts);
	int stats_tap = zones_tap_update(&tap, touchzones_table(), points, num, now);

	// What the PC gets (same expression as the packet code), recomputed here so the input threads stay untouched
	if (remap_menu_open) mirror_neutral(&snap);
	else{
		snap.buttons = remap_buttons(fast_apply(pad->buttons)) | ps_buttons();
		if (usb_connection() != CONNECTION_WIFI || touchzones_owns_front()) snap.buttons |= zone_bits;
		snap.lx = pad->lx;
		snap.ly = pad->ly;
		snap.rx = pad->rx;
		snap.ry = pad->ry;
	}
	mirror_compute(&snap, usb_connection() == CONNECTION_WIFI ? MIRROR_DPAD_DS4_CLIENT : MIRROR_DPAD_USB_HAT, &mirror);

	ScreenInput in;
	memset(&in, 0, sizeof(in));
	in.now_us = now;
	in.wifi_link = wifi_link;
	in.usb_link = usb_state() == USB_STATE_CONNECTED;
	in.remap_open = remap_menu_open;
	in.screen_off = screen_off;
	in.activity = screen_activity(&activity_ref, &snap, new_contacts);
	in.new_contacts = new_contacts;
	in.front_count = num;
	in.stats_tap = stats_tap;
	screen_update(&screen, &in);
	// With FrontTouch=Forward on Wi-Fi the PC client decides what a touch means, so the Vita's zone
	// outlines would be wrong: never show the hint there. (The stats tap still works in that mode, and
	// the same touch also reaches the PC.)
	if (usb_connection() == CONNECTION_WIFI && !touchzones_owns_front()){
		screen.hint = 0;
		screen.hint_until_us = 0;
	}

	memset(&setup, 0, sizeof(setup));
	setup.has_ip = ip != NULL;
	if (ip != NULL) snprintf(setup.ip, sizeof(setup.ip), "%s", ip);
	setup.port = GAMEPAD_PORT;
	setup.connection = usb_connection();
	setup.usb_state = usb_state();
	setup.ps_available = ps_available();
	setup.ps_mode = ps_mode();
	setup.remap_count = remap_changed_count();

	// The kernel call behind fast_stats() is only worth making while the numbers are on screen
	if (screen.stats_level > 0){
		memset(&stats_input, 0, sizeof(stats_input));
		input_rate_get(&stats_input.rates);
		stats_input.fast_ok = fast_stats(&stats_input.fast) != 0;
		stats_input.fast_enabled = fast_enabled();
		stats_input.battery = battery;
		stats_input.wifi_link = wifi_link;
		stats_input.usb_link = in.usb_link;
		stats_input.zones_owned = touchzones_owns_front();
		stats_input.zones_source = touchzones_source();
		stats_input.zones_count = touchzones_table()->count;
		stats_input.mirror_enabled = mirror_enabled;
		stats_input.art_loaded = art_loaded;
	}
}

static void draw_text(const DrawItem *it){
	float scale = it->scale;
	int width = vita2d_pgf_text_width(font, scale, it->text);
	if (it->x1 > 0){
		// Long text shrinks to fit instead of running over the artwork
		while (width > it->x1 && scale - TEXT_SCALE_STEP >= TEXT_MIN_SCALE - 0.001f){
			scale -= TEXT_SCALE_STEP;
			width = vita2d_pgf_text_width(font, scale, it->text);
		}
	}
	int x = it->align == ALIGN_CENTER ? it->x - width / 2 : it->x;
	vita2d_pgf_draw_text(font, x, it->y, it->color, scale, it->text);
}

void ui_draw(void){
	UiModel model;
	model.screen = &screen;
	model.mirror = &mirror;
	model.zones = touchzones_table();
	model.setup = &setup;
	model.stats = &stats_input;
	model.frame = &frame_time;
	model.mirror_enabled = mirror_enabled;
	model.art_loaded = art_loaded;
	uiplan_build(&model, &list);

	for (int i = 0; i < list.count; i++){
		const DrawItem *it = &list.items[i];
		switch (it->kind){
		case DRAW_SPRITE:
			if (!art_loaded || it->sprite >= SPR_COUNT) break;
			{
				const SpriteRect *r = &sprite_rects[it->sprite];
				vita2d_draw_texture_tint_part(atlas, it->x, it->y, r->ax, r->ay, r->w, r->h, it->color);
			}
			break;
		case DRAW_LINE:
			vita2d_draw_line(it->x, it->y, it->x1, it->y1, it->color);
			break;
		case DRAW_TEXT:
			draw_text(it);
			break;
		}
	}
}

void ui_frame_rendered(void){
	frametime_rendered(&frame_time, sceKernelGetProcessTimeWide());
}

void ui_frame_swapped(void){
	frametime_swapped(&frame_time, sceKernelGetProcessTimeWide());
}

int ui_mirror_enabled(void){
	return mirror_enabled;
}

void ui_set_mirror_enabled(int enabled){
	mirror_enabled = enabled != 0;
}
