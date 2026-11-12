// Prints the draw list of a named scenario as JSON, for tools/ui_preview.py (UI-SPEC.md 15.2).
// Usage: plan_dump <scenario> [--zones FILE] [--rear-zones FILE]    plan_dump --list
// Every scenario is built from fixed models and a synthetic clock, so the output never changes.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sprites.h"
#include "uiplan.h"
#include "padbits.h"
#include "usbmode.h"
#include "psbutton.h"
#include "protocol.h"

typedef struct {
	const char *name;
	// Which situation to build, see build_scenario
	int id;
} Scenario;

enum {
	S_SETUP_WIFI, S_SETUP_NO_WIFI, S_SETUP_USB_WAITING, S_SETUP_CROSS_L1, S_SETUP_HALF_FADED,
	S_PLAY_REST, S_PLAY_ALL, S_PLAY_STICKS, S_PLAY_L3, S_PLAY_HINT, S_PLAY_SUMMARY, S_PLAY_MIRROR_OFF, S_DIAG,
	S_COUNT
};

static const Scenario scenarios[S_COUNT] = {
	{ "setup_wifi", S_SETUP_WIFI }, { "setup_no_wifi", S_SETUP_NO_WIFI }, { "setup_usb_waiting", S_SETUP_USB_WAITING },
	{ "setup_cross_l1", S_SETUP_CROSS_L1 }, { "setup_half_faded", S_SETUP_HALF_FADED },
	{ "play_rest", S_PLAY_REST }, { "play_all", S_PLAY_ALL }, { "play_sticks", S_PLAY_STICKS }, { "play_l3", S_PLAY_L3 },
	{ "play_hint", S_PLAY_HINT }, { "play_summary", S_PLAY_SUMMARY }, { "play_mirror_off", S_PLAY_MIRROR_OFF },
	{ "diag", S_DIAG },
};

// Synthetic clock: frames of 10 ms, long idle stretches in the 100 ms steps the screen allows
typedef struct {
	Screen s;
	ScreenInput in;
	uint64_t now;
} Sim;

static void sim_init(Sim *sim){
	memset(sim, 0, sizeof(*sim));
	sim->now = 1000 * 1000;
	screen_init(&sim->s, sim->now);
}

// Advances by `us` in frames of `dt`, with the persistent inputs of sim->in; one-shot inputs apply to the first frame only
static void sim_run(Sim *sim, uint64_t us, uint64_t dt){
	for (uint64_t t = 0; t < us; t += dt){
		sim->now += dt;
		sim->in.now_us = sim->now;
		screen_update(&sim->s, &sim->in);
		sim->in.activity = 0;
		sim->in.new_contacts = 0;
		sim->in.stats_tap = 0;
	}
}

static const PadSnapshot *snapshot(uint32_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry){
	static PadSnapshot snap;
	snap.buttons = buttons;
	snap.lx = lx;
	snap.ly = ly;
	snap.rx = rx;
	snap.ry = ry;
	return &snap;
}

#define ALL_BUTTONS (PAD_SELECT | PAD_L3 | PAD_R3 | PAD_START | PAD_UP | PAD_RIGHT | PAD_DOWN | PAD_LEFT | PAD_LTRIGGER | PAD_RTRIGGER \
	| PAD_L1 | PAD_R1 | PAD_TRIANGLE | PAD_CIRCLE | PAD_CROSS | PAD_SQUARE | PAD_PS)

static DrawList list;

