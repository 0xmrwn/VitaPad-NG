#include <stdint.h>
#include <string.h>

#include "mirror.h"
#include "padbits.h"

// Arms of the D-pad, as bits: the same four bits the client and the USB hat look at
#define ARM_UP    1
#define ARM_RIGHT 2
#define ARM_DOWN  4
#define ARM_LEFT  8

void mirror_neutral(PadSnapshot *snap){
	snap->buttons = 0;
	snap->lx = snap->ly = snap->rx = snap->ry = 128;
}

// Client/ViGEm.cpp:206-220: the four directions in order UP, RIGHT, DOWN, LEFT with the last present winning,
// then the four diagonals in order NE, SE, SW, NW with the last present winning. Returns the arms it lights.
static int dpad_client(uint32_t b){
	int up = (b & PAD_UP) != 0, right = (b & PAD_RIGHT) != 0, down = (b & PAD_DOWN) != 0, left = (b & PAD_LEFT) != 0;
	int arms = 0;
	if (up) arms = ARM_UP;
	if (right) arms = ARM_RIGHT;
	if (down) arms = ARM_DOWN;
	if (left) arms = ARM_LEFT;
	if (up && right) arms = ARM_UP | ARM_RIGHT;
	if (right && down) arms = ARM_RIGHT | ARM_DOWN;
	if (down && left) arms = ARM_DOWN | ARM_LEFT;
	if (left && up) arms = ARM_LEFT | ARM_UP;
	return arms;
}

// usbmode.c:57-69: first match in order NE, SE, SW, NW, UP, RIGHT, DOWN, LEFT
static int dpad_usb(uint32_t b){
	int up = (b & PAD_UP) != 0, right = (b & PAD_RIGHT) != 0, down = (b & PAD_DOWN) != 0, left = (b & PAD_LEFT) != 0;
	if (up && right) return ARM_UP | ARM_RIGHT;
	if (right && down) return ARM_RIGHT | ARM_DOWN;
	if (down && left) return ARM_DOWN | ARM_LEFT;
	if (left && up) return ARM_LEFT | ARM_UP;
	if (up) return ARM_UP;
	if (right) return ARM_RIGHT;
	if (down) return ARM_DOWN;
	if (left) return ARM_LEFT;
	return 0;
}

int mirror_stick_offset(uint8_t value){
	int d = (int)value - 128;
	// Rounds to nearest with C truncation: 0 gives -12, 128 gives 0, 255 gives +12
	int off = (d * MIRROR_STICK_TRAVEL_PX + (d > 0 ? 63 : -63)) / 127;
	if (off < -MIRROR_STICK_TRAVEL_PX) off = -MIRROR_STICK_TRAVEL_PX;
	if (off > MIRROR_STICK_TRAVEL_PX) off = MIRROR_STICK_TRAVEL_PX;
	return off;
}

static int abs_int(int v){ return v < 0 ? -v : v; }

static int deflected(uint8_t x, uint8_t y){
	return abs_int((int)x - 128) > MIRROR_STICK_DEADBAND || abs_int((int)y - 128) > MIRROR_STICK_DEADBAND;
}

void mirror_compute(const PadSnapshot *snap, int dpad_rule, MirrorState *out){
	static const struct { uint32_t bit; int part; } map[] = {
		{ PAD_SELECT, PART_SHARE }, { PAD_START, PART_OPTIONS },
		{ PAD_LTRIGGER, PART_L2 }, { PAD_RTRIGGER, PART_R2 },
		{ PAD_TRIANGLE, PART_TRIANGLE }, { PAD_CIRCLE, PART_CIRCLE }, { PAD_CROSS, PART_CROSS }, { PAD_SQUARE, PART_SQUARE },
		{ PAD_L1, PART_L1 }, { PAD_R1, PART_R1 },
		{ PAD_L3, PART_L3 }, { PAD_R3, PART_R3 },
		{ PAD_PS, PART_PS },
	};
	memset(out, 0, sizeof(*out));
	// Headphone, volume and power bits mean nothing to the PC
	uint32_t b = snap->buttons & PAD_MIRROR_MASK;
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++){
		if (b & map[i].bit) out->lit |= 1u << map[i].part;
	}
	int arms = dpad_rule == MIRROR_DPAD_USB_HAT ? dpad_usb(b) : dpad_client(b);
	if (arms & ARM_UP) out->lit |= 1u << PART_DPAD_UP;
	if (arms & ARM_RIGHT) out->lit |= 1u << PART_DPAD_RIGHT;
	if (arms & ARM_DOWN) out->lit |= 1u << PART_DPAD_DOWN;
	if (arms & ARM_LEFT) out->lit |= 1u << PART_DPAD_LEFT;

	// Inside the dead band the cap rests, so rest noise never moves it; outside, the raw value is used (no jump)
	if (deflected(snap->lx, snap->ly)){
		out->lcap_deflected = 1;
		out->lcap_dx = (int8_t)mirror_stick_offset(snap->lx);
		out->lcap_dy = (int8_t)mirror_stick_offset(snap->ly);
	}
	if (deflected(snap->rx, snap->ry)){
		out->rcap_deflected = 1;
		out->rcap_dx = (int8_t)mirror_stick_offset(snap->rx);
		out->rcap_dy = (int8_t)mirror_stick_offset(snap->ry);
	}
}
