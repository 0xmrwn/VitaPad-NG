#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "zones.h"
#include "padbits.h"

// zones_hit, zones_resolve and zones_normalise run on latency-critical input threads: bounded loops,
// integer maths only, no I/O.

#define ZONES_FILE_MAX 2047 // bytes of zones.txt

static void set_zone(Zone *z, int shape, int action, uint32_t buttons, int a, int b, int c, int d){
	z->shape = (uint8_t)shape;
	z->action = (uint8_t)action;
	z->a = (uint16_t)a;
	z->b = (uint16_t)b;
	z->c = (uint16_t)c;
	z->d = (uint16_t)d;
	z->buttons = buttons;
}

void zones_default(ZoneTable *table){
	memset(table, 0, sizeof(*table));
	table->count = 5;
	// The centre ellipse comes first so it cuts a hole in the four quadrants: a thumb landing there presses nothing
	set_zone(&table->zones[0], ZONE_SHAPE_ELLIPSE, ZONE_ACTION_STATS, 0, 960, 544, 240, 200);
	set_zone(&table->zones[1], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L1, 0, 0, TOUCH_WIDTH / 2, TOUCH_HEIGHT / 2);
	set_zone(&table->zones[2], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R1, TOUCH_WIDTH / 2, 0, TOUCH_WIDTH, TOUCH_HEIGHT / 2);
	set_zone(&table->zones[3], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L3, 0, TOUCH_HEIGHT / 2, TOUCH_WIDTH / 2, TOUCH_HEIGHT);
	set_zone(&table->zones[4], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R3, TOUCH_WIDTH / 2, TOUCH_HEIGHT / 2, TOUCH_WIDTH, TOUCH_HEIGHT);
}

void zones_default_rear(ZoneTable *table){
	memset(table, 0, sizeof(*table));
	table->count = 4;
	// Fingers gripping the Vita rest on x 0..450 and x 1469..1919 all the time, so only the inner columns
	// count, with a dead gap between them. Coordinates are as seen from the front: no mirroring needed
	set_zone(&table->zones[0], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L1, 451, 0, 941, TOUCH_HEIGHT / 2);
	set_zone(&table->zones[1], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_L3, 451, TOUCH_HEIGHT / 2, 941, TOUCH_HEIGHT);
	set_zone(&table->zones[2], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R1, 979, 0, 1469, TOUCH_HEIGHT / 2);
	set_zone(&table->zones[3], ZONE_SHAPE_RECT, ZONE_ACTION_BUTTON, PAD_R3, 979, TOUCH_HEIGHT / 2, 1469, TOUCH_HEIGHT);
}

static int is_space(char c){
	// '\r' counts so files written on Windows parse
	return c == ' ' || c == '\t' || c == '\r';
}

// Parses an unsigned decimal token into *value. Rejects signs, empty tokens and anything past 99999
static int parse_number(const char *s, size_t len, int *value){
	if (len == 0 || len > 5) return 0;
	int v = 0;
	for (size_t i = 0; i < len; i++){
		if (s[i] < '0' || s[i] > '9') return 0;
		v = v * 10 + (s[i] - '0');
	}
	*value = v;
	return 1;
}

static int token_is(const char *s, size_t len, const char *word){
	return strlen(word) == len && memcmp(s, word, len) == 0;
}

// One line, comment already cut. Returns 1 for a zone, 0 for a blank line, -1 for an error
static int parse_line(const char *s, size_t len, Zone *z){
	const char *tok[6];
	size_t tok_len[6];
	int n = 0;
	size_t i = 0;
	while (i < len){
		while (i < len && is_space(s[i])) i++;
		if (i >= len) break;
		size_t start = i;
		while (i < len && !is_space(s[i])) i++;
		if (n == 6) return -1; // seventh token
		tok[n] = s + start;
		tok_len[n] = i - start;
		n++;
	}
	if (n == 0) return 0;
	if (n != 6) return -1;

	int action, shape;
	uint32_t buttons = 0;
	if (token_is(tok[0], tok_len[0], "L1")){ action = ZONE_ACTION_BUTTON; buttons = PAD_L1; }
	else if (token_is(tok[0], tok_len[0], "R1")){ action = ZONE_ACTION_BUTTON; buttons = PAD_R1; }
	else if (token_is(tok[0], tok_len[0], "L3")){ action = ZONE_ACTION_BUTTON; buttons = PAD_L3; }
	else if (token_is(tok[0], tok_len[0], "R3")){ action = ZONE_ACTION_BUTTON; buttons = PAD_R3; }
	else if (token_is(tok[0], tok_len[0], "Stats")) action = ZONE_ACTION_STATS;
	else if (token_is(tok[0], tok_len[0], "None")) action = ZONE_ACTION_NONE;
	else return -1;

	if (token_is(tok[1], tok_len[1], "rect")) shape = ZONE_SHAPE_RECT;
	else if (token_is(tok[1], tok_len[1], "ellipse")) shape = ZONE_SHAPE_ELLIPSE;
	else return -1;

	int v[4];
	for (int k = 0; k < 4; k++){
		if (!parse_number(tok[2 + k], tok_len[2 + k], &v[k])) return -1;
	}
	if (shape == ZONE_SHAPE_RECT){
		if (!(v[0] < v[2] && v[2] <= TOUCH_WIDTH && v[1] < v[3] && v[3] <= TOUCH_HEIGHT)) return -1;
	}else{
		if (!(v[0] < TOUCH_WIDTH && v[1] < TOUCH_HEIGHT && v[2] >= 1 && v[2] <= TOUCH_WIDTH && v[3] >= 1 && v[3] <= TOUCH_HEIGHT)) return -1;
	}
	set_zone(z, shape, action, buttons, v[0], v[1], v[2], v[3]);
	return 1;
}

