// Host unit tests of the pure UI modules (UI-SPEC.md 15.1). No framework: CHECK counts failures.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sprites.h"
#include "padbits.h"
#include "zones.h"
#include "mirror.h"
#include "screen.h"
#include "stats.h"
#include "uiplan.h"
#include "usbmode.h"
#include "psbutton.h"

static int checks = 0, failed = 0;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)){ failed++; printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_STR(got, want) do { \
	checks++; \
	if (strcmp((got), (want)) != 0){ failed++; printf("%s:%d: got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (got), (want)); } \
} while (0)

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

// ---------------------------------------------------------------- zones

static const char default_text[] =
	"# Default zones\n"
	"Stats ellipse 960 544 240 200\n"
	"L1 rect 0 0 960 544\n"
	"R1 rect 960 0 1920 544\n"
	"L3 rect 0 544 960 1088\n"
	"R3 rect 960 544 1920 1088\n";

static int zone_is(const Zone *z, int shape, int action, uint32_t buttons, int a, int b, int c, int d){
	return z->shape == shape && z->action == action && z->buttons == buttons && z->a == a && z->b == b && z->c == c && z->d == d;
}

static int tables_equal(const ZoneTable *x, const ZoneTable *y){
	if (x->count != y->count) return 0;
	for (int i = 0; i < x->count; i++){
		if (!zone_is(&x->zones[i], y->zones[i].shape, y->zones[i].action, y->zones[i].buttons,
			y->zones[i].a, y->zones[i].b, y->zones[i].c, y->zones[i].d)) return 0;
	}
	return 1;
}

static void test_default_table(void){
	ZoneTable t;
	zones_default(&t);
	CHECK(t.count == 5);
	CHECK(zone_is(&t.zones[0], ZONE_SHAPE_ELLIPSE, ZONE_ACTION_STATS, 0, 960, 544, 240, 200));
	CHECK(zone_is(&t.zones[1], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L1, 0, 0, 960, 544));
	CHECK(zone_is(&t.zones[2], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R1, 960, 0, 1920, 544));
	CHECK(zone_is(&t.zones[3], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L3, 0, 544, 960, 1088));
	CHECK(zone_is(&t.zones[4], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R3, 960, 544, 1920, 1088));
}

static void test_hit(void){
	ZoneTable t;
	zones_default(&t);
	CHECK(zones_hit(&t, 0, 0) == 1);          // L1
	CHECK(zones_hit(&t, 1919, 1087) == 4);    // R3
	// The quadrant split at x = 960 and y = 544, away from the centre ellipse
	CHECK(zones_hit(&t, 959, 300) == 1);
	CHECK(zones_hit(&t, 960, 300) == 2);
	CHECK(zones_hit(&t, 959, 800) == 3);
	CHECK(zones_hit(&t, 960, 800) == 4);
	CHECK(zones_hit(&t, 700, 543) == 1);
	CHECK(zones_hit(&t, 700, 544) == 3);
	CHECK(zones_hit(&t, 1230, 543) == 2);
	CHECK(zones_hit(&t, 1230, 544) == 4);
	// The points next to the centre are inside the ellipse, which comes first in the table
	CHECK(zones_hit(&t, 959, 543) == 0);
	CHECK(zones_hit(&t, 960, 543) == 0);
	CHECK(zones_hit(&t, 959, 544) == 0);
	CHECK(zones_hit(&t, 960, 544) == 0);      // stats
	CHECK(zones_hit(&t, 960, 344) == 0);      // ellipse top, inclusive
	CHECK(zones_hit(&t, 960, 343) == 2);
	CHECK(zones_hit(&t, 720, 544) == 0);      // ellipse left end
	CHECK(zones_hit(&t, 719, 544) == 3);
	CHECK(zones_hit(&t, 1200, 544) == 0);     // right end
	CHECK(zones_hit(&t, 1201, 544) == 4);
	CHECK(zones_hit(&t, 960, 744) == 0);      // bottom
	CHECK(zones_hit(&t, 960, 745) == 4);

	// A table with gaps: outside every zone is -1, and None blocks later zones
	ZoneTable g;
	CHECK(zones_parse("None rect 0 0 100 100\nL1 rect 0 0 500 500\n", &g) == 2);
	CHECK(zones_hit(&g, 50, 50) == 0);
	CHECK(zones_hit(&g, 200, 200) == 1);
	CHECK(zones_hit(&g, 600, 600) == -1);
}

static void test_parse(void){
	ZoneTable def, t;
	zones_default(&def);
	CHECK(zones_parse(default_text, &t) == 5);
	CHECK(tables_equal(&t, &def));

	// Comments, blank lines, tabs, trailing comment, CRLF, no final newline
	CHECK(zones_parse("# c\n\n\tL1\trect  0 0 960 544  # trailing\r\n   \nR3 ellipse 100 200 30 40", &t) == 2);
	CHECK(zone_is(&t.zones[0], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L1, 0, 0, 960, 544));
	CHECK(zone_is(&t.zones[1], ZONE_SHAPE_ELLIPSE, ZONE_ACTION_BUTTON, PAD_R3, 100, 200, 30, 40));
	CHECK(zones_parse("None rect 0 0 1920 1088\n", &t) == 1);
	CHECK(t.zones[0].action == ZONE_ACTION_NONE && t.zones[0].buttons == 0);
	CHECK(zones_parse("Stats ellipse 1919 1087 1920 1088\n", &t) == 1); // largest legal ellipse

	// Rejections leave *out untouched
	char many[ZONE_MAX * 2 + 1][40];
	(void)many;
	static char seventeen[2048];
	seventeen[0] = 0;
	for (int i = 0; i < 17; i++) strcat(seventeen, "L1 rect 0 0 10 10\n");
	static char sixteen[2048];
	sixteen[0] = 0;
	for (int i = 0; i < 16; i++) strcat(sixteen, "L1 rect 0 0 10 10\n");
	static char too_long[2300];
	memset(too_long, '#', sizeof(too_long) - 1);
	too_long[sizeof(too_long) - 1] = 0;
	memcpy(too_long, "L1 rect 0 0 10 10\n", 18);
	too_long[18] = '#';
	// Exactly 2047 bytes is accepted, 2048 is not
	static char limit[2049];
	memset(limit, ' ', sizeof(limit) - 1);
	limit[sizeof(limit) - 1] = 0;
	memcpy(limit, "L1 rect 0 0 10 10", 17);
	limit[2047] = 0;
	CHECK(strlen(limit) == 2047);
	CHECK(zones_parse(limit, &t) == 1);
	limit[2047] = ' ';
	CHECK(strlen(limit) == 2048);

	CHECK(zones_parse(sixteen, &t) == 16);
	const char *bad[] = {
		"L2 rect 0 0 10 10\n",               // unknown kind
		"l1 rect 0 0 10 10\n",               // kind is case sensitive
		"L1 circle 0 0 10 10\n",             // unknown shape
		"L1 rect 0 0 10\n",                  // 5 tokens
		"L1 rect 0 0 10 10 10\n",            // 7 tokens
		"L1 rect -1 0 10 10\n",              // negative
		"L1 rect +1 0 10 10\n",
		"L1 rect 1x 0 10 10\n",
		"L1 rect 10 0 10 10\n",              // x0 >= x1
		"L1 rect 0 10 10 10\n",              // y0 >= y1
		"L1 rect 0 0 1921 10\n",             // c > 1920
		"L1 rect 0 0 10 1089\n",             // d > 1088
		"L1 rect 0 0 99999999 10\n",
		"L1 ellipse 10 10 0 5\n",            // radius 0
		"L1 ellipse 10 10 5 0\n",
		"L1 ellipse 1920 10 5 5\n",          // centre outside
		"L1 ellipse 10 1088 5 5\n",
		"L1 ellipse 10 10 1921 5\n",
		"",                                   // empty
		"# only a comment\n\n",
		"L1 rect 0 0 10 10\nbogus\n",        // one bad line spoils the file
		seventeen,
		too_long,
		limit,
	};
	for (size_t i = 0; i < ARRAY_LEN(bad); i++){
		ZoneTable keep;
		memset(&keep, 0xAB, sizeof(keep));
		ZoneTable ref = keep;
		CHECK(zones_parse(bad[i], &keep) == -1);
		CHECK(memcmp(&keep, &ref, sizeof(keep)) == 0);
	}
	CHECK(zones_parse(NULL, &t) == -1);
}

// fill_touch (main.c:65-74), written out with its own arithmetic as the reference
static int ref_normalise(int raw, int min, int max, int range){
	int x = raw - min;
	int w = max - min;
	if (w > 0) x = x * (range - 1) / w;
	if (x < 0) x = 0;
	if (x > range - 1) x = range - 1;
	return x;
}

static void test_normalise(void){
	CHECK(zones_normalise(0, 0, 1919, 1920) == 0);
	CHECK(zones_normalise(1919, 0, 1919, 1920) == 1919);
	CHECK(zones_normalise(959, 0, 1919, 1920) == 959);
	CHECK(zones_normalise(-50, 0, 1919, 1920) == 0);          // below min
	CHECK(zones_normalise(5000, 0, 1919, 1920) == 1919);      // above max
	CHECK(zones_normalise(100, 100, 1819, 1920) == 0);
	CHECK(zones_normalise(1819, 100, 1819, 1920) == 1919);
	CHECK(zones_normalise(960, 0, 1087, 1088) == 960);
	CHECK(zones_normalise(10, 5, 5, 1920) == 5);              // zero-width panel: raw minus min
	CHECK(zones_normalise(5000, 5, 5, 1920) == 1919);
	CHECK(zones_normalise(2, 5, 5, 1920) == 0);
	const int mins[] = { 0, 100, 5 }, maxs[] = { 1919, 1819, 5 };
	for (int p = 0; p < 3; p++){
		for (int raw = -20; raw < 2100; raw += 7){
			CHECK(zones_normalise(raw, mins[p], maxs[p], TOUCH_WIDTH) == ref_normalise(raw, mins[p], maxs[p], TOUCH_WIDTH));
			CHECK(zones_normalise(raw, mins[p], maxs[p], TOUCH_HEIGHT) == ref_normalise(raw, mins[p], maxs[p], TOUCH_HEIGHT));
		}
	}
}

static void test_resolve(void){
	ZoneTable t;
	zones_default(&t);
	ZoneLatch latch = { 0 };   // zero-initialised is a valid latch
	int fresh = -1;
	ZonePoint p[10];

	p[0] = (ZonePoint){ 100, 100, 1 };
	CHECK(zones_resolve(&t, p, 1, &latch, &fresh) == PAD_L1);
	CHECK(fresh == 1);
	p[0] = (ZonePoint){ 1500, 900, 1 };       // same id slides into R3: still L1
	CHECK(zones_resolve(&t, p, 1, &latch, &fresh) == PAD_L1);
	CHECK(fresh == 0);
	CHECK(zones_resolve(&t, p, 0, &latch, &fresh) == 0);   // lift
	CHECK(latch.count == 0);
	CHECK(zones_resolve(&t, p, 1, &latch, &fresh) == PAD_R3); // lands again in R3
	CHECK(fresh == 1);

	// Starting in the stats ellipse produces nothing, also after sliding into L1
	zones_latch_reset(&latch);
	p[0] = (ZonePoint){ 960, 544, 2 };
	CHECK(zones_resolve(&t, p, 1, &latch, &fresh) == 0);
	p[0] = (ZonePoint){ 100, 100, 2 };
	CHECK(zones_resolve(&t, p, 1, &latch, &fresh) == 0);
	CHECK(fresh == 0);

	// Six contacts in four zones: every bit, each once
	zones_latch_reset(&latch);
	p[0] = (ZonePoint){ 100, 100, 10 };
	p[1] = (ZonePoint){ 1500, 100, 11 };
	p[2] = (ZonePoint){ 100, 900, 12 };
	p[3] = (ZonePoint){ 1500, 900, 13 };
	p[4] = (ZonePoint){ 200, 200, 14 };
	p[5] = (ZonePoint){ 1600, 200, 15 };
	CHECK(zones_resolve(&t, p, 6, &latch, &fresh) == (PAD_L1 | PAD_R1 | PAD_L3 | PAD_R3));
	CHECK(fresh == 6);
	CHECK(latch.count == 6);

	// Eight contacts are all handled; more than eight are cut to eight
	zones_latch_reset(&latch);
	for (int i = 0; i < 10; i++) p[i] = (ZonePoint){ (uint16_t)(100 + i), 100, (uint8_t)(20 + i) };
	CHECK(zones_resolve(&t, p, 8, &latch, &fresh) == PAD_L1);
	CHECK(latch.count == 8 && fresh == 8);
	zones_latch_reset(&latch);
	CHECK(zones_resolve(&t, p, 10, &latch, &fresh) == PAD_L1);
	CHECK(latch.count == ZONE_MAX_CONTACTS);

	// new_contacts counts only unseen ids; contacts missing from the sample are dropped; NULL is allowed
	zones_latch_reset(&latch);
	p[0] = (ZonePoint){ 100, 100, 1 };
	p[1] = (ZonePoint){ 1500, 100, 2 };
	CHECK(zones_resolve(&t, p, 2, &latch, &fresh) == (PAD_L1 | PAD_R1));
	CHECK(fresh == 2);
	p[0] = (ZonePoint){ 1500, 100, 2 };
	p[1] = (ZonePoint){ 100, 900, 3 };
	CHECK(zones_resolve(&t, p, 2, &latch, &fresh) == (PAD_R1 | PAD_L3));
	CHECK(fresh == 1);
	CHECK(latch.count == 2);
	CHECK(zones_resolve(&t, p, 2, &latch, NULL) == (PAD_R1 | PAD_L3));

	// A contact outside every zone produces nothing
	ZoneTable g;
	CHECK(zones_parse("L1 rect 0 0 100 100\n", &g) == 1);
	zones_latch_reset(&latch);
	p[0] = (ZonePoint){ 500, 500, 1 };
	CHECK(zones_resolve(&g, p, 1, &latch, &fresh) == 0);
	CHECK(latch.zone[0] == -1);
	// A None zone latches too, and slides out of it press nothing
	CHECK(zones_parse("None rect 0 0 100 100\nL1 rect 0 0 1000 1000\n", &g) == 2);
	zones_latch_reset(&latch);
	p[0] = (ZonePoint){ 50, 50, 1 };
	CHECK(zones_resolve(&g, p, 1, &latch, &fresh) == 0);
	p[0] = (ZonePoint){ 500, 500, 1 };
	CHECK(zones_resolve(&g, p, 1, &latch, &fresh) == 0);
}

static void test_tap(void){
	ZoneTable t;
	zones_default(&t);
	TapTracker tr;
	memset(&tr, 0, sizeof(tr));
	const uint64_t t0 = 5 * 1000 * 1000;
	ZonePoint p[3];

	// 200 ms, 30 units of drift, stats zone: a tap
	p[0] = (ZonePoint){ 960, 544, 1 };
	CHECK(zones_tap_update(&tr, &t, p, 1, t0) == 0);
	p[0] = (ZonePoint){ 990, 544, 1 };
	CHECK(zones_tap_update(&tr, &t, p, 1, t0 + 100000) == 0);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 200000) == 1);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 216000) == 0);   // reports only once

	// 400 ms: not a tap
	memset(&tr, 0, sizeof(tr));
	p[0] = (ZonePoint){ 960, 544, 1 };
	zones_tap_update(&tr, &t, p, 1, t0);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 400000) == 0);

	// The limit itself is a tap
	memset(&tr, 0, sizeof(tr));
	zones_tap_update(&tr, &t, p, 1, t0);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + ZONE_TAP_MAX_US) == 1);

	// Drift of 100 units: not a tap; exactly 64 is
	memset(&tr, 0, sizeof(tr));
	zones_tap_update(&tr, &t, p, 1, t0);
	p[0] = (ZonePoint){ 1060, 544, 1 };
	zones_tap_update(&tr, &t, p, 1, t0 + 50000);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 100000) == 0);
	memset(&tr, 0, sizeof(tr));
	p[0] = (ZonePoint){ 960, 544, 1 };
	zones_tap_update(&tr, &t, p, 1, t0);
	p[0] = (ZonePoint){ 960 + ZONE_TAP_SLOP, 544, 1 };
	zones_tap_update(&tr, &t, p, 1, t0 + 50000);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 100000) == 1);

	// Drift out and back still counts as drift
	memset(&tr, 0, sizeof(tr));
	p[0] = (ZonePoint){ 960, 544, 1 };
	zones_tap_update(&tr, &t, p, 1, t0);
	p[0] = (ZonePoint){ 1100, 544, 1 };
	zones_tap_update(&tr, &t, p, 1, t0 + 30000);
	p[0] = (ZonePoint){ 960, 544, 1 };
	zones_tap_update(&tr, &t, p, 1, t0 + 60000);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 90000) == 0);

	// Starting in L1: not a tap
	memset(&tr, 0, sizeof(tr));
	p[0] = (ZonePoint){ 100, 100, 1 };
	zones_tap_update(&tr, &t, p, 1, t0);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 100000) == 0);

	// Two taps in one frame report 1
	memset(&tr, 0, sizeof(tr));
	p[0] = (ZonePoint){ 960, 544, 1 };
	p[1] = (ZonePoint){ 1000, 544, 2 };
	zones_tap_update(&tr, &t, p, 2, t0);
	CHECK(zones_tap_update(&tr, &t, p, 0, t0 + 100000) == 1);
	CHECK(tr.count == 0);

	// A second finger on a button zone doesn't spoil a tap, and a held contact is not a lift
	memset(&tr, 0, sizeof(tr));
	p[0] = (ZonePoint){ 960, 544, 1 };
	p[1] = (ZonePoint){ 100, 100, 2 };
	zones_tap_update(&tr, &t, p, 2, t0);
	CHECK(zones_tap_update(&tr, &t, p + 1, 1, t0 + 100000) == 1); // id 1 lifted
	CHECK(zones_tap_update(&tr, &t, p + 1, 1, t0 + 500000) == 0);
}