static void build_scenario(int id, const ZoneTable *zones, const ZoneTable *rear_zones, uint64_t *cost_out){
	Sim sim;
	sim_init(&sim);
	sim_run(&sim, 300 * 1000, 10 * 1000); // boot: SETUP fades in

	SetupInfo setup;
	memset(&setup, 0, sizeof(setup));
	setup.has_ip = 1;
	snprintf(setup.ip, sizeof(setup.ip), "192.168.1.42");
	setup.port = GAMEPAD_PORT;
	setup.connection = CONNECTION_WIFI;
	setup.usb_state = USB_STATE_OFF;
	setup.ps_available = 1;
	setup.ps_mode = PS_MODE_DOUBLE_TAP;
	setup.remap_count = 0;

	StatsInput stats;
	memset(&stats, 0, sizeof(stats));
	stats.rates.ctrl = 60;
	stats.rates.touch = 60;
	stats.rates.motion = 120;
	stats.rates.polls = 250;
	stats.rates.fresh = 60;
	stats.rates.streamed = 0;
	stats.fast_ok = 1;
	stats.fast_enabled = 1;
	stats.fast.available = 1;
	stats.fast.running = 1;
	stats.fast.learned = 12;
	stats.fast.avg_us = 8200;
	stats.fast.matched = 480;
	stats.fast.reads_per_s = 250;
	stats.fast.read_us = 55;
	stats.fast.read_max_us = 130;
	stats.fast.fallbacks = 0;
	stats.battery = 87;
	stats.zones_owned = 1;
	stats.zones_source = 0;
	stats.zones_count = zones->count;
	stats.rear_owned = 1;
	stats.rear_source = 0;
	stats.rear_count = rear_zones->count;
	stats.mirror_enabled = 1;
	stats.art_loaded = 1;

	// One full frame-time window at 60 fps
	FrameTime ft;
	frametime_init(&ft, 0);
	for (int i = 1; i <= 60; i++){
		frametime_rendered(&ft, (uint64_t)i * 16667 - 11000);
		frametime_swapped(&ft, (uint64_t)i * 16667);
	}

	PadSnapshot snap;
	mirror_neutral(&snap);
	int dpad_rule = MIRROR_DPAD_DS4_CLIENT;
	int mirror_enabled = 1;

	switch (id){
	case S_SETUP_WIFI:
		sim_run(&sim, 100 * 1000, 10 * 1000);
		break;
	case S_SETUP_NO_WIFI:
		setup.has_ip = 0;
		sim_run(&sim, 100 * 1000, 10 * 1000);
		break;
	case S_SETUP_USB_WAITING:
		setup.connection = CONNECTION_USB_DS4;
		setup.usb_state = USB_STATE_WAITING;
		dpad_rule = MIRROR_DPAD_USB_HAT;
		sim_run(&sim, 100 * 1000, 10 * 1000);
		break;
	case S_SETUP_CROSS_L1:
		snap = *snapshot(PAD_CROSS | PAD_L1, 128, 128, 128, 128);
		sim.in.activity = 1;
		sim_run(&sim, 300 * 1000, 10 * 1000);
		break;
	case S_SETUP_HALF_FADED:
		// Idle until the SETUP timeout (the 300 ms boot run above counts), then 300 ms into the 600 ms fade
		sim_run(&sim, UI_IDLE_TIMEOUT_SETUP_US - 300 * 1000, UI_MAX_STEP_US);
		sim_run(&sim, 300 * 1000, 10 * 1000);
		break;
	default:
		break;
	}

	if (id >= S_PLAY_REST){
		sim.in.wifi_link = 1;
		sim.in.activity = 0;
		// A PC is connected and streaming in every PLAY scenario, so the stats lines say so
		stats.wifi_link = 1;
		stats.rates.streamed = 165;
		switch (id){
		case S_PLAY_REST:
			sim_run(&sim, 4 * 1000 * 1000, 10 * 1000); // hint gone, body settled at the dimmed level
			break;
		case S_PLAY_ALL:
			sim_run(&sim, 4 * 1000 * 1000, 10 * 1000);
			snap = *snapshot(ALL_BUTTONS, 200, 40, 60, 220);
			sim.in.activity = 1;
			sim_run(&sim, 300 * 1000, 10 * 1000);
			break;
		case S_PLAY_STICKS:
			sim_run(&sim, 4 * 1000 * 1000, 10 * 1000);
			snap = *snapshot(0, 255, 0, 0, 255);
			sim.in.activity = 1;
			sim_run(&sim, 300 * 1000, 10 * 1000);
			break;
		case S_PLAY_L3:
			sim_run(&sim, 4 * 1000 * 1000, 10 * 1000);
			snap = *snapshot(PAD_L3, 200, 90, 128, 128);
			sim.in.activity = 1;
			sim_run(&sim, 300 * 1000, 10 * 1000);
			break;
		case S_PLAY_HINT:
			sim_run(&sim, 800 * 1000, 10 * 1000); // body already dimmed, hint still shown
			break;
		case S_PLAY_SUMMARY:
			sim_run(&sim, 4 * 1000 * 1000, 10 * 1000);
			sim.in.stats_tap = 1;
			sim_run(&sim, 500 * 1000, 10 * 1000);
			break;
		case S_PLAY_MIRROR_OFF:
			mirror_enabled = 0;
			snap = *snapshot(PAD_CROSS | PAD_L1, 128, 128, 128, 128);
			sim.in.activity = 1;
			sim_run(&sim, 1000 * 1000, 10 * 1000);
			break;
		case S_DIAG:
			sim_run(&sim, 4 * 1000 * 1000, 10 * 1000);
			sim.in.stats_tap = 1;
			sim_run(&sim, 100 * 1000, 10 * 1000);
			sim.in.stats_tap = 1;
			sim_run(&sim, 500 * 1000, 10 * 1000);
			break;
		default:
			break;
		}
	}

	MirrorState mirror;
	mirror_compute(&snap, dpad_rule, &mirror);

	UiModel model;
	memset(&model, 0, sizeof(model));
	model.screen = &sim.s;
	model.mirror = &mirror;
	model.zones = zones;
	model.rear_zones = rear_zones;
	model.rear_owned = 1;
	model.setup = &setup;
	model.stats = &stats;
	model.frame = &ft;
	model.mirror_enabled = mirror_enabled;
	model.art_loaded = 1;
	uiplan_build(&model, &list);
	*cost_out = (uint64_t)uiplan_cost(&list);
}

