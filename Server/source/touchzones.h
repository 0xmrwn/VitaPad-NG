#pragma once
#include <stdint.h>
#include <psp2/touch.h>
#include "zones.h"

// One latch per front and rear panel for each caller (a contact keeps the zone it landed in until it lifts)
typedef struct {
	ZoneLatch front, rear;
} TouchLatches;

// Front and rear panel info, then ux0:data/VitaPadNG/zones.txt and rear-zones.txt or the built-in tables.
// Call on the main thread before usb_init() and before the network threads start.
void touchzones_init(void);
// Normalised front contacts (at most ZONE_MAX_CONTACTS), returns how many
int touchzones_read(const SceTouchData *front, ZonePoint points[ZONE_MAX_CONTACTS]);
// PAD_ZONE_MASK bits for this front sample. Constant time, no lock, no I/O: safe on the input threads
uint32_t touchzones_buttons(const SceTouchData *front, ZoneLatch *latch);
const ZoneTable *touchzones_table(void);
int touchzones_source(void);          // 0 built-in, 1 zones.txt, -1 zones.txt invalid
int touchzones_owns_front(void);      // FrontTouch setting: 1 = Zones (default), 0 = Forward
void touchzones_set_owns_front(int owns);

// Same for the rear panel (coordinates as seen from the front, no mirroring). RearTouch=Zones by default
int touchzones_rear_read(const SceTouchData *rear, ZonePoint points[ZONE_MAX_CONTACTS]);
uint32_t touchzones_rear_buttons(const SceTouchData *rear, ZoneLatch *latch);
const ZoneTable *touchzones_rear_table(void);
int touchzones_rear_source(void);     // 0 built-in, 1 rear-zones.txt, -1 rear-zones.txt invalid
int touchzones_owns_rear(void);       // RearTouch setting: 1 = Zones (default), 0 = Forward
void touchzones_set_owns_rear(int owns);