// ---------------------------------------------------------------- mirror

static MirrorState mirror_of(uint32_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, int rule){
	PadSnapshot s = { buttons, lx, ly, rx, ry };
	MirrorState m;
	mirror_compute(&s, rule, &m);
	return m;
}

static void test_mirror_bits(void){
	static const struct { uint32_t bit; int part; } single[] = {
		{ PAD_SELECT, PART_SHARE }, { PAD_START, PART_OPTIONS },
		{ PAD_LTRIGGER, PART_L2 }, { PAD_RTRIGGER, PART_R2 },
		{ PAD_TRIANGLE, PART_TRIANGLE }, { PAD_CIRCLE, PART_CIRCLE }, { PAD_CROSS, PART_CROSS }, { PAD_SQUARE, PART_SQUARE },
		{ PAD_L1, PART_L1 }, { PAD_R1, PART_R1 }, { PAD_L3, PART_L3 }, { PAD_R3, PART_R3 }, { PAD_PS, PART_PS },
		{ PAD_UP, PART_DPAD_UP }, { PAD_RIGHT, PART_DPAD_RIGHT }, { PAD_DOWN, PART_DPAD_DOWN }, { PAD_LEFT, PART_DPAD_LEFT },
	};
	for (int rule = 0; rule < 2; rule++){
		for (size_t i = 0; i < ARRAY_LEN(single); i++){
			MirrorState m = mirror_of(single[i].bit, 128, 128, 128, 128, rule);
			CHECK(m.lit == (1u << single[i].part));
		}
		MirrorState none = mirror_of(0, 128, 128, 128, 128, rule);
		CHECK(none.lit == 0);
		// Headphone, volume, power and anything above PAD_PS
		CHECK(mirror_of(0x80000, 128, 128, 128, 128, rule).lit == 0);
		CHECK(mirror_of(0x100000, 128, 128, 128, 128, rule).lit == 0);
		CHECK(mirror_of(0x20000, 128, 128, 128, 128, rule).lit == 0);
		CHECK(mirror_of(0xFFFE0000u, 128, 128, 128, 128, rule).lit == 0);
	}
	// Everything at once, with no opposite directions, lights every part
	MirrorState all = mirror_of(PAD_MIRROR_MASK & ~(PAD_DOWN | PAD_LEFT), 128, 128, 128, 128, MIRROR_DPAD_USB_HAT);
	CHECK((all.lit & ((1u << PART_COUNT) - 1)) == (((1u << PART_COUNT) - 1) & ~((1u << PART_DPAD_DOWN) | (1u << PART_DPAD_LEFT))));
	CHECK((all.lit >> PART_COUNT) == 0);
}

static void test_dpad(void){
	// Hand-written from Client/ViGEm.cpp:206-220 (last present wins among UP, RIGHT, DOWN, LEFT, then among
	// NE, SE, SW, NW) and usbmode.c:57-69 (first match of NE, SE, SW, NW, UP, RIGHT, DOWN, LEFT).
	// Index bits: 1 = UP, 2 = RIGHT, 4 = DOWN, 8 = LEFT. Value: arms lit with the same bits.
	static const int client[16] = {
		/* none      */ 0,
		/* U         */ 1,
		/* R         */ 2,
		/* U+R       */ 1 | 2,
		/* D         */ 4,
		/* U+D       */ 4,
		/* R+D       */ 2 | 4,
		/* U+R+D     */ 2 | 4,
		/* L         */ 8,
		/* U+L       */ 1 | 8,
		/* R+L       */ 8,
		/* U+R+L     */ 1 | 8,
		/* D+L       */ 4 | 8,
		/* U+D+L     */ 1 | 8,
		/* R+D+L     */ 4 | 8,
		/* U+R+D+L   */ 1 | 8,
	};
	static const int usb[16] = {
		/* none      */ 0,
		/* U         */ 1,
		/* R         */ 2,
		/* U+R       */ 1 | 2,
		/* D         */ 4,
		/* U+D       */ 1,
		/* R+D       */ 2 | 4,
		/* U+R+D     */ 1 | 2,
		/* L         */ 8,
		/* U+L       */ 1 | 8,
		/* R+L       */ 2,
		/* U+R+L     */ 1 | 2,
		/* D+L       */ 4 | 8,
		/* U+D+L     */ 4 | 8,
		/* R+D+L     */ 2 | 4,
		/* U+R+D+L   */ 1 | 2,
	};
	for (int i = 0; i < 16; i++){
		uint32_t b = ((i & 1) ? PAD_UP : 0) | ((i & 2) ? PAD_RIGHT : 0) | ((i & 4) ? PAD_DOWN : 0) | ((i & 8) ? PAD_LEFT : 0);
		const uint32_t arm_mask = (1u << PART_DPAD_UP) | (1u << PART_DPAD_RIGHT) | (1u << PART_DPAD_DOWN) | (1u << PART_DPAD_LEFT);
		for (int rule = 0; rule < 2; rule++){
			int want = rule == MIRROR_DPAD_DS4_CLIENT ? client[i] : usb[i];
			uint32_t want_bits = ((want & 1) ? 1u << PART_DPAD_UP : 0) | ((want & 2) ? 1u << PART_DPAD_RIGHT : 0)
				| ((want & 4) ? 1u << PART_DPAD_DOWN : 0) | ((want & 8) ? 1u << PART_DPAD_LEFT : 0);
			MirrorState m = mirror_of(b, 128, 128, 128, 128, rule);
			CHECK((m.lit & arm_mask) == want_bits);
			CHECK((m.lit & ~arm_mask) == 0);
		}
	}
	// The example of the spec: UP + DOWN
	MirrorState c = mirror_of(PAD_UP | PAD_DOWN, 128, 128, 128, 128, MIRROR_DPAD_DS4_CLIENT);
	MirrorState u = mirror_of(PAD_UP | PAD_DOWN, 128, 128, 128, 128, MIRROR_DPAD_USB_HAT);
	CHECK(c.lit == (1u << PART_DPAD_DOWN));
	CHECK(u.lit == (1u << PART_DPAD_UP));
}

static void test_sticks(void){
	CHECK(mirror_stick_offset(0) == -12);
	CHECK(mirror_stick_offset(1) == -12);
	CHECK(mirror_stick_offset(64) == -6);
	CHECK(mirror_stick_offset(128) == 0);
	CHECK(mirror_stick_offset(192) == 6);
	CHECK(mirror_stick_offset(255) == 12);
	for (int v = 0; v < 255; v++) CHECK(mirror_stick_offset((uint8_t)v) <= mirror_stick_offset((uint8_t)(v + 1)));

	MirrorState m = mirror_of(0, 144, 112, 128, 128, MIRROR_DPAD_DS4_CLIENT);
	CHECK(!m.lcap_deflected && m.lcap_dx == 0 && m.lcap_dy == 0);
	m = mirror_of(0, 145, 128, 128, 128, MIRROR_DPAD_DS4_CLIENT);
	CHECK(m.lcap_deflected && m.lcap_dx == mirror_stick_offset(145) && m.lcap_dy == 0);
	CHECK(!m.rcap_deflected);
	m = mirror_of(0, 128, 128, 255, 0, MIRROR_DPAD_DS4_CLIENT);
	CHECK(!m.lcap_deflected);
	CHECK(m.rcap_deflected && m.rcap_dx == 12 && m.rcap_dy == -12);
	m = mirror_of(0, 0, 255, 128, 128, MIRROR_DPAD_DS4_CLIENT);
	CHECK(m.lcap_dx == -12 && m.lcap_dy == 12);
	// A stick alone never lights a part
	CHECK(m.lit == 0);
}

static void test_l3_neutral(void){
	MirrorState m = mirror_of(PAD_L3, 200, 60, 128, 128, MIRROR_DPAD_DS4_CLIENT);
	CHECK(m.lit == (1u << PART_L3));
	CHECK(m.lcap_deflected);
	CHECK(m.lcap_dx == mirror_stick_offset(200) && m.lcap_dy == mirror_stick_offset(60));
	CHECK(m.lcap_dx != 0 && m.lcap_dy != 0);

	PadSnapshot s = { 0xFFFFFFFFu, 1, 2, 3, 4 };
	mirror_neutral(&s);
	CHECK(s.buttons == 0 && s.lx == 128 && s.ly == 128 && s.rx == 128 && s.ry == 128);
	mirror_compute(&s, MIRROR_DPAD_DS4_CLIENT, &m);
	CHECK(m.lit == 0);
	CHECK(m.lcap_dx == 0 && m.lcap_dy == 0 && m.rcap_dx == 0 && m.rcap_dy == 0);
	CHECK(!m.lcap_deflected && !m.rcap_deflected);
}

// ---------------------------------------------------------------- screen

