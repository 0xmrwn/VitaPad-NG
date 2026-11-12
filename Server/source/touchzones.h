#pragma once
#include <stdint.h>
#include <psp2/touch.h>
#include "zones.h"

// Front panel info, then ux0:data/VitaPadNG/zones.txt or the built-in table.
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
