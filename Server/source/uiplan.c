#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define SPRITES_IMPLEMENTATION
#include "sprites.h"
#include "uiplan.h"
#include "padbits.h"
#include "usbmode.h"
#include "psbutton.h"

// Colours of section 8.2 as 0xRRGGBB; the alpha comes from the layer
#define COL_VALUE      0xC3CAD6u
#define COL_LABEL      0x7D8696u
#define COL_TITLE      0x566070u
#define COL_HINT_LINE  0x3A4A62u
#define COL_HINT_LABEL 0x6F93C8u
// The rear zones get their own hue so they cannot be mistaken for front zones
#define COL_HINT_REAR_LINE  0x4A3A62u
#define COL_HINT_REAR_LABEL 0x9A7FC8u

// Screen layout of section 12 (the screen is 960 wide, everything is centred on its middle)
#define CENTRE_X 480

// uint32_t maths on purpose: shifting 255 into the sign bit of an int (UI_RGBA) is undefined
static uint32_t rgba(uint32_t rgb, int alpha){
	return ((uint32_t)alpha << 24) | ((rgb & 0xFFu) << 16) | (rgb & 0xFF00u) | ((rgb >> 16) & 0xFFu);
}

static int level_alpha(int level){
	return level * 255 / UI_LEVEL_MAX;
}

static DrawItem *item_new(DrawList *l, int kind){
	if (l->count >= UI_DRAW_MAX) return NULL; // full: drop silently
	DrawItem *it = &l->items[l->count++];
	memset(it, 0, sizeof(*it));
	it->kind = (uint8_t)kind;
	return it;
}

static void add_sprite(DrawList *l, int id, int x, int y, int alpha){
	if (alpha <= 0) return;
	DrawItem *it = item_new(l, DRAW_SPRITE);
	if (!it) return;
	it->sprite = (uint16_t)id;
	it->x = (int16_t)x;
	it->y = (int16_t)y;
	it->color = rgba(0xFFFFFFu, alpha);
}

static void add_line(DrawList *l, int x0, int y0, int x1, int y1, uint32_t rgb, int alpha){
	if (alpha <= 0) return;
	DrawItem *it = item_new(l, DRAW_LINE);
	if (!it) return;
	it->x = (int16_t)x0;
	it->y = (int16_t)y0;
	it->x1 = (int16_t)x1;
	it->y1 = (int16_t)y1;
	it->color = rgba(rgb, alpha);
}

static void add_text(DrawList *l, int align, int x, int y, int max_w, float scale, uint32_t rgb, int alpha, const char *text){
	if (alpha <= 0 || !text || !text[0]) return;
	DrawItem *it = item_new(l, DRAW_TEXT);
	if (!it) return;
	it->align = (uint8_t)align;
	it->x = (int16_t)x;
	it->y = (int16_t)y;
	it->x1 = (int16_t)max_w;
	it->color = rgba(rgb, alpha);
	it->scale = scale;
	// Bounded copy; longer strings are cut
	size_t i = 0;
	for (; i < UI_TEXT_MAX - 1 && text[i]; i++) it->text[i] = text[i];
	it->text[i] = 0;
}

// Which sprite is the lit sprite of part p (the 15 simple parts are consecutive in both enums)
static int part_sprite(int part){
	return SPR_LIT_L2 + part;
}

