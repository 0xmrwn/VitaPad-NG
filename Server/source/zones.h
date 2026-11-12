#pragma once
#include <stdint.h>
#include "protocol.h"  // TOUCH_WIDTH, TOUCH_HEIGHT (pure, stdint only)

#define ZONE_MAX 16          // zones per table
#define ZONE_MAX_CONTACTS 8  // = SCE_TOUCH_MAX_REPORT (psp2/touch.h:24)

// Tap that toggles the stats: tune after capture 2
#define ZONE_TAP_MAX_US (300 * 1000)
#define ZONE_TAP_SLOP 64     // touch units the finger may drift during a tap

enum { ZONE_SHAPE_RECT = 0, ZONE_SHAPE_ELLIPSE };
enum { ZONE_ACTION_BUTTON = 0, ZONE_ACTION_STATS, ZONE_ACTION_NONE };

typedef struct {
	uint8_t shape;    // ZONE_SHAPE_*
	uint8_t action;   // ZONE_ACTION_*
	uint16_t a, b, c, d; // RECT: x0, y0, x1, y1, half-open [x0,x1) x [y0,y1). ELLIPSE: cx, cy, rx, ry
	uint32_t buttons; // one PAD_ZONE_MASK bit for ZONE_ACTION_BUTTON, else 0
} Zone;

typedef struct {
	int count;        // 1..ZONE_MAX
	Zone zones[ZONE_MAX];
} ZoneTable;

typedef struct {
	uint16_t x, y;    // touch units
	uint8_t id;       // SceTouchReport.id
} ZonePoint;

// Which zone each live contact started in. All-zero is a valid empty latch.
typedef struct {
	int count;
	uint8_t id[ZONE_MAX_CONTACTS];
	int8_t zone[ZONE_MAX_CONTACTS]; // index into the table, -1 = started outside every zone
} ZoneLatch;

typedef struct {
	int count;
	struct {
		uint8_t id;
		uint8_t stats;       // started in a ZONE_ACTION_STATS zone
		uint16_t x0, y0;     // where it started
		uint32_t max_d2;     // largest squared distance from (x0, y0) so far
		uint64_t t0_us;      // when it started
	} c[ZONE_MAX_CONTACTS];
} TapTracker;               // all-zero is valid

void zones_default(ZoneTable *table);
// Rear panel: two inner columns, four buttons. The outer parts, where the fingers rest, are dead (as seen from the front)
void zones_default_rear(ZoneTable *table);
// Parses zones.txt (section 4.4). Returns the zone count (>= 1) and fills *out, or -1 and leaves *out untouched.
int zones_parse(const char *text, ZoneTable *out);
// First zone containing (x, y) in table order, or -1
int zones_hit(const ZoneTable *table, uint16_t x, uint16_t y);
void zones_latch_reset(ZoneLatch *latch);
// Updates the latch from the current contacts and returns the OR of their latched zones' buttons.
// new_contacts (may be NULL) receives how many ids were not in the latch before.
uint32_t zones_resolve(const ZoneTable *table, const ZonePoint *points, int num, ZoneLatch *latch, int *new_contacts);
// Returns 1 when a contact that started in a stats zone lifted within ZONE_TAP_MAX_US and ZONE_TAP_SLOP
int zones_tap_update(TapTracker *tracker, const ZoneTable *table, const ZonePoint *points, int num, uint64_t now_us);
// Same arithmetic as fill_touch (main.c:65-74): (raw - min) * (range - 1) / (max - min), clamped to [0, range - 1]
uint16_t zones_normalise(int raw, int min, int max, int range);