int zones_parse(const char *text, ZoneTable *out){
	if (!text) return -1;
	if (strlen(text) > ZONES_FILE_MAX) return -1;
	ZoneTable t;
	memset(&t, 0, sizeof(t));
	const char *p = text;
	while (*p){
		const char *end = p;
		while (*end && *end != '\n') end++;
		// '#' starts a comment to the end of the line
		size_t len = 0;
		while (p + len < end && p[len] != '#') len++;
		Zone z;
		int r = parse_line(p, len, &z);
		if (r < 0) return -1;
		if (r > 0){
			if (t.count == ZONE_MAX) return -1;
			t.zones[t.count++] = z;
		}
		p = *end ? end + 1 : end;
	}
	if (t.count < 1) return -1;
	*out = t;
	return t.count;
}

static int zone_contains(const Zone *z, uint16_t x, uint16_t y){
	if (z->shape == ZONE_SHAPE_RECT) return x >= z->a && x < z->c && y >= z->b && y < z->d;
	int64_t dx = (int64_t)x - z->a;
	int64_t dy = (int64_t)y - z->b;
	int64_t c = z->c, d = z->d;
	return dx * dx * d * d + dy * dy * c * c <= c * c * d * d;
}

int zones_hit(const ZoneTable *table, uint16_t x, uint16_t y){
	int n = table->count;
	if (n > ZONE_MAX) n = ZONE_MAX;
	for (int i = 0; i < n; i++){
		if (zone_contains(&table->zones[i], x, y)) return i;
	}
	return -1;
}

void zones_latch_reset(ZoneLatch *latch){
	memset(latch, 0, sizeof(*latch));
}

uint32_t zones_resolve(const ZoneTable *table, const ZonePoint *points, int num, ZoneLatch *latch, int *new_contacts){
	ZoneLatch next;
	memset(&next, 0, sizeof(next));
	if (num > ZONE_MAX_CONTACTS) num = ZONE_MAX_CONTACTS;
	int old_count = latch->count;
	if (old_count > ZONE_MAX_CONTACTS) old_count = ZONE_MAX_CONTACTS;
	int added = 0;
	uint32_t buttons = 0;

	for (int i = 0; i < num; i++){
		int zone = 0, found = 0;
		for (int j = 0; j < old_count; j++){
			if (latch->id[j] == points[i].id){
				zone = latch->zone[j];
				found = 1;
				break;
			}
		}
		if (!found){
			zone = zones_hit(table, points[i].x, points[i].y);
			added++;
		}
		// A latch from an older, longer table must not index past this one
		if (zone >= table->count) zone = -1;
		next.id[next.count] = points[i].id;
		next.zone[next.count] = (int8_t)zone;
		next.count++;
		if (zone >= 0 && table->zones[zone].action == ZONE_ACTION_BUTTON) buttons |= table->zones[zone].buttons;
	}
	// Contacts missing from this sample have lifted and are dropped with the old latch
	*latch = next;
	if (new_contacts) *new_contacts = added;
	return buttons;
}

int zones_tap_update(TapTracker *tracker, const ZoneTable *table, const ZonePoint *points, int num, uint64_t now_us){
	TapTracker next;
	memset(&next, 0, sizeof(next));
	if (num > ZONE_MAX_CONTACTS) num = ZONE_MAX_CONTACTS;
	int old_count = tracker->count;
	if (old_count > ZONE_MAX_CONTACTS) old_count = ZONE_MAX_CONTACTS;
	uint8_t seen[ZONE_MAX_CONTACTS];
	memset(seen, 0, sizeof(seen));

	for (int i = 0; i < num; i++){
		int k = next.count++;
		int old = -1;
		for (int j = 0; j < old_count; j++){
			if (tracker->c[j].id == points[i].id){ old = j; break; }
		}
		if (old >= 0){
			next.c[k] = tracker->c[old];
			seen[old] = 1;
			int dx = (int)points[i].x - (int)next.c[k].x0;
			int dy = (int)points[i].y - (int)next.c[k].y0;
			uint32_t d2 = (uint32_t)(dx * dx + dy * dy);
			if (d2 > next.c[k].max_d2) next.c[k].max_d2 = d2;
		}else{
			int zone = zones_hit(table, points[i].x, points[i].y);
			next.c[k].id = points[i].id;
			next.c[k].stats = zone >= 0 && table->zones[zone].action == ZONE_ACTION_STATS;
			next.c[k].x0 = points[i].x;
			next.c[k].y0 = points[i].y;
			next.c[k].max_d2 = 0;
			next.c[k].t0_us = now_us;
		}
	}

	// Anything that was tracked and is gone has lifted: a quick, still tap that began in a stats zone counts
	int tap = 0;
	for (int j = 0; j < old_count; j++){
		if (seen[j]) continue;
		if (tracker->c[j].stats && now_us - tracker->c[j].t0_us <= ZONE_TAP_MAX_US
			&& tracker->c[j].max_d2 <= (uint32_t)ZONE_TAP_SLOP * ZONE_TAP_SLOP) tap = 1;
	}
	*tracker = next;
	return tap;
}

uint16_t zones_normalise(int raw, int min, int max, int range){
	int v = raw - min;
	int w = max - min;
	if (w > 0) v = v * (range - 1) / w;
	if (v < 0) v = 0;
	if (v > range - 1) v = range - 1;
	return (uint16_t)v;
}