static void emit_mirror(const UiModel *m, DrawList *l, int body_alpha, int lit_alpha){
	const Screen *s = m->screen;
	const MirrorState *ms = m->mirror;
	const int ldx = ms->lcap_dx, ldy = ms->lcap_dy, rdx = ms->rcap_dx, rdy = ms->rcap_dy;

	// Mirror=Off blanks PLAY completely (for A/B measurements); SETUP is unaffected
	const int shown = m->mirror_enabled || s->mode == SCREEN_SETUP;
	if (m->art_loaded && body_alpha > 0 && shown){
		add_sprite(l, SPR_BODY, sprite_rects[SPR_BODY].sx, sprite_rects[SPR_BODY].sy, body_alpha);
		add_sprite(l, SPR_STICK_L_CAP, sprite_rects[SPR_STICK_L_CAP].sx + ldx, sprite_rects[SPR_STICK_L_CAP].sy + ldy, body_alpha);
		add_sprite(l, SPR_STICK_R_CAP, sprite_rects[SPR_STICK_R_CAP].sx + rdx, sprite_rects[SPR_STICK_R_CAP].sy + rdy, body_alpha);
	}

	if (!m->art_loaded || lit_alpha <= 0 || !shown) return;

	for (int part = PART_L2; part <= PART_PS; part++){
		if (ms->lit & (1u << part)){
			int id = part_sprite(part);
			add_sprite(l, id, sprite_rects[id].sx, sprite_rects[id].sy, lit_alpha);
		}
	}
	const int l_click = (ms->lit & (1u << PART_L3)) != 0, r_click = (ms->lit & (1u << PART_R3)) != 0;
	// The well doesn't move, so the ring stays on the rest position
	if (l_click) add_sprite(l, SPR_LIT_STICK_L_RING, sprite_rects[SPR_LIT_STICK_L_RING].sx, sprite_rects[SPR_LIT_STICK_L_RING].sy, lit_alpha);
	if (r_click) add_sprite(l, SPR_LIT_STICK_R_RING, sprite_rects[SPR_LIT_STICK_R_RING].sx, sprite_rects[SPR_LIT_STICK_R_RING].sy, lit_alpha);
	const int soft = lit_alpha * UI_STICK_DEFLECT_ALPHA / 255;
	if (l_click || ms->lcap_deflected)
		add_sprite(l, SPR_LIT_STICK_L_CAP, sprite_rects[SPR_LIT_STICK_L_CAP].sx + ldx, sprite_rects[SPR_LIT_STICK_L_CAP].sy + ldy, l_click ? lit_alpha : soft);
	if (r_click || ms->rcap_deflected)
		add_sprite(l, SPR_LIT_STICK_R_CAP, sprite_rects[SPR_LIT_STICK_R_CAP].sx + rdx, sprite_rects[SPR_LIT_STICK_R_CAP].sy + rdy, r_click ? lit_alpha : soft);
}

static void emit_setup_text(const UiModel *m, DrawList *l, int alpha, int info_alpha){
	const SetupInfo *si = m->setup;
	if (!si) return;
	char buf[UI_TEXT_MAX];

	// R_TITLE
	add_text(l, ALIGN_CENTER, CENTRE_X, 22, 360, 0.8f, COL_TITLE, alpha, "VitaPad NG 2.0");

	// R_CONN
	const char *line1, *line2;
	char line3[96];
	line3[0] = 0;
	char ipport[64];
	ipport[0] = 0;
	if (si->has_ip) snprintf(ipport, sizeof(ipport), "%s:%d", si->ip, si->port);
	if (si->connection == CONNECTION_WIFI){
		if (si->has_ip){
			line1 = ipport;
			line2 = "Waiting for the PC client";
		}else{
			line1 = "No Wi-Fi";
			line2 = "Waiting for a Wi-Fi connection";
		}
	}else{
		line1 = si->connection == CONNECTION_USB_GENERIC ? "USB: standard gamepad" : "USB: DualShock 4";
		if (si->usb_state == USB_STATE_OFF) line2 = si->ps_available ? "Starting USB mode..." : "USB needs Unsafe Homebrew";
		else line2 = "Plug the Vita into a computer";
		if (si->has_ip) snprintf(line3, sizeof(line3), "Wi-Fi: %s", ipport);
	}
	add_text(l, ALIGN_CENTER, CENTRE_X, 120, 224, 1.2f, COL_VALUE, alpha, line1);
	add_text(l, ALIGN_CENTER, CENTRE_X, 146, 224, 0.85f, COL_LABEL, alpha, line2);
	add_text(l, ALIGN_CENTER, CENTRE_X, 168, 224, 0.85f, COL_LABEL, alpha, line3);

	// R_INFO
	const char *ps;
	if (!si->ps_available) ps = "PS button: needs Unsafe Homebrew";
	else if (si->ps_mode == PS_MODE_DOUBLE_TAP) ps = "PS button: double-tap for the LiveArea";
	else if (si->ps_mode == PS_MODE_HOLD) ps = "PS button: hold for the LiveArea";
	else ps = "PS button: normal";
	if (si->remap_count <= 0) snprintf(buf, sizeof(buf), "No buttons remapped");
	else if (si->remap_count == 1) snprintf(buf, sizeof(buf), "1 button remapped");
	else snprintf(buf, sizeof(buf), "%d buttons remapped", si->remap_count);
	add_text(l, ALIGN_CENTER, CENTRE_X, 388, 340, 0.85f, COL_LABEL, info_alpha, ps);
	add_text(l, ALIGN_CENTER, CENTRE_X, 410, 340, 0.85f, COL_LABEL, info_alpha, buf);
	add_text(l, ALIGN_CENTER, CENTRE_X, 432, 340, 0.85f, COL_LABEL, info_alpha, "Hold L + R + SELECT 1 s: screen off");
	add_text(l, ALIGN_CENTER, CENTRE_X, 454, 340, 0.85f, COL_LABEL, info_alpha, "Hold L + R + START 1 s: remap menu");
	add_text(l, ALIGN_CENTER, CENTRE_X, 476, 340, 0.85f, COL_LABEL, info_alpha, "Tap the centre: stats");
}