// A synthetic clock around one Screen. dt is the frame step; persistent inputs live in `in`,
// one-shot ones (activity, new_contacts, stats_tap) apply to the next frame only
typedef struct {
	Screen s;
	ScreenInput in;
	uint64_t now;
} Sim;

static void sim_init(Sim *sim, uint64_t t0){
	memset(sim, 0, sizeof(*sim));
	sim->now = t0;
	screen_init(&sim->s, t0);
}

static void sim_frame(Sim *sim, uint64_t dt){
	sim->now += dt;
	sim->in.now_us = sim->now;
	screen_update(&sim->s, &sim->in);
	sim->in.activity = 0;
	sim->in.new_contacts = 0;
	sim->in.stats_tap = 0;
}

static void sim_run(Sim *sim, uint64_t duration, uint64_t dt){
	for (uint64_t t = 0; t < duration; t += dt) sim_frame(sim, dt);
}

static void test_boot(void){
	Sim sim;
	sim_init(&sim, 1000000);
	CHECK(sim.s.mode == SCREEN_SETUP);
	CHECK(sim.s.body == 0 && sim.s.text == 0 && sim.s.lit == 0 && sim.s.hint == 0 && sim.s.stats_level == 0);
	sim_run(&sim, UI_FADE_IN_US / 2, 10000);
	CHECK(sim.s.body > 0 && sim.s.body < UI_LEVEL_MAX);
	CHECK(sim.s.text > 0 && sim.s.text < UI_LEVEL_MAX);
	sim_run(&sim, UI_FADE_IN_US / 2, 10000);
	CHECK(sim.s.body == UI_LEVEL_MAX);
	CHECK(sim.s.text == UI_LEVEL_MAX);
	CHECK(sim.s.lit == UI_LEVEL_MAX);
	CHECK(sim.s.mode == SCREEN_SETUP);
	CHECK(sim.s.hint == 0);
	CHECK(sim.s.stats == STATS_HIDDEN);

	// A clock that starts at 0 behaves the same: the link bookkeeping must not think a link was ever up
	Sim z;
	sim_init(&z, 0);
	sim_run(&z, 1000000, 10000);
	CHECK(z.s.mode == SCREEN_SETUP);
}

static void test_wifi_rise(void){
	Sim sim;
	sim_init(&sim, 1000000);
	sim_run(&sim, 300000, 10000);
	CHECK(sim.s.body == UI_LEVEL_MAX && sim.s.text == UI_LEVEL_MAX);
	sim.in.wifi_link = 1;
	sim_frame(&sim, 10000);
	const uint64_t t_link = sim.now;
	CHECK(sim.s.mode == SCREEN_PLAY);
	CHECK(sim.s.hint_until_us == t_link + UI_HINT_SHOW_US);
	sim_run(&sim, 100000, 10000);
	CHECK(sim.s.body > UI_PLAY_BODY_LEVEL && sim.s.body < UI_LEVEL_MAX);   // on its way down
	CHECK(sim.s.text > 0 && sim.s.text < UI_LEVEL_MAX);
	sim_run(&sim, UI_FADE_OUT_US - 100000, 10000);
	CHECK(sim.s.body == UI_PLAY_BODY_LEVEL);              // stops at the dimmed level
	CHECK(sim.s.text == 0);                               // the text is gone after UI_FADE_OUT_US
	CHECK(sim.s.hint == UI_LEVEL_MAX);
	CHECK(sim.s.lit == UI_LEVEL_MAX);
	sim_run(&sim, UI_HINT_SHOW_US - UI_FADE_OUT_US - 20000, 10000);
	CHECK(sim.s.hint == UI_LEVEL_MAX);                    // still inside the 2.5 s
	sim_run(&sim, UI_FADE_OUT_US + 100000, 10000);
	CHECK(sim.s.hint == 0);
	CHECK(sim.s.mode == SCREEN_PLAY);
	CHECK(sim.s.body == UI_PLAY_BODY_LEVEL && sim.s.text == 0);   // the body holds, it never reaches 0
}

static void test_link_grace(void){
	// Wi-Fi: 4.9 s down and back: still PLAY, no second hint
	Sim sim;
	sim_init(&sim, 1000000);
	sim.in.wifi_link = 1;
	sim_frame(&sim, 100000);
	const uint64_t hint_until = sim.s.hint_until_us;
	sim_run(&sim, 5000000, 100000);
	sim.in.wifi_link = 0;
	const uint64_t last_up = sim.now;
	while (sim.now + 100000 < last_up + 4900000) sim_frame(&sim, 100000);
	sim_frame(&sim, last_up + 4900000 - sim.now);
	CHECK(sim.now - last_up == 4900000);
	CHECK(sim.s.mode == SCREEN_PLAY);
	sim.in.wifi_link = 1;
	sim_frame(&sim, 100000);
	CHECK(sim.s.mode == SCREEN_PLAY);
	CHECK(sim.s.hint_until_us == hint_until);

	// 5.0 s down: SETUP
	sim.in.wifi_link = 0;
	const uint64_t last_up2 = sim.now;
	while (sim.now + 100000 < last_up2 + 5000000){
		sim_frame(&sim, 100000);
		CHECK(sim.s.mode == SCREEN_PLAY);
	}
	sim_frame(&sim, last_up2 + 5000000 - sim.now);
	CHECK(sim.now - last_up2 == 5000000);
	CHECK(sim.s.mode == SCREEN_SETUP);

	// USB: 0.9 s keeps PLAY, 1.0 s returns SETUP
	Sim u;
	sim_init(&u, 1000000);
	u.in.usb_link = 1;
	sim_run(&u, 2000000, 100000);
	CHECK(u.s.mode == SCREEN_PLAY);
	u.in.usb_link = 0;
	const uint64_t usb_up = u.now;
	sim_run(&u, 800000, 100000);
	sim_frame(&u, 100000);
	CHECK(u.now - usb_up == 900000);
	CHECK(u.s.mode == SCREEN_PLAY);
	sim_frame(&u, 100000);
	CHECK(u.now - usb_up == 1000000);
	CHECK(u.s.mode == SCREEN_SETUP);

	// With both links in play, the longer grace (Wi-Fi) decides while only USB was up long ago
	Sim w;
	sim_init(&w, 1000000);
	w.in.wifi_link = 1;
	sim_run(&w, 3000000, 100000);
	w.in.wifi_link = 0;
	sim_run(&w, 2000000, 100000);
	CHECK(w.s.mode == SCREEN_PLAY);
}

static void test_idle(void){
	// PLAY
	Sim sim;
	sim_init(&sim, 1000000);
	sim.in.wifi_link = 1;
	sim_frame(&sim, 100000);          // switch to PLAY: starts awake
	const uint64_t t_awake = sim.now;
	sim_run(&sim, 2000000, 100000);
	CHECK(sim.s.lit == UI_LEVEL_MAX && sim.s.body == UI_PLAY_BODY_LEVEL);
	while (sim.now + 100000 <= t_awake + UI_IDLE_TIMEOUT_US - 100000) sim_frame(&sim, 100000);
	CHECK(sim.now == t_awake + UI_IDLE_TIMEOUT_US - 100000);
	CHECK(sim.s.lit == UI_LEVEL_MAX && sim.s.body == UI_PLAY_BODY_LEVEL);
	sim_frame(&sim, 100000);
	CHECK(sim.now == t_awake + UI_IDLE_TIMEOUT_US);
	CHECK(sim.s.lit < UI_LEVEL_MAX);                       // falls from the timeout
	CHECK(sim.s.body < UI_PLAY_BODY_LEVEL);
	sim_run(&sim, UI_FADE_OUT_US, 100000);
	CHECK(sim.s.lit == 0 && sim.s.body == 0 && sim.s.text == 0);
	CHECK(sim.s.hint == 0);
	// A button change restores it in 150 ms: the dimmed body and the full lit level
	sim.in.activity = 1;
	sim_run(&sim, UI_FADE_IN_US, 10000);
	CHECK(sim.s.lit == UI_LEVEL_MAX);
	CHECK(sim.s.body == UI_PLAY_BODY_LEVEL);
	CHECK(sim.s.text == 0);

	// SETUP: the body and text hold one frame before the timeout and fall from it
	Sim su;
	sim_init(&su, 1000000);
	const uint64_t t0 = su.now;
	while (su.now + 100000 <= t0 + UI_IDLE_TIMEOUT_SETUP_US - 100000) sim_frame(&su, 100000);
	CHECK(su.now == t0 + UI_IDLE_TIMEOUT_SETUP_US - 100000);
	CHECK(su.s.body == UI_LEVEL_MAX && su.s.text == UI_LEVEL_MAX);
	sim_frame(&su, 100000);
	CHECK(su.now == t0 + UI_IDLE_TIMEOUT_SETUP_US);
	CHECK(su.s.body < UI_LEVEL_MAX && su.s.text < UI_LEVEL_MAX);
	sim_run(&su, UI_FADE_OUT_US, 100000);
	CHECK(su.s.body == 0 && su.s.text == 0);

	// A mode switch after a long idle starts the new mode awake: SETUP asleep, then the link rises
	su.in.wifi_link = 1;
	sim_frame(&su, 10000);
	CHECK(su.s.mode == SCREEN_PLAY);
	sim_run(&su, UI_FADE_IN_US, 10000);
	CHECK(su.s.lit == UI_LEVEL_MAX && su.s.hint == UI_LEVEL_MAX);
	CHECK(su.s.body == UI_PLAY_BODY_LEVEL && su.s.text == 0);
	// ... and PLAY asleep, then the link drops for good: SETUP fades in
	Sim pl;
	sim_init(&pl, 1000000);
	pl.in.wifi_link = 1;
	sim_run(&pl, UI_IDLE_TIMEOUT_US + UI_FADE_OUT_US + 100000, 100000);
	CHECK(pl.s.lit == 0 && pl.s.body == 0 && pl.s.text == 0);
	pl.in.wifi_link = 0;
	sim_run(&pl, UI_WIFI_LOST_GRACE_US + 100000, 100000);
	CHECK(pl.s.mode == SCREEN_SETUP);
	sim_run(&pl, UI_FADE_IN_US + 20000, 10000);
	CHECK(pl.s.body == UI_LEVEL_MAX && pl.s.text == UI_LEVEL_MAX);
}

static void test_activity(void){
	ActivityRef ref;
	memset(&ref, 0, sizeof(ref));
	PadSnapshot snap;
	mirror_neutral(&snap);
	CHECK(screen_activity(&ref, &snap, 0) == 1);      // first frame
	CHECK(screen_activity(&ref, &snap, 0) == 0);
	snap.lx = 128 + UI_STICK_ACTIVITY_DELTA - 1;
	CHECK(screen_activity(&ref, &snap, 0) == 0);
	snap.lx = 128 + UI_STICK_ACTIVITY_DELTA;
	CHECK(screen_activity(&ref, &snap, 0) == 1);
	CHECK(screen_activity(&ref, &snap, 0) == 0);      // the reference moved
}

static void test_activity_details(void){
	ActivityRef ref;
	memset(&ref, 0, sizeof(ref));
	PadSnapshot snap;
	mirror_neutral(&snap);
	screen_activity(&ref, &snap, 0);

	// Each axis counts
	uint8_t *axes[4] = { &snap.lx, &snap.ly, &snap.rx, &snap.ry };
	for (int a = 0; a < 4; a++){
		mirror_neutral(&snap);
		memset(&ref, 0, sizeof(ref));
		screen_activity(&ref, &snap, 0);
		*axes[a] = 128 - (UI_STICK_ACTIVITY_DELTA - 1);
		CHECK(screen_activity(&ref, &snap, 0) == 0);
		*axes[a] = 128 - UI_STICK_ACTIVITY_DELTA;
		CHECK(screen_activity(&ref, &snap, 0) == 1);
	}

	// Slow drift of 1 per frame for 100 frames: activity at most once per 24 units
	mirror_neutral(&snap);
	memset(&ref, 0, sizeof(ref));
	screen_activity(&ref, &snap, 0);
	int events = 0;
	for (int i = 1; i <= 100; i++){
		snap.lx = (uint8_t)(128 + i);
		events += screen_activity(&ref, &snap, 0);
	}
	CHECK(events == 100 / UI_STICK_ACTIVITY_DELTA);
	CHECK(events >= 1);

	// A held button is activity on its first frame only; release is activity too
	mirror_neutral(&snap);
	memset(&ref, 0, sizeof(ref));
	screen_activity(&ref, &snap, 0);
	snap.buttons = PAD_CROSS;
	CHECK(screen_activity(&ref, &snap, 0) == 1);
	for (int i = 0; i < 50; i++) CHECK(screen_activity(&ref, &snap, 0) == 0);
	snap.buttons = 0;
	CHECK(screen_activity(&ref, &snap, 0) == 1);

	// A new front contact is activity; bits the PC never sees are not
	CHECK(screen_activity(&ref, &snap, 0) == 0);
	CHECK(screen_activity(&ref, &snap, 1) == 1);
	snap.buttons = 0x80000;
	CHECK(screen_activity(&ref, &snap, 0) == 0);
	snap.buttons = 0x100000;
	CHECK(screen_activity(&ref, &snap, 0) == 0);
}

