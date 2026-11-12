#pragma once
#include <stdint.h>
#include "screen.h"
#include "mirror.h"
#include "zones.h"
#include "stats.h"

// Same byte layout as vita2d's RGBA8 (vita2d.h:14)
#define UI_RGBA(r, g, b, a) ((((a) & 0xFF) << 24) | (((b) & 0xFF) << 16) | (((g) & 0xFF) << 8) | ((r) & 0xFF))

#define UI_TEXT_MAX 120
#define UI_DRAW_MAX 512           // items; a 16-ellipse zones.txt hint alone needs 16 x 24 lines + 16 labels
#define UI_DRAW_BUDGET 500        // draw calls, any non-diagnostic frame with the built-in zones
#define UI_DRAW_BUDGET_DIAG 1200  // diagnostic view, and any frame with a 16-zone zones.txt
#define UI_HINT_ELLIPSE_SEGMENTS 24
#define UI_STICK_DEFLECT_ALPHA 160  // 0..255, cap of a deflected but not clicked stick

enum { DRAW_SPRITE = 0, DRAW_LINE, DRAW_TEXT };
enum { ALIGN_LEFT = 0, ALIGN_CENTER };

typedef struct {
	uint8_t kind;          // DRAW_*
	uint8_t align;         // DRAW_TEXT
	uint16_t sprite;       // DRAW_SPRITE: SpriteId
	int16_t x, y;          // SPRITE: top-left. LINE: start. TEXT: baseline origin (centre x if ALIGN_CENTER)
	int16_t x1, y1;        // LINE: end. TEXT: x1 = max width in pixels, 0 = none
	uint32_t color;        // UI_RGBA, alpha already multiplied by the layer level
	float scale;           // DRAW_TEXT
	char text[UI_TEXT_MAX];// DRAW_TEXT, ASCII only
} DrawItem;

typedef struct {
	int count;
	DrawItem items[UI_DRAW_MAX];
} DrawList;

typedef struct {
	int has_ip;
	char ip[32];
	int port;              // GAMEPAD_PORT
	int connection;        // CONNECTION_* (usbmode.h)
	int usb_state;         // USB_STATE_* (usbmode.h)
	int ps_available;
	int ps_mode;           // PS_MODE_* (psbutton.h)
	int remap_count;
} SetupInfo;

typedef struct {
	const Screen *screen;
	const MirrorState *mirror;
	const ZoneTable *zones;
	const SetupInfo *setup;
	const StatsInput *stats;
	const FrameTime *frame;
	int mirror_enabled;    // Mirror=On
	int art_loaded;        // atlas present; when 0 no DRAW_SPRITE items are emitted
} UiModel;

void uiplan_build(const UiModel *model, DrawList *out);
// Draw calls vita2d will issue: 1 per sprite, 1 per line, 1 per non-space character of text
int uiplan_cost(const DrawList *list);