// cos of k x 15 degrees, times 1000. sin(k) = cos(k - 6): a table keeps the hint free of floating point
// and the same on every compiler
static const int16_t cos24[UI_HINT_ELLIPSE_SEGMENTS] = {
	1000, 966, 866, 707, 500, 259, 0, -259, -500, -707, -866, -966,
	-1000, -966, -866, -707, -500, -259, 0, 259, 500, 707, 866, 966,
};

static int scaled(int radius, int c1000){
	int v = radius * c1000;
	return (v >= 0 ? v + 500 : v - 500) / 1000;
}

static const char *zone_label(const Zone *z){
	if (z->action == ZONE_ACTION_STATS) return "stats";
	if (z->action != ZONE_ACTION_BUTTON) return NULL;
	if (z->buttons == PAD_L1) return "L1";
	if (z->buttons == PAD_R1) return "R1";
	if (z->buttons == PAD_L3) return "L3";
	if (z->buttons == PAD_R3) return "R3";
	return NULL;
}

// Rear zones are buttons only: a Stats zone is legal in rear-zones.txt but does nothing there, so it gets no label
static const char *rear_zone_label(const Zone *z){
	if (z->action != ZONE_ACTION_BUTTON) return NULL;
	if (z->buttons == PAD_L1) return "L1 rear";
	if (z->buttons == PAD_R1) return "R1 rear";
	if (z->buttons == PAD_L3) return "L3 rear";
	if (z->buttons == PAD_R3) return "R3 rear";
	return NULL;
}