static void test_hint_rearm(void){
	// First contact after boot
	Sim sim;
	sim_init(&sim, 1000000);
	sim_run(&sim, 500000, 100000);
	CHECK(sim.s.hint_until_us == 0);
	sim.in.new_contacts = 1;
	sim.in.front_count = 1;
	sim.in.activity = 1;
	sim_frame(&sim, 100000);
	const uint64_t t0 = sim.now;
	CHECK(sim.s.hint_until_us == t0 + UI_HINT_SHOW_US);
	sim.in.front_count = 0;

	// A new contact after 30 s of quiet re-shows it
	sim_run(&sim, UI_HINT_REARM_QUIET_US - 100000, 100000);
	sim.in.new_contacts = 1;
	sim.in.front_count = 1;
	sim_frame(&sim, 100000);
	CHECK(sim.now == t0 + UI_HINT_REARM_QUIET_US);
	CHECK(sim.s.hint_until_us == sim.now + UI_HINT_SHOW_US);

	// After only 29 s it does not
	Sim s2;
	sim_init(&s2, 1000000);
	s2.in.new_contacts = 1;
	s2.in.front_count = 1;
	sim_frame(&s2, 100000);
	const uint64_t first = s2.s.hint_until_us;
	const uint64_t t1 = s2.now;
	s2.in.front_count = 0;
	sim_run(&s2, 28900000, 100000);
	s2.in.new_contacts = 1;
	s2.in.front_count = 1;
	sim_frame(&s2, 100000);
	CHECK(s2.now == t1 + 29000000);
	CHECK(s2.s.hint_until_us == first);

	// A finger that stays down keeps the quiet timer from running out
	Sim s3;
	sim_init(&s3, 1000000);
	s3.in.new_contacts = 1;
	s3.in.front_count = 1;
	sim_frame(&s3, 100000);
	const uint64_t first3 = s3.s.hint_until_us;
	sim_run(&s3, 40000000, 100000);
	s3.in.new_contacts = 1;
	sim_frame(&s3, 100000);
	CHECK(s3.s.hint_until_us == first3);
}

static void test_stats_cycle(void){
	Sim sim;
	sim_init(&sim, 1000000);
	sim_run(&sim, 500000, 100000);
	sim.in.stats_tap = 1;
	sim_frame(&sim, 100000);
	CHECK(sim.s.stats == STATS_SUMMARY);
	CHECK(sim.s.stats_until_us == sim.now + UI_STATS_SHOW_US);
	CHECK(sim.s.stats_drawn == STATS_SUMMARY);
	const uint64_t tap = sim.now;
	sim_run(&sim, 1000000, 100000);
	CHECK(sim.s.stats_level == UI_LEVEL_MAX);
	while (sim.now + 100000 < tap + UI_STATS_SHOW_US) sim_frame(&sim, 100000);
	CHECK(sim.s.stats == STATS_SUMMARY);
	sim_frame(&sim, 100000);
	CHECK(sim.now == tap + UI_STATS_SHOW_US);
	CHECK(sim.s.stats == STATS_HIDDEN);
	CHECK(sim.s.stats_drawn == STATS_SUMMARY);       // kept while it fades
	CHECK(sim.s.stats_level < UI_LEVEL_MAX);
	sim_run(&sim, UI_FADE_OUT_US, 100000);
	CHECK(sim.s.stats_level == 0);

	// Tap, tap: diagnostic view; the mirror and body targets drop to 0
	sim.in.stats_tap = 1;
	sim_frame(&sim, 100000);
	sim.in.stats_tap = 1;
	sim_frame(&sim, 100000);
	CHECK(sim.s.stats == STATS_DIAG);
	CHECK(sim.s.stats_until_us == sim.now + UI_DIAG_SHOW_US);
	CHECK(sim.s.stats_drawn == STATS_DIAG);
	sim_run(&sim, UI_FADE_OUT_US + 100000, 100000);
	CHECK(sim.s.body == 0 && sim.s.text == 0 && sim.s.lit == 0 && sim.s.hint == 0);
	CHECK(sim.s.stats_level == UI_LEVEL_MAX);
	// 60 s after entering: hidden
	const uint64_t entered = sim.s.stats_until_us - UI_DIAG_SHOW_US;
	while (sim.now + 100000 < entered + UI_DIAG_SHOW_US) sim_frame(&sim, 100000);
	CHECK(sim.s.stats == STATS_DIAG);
	sim_frame(&sim, 100000);
	CHECK(sim.now == entered + UI_DIAG_SHOW_US);
	CHECK(sim.s.stats == STATS_HIDDEN);
	CHECK(sim.s.stats_drawn == STATS_DIAG);

	// Third tap hides at once and keeps the last view during the fade
	Sim t3;
	sim_init(&t3, 1000000);
	t3.in.stats_tap = 1;
	sim_frame(&t3, 100000);
	t3.in.stats_tap = 1;
	sim_frame(&t3, 100000);
	sim_run(&t3, 500000, 100000);
	CHECK(t3.s.stats == STATS_DIAG);
	t3.in.stats_tap = 1;
	sim_frame(&t3, 100000);
	CHECK(t3.s.stats == STATS_HIDDEN);
	CHECK(t3.s.stats_drawn == STATS_DIAG);
	CHECK(t3.s.stats_level < UI_LEVEL_MAX);
	// With the diagnostic gone the body, text and lit levels come back (the screen is still awake)
	sim_run(&t3, 1000000, 100000);
	CHECK(t3.s.body == UI_LEVEL_MAX && t3.s.text == UI_LEVEL_MAX && t3.s.lit == UI_LEVEL_MAX);

	// A tap while the summary is up goes to the diagnostic view, not hidden
	Sim t2;
	sim_init(&t2, 1000000);
	t2.in.stats_tap = 1;
	sim_frame(&t2, 100000);
	t2.in.stats_tap = 1;
	sim_frame(&t2, 100000);
	CHECK(t2.s.stats == STATS_DIAG);
}

static void test_pause(void){
	Sim sim;
	sim_init(&sim, 1000000);
	sim_run(&sim, 80000, 10000);
	const uint16_t body = sim.s.body;
	CHECK(body > 0 && body < UI_LEVEL_MAX);
	const uint64_t act = sim.s.last_activity_us;

	// Remap menu open: levels, mode and the activity timestamp freeze, even over a 10 s gap
	sim.in.remap_open = 1;
	sim.in.wifi_link = 1;
	sim_frame(&sim, 10000000);
	CHECK(sim.s.paused == 1);
	CHECK(sim.s.body == body && sim.s.mode == SCREEN_SETUP && sim.s.last_activity_us == act);
	sim_run(&sim, 1000000, 100000);
	CHECK(sim.s.body == body && sim.s.mode == SCREEN_SETUP);
	// Closing it: the first unpaused frame counts as activity, and the 10 s of pause is not a time step
	sim.in.remap_open = 0;
	sim_frame(&sim, 10000);
	CHECK(sim.s.paused == 0);
	CHECK(sim.s.last_activity_us == sim.now);
	CHECK(sim.s.mode == SCREEN_PLAY);

	// Screen off pauses the same way
	Sim off;
	sim_init(&off, 1000000);
	sim_run(&off, 80000, 10000);
	const uint16_t b2 = off.s.body;
	off.in.screen_off = 1;
	sim_run(&off, 500000, 100000);
	CHECK(off.s.body == b2 && off.s.paused == 1);
	off.in.screen_off = 0;
	sim_frame(&off, 10000);
	CHECK(off.s.last_activity_us == off.now);

	// A clock going backwards gives step 0
	Sim back;
	sim_init(&back, 5000000);
	sim_run(&back, 50000, 10000);
	const uint16_t b3 = back.s.body;
	back.in.now_us = back.now - 1000000;
	screen_update(&back.s, &back.in);
	CHECK(back.s.body == b3);
	CHECK(back.s.last_us == back.now - 1000000);

	// A 10 s step moves a level by at most one UI_MAX_STEP_US worth
	Sim big;
	sim_init(&big, 1000000);
	big.in.now_us = big.now + 10000000;
	screen_update(&big.s, &big.in);
	const int max_up = (UI_MAX_STEP_US * UI_LEVEL_MAX + UI_FADE_IN_US - 1) / UI_FADE_IN_US;
	CHECK(big.s.body == max_up);
	CHECK(big.s.lit == max_up);
}

// ---------------------------------------------------------------- stats

static void test_frametime(void){
	FrameTime f;
	frametime_init(&f, 0);
	uint64_t t = 0;
	for (int i = 0; i < 59; i++){
		frametime_rendered(&f, t + 5000);
		t += 16667;
		frametime_swapped(&f, t);
	}
	CHECK(!f.valid);                       // 983 ms: not a full window yet
	frametime_rendered(&f, t + 5000);
	t += 16667;
	frametime_swapped(&f, t);
	CHECK(f.valid);
	CHECK(f.avg_period_us == 16667 && f.peak_period_us == 16667);
	CHECK(f.late_frames == 0);
	CHECK(f.avg_work_us == 5000 && f.peak_work_us == 5000);
	char text[32];
	snprintf(text, sizeof(text), "%.1f", f.avg_period_us / 1000.0f);
	CHECK_STR(text, "16.7");
	CHECK(f.frames == 0);                  // the next window starts empty

	// One long period in the next window
	frametime_rendered(&f, t + 20000);
	t += 33333;
	frametime_swapped(&f, t);
	uint64_t ws = f.window_start_us;
	while (f.window_start_us == ws){
		frametime_rendered(&f, t + 5000);
		t += 16667;
		frametime_swapped(&f, t);
	}
	CHECK(f.late_frames == 1);
	CHECK(f.peak_period_us == 33333);
	CHECK(f.peak_work_us == 20000);
	snprintf(text, sizeof(text), "%.1f", f.peak_period_us / 1000.0f);
	CHECK_STR(text, "33.3");

	// A frame exactly at FRAME_LATE_US is not late
	FrameTime g;
	frametime_init(&g, 0);
	t = 0;
	while (!g.valid){
		t += FRAME_LATE_US;
		frametime_swapped(&g, t);
	}
	CHECK(g.late_frames == 0);
}

static StatsInput base_stats(void){
	StatsInput in;
	memset(&in, 0, sizeof(in));
	in.fast_ok = 1;
	in.fast_enabled = 1;
	in.fast.available = 1;
	in.fast.running = 1;
	in.fast.learned = 12;
	in.fast.avg_us = 3500;
	in.fast.fallbacks = 2;
	in.battery = 100;
	in.zones_owned = 1;
	in.zones_count = 5;
	in.rear_owned = 1;
	in.rear_count = 4;
	in.mirror_enabled = 1;
	in.art_loaded = 1;
	return in;
}

static void test_summary(void){
	char L[STATS_SUMMARY_LINES][STATS_LINE_MAX];
	FrameTime ft;
	memset(&ft, 0, sizeof(ft));
	StatsInput in = base_stats();

	// Line 1
	in.rates.streamed = 300;
	in.rates.polls = 111;
	in.rates.fresh = 55;
	in.wifi_link = 1;
	CHECK(stats_summary(&in, &ft, L) == STATS_SUMMARY_LINES);
	CHECK_STR(L[0], "Stream: 300 pkt/s");
	in.rates.streamed = 0;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[0], "Polls: 111/s, 55 with new data");
	in.wifi_link = 0;
	in.usb_link = 1;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[0], "USB: connected");
	in.usb_link = 0;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[0], "PC: not connected");

	// Line 2
	in.fast_ok = 0;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[1], "Fast buttons: unavailable");
	in.fast_ok = 1;
	in.fast_enabled = 0;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[1], "Fast buttons: off");
	in.fast_enabled = 1;
	in.fast.available = 0;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[1], "Fast buttons: unavailable");
	in.fast.available = 1;
	in.fast.running = 0;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[1], "Fast buttons: starting");
	in.fast.running = 1;
	in.fast.learned = 11;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[1], "Fast buttons: starting");
	in.fast.learned = 12;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[1], "Fast buttons: 3.5 ms sooner, 2 fallbacks");

	// Line 3
	stats_summary(&in, &ft, L);
	CHECK_STR(L[2], "Frame: measuring");
	ft.valid = 1;
	ft.avg_period_us = 16667;
	ft.peak_period_us = 33333;
	ft.late_frames = 1;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[2], "Frame: 16.7 ms, max 33.3, 1 late");

	// Line 4
	CHECK_STR(L[3], "Battery: 100%");
	in.battery = 7;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[3], "Battery: 7%");
	in.battery = 0;
	stats_summary(&in, &ft, L);
	CHECK_STR(L[3], "Battery: 0%");
}