static void json_string(const char *s){
	putchar('"');
	for (; *s; s++){
		unsigned char c = (unsigned char)*s;
		if (c == '"') fputs("\\\"", stdout);
		else if (c == '\\') fputs("\\\\", stdout);
		else if (c < 0x20 || c >= 0x7F) printf("\\u%04x", c);
		else putchar(c);
	}
	putchar('"');
}

static void dump(const char *name, uint64_t cost){
	static const char *const kinds[] = { "sprite", "line", "text" };
	static const char *const aligns[] = { "left", "center" };
	printf("{\"scenario\": ");
	json_string(name);
	printf(", \"cost\": %llu, \"items\": [", (unsigned long long)cost);
	for (int i = 0; i < list.count; i++){
		const DrawItem *it = &list.items[i];
		printf("%s\n  {\"kind\": \"%s\", \"sprite\": %d, \"x\": %d, \"y\": %d, \"x1\": %d, \"y1\": %d, \"color\": %u, \"scale\": %.3f, \"align\": \"%s\", \"text\": ",
			i ? "," : "", kinds[it->kind <= DRAW_TEXT ? it->kind : 0], it->sprite, it->x, it->y, it->x1, it->y1,
			(unsigned)it->color, (double)it->scale, aligns[it->align <= ALIGN_CENTER ? it->align : 0]);
		json_string(it->text);
		putchar('}');
	}
	printf("\n]}\n");
}

// Reads a zones file over *table; returns 0 on success, else the exit code
static int load_zones_file(const char *path, ZoneTable *table){
	FILE *f = fopen(path, "rb");
	if (!f){
		fprintf(stderr, "%s: cannot open\n", path);
		return 1;
	}
	static char text[4096];
	size_t len = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[len] = 0;
	if (zones_parse(text, table) < 0){
		fprintf(stderr, "%s: invalid zones file (run zones_lint on it)\n", path);
		return 1;
	}
	return 0;
}

#define USAGE "usage: plan_dump <scenario> [--zones FILE] [--rear-zones FILE] | plan_dump --list\n"

int main(int argc, char **argv){
	const char *name = NULL, *zones_file = NULL, *rear_file = NULL;
	for (int i = 1; i < argc; i++){
		if (strcmp(argv[i], "--list") == 0){
			for (int k = 0; k < S_COUNT; k++) puts(scenarios[k].name);
			return 0;
		}else if (strcmp(argv[i], "--zones") == 0 && i + 1 < argc) zones_file = argv[++i];
		else if (strcmp(argv[i], "--rear-zones") == 0 && i + 1 < argc) rear_file = argv[++i];
		else if (!name && argv[i][0] != '-') name = argv[i];
		else{
			fprintf(stderr, USAGE);
			return 2;
		}
	}
	if (!name){
		fprintf(stderr, USAGE);
		return 2;
	}
	int id = -1;
	for (int k = 0; k < S_COUNT; k++) if (strcmp(scenarios[k].name, name) == 0) id = scenarios[k].id;
	if (id < 0){
		fprintf(stderr, "unknown scenario \"%s\" (plan_dump --list shows them)\n", name);
		return 2;
	}

	static ZoneTable zones, rear_zones;
	zones_default(&zones);
	zones_default_rear(&rear_zones);
	if (zones_file && load_zones_file(zones_file, &zones)) return 1;
	if (rear_file && load_zones_file(rear_file, &rear_zones)) return 1;

	uint64_t cost = 0;
	build_scenario(id, &zones, &rear_zones, &cost);
	dump(name, cost);
	return 0;
}