// One table: outlines first, then labels. Both panels share the geometry (touch units / 2 = screen pixels)
static void emit_zone_table(const ZoneTable *zt, DrawList *l, int rear, int alpha){
	const uint32_t line_col = rear ? COL_HINT_REAR_LINE : COL_HINT_LINE;
	const uint32_t label_col = rear ? COL_HINT_REAR_LABEL : COL_HINT_LABEL;
	int n = zt->count > ZONE_MAX ? ZONE_MAX : zt->count;
	// Touch units to screen pixels: the front panel covers the 960x544 screen at half scale
	for (int i = 0; i < n; i++){
		const Zone *z = &zt->zones[i];
		if (z->shape == ZONE_SHAPE_RECT){
			int x0 = z->a / 2, y0 = z->b / 2, x1 = z->c / 2 - 1, y1 = z->d / 2 - 1;
			add_line(l, x0, y0, x1, y0, line_col, alpha);
			add_line(l, x1, y0, x1, y1, line_col, alpha);
			add_line(l, x1, y1, x0, y1, line_col, alpha);
			add_line(l, x0, y1, x0, y0, line_col, alpha);
		}else{
			int cx = z->a / 2, cy = z->b / 2, rx = z->c / 2, ry = z->d / 2;
			for (int k = 0; k < UI_HINT_ELLIPSE_SEGMENTS; k++){
				int k1 = (k + 1) % UI_HINT_ELLIPSE_SEGMENTS;
				add_line(l, cx + scaled(rx, cos24[k]), cy + scaled(ry, cos24[(k + 18) % 24]),
					cx + scaled(rx, cos24[k1]), cy + scaled(ry, cos24[(k1 + 18) % 24]), line_col, alpha);
			}
		}
	}
	for (int i = 0; i < n; i++){
		const Zone *z = &zt->zones[i];
		const char *label = rear ? rear_zone_label(z) : zone_label(z);
		if (!label) continue;
		int cx, cy;
		if (z->shape == ZONE_SHAPE_RECT){
			cx = (z->a + z->c) / 4;
			cy = (z->b + z->d) / 4;
		}else{
			cx = z->a / 2;
			cy = z->b / 2;
		}
		// The baseline sits a little below the centre so the glyphs look centred
		add_text(l, ALIGN_CENTER, cx, cy + 6, 0, 0.9f, label_col, alpha, label);
	}
}

static void emit_hint(const UiModel *m, DrawList *l, int alpha){
	if (m->zones) emit_zone_table(m->zones, l, 0, alpha);
	// The rear panel is a second layer after the front one, only when the Vita owns it
	if (m->rear_owned && m->rear_zones) emit_zone_table(m->rear_zones, l, 1, alpha);
}

static void emit_summary(const UiModel *m, DrawList *l, int alpha){
	if (!m->stats || !m->frame) return;
	char lines[STATS_SUMMARY_LINES][STATS_LINE_MAX];
	int n = stats_summary(m->stats, m->frame, lines);
	for (int i = 0; i < n; i++) add_text(l, ALIGN_CENTER, CENTRE_X, 388 + 22 * i, 340, 0.85f, COL_VALUE, alpha, lines[i]);
}

static void emit_diag(const UiModel *m, DrawList *l, int alpha){
	if (!m->stats || !m->frame) return;
	char lines[STATS_DIAG_LINES][STATS_LINE_MAX];
	int n = stats_diag(m->stats, m->frame, lines);
	for (int i = 0; i < n; i++){
		uint32_t col = i >= 10 ? COL_LABEL : COL_VALUE;
		add_text(l, ALIGN_LEFT, 16, 28 + 21 * i, 0, 0.85f, col, alpha, lines[i]);
	}
}

void uiplan_build(const UiModel *m, DrawList *out){
	out->count = 0;
	const Screen *s = m->screen;

	// The diagnostic view replaces everything, until its text has faded out
	if (s->stats_drawn == STATS_DIAG && s->stats_level > 0){
		emit_diag(m, out, level_alpha(s->stats_level));
		return;
	}

	int body_alpha = level_alpha(s->body);
	int lit_alpha = level_alpha(s->lit);
	emit_mirror(m, out, body_alpha, lit_alpha);
	// The info block makes room for the summary
	int info_alpha = level_alpha(s->text * (UI_LEVEL_MAX - s->stats_level) / UI_LEVEL_MAX);
	emit_setup_text(m, out, level_alpha(s->text), info_alpha);
	emit_hint(m, out, level_alpha(s->hint));
	if (s->stats_drawn == STATS_SUMMARY && s->stats_level > 0) emit_summary(m, out, level_alpha(s->stats_level));
}

int uiplan_cost(const DrawList *list){
	int cost = 0;
	for (int i = 0; i < list->count; i++){
		const DrawItem *it = &list->items[i];
		if (it->kind != DRAW_TEXT) cost++;
		else for (const char *c = it->text; *c; c++) if (*c != ' ') cost++;
	}
	return cost;
}