static void test_diag(void){
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
	char L[STATS_DIAG_LINES][STATS_LINE_MAX];
	FrameTime ft;
	memset(&ft, 0, sizeof(ft));
	StatsInput in = base_stats();
	in.rates.ctrl = 60;
	in.rates.touch = 59;
	in.rates.motion = 120;
	in.rates.streamed = 250;
	in.fast.matched = 480;
	in.fast.reads_per_s = 240;
	in.fast.read_us = 55;
	in.fast.read_max_us = 130;
	CHECK(stats_diag(&in, &ft, L) == 19);
	CHECK(STATS_DIAG_LINES == 19);
	for (int i = 0; i < 9; i++) CHECK_STR(L[10 + i], credits[i]);
	for (int i = 0; i < STATS_DIAG_LINES; i++) CHECK(strlen(L[i]) < STATS_LINE_MAX);
	CHECK_STR(L[0], "VitaPad NG 2.0 diagnostics - tap the centre to close");
	CHECK_STR(L[1], "Input updates/s: buttons & sticks 60, touch 59, motion 120");
	CHECK_STR(L[2], "Streaming to the PC: 250 packets/s (UDP)");
	CHECK_STR(L[3], "Fast buttons: 3.5 ms sooner (480 changes), 240 reads/s of 55 us (max 130), 2 fallbacks");
	CHECK_STR(L[4], "Frame: measuring");
	CHECK_STR(L[5], "Battery: 100%");
	CHECK_STR(L[6], "Front touch: 5 zones from built-in defaults");
	CHECK_STR(L[7], "Rear touch: 4 zones from built-in defaults");
	CHECK_STR(L[8], "Mirror: on");
	CHECK_STR(L[9], "");

	in.rates.streamed = 0;
	in.wifi_link = 1;
	in.rates.polls = 200;
	in.rates.fresh = 50;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[2], "PC polls/s: 200 (50 with new button & stick data)");
	in.wifi_link = 0;
	in.usb_link = 1;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[2], "USB: connected");
	in.usb_link = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[2], "PC: not connected");

	in.fast_ok = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[3], "Fast buttons: need Unsafe Homebrew");
	in.fast_ok = 1;
	in.fast_enabled = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[3], "Fast buttons: off (change it in the remap menu)");
	in.fast_enabled = 1;
	in.fast.available = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[3], "Fast buttons: unavailable on this firmware");
	in.fast.available = 1;
	in.fast.running = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[3], "Fast buttons: starting...");
	in.fast.running = 1;

	ft.valid = 1;
	ft.avg_period_us = 16667;
	ft.peak_period_us = 33333;
	ft.avg_work_us = 5000;
	ft.peak_work_us = 8000;
	ft.late_frames = 1;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[4], "Frame: 16.67 ms avg, 33.33 ms max, work 5.00 ms avg, 8.00 ms max, 1 late");

	in.zones_source = 1;
	in.zones_count = 7;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[6], "Front touch: 7 zones from zones.txt");
	in.zones_source = -1;
	in.zones_count = 5;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[6], "Front touch: 5 zones from built-in defaults (zones.txt is invalid)");
	in.zones_owned = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[6], "Front touch: forwarded to the PC (FrontTouch=Forward)");
	in.rear_source = 1;
	in.rear_count = 3;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[7], "Rear touch: 3 zones from rear-zones.txt");
	in.rear_source = -1;
	in.rear_count = 4;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[7], "Rear touch: 4 zones from built-in defaults (rear-zones.txt is invalid)");
	in.rear_owned = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[7], "Rear touch: forwarded to the PC (RearTouch=Forward)");
	CHECK_STR(L[6], "Front touch: forwarded to the PC (FrontTouch=Forward)");   // independent of the front line
	in.rear_owned = 1;
	in.rear_source = 0;
	in.mirror_enabled = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[8], "Mirror: off (Mirror=Off)");
	in.art_loaded = 0;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[8], "Mirror: off (Mirror=Off) - controller art missing");
	in.mirror_enabled = 1;
	stats_diag(&in, &ft, L);
	CHECK_STR(L[8], "Mirror: on - controller art missing");

	// Even the worst values fit
	in.rates.ctrl = in.rates.touch = in.rates.motion = 2000000000;
	in.rates.streamed = 2000000000;
	in.fast.matched = in.fast.reads_per_s = in.fast.read_us = in.fast.read_max_us = in.fast.fallbacks = -2000000000;
	in.fast.avg_us = -2000000000;
	stats_diag(&in, &ft, L);
	for (int i = 0; i < STATS_DIAG_LINES; i++) CHECK(strlen(L[i]) < STATS_LINE_MAX);
}

// ---------------------------------------------------------------- sprites and plan

static void test_sprites(void){
	static const char *const names[SPR_COUNT] = {
		"spr-body", "spr-stick-l-cap", "spr-stick-r-cap", "spr-lit-l2", "spr-lit-r2", "spr-lit-l1", "spr-lit-r1",
		"spr-lit-share", "spr-lit-options", "spr-lit-dpad-up", "spr-lit-dpad-right", "spr-lit-dpad-down", "spr-lit-dpad-left",
		"spr-lit-triangle", "spr-lit-circle", "spr-lit-cross", "spr-lit-square", "spr-lit-ps",
		"spr-lit-stick-l-ring", "spr-lit-stick-r-ring", "spr-lit-stick-l-cap", "spr-lit-stick-r-cap",
	};
	CHECK(SPR_COUNT == 22);
	CHECK(SPR_BODY == 0 && SPR_STICK_L_CAP == 1 && SPR_LIT_L2 == 3 && SPR_LIT_PS == 17 && SPR_LIT_STICK_R_CAP == 21);
	// The planner relies on the parts being consecutive, in the order of the enum
	CHECK(SPR_LIT_PS - SPR_LIT_L2 == PART_PS - PART_L2);
	CHECK(SPR_LIT_DPAD_UP - SPR_LIT_L2 == PART_DPAD_UP - PART_L2);
	CHECK(SPR_LIT_SQUARE - SPR_LIT_L2 == PART_SQUARE - PART_L2);
	CHECK(SPR_LIT_STICK_R_RING == SPR_LIT_STICK_L_RING + 1 && SPR_LIT_STICK_R_CAP == SPR_LIT_STICK_L_CAP + 1);
	CHECK(SPRITES_CANVAS_W == 960 && SPRITES_CANVAS_H == 544);

	for (int i = 0; i < SPR_COUNT; i++){
		CHECK_STR(sprite_names[i], names[i]);
		const SpriteRect *r = &sprite_rects[i];
		CHECK(r->w > 0 && r->h > 0);
		CHECK(r->ax + r->w <= SPRITES_ATLAS_W && r->ay + r->h <= SPRITES_ATLAS_H);
		CHECK(r->ax >= SPRITES_PAD && r->ay >= SPRITES_PAD);
		CHECK(r->sx >= 0 && r->sy >= 0 && r->sx + r->w <= SPRITES_CANVAS_W && r->sy + r->h <= SPRITES_CANVAS_H);
		for (int j = i + 1; j < SPR_COUNT; j++){
			const SpriteRect *q = &sprite_rects[j];
			int overlap_x = r->ax - SPRITES_PAD < q->ax + q->w + SPRITES_PAD && q->ax - SPRITES_PAD < r->ax + r->w + SPRITES_PAD;
			int overlap_y = r->ay - SPRITES_PAD < q->ay + q->h + SPRITES_PAD && q->ay - SPRITES_PAD < r->ay + r->h + SPRITES_PAD;
			CHECK(!(overlap_x && overlap_y));
		}
	}
	// Base and lit caps share a centre within 1 px (compared in half pixels)
	const SpriteRect *bl = &sprite_rects[SPR_STICK_L_CAP], *ll = &sprite_rects[SPR_LIT_STICK_L_CAP];
	const SpriteRect *br = &sprite_rects[SPR_STICK_R_CAP], *lr = &sprite_rects[SPR_LIT_STICK_R_CAP];
	CHECK(abs((2 * bl->sx + bl->w) - (2 * ll->sx + ll->w)) <= 2);
	CHECK(abs((2 * bl->sy + bl->h) - (2 * ll->sy + ll->h)) <= 2);
	CHECK(abs((2 * br->sx + br->w) - (2 * lr->sx + lr->w)) <= 2);
	CHECK(abs((2 * br->sy + br->h) - (2 * lr->sy + lr->h)) <= 2);
}

typedef struct {
	Screen s;
	MirrorState m;
	ZoneTable z, rz;
	SetupInfo setup;
	StatsInput st;
	FrameTime ft;
	UiModel model;
} Fx;

static DrawList dl;

static void fx_init(Fx *f){
	memset(f, 0, sizeof(*f));
	screen_init(&f->s, 0);
	zones_default(&f->z);
	zones_default_rear(&f->rz);
	f->setup.has_ip = 1;
	snprintf(f->setup.ip, sizeof(f->setup.ip), "192.168.1.42");
	f->setup.port = 5000;
	f->setup.connection = CONNECTION_WIFI;
	f->setup.usb_state = USB_STATE_OFF;
	f->setup.ps_available = 1;
	f->setup.ps_mode = PS_MODE_DOUBLE_TAP;
	f->st = base_stats();
	f->ft.valid = 1;
	f->ft.avg_period_us = 16667;
	f->ft.peak_period_us = 16700;
	f->model.screen = &f->s;
	f->model.mirror = &f->m;
	f->model.zones = &f->z;
	f->model.rear_zones = &f->rz;   // rear_owned stays 0: the rear tests switch it on
	f->model.setup = &f->setup;
	f->model.stats = &f->st;
	f->model.frame = &f->ft;
	f->model.mirror_enabled = 1;
	f->model.art_loaded = 1;
}

static void fx_levels(Fx *f, int mode, int body, int lit, int hint, int stats_level, int stats_drawn){
	f->s.mode = mode;
	f->s.body = (uint16_t)body;
	f->s.text = (uint16_t)(mode == SCREEN_SETUP ? body : 0);   // the SETUP text has its own level; tests that want another set it after
	f->s.lit = (uint16_t)lit;
	f->s.hint = (uint16_t)hint;
	f->s.stats_level = (uint16_t)stats_level;
	f->s.stats_drawn = stats_drawn;
	f->s.stats = stats_level > 0 ? stats_drawn : STATS_HIDDEN;
}

static void fx_all_pressed(Fx *f){
	PadSnapshot snap = { PAD_MIRROR_MASK, 200, 60, 20, 240 };
	mirror_compute(&snap, MIRROR_DPAD_USB_HAT, &f->m);
	// Every part at once, which no real D-pad resolution produces: the planner must draw them all
	f->m.lit = (1u << PART_COUNT) - 1;
}

static int alpha_of(const DrawItem *it){ return (int)(it->color >> 24) & 0xFF; }

// Bounds-safe access: an index past the list gives an all-zero item (callers CHECK the count too)
static const DrawItem *item_at(const DrawList *l, int i){
	static const DrawItem none;
	return i >= 0 && i < l->count ? &l->items[i] : &none;
}

static int count_kind(const DrawList *l, int kind){
	int n = 0;
	for (int i = 0; i < l->count; i++) n += l->items[i].kind == kind;
	return n;
}

