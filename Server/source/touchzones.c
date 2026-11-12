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
// A zones.txt of this many bytes or more is rejected
#define ZONES_FILE_MAX 2048

// Written only by touchzones_init() (and the setter), before the input threads exist; read-only afterwards,
// so the input threads need no lock
static SceTouchPanelInfo front_panel;
static ZoneTable table;
static int source = 0;
static volatile int owns_front = 1;

void touchzones_init(void){
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &front_panel);
	zones_default(&table);
	source = 0;

	FILE *f = fopen(ZONES_FILE, "r");
	if (f == NULL) return;
	static char text[ZONES_FILE_MAX];
	size_t len = fread(text, 1, sizeof(text), f);
	fclose(f);
	// Too long (or exactly full, so it may be truncated): the whole file counts as invalid
	if (len >= sizeof(text)){
		source = -1;
		return;
	}
	text[len] = 0;
	// zones_parse leaves the table alone on error, so the built-in one stays
	source = zones_parse(text, &table) > 0 ? 1 : -1;
}

// Same arithmetic as fill_touch in main.c
int touchzones_read(const SceTouchData *front, ZonePoint points[ZONE_MAX_CONTACTS]){
	if (front == NULL) return 0;
	int num = front->reportNum > ZONE_MAX_CONTACTS ? ZONE_MAX_CONTACTS : (int)front->reportNum;
	for (int i = 0; i < num; i++){
		points[i].x = zones_normalise(front->report[i].x, front_panel.minAaX, front_panel.maxAaX, TOUCH_WIDTH);
		points[i].y = zones_normalise(front->report[i].y, front_panel.minAaY, front_panel.maxAaY, TOUCH_HEIGHT);
		points[i].id = front->report[i].id;
	}
	return num;
}

uint32_t touchzones_buttons(const SceTouchData *front, ZoneLatch *latch){
	ZonePoint points[ZONE_MAX_CONTACTS];
	int num = touchzones_read(front, points);
	return zones_resolve(&table, points, num, latch, NULL);
}

const ZoneTable *touchzones_table(void){
	return &table;
}

int touchzones_source(void){
	return source;
}

int touchzones_owns_front(void){
	return owns_front;
}

void touchzones_set_owns_front(int owns){
	owns_front = owns != 0;
}
