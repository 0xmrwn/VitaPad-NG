#pragma once
#include <stdint.h>

#define MIRROR_STICK_TRAVEL_PX 12  // cap travel at full deflection, screen pixels
#define MIRROR_STICK_DEADBAND 16   // |v - 128| at or below this on both axes = at rest. Tune after capture 3

enum {
	MIRROR_DPAD_DS4_CLIENT = 0, // Wi-Fi: resolution of Client/ViGEm.cpp:206-220
	MIRROR_DPAD_USB_HAT,        // USB: resolution of usbmode.c:57-69
};

// Parts, in draw order (section 10). Bit i of MirrorState.lit = part i.
enum {
	PART_L2 = 0, PART_R2, PART_L1, PART_R1, PART_SHARE, PART_OPTIONS,
	PART_DPAD_UP, PART_DPAD_RIGHT, PART_DPAD_DOWN, PART_DPAD_LEFT,
	PART_TRIANGLE, PART_CIRCLE, PART_CROSS, PART_SQUARE, PART_PS,
	PART_L3, PART_R3,   // click: ring plus full-strength cap
	PART_COUNT
};

typedef struct {
	uint32_t buttons;           // PAD_* bits
	uint8_t lx, ly, rx, ry;     // 0..255, 128 = centre, 0 = left/up
} PadSnapshot;

typedef struct {
	uint32_t lit;               // bit per PART_*
	int8_t lcap_dx, lcap_dy, rcap_dx, rcap_dy; // pixels, -12..12
	uint8_t lcap_deflected, rcap_deflected;    // outside the dead band
} MirrorState;

void mirror_neutral(PadSnapshot *snap);       // buttons 0, sticks 128 (what the PC gets while the remap menu is open)
void mirror_compute(const PadSnapshot *snap, int dpad_rule, MirrorState *out);
int mirror_stick_offset(uint8_t value);       // section 8.3