static void test_draw_order(void){
	Fx f;
	fx_init(&f);
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 1000, 500, STATS_SUMMARY);
	fx_all_pressed(&f);
	// Lit parts are L1... every enum part plus both stick extras
	f.m.lit |= 1u << PART_DPAD_DOWN;
	f.m.lcap_deflected = f.m.rcap_deflected = 1;
	uiplan_build(&f.model, &dl);

	// 1-3: sprites in enum order: body, caps, lit parts in PART order, rings, lit caps
	int idx = 0;
	int expect_ids[SPR_COUNT];
	int n = 0;
	expect_ids[n++] = SPR_BODY;
	expect_ids[n++] = SPR_STICK_L_CAP;
	expect_ids[n++] = SPR_STICK_R_CAP;
	for (int part = PART_L2; part <= PART_PS; part++) expect_ids[n++] = SPR_LIT_L2 + part;
	expect_ids[n++] = SPR_LIT_STICK_L_RING;
	expect_ids[n++] = SPR_LIT_STICK_R_RING;
	expect_ids[n++] = SPR_LIT_STICK_L_CAP;
	expect_ids[n++] = SPR_LIT_STICK_R_CAP;
	CHECK(n == SPR_COUNT);
	for (; idx < n && idx < dl.count; idx++){
		CHECK(item_at(&dl, idx)->kind == DRAW_SPRITE);
		CHECK(item_at(&dl, idx)->sprite == expect_ids[idx]);
	}
	CHECK(count_kind(&dl, DRAW_SPRITE) == SPR_COUNT);
	CHECK(dl.count > n);
	CHECK(dl.count == n + 8 + 4 * 4 + UI_HINT_ELLIPSE_SEGMENTS + 5 + STATS_SUMMARY_LINES);   // every index below is in range

	// 4: setup text, 5: hint, 6: summary: kinds in order text, then lines, then labels, then summary text
	int i = n;
	CHECK(item_at(&dl, i)->kind == DRAW_TEXT && item_at(&dl, i)->y == 22);        // title first
	int last_setup = -1;
	for (int k = n; k < dl.count; k++) if (item_at(&dl, k)->kind == DRAW_TEXT && item_at(&dl, k)->y == 476) last_setup = k; // info line 5
	CHECK(last_setup > 0);
	for (int k = n; k <= last_setup; k++) CHECK(item_at(&dl, k)->kind == DRAW_TEXT);
	int first_line = last_setup + 1;
	CHECK(item_at(&dl, first_line)->kind == DRAW_LINE);
	int last_line = first_line;
	while (last_line + 1 < dl.count && item_at(&dl, last_line + 1)->kind == DRAW_LINE) last_line++;
	CHECK(last_line - first_line + 1 == 4 * 4 + UI_HINT_ELLIPSE_SEGMENTS);
	// Labels: L1 R1 L3 R3 stats follow the lines in table order (stats zone is first in the table)
	static const char *const labels[5] = { "stats", "L1", "R1", "L3", "R3" };
	for (int k = 0; k < 5; k++){
		const DrawItem *it = item_at(&dl, last_line + 1 + k);
		CHECK(it->kind == DRAW_TEXT);
		CHECK_STR(it->text, labels[k]);
		CHECK(it->align == ALIGN_CENTER && it->scale > 0.89f && it->scale < 0.91f);
	}
	int sum = last_line + 6;
	CHECK(dl.count - sum == STATS_SUMMARY_LINES);
	CHECK(item_at(&dl, sum)->kind == DRAW_TEXT && item_at(&dl, sum)->y == 388);
	CHECK(item_at(&dl, dl.count - 1)->y == 454);
	// The info block faded to half with the summary at half: body 1000 x (1000 - 500) / 1000 = 500
	CHECK(alpha_of(item_at(&dl, last_setup)) == 500 * 255 / 1000);
	CHECK(alpha_of(item_at(&dl, n)) == 255);                                // title keeps the body level

	// PLAY at rest: the dimmed body and the two base caps, nothing else
	Fx r;
	fx_init(&r);
	fx_levels(&r, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 0, 0, STATS_SUMMARY);
	uiplan_build(&r.model, &dl);
	CHECK(dl.count == 3);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 3);
	CHECK(item_at(&dl, 0)->sprite == SPR_BODY && item_at(&dl, 1)->sprite == SPR_STICK_L_CAP && item_at(&dl, 2)->sprite == SPR_STICK_R_CAP);

	// Mirror=Off: PLAY emits no sprites even with parts lit (hint lines still show); SETUP still lights them
	Fx o;
	fx_init(&o);
	fx_levels(&o, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 1000, 0, STATS_SUMMARY);
	fx_all_pressed(&o);
	o.model.mirror_enabled = 0;
	uiplan_build(&o.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 0);
	CHECK(count_kind(&dl, DRAW_LINE) > 0);
	fx_levels(&o, SCREEN_SETUP, 1000, 1000, 0, 0, STATS_SUMMARY);
	uiplan_build(&o.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) > 3);
	// The body fading down after SETUP to PLAY draws only with Mirror on
	fx_levels(&o, SCREEN_PLAY, 800, 0, 0, 0, STATS_SUMMARY);   // lit 0: only the body shows
	uiplan_build(&o.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 0);
	o.model.mirror_enabled = 1;
	uiplan_build(&o.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 3);

	// No atlas: no sprites, the text stays
	Fx a;
	fx_init(&a);
	fx_levels(&a, SCREEN_SETUP, 1000, 1000, 1000, 0, STATS_SUMMARY);
	fx_all_pressed(&a);
	a.model.art_loaded = 0;
	uiplan_build(&a.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 0);
	CHECK(count_kind(&dl, DRAW_TEXT) > 5);
	CHECK(count_kind(&dl, DRAW_LINE) > 0);

	// A deflected, unclicked stick shows only its cap, at the rest position plus the offset
	Fx d;
	fx_init(&d);
	fx_levels(&d, SCREEN_PLAY, 0, 1000, 0, 0, STATS_SUMMARY);   // body 0: lit parts only
	PadSnapshot snap = { 0, 255, 0, 128, 128 };
	mirror_compute(&snap, MIRROR_DPAD_DS4_CLIENT, &d.m);
	uiplan_build(&d.model, &dl);
	CHECK(dl.count == 1 && item_at(&dl, 0)->sprite == SPR_LIT_STICK_L_CAP);
	CHECK(item_at(&dl, 0)->x == sprite_rects[SPR_LIT_STICK_L_CAP].sx + 12);
	CHECK(item_at(&dl, 0)->y == sprite_rects[SPR_LIT_STICK_L_CAP].sy - 12);
	// Clicked: ring first, then the cap, at full alpha
	snap.buttons = PAD_L3;
	mirror_compute(&snap, MIRROR_DPAD_DS4_CLIENT, &d.m);
	uiplan_build(&d.model, &dl);
	CHECK(dl.count == 2);
	CHECK(item_at(&dl, 0)->sprite == SPR_LIT_STICK_L_RING && item_at(&dl, 1)->sprite == SPR_LIT_STICK_L_CAP);
	CHECK(item_at(&dl, 0)->x == sprite_rects[SPR_LIT_STICK_L_RING].sx);   // the ring doesn't move
	CHECK(alpha_of(item_at(&dl, 1)) == 255);
	// SETUP: the base cap follows the stick too
	fx_levels(&d, SCREEN_SETUP, 1000, 1000, 0, 0, STATS_SUMMARY);
	uiplan_build(&d.model, &dl);
	CHECK(item_at(&dl, 1)->sprite == SPR_STICK_L_CAP && item_at(&dl, 1)->x == sprite_rects[SPR_STICK_L_CAP].sx + 12);
	CHECK(item_at(&dl, 2)->sprite == SPR_STICK_R_CAP && item_at(&dl, 2)->x == sprite_rects[SPR_STICK_R_CAP].sx);

	// Diagnostic: only text, in the 19-line layout, and nothing else while it is visible
	Fx g;
	fx_init(&g);
	fx_levels(&g, SCREEN_SETUP, 1000, 1000, 1000, 1000, STATS_DIAG);
	fx_all_pressed(&g);
	uiplan_build(&g.model, &dl);
	CHECK(count_kind(&dl, DRAW_TEXT) == dl.count);
	CHECK(dl.count == STATS_DIAG_LINES - 1);                   // line 9 is empty and not emitted
	CHECK(item_at(&dl, 0)->x == 16 && item_at(&dl, 0)->y == 28 && item_at(&dl, 0)->align == ALIGN_LEFT);
	CHECK(item_at(&dl, dl.count - 1)->y == 28 + 21 * 18);
	// While it fades out nothing else is drawn; at level 0 the mirror is back
	fx_levels(&g, SCREEN_SETUP, 1000, 1000, 0, 1, STATS_DIAG);
	uiplan_build(&g.model, &dl);
	CHECK(dl.count == 0);                                       // alpha 1 x 255 / 1000 = 0
	fx_levels(&g, SCREEN_SETUP, 1000, 1000, 0, 0, STATS_DIAG);
	uiplan_build(&g.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) > 0);
}

static int plan_cost(const Fx *f){
	uiplan_build(&f->model, &dl);
	CHECK(dl.count <= UI_DRAW_MAX);
	return uiplan_cost(&dl);
}

static void test_budget(void){
	Fx f;

	fx_init(&f);
	fx_levels(&f, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 0, 0, STATS_SUMMARY);
	CHECK(plan_cost(&f) == 3);                       // body and the two base caps

	fx_init(&f);
	fx_levels(&f, SCREEN_PLAY, 0, 1000, 0, 0, STATS_SUMMARY);
	CHECK(plan_cost(&f) == 0);                       // body faded out (idle): nothing

	fx_init(&f);
	fx_levels(&f, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 0, 0, STATS_SUMMARY);
	fx_all_pressed(&f);
	f.m.lcap_deflected = f.m.rcap_deflected = 1;
	int c = plan_cost(&f);
	CHECK(c <= 19 + 3 && c > 3);

	fx_levels(&f, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 1000, 0, STATS_SUMMARY);
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET);
	CHECK(c <= 72 + 2 + 3);

	fx_levels(&f, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 0, 1000, STATS_SUMMARY);
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET);
	CHECK(c <= 179 + 3);

	fx_init(&f);
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 0, 0, STATS_SUMMARY);
	c = plan_cost(&f);
	CHECK(c <= 307 && c <= UI_DRAW_BUDGET);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 3);

	// SETUP worst: everything pressed, hint, summary half faded (both text blocks visible), USB text
	fx_init(&f);
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 1000, 500, STATS_SUMMARY);
	fx_all_pressed(&f);
	f.m.lit |= 1u << PART_DPAD_DOWN;
	f.m.lcap_deflected = f.m.rcap_deflected = 1;
	f.setup.connection = CONNECTION_USB_GENERIC;
	f.setup.usb_state = USB_STATE_WAITING;
	f.setup.remap_count = 12;
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET);
	CHECK(count_kind(&dl, DRAW_SPRITE) == SPR_COUNT);

	// Diagnostic
	fx_init(&f);
	fx_levels(&f, SCREEN_SETUP, 0, 0, 0, 1000, STATS_DIAG);
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET_DIAG);
	CHECK(c > 0);

	// 16 ellipses in zones.txt: the hint alone is 16 x 24 lines + 16 labels, within the diagnostic budget
	static char big[2048];
	big[0] = 0;
	for (int i = 0; i < 16; i++){
		char line[64];
		snprintf(line, sizeof(line), "%s ellipse %d %d 200 100\n", i % 2 ? "L1" : "Stats", 200 + 100 * (i % 8), 200 + 300 * (i / 8));
		strcat(big, line);
	}
	fx_init(&f);
	CHECK(zones_parse(big, &f.z) == 16);
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 1000, 500, STATS_SUMMARY);
	fx_all_pressed(&f);
	f.m.lit |= 1u << PART_DPAD_DOWN;
	f.m.lcap_deflected = f.m.rcap_deflected = 1;
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET_DIAG);
	CHECK(count_kind(&dl, DRAW_LINE) == 16 * UI_HINT_ELLIPSE_SEGMENTS);
	CHECK(dl.count <= UI_DRAW_MAX);
	fx_levels(&f, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 1000, 0, STATS_SUMMARY);
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET_DIAG);
	CHECK(count_kind(&dl, DRAW_TEXT) == 16);

	// Sixteen rectangles
	big[0] = 0;
	for (int i = 0; i < 16; i++){
		char line[64];
		snprintf(line, sizeof(line), "R3 rect %d 0 %d 100\n", 100 * i, 100 * i + 50);
		strcat(big, line);
	}
	CHECK(zones_parse(big, &f.z) == 16);
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET && count_kind(&dl, DRAW_LINE) == 64);

	// uiplan_cost counts sprites, lines and non-space characters
	DrawList small;
	memset(&small, 0, sizeof(small));
	small.count = 3;
	small.items[0].kind = DRAW_SPRITE;
	small.items[1].kind = DRAW_LINE;
	small.items[2].kind = DRAW_TEXT;
	snprintf(small.items[2].text, UI_TEXT_MAX, "ab c");
	CHECK(uiplan_cost(&small) == 5);
}

static void test_alpha(void){
	Fx f;
	fx_init(&f);
	fx_all_pressed(&f);
	f.m.lcap_deflected = f.m.rcap_deflected = 1;

	// Layer by layer: body 500 -> 127, text 400 -> 102, lit 300 -> 76, hint 250 -> 63, stats 800 -> 204
	fx_levels(&f, SCREEN_SETUP, 500, 300, 250, 800, STATS_SUMMARY);
	f.s.text = 400;                                   // the SETUP text has its own layer: 400 -> 102
	uiplan_build(&f.model, &dl);
	int saw_body = 0, saw_lit = 0, saw_soft = 0, saw_line = 0, saw_title = 0, saw_summary = 0, saw_info = 0;
	for (int i = 0; i < dl.count; i++){
		const DrawItem *it = &dl.items[i];
		CHECK(alpha_of(it) > 0);                       // nothing with alpha 0
		if (it->kind == DRAW_SPRITE){
			CHECK((it->color & 0x00FFFFFF) == 0x00FFFFFF);
			if (it->sprite <= SPR_STICK_R_CAP){ CHECK(alpha_of(it) == 500 * 255 / 1000); saw_body = 1; }
			else if (it->sprite >= SPR_LIT_STICK_L_CAP){
				// All-pressed means both sticks clicked: full lit alpha
				CHECK(alpha_of(it) == 300 * 255 / 1000);
				saw_lit = 1;
			}else{ CHECK(alpha_of(it) == 300 * 255 / 1000); saw_lit = 1; }
		}else if (it->kind == DRAW_LINE){
			CHECK(alpha_of(it) == 250 * 255 / 1000);
			saw_line = 1;
		}else if (it->y == 22){
			CHECK(alpha_of(it) == 400 * 255 / 1000);
			saw_title = 1;
		}else if (it->y == 388 && alpha_of(it) == 800 * 255 / 1000){
			saw_summary = 1;
		}else if (it->y == 388 || it->y == 476){
			// info block: text x (1000 - stats) / 1000 = 80
			CHECK(alpha_of(it) == 80 * 255 / 1000);
			saw_info = 1;
		}
	}
	CHECK(saw_body && saw_lit && saw_line && saw_title && saw_summary && saw_info);
	(void)saw_soft;

	// Deflected, not clicked: alpha x 160 / 255 (on top of the level)
	Fx d;
	fx_init(&d);
	PadSnapshot snap = { 0, 255, 128, 128, 255 };
	mirror_compute(&snap, MIRROR_DPAD_DS4_CLIENT, &d.m);
	fx_levels(&d, SCREEN_PLAY, 0, 1000, 0, 0, STATS_SUMMARY);
	uiplan_build(&d.model, &dl);
	CHECK(dl.count == 2);
	CHECK(alpha_of(item_at(&dl, 0)) == UI_STICK_DEFLECT_ALPHA);
	fx_levels(&d, SCREEN_PLAY, 0, 500, 0, 0, STATS_SUMMARY);
	uiplan_build(&d.model, &dl);
	CHECK(dl.count == 2);
	CHECK(alpha_of(item_at(&dl, 0)) == (500 * 255 / 1000) * UI_STICK_DEFLECT_ALPHA / 255);

	// Levels whose alpha rounds to 0 omit the layer
	Fx z;
	fx_init(&z);
	fx_all_pressed(&z);
	fx_levels(&z, SCREEN_SETUP, 3, 3, 3, 3, STATS_SUMMARY);
	uiplan_build(&z.model, &dl);
	CHECK(dl.count == 0);
	fx_levels(&z, SCREEN_SETUP, 4, 0, 0, 0, STATS_SUMMARY);   // 4 x 255 / 1000 = 1
	uiplan_build(&z.model, &dl);
	CHECK(dl.count > 0);
	for (int i = 0; i < dl.count; i++) CHECK(alpha_of(&dl.items[i]) == 1);
	fx_levels(&z, SCREEN_SETUP, 1000, 0, 1000, 0, STATS_SUMMARY);   // lit 0: no lit sprites
	uiplan_build(&z.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 3);
	fx_levels(&z, SCREEN_SETUP, 1000, 1000, 0, 0, STATS_SUMMARY);   // hint 0: no lines
	uiplan_build(&z.model, &dl);
	CHECK(count_kind(&dl, DRAW_LINE) == 0);
	// The summary replaces the info block completely at full stats level
	fx_levels(&z, SCREEN_SETUP, 1000, 1000, 0, 1000, STATS_SUMMARY);
	uiplan_build(&z.model, &dl);
	CHECK(count_kind(&dl, DRAW_TEXT) > 0);
	for (int i = 0; i < dl.count; i++) if (dl.items[i].kind == DRAW_TEXT && dl.items[i].y == 476) CHECK(0);
	// The SETUP text fades with its own level, not the body's: the body is drawn, the text is not
	fx_levels(&z, SCREEN_SETUP, 1000, 1000, 0, 0, STATS_SUMMARY);
	z.s.text = 0;
	uiplan_build(&z.model, &dl);
	CHECK(count_kind(&dl, DRAW_TEXT) == 0);
	// Body alpha follows the body level in PLAY too
	fx_levels(&z, SCREEN_PLAY, 400, 0, 0, 0, STATS_SUMMARY);
	uiplan_build(&z.model, &dl);
	CHECK(dl.count == 3);
	for (int i = 0; i < dl.count; i++) CHECK(alpha_of(item_at(&dl, i)) == 400 * 255 / 1000);
}

