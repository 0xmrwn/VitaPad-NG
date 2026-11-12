#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>

#include "identity.h"
#include "padbits.h"
#include "touchzones.h"

// The pure code (zones, mirror) uses its own copies of the button bits so it builds on the Mac:
// make sure they can never drift from the SDK values
_Static_assert(PAD_SELECT == SCE_CTRL_SELECT, "PAD_SELECT");
_Static_assert(PAD_L3 == SCE_CTRL_L3, "PAD_L3");
_Static_assert(PAD_R3 == SCE_CTRL_R3, "PAD_R3");
_Static_assert(PAD_START == SCE_CTRL_START, "PAD_START");
_Static_assert(PAD_UP == SCE_CTRL_UP, "PAD_UP");
_Static_assert(PAD_RIGHT == SCE_CTRL_RIGHT, "PAD_RIGHT");
_Static_assert(PAD_DOWN == SCE_CTRL_DOWN, "PAD_DOWN");
_Static_assert(PAD_LEFT == SCE_CTRL_LEFT, "PAD_LEFT");
_Static_assert(PAD_LTRIGGER == SCE_CTRL_LTRIGGER, "PAD_LTRIGGER");
_Static_assert(PAD_RTRIGGER == SCE_CTRL_RTRIGGER, "PAD_RTRIGGER");
_Static_assert(PAD_L1 == SCE_CTRL_L1, "PAD_L1");
_Static_assert(PAD_R1 == SCE_CTRL_R1, "PAD_R1");
_Static_assert(PAD_TRIANGLE == SCE_CTRL_TRIANGLE, "PAD_TRIANGLE");
_Static_assert(PAD_CIRCLE == SCE_CTRL_CIRCLE, "PAD_CIRCLE");
_Static_assert(PAD_CROSS == SCE_CTRL_CROSS, "PAD_CROSS");
_Static_assert(PAD_SQUARE == SCE_CTRL_SQUARE, "PAD_SQUARE");
_Static_assert(PAD_PS == SCE_CTRL_PSBUTTON, "PAD_PS");

#define ZONES_FILE APP_DATA_DIR "/zones.txt"
#define REAR_ZONES_FILE APP_DATA_DIR "/rear-zones.txt"
// A zones file of this many bytes or more is rejected
#define ZONES_FILE_MAX 2048

// Written only by touchzones_init() (and the setters), before the input threads exist; read-only afterwards,
// so the input threads need no lock
static SceTouchPanelInfo front_panel, rear_panel;
static ZoneTable table, rear_table;
static int source = 0, rear_source = 0;
static volatile int owns_front = 1;
static volatile int owns_rear = 1;

// Reads a zones file over *t. Returns 0 when the file does not exist, 1 when it replaced *t, -1 when it is invalid
// (*t is left alone then, so the built-in table stays)
static int load_table(const char *path, ZoneTable *t){
	FILE *f = fopen(path, "r");
	if (f == NULL) return 0;
	static char text[ZONES_FILE_MAX];
	size_t len = fread(text, 1, sizeof(text), f);
	fclose(f);
	// Too long (or exactly full, so it may be truncated): the whole file counts as invalid
	if (len >= sizeof(text)) return -1;
	text[len] = 0;
	return zones_parse(text, t) > 0 ? 1 : -1;
}

void touchzones_init(void){
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &front_panel);
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &rear_panel);
	zones_default(&table);
	zones_default_rear(&rear_table);
	source = load_table(ZONES_FILE, &table);
	rear_source = load_table(REAR_ZONES_FILE, &rear_table);
}

// Same arithmetic as fill_touch in main.c
static int read_panel(const SceTouchData *data, const SceTouchPanelInfo *panel, ZonePoint points[ZONE_MAX_CONTACTS]){
	if (data == NULL) return 0;
	int num = data->reportNum > ZONE_MAX_CONTACTS ? ZONE_MAX_CONTACTS : (int)data->reportNum;
	for (int i = 0; i < num; i++){
		points[i].x = zones_normalise(data->report[i].x, panel->minAaX, panel->maxAaX, TOUCH_WIDTH);
		points[i].y = zones_normalise(data->report[i].y, panel->minAaY, panel->maxAaY, TOUCH_HEIGHT);
		points[i].id = data->report[i].id;
	}
	return num;
}

int touchzones_read(const SceTouchData *front, ZonePoint points[ZONE_MAX_CONTACTS]){
	return read_panel(front, &front_panel, points);
}

int touchzones_rear_read(const SceTouchData *rear, ZonePoint points[ZONE_MAX_CONTACTS]){
	return read_panel(rear, &rear_panel, points);
}

uint32_t touchzones_buttons(const SceTouchData *front, ZoneLatch *latch){
	ZonePoint points[ZONE_MAX_CONTACTS];
	int num = touchzones_read(front, points);
	return zones_resolve(&table, points, num, latch, NULL);
}

uint32_t touchzones_rear_buttons(const SceTouchData *rear, ZoneLatch *latch){
	ZonePoint points[ZONE_MAX_CONTACTS];
	int num = touchzones_rear_read(rear, points);
	return zones_resolve(&rear_table, points, num, latch, NULL);
}

const ZoneTable *touchzones_table(void){
	return &table;
}

const ZoneTable *touchzones_rear_table(void){
	return &rear_table;
}

int touchzones_source(void){
	return source;
}

int touchzones_rear_source(void){
	return rear_source;
}

int touchzones_owns_front(void){
	return owns_front;
}

void touchzones_set_owns_front(int owns){
	owns_front = owns != 0;
}

int touchzones_owns_rear(void){
	return owns_rear;
}

void touchzones_set_owns_rear(int owns){
	owns_rear = owns != 0;
}