// PLAY keeps the controller on screen, dimmed
static void test_play_body(void){
	// a. SETUP to PLAY: the body falls to the dimmed level and stays, the text goes
	Sim a;
	sim_init(&a, 1000000);
	sim_run(&a, 2 * UI_FADE_IN_US, 10000);
	CHECK(a.s.body == UI_LEVEL_MAX && a.s.text == UI_LEVEL_MAX);
	a.in.wifi_link = 1;
	sim_frame(&a, 10000);
	CHECK(a.s.mode == SCREEN_PLAY);
	sim_run(&a, UI_FADE_OUT_US, 10000);
	CHECK(a.s.body == UI_PLAY_BODY_LEVEL);
	CHECK(a.s.text == 0);
	sim_run(&a, 1000000, 10000);
	CHECK(a.s.body == UI_PLAY_BODY_LEVEL && a.s.text == 0);
	CHECK(a.s.mode == SCREEN_PLAY);

	// b. The link drops for longer than the grace period: back to SETUP, body and text fade in
	a.in.wifi_link = 0;
	sim_run(&a, UI_WIFI_LOST_GRACE_US + 100000, 100000);
	CHECK(a.s.mode == SCREEN_SETUP);
	sim_run(&a, UI_FADE_IN_US, 10000);
	CHECK(a.s.body == UI_LEVEL_MAX);
	CHECK(a.s.text == UI_LEVEL_MAX);

	// c. The diagnostic view in PLAY: body and lit go to 0, and the body comes back dimmed
	Sim c;
	sim_init(&c, 1000000);
	c.in.wifi_link = 1;
	sim_run(&c, 2 * UI_FADE_OUT_US, 10000);
	CHECK(c.s.body == UI_PLAY_BODY_LEVEL && c.s.lit == UI_LEVEL_MAX);
	c.in.stats_tap = 1;
	sim_frame(&c, 100000);
	c.in.stats_tap = 1;
	sim_frame(&c, 100000);
	CHECK(c.s.stats == STATS_DIAG);
	sim_run(&c, UI_FADE_OUT_US, 100000);
	CHECK(c.s.body == 0 && c.s.lit == 0 && c.s.text == 0);
	c.in.stats_tap = 1;
	sim_frame(&c, 100000);
	CHECK(c.s.stats == STATS_HIDDEN);
	sim_run(&c, UI_FADE_IN_US + UI_FADE_OUT_US, 10000);
	CHECK(c.s.body == UI_PLAY_BODY_LEVEL);
	CHECK(c.s.lit == UI_LEVEL_MAX);

	// d. Settled PLAY at rest: the three sprites at the dimmed alpha, no text
	Fx f;
	fx_init(&f);
	fx_levels(&f, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, UI_LEVEL_MAX, 0, 0, STATS_SUMMARY);
	uiplan_build(&f.model, &dl);
	CHECK(dl.count == 3);
	CHECK(item_at(&dl, 0)->kind == DRAW_SPRITE && item_at(&dl, 0)->sprite == SPR_BODY);
	CHECK(item_at(&dl, 1)->kind == DRAW_SPRITE && item_at(&dl, 1)->sprite == SPR_STICK_L_CAP);
	CHECK(item_at(&dl, 2)->kind == DRAW_SPRITE && item_at(&dl, 2)->sprite == SPR_STICK_R_CAP);
	for (int i = 0; i < dl.count; i++) CHECK(alpha_of(item_at(&dl, i)) == UI_PLAY_BODY_LEVEL * 255 / 1000);
	CHECK(count_kind(&dl, DRAW_TEXT) == 0);

	// e. Mirror=Off with buttons pressed: no sprite at all in PLAY
	Fx o;
	fx_init(&o);
	fx_levels(&o, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, UI_LEVEL_MAX, 0, 0, STATS_SUMMARY);
	fx_all_pressed(&o);
	o.model.mirror_enabled = 0;
	uiplan_build(&o.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) == 0);

	// f. SETUP with the body up but the text level at 0: sprites, no text
	Fx t;
	fx_init(&t);
	fx_levels(&t, SCREEN_SETUP, UI_LEVEL_MAX, UI_LEVEL_MAX, 0, 0, STATS_SUMMARY);
	t.s.text = 0;
	uiplan_build(&t.model, &dl);
	CHECK(count_kind(&dl, DRAW_SPRITE) >= 3);
	CHECK(count_kind(&dl, DRAW_TEXT) == 0);
}

typedef struct { int x, y, w, h; } Region;

static void check_in_regions(const DrawList *l, int first_text_only_setup){
	static const Region R_TITLE = { 300, 4, 360, 24 };
	static const Region R_CONN = { 364, 86, 232, 104 };
	static const Region R_INFO = { 310, 366, 340, 170 };
	const Region *regions[3] = { &R_TITLE, &R_CONN, &R_INFO };
	(void)first_text_only_setup;
	for (int i = 0; i < l->count; i++){
		const DrawItem *it = &l->items[i];
		if (it->kind != DRAW_TEXT) continue;
		int ok = 0;
		for (int r = 0; r < 3; r++){
			const Region *g = regions[r];
			if (it->y >= g->y && it->y <= g->y + g->h && it->x1 > 0
				&& it->x - it->x1 / 2 >= g->x && it->x + it->x1 / 2 <= g->x + g->w) ok = 1;
		}
		CHECK(ok);
		CHECK(it->align == ALIGN_CENTER && it->x == 480);
	}
}

static const DrawItem *text_at(const DrawList *l, int baseline){
	for (int i = 0; i < l->count; i++) if (l->items[i].kind == DRAW_TEXT && l->items[i].y == baseline) return &l->items[i];
	return NULL;
}

static void test_layout(void){
	Fx f;
	fx_init(&f);
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 0, 0, STATS_SUMMARY);

	// Wi-Fi with an IP
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);
	CHECK(text_at(&dl, 22) && text_at(&dl, 120) && text_at(&dl, 146) && text_at(&dl, 388));
	CHECK(text_at(&dl, 168) == NULL);
	CHECK_STR(text_at(&dl, 22)->text, "VitaPad NG 2.0");
	CHECK_STR(text_at(&dl, 120)->text, "192.168.1.42:5000");
	CHECK_STR(text_at(&dl, 146)->text, "Waiting for the PC client");
	CHECK(text_at(&dl, 120)->scale > 1.19f && text_at(&dl, 120)->scale < 1.21f);
	CHECK(text_at(&dl, 146)->scale > 0.84f && text_at(&dl, 146)->scale < 0.86f);
	CHECK_STR(text_at(&dl, 388)->text, "PS button: double-tap for the LiveArea");
	CHECK_STR(text_at(&dl, 410)->text, "No buttons remapped");
	CHECK_STR(text_at(&dl, 432)->text, "Hold L + R + SELECT 1 s: screen off");
	CHECK_STR(text_at(&dl, 454)->text, "Hold L + R + START 1 s: remap menu");
	CHECK_STR(text_at(&dl, 476)->text, "Tap the centre: stats");
	CHECK(text_at(&dl, 120)->color == (uint32_t)UI_RGBA(0xC3, 0xCA, 0xD6, 255));
	CHECK(text_at(&dl, 146)->color == (uint32_t)UI_RGBA(0x7D, 0x86, 0x96, 255));
	CHECK(text_at(&dl, 22)->color == (uint32_t)UI_RGBA(0x56, 0x60, 0x70, 255));

	// Wi-Fi without IP
	f.setup.has_ip = 0;
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);
	CHECK_STR(text_at(&dl, 120)->text, "No Wi-Fi");
	CHECK_STR(text_at(&dl, 146)->text, "Waiting for a Wi-Fi connection");
	CHECK(text_at(&dl, 168) == NULL);

	// USB, waiting, with and without IP
	f.setup.connection = CONNECTION_USB_DS4;
	f.setup.usb_state = USB_STATE_WAITING;
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);
	CHECK_STR(text_at(&dl, 120)->text, "USB: DualShock 4");
	CHECK_STR(text_at(&dl, 146)->text, "Plug the Vita into a computer");
	CHECK(text_at(&dl, 168) == NULL);
	f.setup.has_ip = 1;
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);
	CHECK_STR(text_at(&dl, 168)->text, "Wi-Fi: 192.168.1.42:5000");
	CHECK(text_at(&dl, 168)->scale > 0.84f && text_at(&dl, 168)->scale < 0.86f);
	f.setup.connection = CONNECTION_USB_GENERIC;
	uiplan_build(&f.model, &dl);
	CHECK_STR(text_at(&dl, 120)->text, "USB: standard gamepad");

	// USB off: starting, or needs Unsafe Homebrew
	f.setup.usb_state = USB_STATE_OFF;
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);
	CHECK_STR(text_at(&dl, 146)->text, "Starting USB mode...");
	f.setup.ps_available = 0;
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);
	CHECK_STR(text_at(&dl, 146)->text, "USB needs Unsafe Homebrew");
	CHECK_STR(text_at(&dl, 388)->text, "PS button: needs Unsafe Homebrew");

	// PS lines and the remap count
	f.setup.connection = CONNECTION_WIFI;
	f.setup.ps_available = 1;
	f.setup.ps_mode = PS_MODE_HOLD;
	f.setup.remap_count = 1;
	uiplan_build(&f.model, &dl);
	CHECK_STR(text_at(&dl, 388)->text, "PS button: hold for the LiveArea");
	CHECK_STR(text_at(&dl, 410)->text, "1 button remapped");
	f.setup.ps_mode = PS_MODE_NORMAL;
	f.setup.remap_count = 12;
	uiplan_build(&f.model, &dl);
	CHECK_STR(text_at(&dl, 388)->text, "PS button: normal");
	CHECK_STR(text_at(&dl, 410)->text, "12 buttons remapped");
	f.setup.remap_count = 2;
	uiplan_build(&f.model, &dl);
	CHECK_STR(text_at(&dl, 410)->text, "2 buttons remapped");

	// The summary sits in R_INFO too
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 0, 1000, STATS_SUMMARY);
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);
	CHECK(text_at(&dl, 388) && text_at(&dl, 410) && text_at(&dl, 432) && text_at(&dl, 454) && text_at(&dl, 476) == NULL);
	CHECK(text_at(&dl, 388)->color == (uint32_t)UI_RGBA(0xC3, 0xCA, 0xD6, 255));
	CHECK_STR(text_at(&dl, 454)->text, "Battery: 100%");
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 0, 500, STATS_SUMMARY);
	uiplan_build(&f.model, &dl);
	check_in_regions(&dl, 1);

	// Hint geometry with the default table: rect corners and the ellipse points
	fx_levels(&f, SCREEN_PLAY, 0, 0, 1000, 0, STATS_SUMMARY);
	uiplan_build(&f.model, &dl);
	// First 24 lines: the stats ellipse around (480, 272), radii (120, 100)
	int minx = 9999, maxx = -9999, miny = 9999, maxy = -9999;
	for (int i = 0; i < UI_HINT_ELLIPSE_SEGMENTS; i++){
		const DrawItem *it = &dl.items[i];
		CHECK(it->kind == DRAW_LINE);
		if (it->x < minx) minx = it->x;
		if (it->x > maxx) maxx = it->x;
		if (it->y < miny) miny = it->y;
		if (it->y > maxy) maxy = it->y;
		CHECK(it->x1 == dl.items[(i + 1) % UI_HINT_ELLIPSE_SEGMENTS].x && it->y1 == dl.items[(i + 1) % UI_HINT_ELLIPSE_SEGMENTS].y);
	}
	CHECK(minx == 360 && maxx == 600 && miny == 172 && maxy == 372);
	// Then the L1 rectangle: (0, 0) to (479, 271)
	CHECK(dl.items[24].x == 0 && dl.items[24].y == 0 && dl.items[24].x1 == 479 && dl.items[24].y1 == 0);
	CHECK(dl.items[25].x == 479 && dl.items[25].y1 == 271);
	CHECK(dl.items[26].x == 479 && dl.items[26].y == 271 && dl.items[26].x1 == 0);
	CHECK(dl.items[27].x == 0 && dl.items[27].y == 271 && dl.items[27].y1 == 0);
}

// ---------------------------------------------------------------- rear zones

static const char rear_default_text[] =
	"# Default rear zones\n"
	"L1 rect 451 0 941 544\n"
	"L3 rect 451 544 941 1088\n"
	"R1 rect 979 0 1469 544\n"
	"R3 rect 979 544 1469 1088\n";

static void test_rear_table(void){
	ZoneTable t;
	zones_default_rear(&t);
	CHECK(t.count == 4);
	CHECK(zone_is(&t.zones[0], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L1, 451, 0, 941, 544));
	CHECK(zone_is(&t.zones[1], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L3, 451, 544, 941, 1088));
	CHECK(zone_is(&t.zones[2], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R1, 979, 0, 1469, 544));
	CHECK(zone_is(&t.zones[3], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R3, 979, 544, 1469, 1088));

	// The built-in table text parses back to the same table
	ZoneTable p;
	CHECK(zones_parse(rear_default_text, &p) == 4);
	CHECK(tables_equal(&p, &t));

	// Inside each zone
	CHECK(zones_hit(&t, 700, 200) == 0);
	CHECK(zones_hit(&t, 700, 800) == 1);
	CHECK(zones_hit(&t, 1200, 200) == 2);
	CHECK(zones_hit(&t, 1200, 800) == 3);
	// Corners: first and last live unit of each rectangle
	CHECK(zones_hit(&t, 451, 0) == 0);
	CHECK(zones_hit(&t, 940, 543) == 0);
	CHECK(zones_hit(&t, 451, 1087) == 1);
	CHECK(zones_hit(&t, 940, 1087) == 1);
	CHECK(zones_hit(&t, 979, 0) == 2);
	CHECK(zones_hit(&t, 1468, 543) == 2);
	CHECK(zones_hit(&t, 979, 1087) == 3);
	CHECK(zones_hit(&t, 1468, 1087) == 3);
	// Dead edges: x 450 and x 1469 (half-open), the outer margins, the centre gap
	CHECK(zones_hit(&t, 450, 200) == -1);
	CHECK(zones_hit(&t, 450, 800) == -1);
	CHECK(zones_hit(&t, 1469, 200) == -1);
	CHECK(zones_hit(&t, 1469, 800) == -1);
	CHECK(zones_hit(&t, 0, 0) == -1);
	CHECK(zones_hit(&t, 1919, 1087) == -1);
	for (int x = 941; x <= 978; x++){
		CHECK(zones_hit(&t, (uint16_t)x, 200) == -1);
		CHECK(zones_hit(&t, (uint16_t)x, 800) == -1);
	}
	// The y split
	CHECK(zones_hit(&t, 700, 543) == 0);
	CHECK(zones_hit(&t, 700, 544) == 1);
	CHECK(zones_hit(&t, 1200, 543) == 2);
	CHECK(zones_hit(&t, 1200, 544) == 3);

	// Resolve: a contact that lands in the dead margin never presses anything, wherever it slides
	ZoneLatch latch;
	zones_latch_reset(&latch);
	ZonePoint p1 = { 300, 200, 1 };
	int fresh = 0;
	CHECK(zones_resolve(&t, &p1, 1, &latch, &fresh) == 0);
	CHECK(fresh == 1);
	p1.x = 700;
	CHECK(zones_resolve(&t, &p1, 1, &latch, &fresh) == 0);
	CHECK(fresh == 0);
	CHECK(latch.zone[0] == -1);
	CHECK(zones_resolve(&t, &p1, 0, &latch, &fresh) == 0);   // lifts
	CHECK(latch.count == 0);
	CHECK(zones_resolve(&t, &p1, 1, &latch, &fresh) == PAD_L1);   // lands again, now inside L1
	// A contact that lands in L1 keeps L1 when it slides into the R3 rectangle
	zones_latch_reset(&latch);
	ZonePoint p2 = { 700, 200, 2 };
	CHECK(zones_resolve(&t, &p2, 1, &latch, &fresh) == PAD_L1);
	p2.x = 1200;
	p2.y = 800;
	CHECK(zones_resolve(&t, &p2, 1, &latch, &fresh) == PAD_L1);
	CHECK(fresh == 0);
	// Two contacts in L1 and R3 give both bits
	zones_latch_reset(&latch);
	ZonePoint two[2] = { { 700, 200, 3 }, { 1200, 800, 4 } };
	CHECK(zones_resolve(&t, two, 2, &latch, &fresh) == (PAD_L1 | PAD_R3));
	CHECK(fresh == 2);

	// A Stats zone in a rear table parses, but resolves to no button
	ZoneTable st;
	CHECK(zones_parse("Stats rect 451 0 941 544\n", &st) == 1);
	ZonePoint ps = { 700, 200, 5 };
	zones_latch_reset(&latch);
	CHECK(zones_resolve(&st, &ps, 1, &latch, &fresh) == 0);
}

static int count_text(const DrawList *l, const char *text, uint32_t rgb_color_alpha255){
	int n = 0;
	for (int i = 0; i < l->count; i++){
		const DrawItem *it = &l->items[i];
		if (it->kind == DRAW_TEXT && strcmp(it->text, text) == 0 && (rgb_color_alpha255 == 0 || it->color == rgb_color_alpha255)) n++;
	}
	return n;
}

static int count_color(const DrawList *l, int kind, uint32_t color){
	int n = 0;
	for (int i = 0; i < l->count; i++) n += l->items[i].kind == kind && l->items[i].color == color;
	return n;
}

static void test_rear_hint(void){
	const uint32_t line_col = (uint32_t)UI_RGBA(0x4A, 0x3A, 0x62, 255);
	const uint32_t label_col = (uint32_t)UI_RGBA(0x9A, 0x7F, 0xC8, 255);
	static const char *const labels[4] = { "L1 rear", "L3 rear", "R1 rear", "R3 rear" };   // table order

	Fx f;
	fx_init(&f);
	fx_levels(&f, SCREEN_PLAY, 0, 0, 1000, 0, STATS_SUMMARY);
	uiplan_build(&f.model, &dl);
	const int base_count = dl.count;
	CHECK(base_count == 24 + 4 * 4 + 5);                       // front only: ellipse + 4 rects + 5 labels
	CHECK(count_color(&dl, DRAW_LINE, line_col) == 0);

	f.model.rear_owned = 1;
	uiplan_build(&f.model, &dl);
	CHECK(dl.count == base_count + 16 + 4);
	CHECK(count_color(&dl, DRAW_LINE, line_col) == 16);
	CHECK(count_color(&dl, DRAW_TEXT, label_col) == 4);
	// After the front zones: front lines, front labels, rear lines, rear labels
	const int rear_first = base_count;
	for (int k = 0; k < 16; k++){
		CHECK(item_at(&dl, rear_first + k)->kind == DRAW_LINE);
		CHECK(item_at(&dl, rear_first + k)->color == line_col);
	}
	// First rear rectangle: L1 rear, x 451..941 and y 0..544 in touch units, so (225, 0) to (469, 271)
	CHECK(item_at(&dl, rear_first)->x == 225 && item_at(&dl, rear_first)->y == 0);
	CHECK(item_at(&dl, rear_first)->x1 == 469 && item_at(&dl, rear_first)->y1 == 0);
	CHECK(item_at(&dl, rear_first + 1)->x == 469 && item_at(&dl, rear_first + 1)->y1 == 271);
	for (int k = 0; k < 4; k++){
		const DrawItem *it = item_at(&dl, rear_first + 16 + k);
		CHECK(it->kind == DRAW_TEXT);
		CHECK_STR(it->text, labels[k]);
		CHECK(it->color == label_col);
		CHECK(it->align == ALIGN_CENTER && it->scale > 0.89f && it->scale < 0.91f);
		const Zone *z = &f.rz.zones[k];
		CHECK(it->x == (z->a + z->c) / 4 && it->y == (z->b + z->d) / 4 + 6);
	}
	// The front items are untouched by the rear layer
	Fx g;
	fx_init(&g);
	fx_levels(&g, SCREEN_PLAY, 0, 0, 1000, 0, STATS_SUMMARY);
	static DrawList front_only;
	uiplan_build(&g.model, &front_only);
	for (int i = 0; i < base_count; i++) CHECK(memcmp(&front_only.items[i], &dl.items[i], sizeof(DrawItem)) == 0);

	// Not owned, or no table: none of them
	f.model.rear_owned = 0;
	uiplan_build(&f.model, &dl);
	CHECK(dl.count == base_count);
	CHECK(count_text(&dl, "L1 rear", 0) == 0);
	f.model.rear_owned = 1;
	f.model.rear_zones = NULL;
	uiplan_build(&f.model, &dl);
	CHECK(dl.count == base_count);
	CHECK(count_color(&dl, DRAW_LINE, line_col) == 0);

	// The hint level scales the rear alpha like the front one
	f.model.rear_zones = &f.rz;
	fx_levels(&f, SCREEN_PLAY, 0, 0, 500, 0, STATS_SUMMARY);
	uiplan_build(&f.model, &dl);
	CHECK(alpha_of(item_at(&dl, rear_first)) == 500 * 255 / 1000);

	// A hint level of 0 draws nothing at all, rear included
	fx_levels(&f, SCREEN_PLAY, 0, 0, 0, 0, STATS_SUMMARY);
	uiplan_build(&f.model, &dl);
	CHECK(dl.count == 0);

	// A Stats zone in the rear table is outlined but has no label
	fx_levels(&f, SCREEN_PLAY, 0, 0, 1000, 0, STATS_SUMMARY);
	CHECK(zones_parse("Stats rect 451 0 941 544\nR1 rect 979 0 1469 544\n", &f.rz) == 2);
	uiplan_build(&f.model, &dl);
	CHECK(count_color(&dl, DRAW_LINE, line_col) == 8);
	CHECK(count_color(&dl, DRAW_TEXT, label_col) == 1);
	CHECK(count_text(&dl, "stats", 0) == 1);                   // the front one only

	// Budgets with both built-in tables, the worst frames
	fx_init(&f);
	f.model.rear_owned = 1;
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 1000, 500, STATS_SUMMARY);
	fx_all_pressed(&f);
	f.m.lit |= 1u << PART_DPAD_DOWN;
	f.m.lcap_deflected = f.m.rcap_deflected = 1;
	f.setup.connection = CONNECTION_USB_GENERIC;
	f.setup.usb_state = USB_STATE_WAITING;
	f.setup.remap_count = 12;
	int c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET);
	fx_levels(&f, SCREEN_PLAY, UI_PLAY_BODY_LEVEL, 1000, 1000, 0, STATS_SUMMARY);
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET);
	fx_levels(&f, SCREEN_SETUP, 0, 0, 0, 1000, STATS_DIAG);
	c = plan_cost(&f);
	CHECK(c <= UI_DRAW_BUDGET_DIAG);

	// Sixteen ellipses in both zones.txt and rear-zones.txt still fit in the list, and the builder never overruns it
	static char big[2048];
	big[0] = 0;
	for (int i = 0; i < 16; i++){
		char line[64];
		snprintf(line, sizeof(line), "%s ellipse %d %d 200 100\n", i % 2 ? "L1" : "Stats", 200 + 100 * (i % 8), 200 + 300 * (i / 8));
		strcat(big, line);
	}
	fx_init(&f);
	CHECK(zones_parse(big, &f.z) == 16);
	CHECK(zones_parse(big, &f.rz) == 16);
	f.model.rear_owned = 1;
	fx_levels(&f, SCREEN_SETUP, 1000, 1000, 1000, 500, STATS_SUMMARY);
	fx_all_pressed(&f);
	f.m.lit |= 1u << PART_DPAD_DOWN;
	f.m.lcap_deflected = f.m.rcap_deflected = 1;
	c = plan_cost(&f);
	CHECK(count_kind(&dl, DRAW_LINE) == 32 * UI_HINT_ELLIPSE_SEGMENTS);
	CHECK(dl.count <= UI_DRAW_MAX);
	CHECK(c <= UI_DRAW_BUDGET_DIAG);

	// A list that is already nearly full: nothing is written past UI_DRAW_MAX
	static DrawList tight;
	memset(&tight, 0xA5, sizeof(tight));
	uiplan_build(&f.model, &tight);
	CHECK(tight.count <= UI_DRAW_MAX);
}


int main(void){
	test_default_table();
	test_hit();
	test_parse();
	test_normalise();
	test_resolve();
	test_tap();
	test_mirror_bits();
	test_dpad();
	test_sticks();
	test_l3_neutral();
	test_boot();
	test_wifi_rise();
	test_play_body();
	test_link_grace();
	test_idle();
	test_activity();
	test_activity_details();
	test_hint_rearm();
	test_stats_cycle();
	test_pause();
	test_frametime();
	test_summary();
	test_diag();
	test_sprites();
	test_draw_order();
	test_budget();
	test_alpha();
	test_layout();
	test_rear_table();
	test_rear_hint();
	printf("%d checks, %d failed\n", checks, failed);
	return failed ? 1 : 0;
}
