# VitaPad-NG UI rewrite: technical specification

Status: ready to implement. Written 2026-11-12 against upstream v1.9.2 (`bfecfd2`).
Input: the twenty fixed decisions and assumed defaults in `UI-DECISIONS.md` (cited as D1 to D20).
Every `path:line` reference points at the unmodified v1.9.2 tree.

> **Revision, 2026-11-12 (after the first run on the Vita).** PLAY no longer goes black. Where this
> document says PLAY shows "only pressed parts" on black, read: the body and the two base stick caps
> stay on screen at level `UI_PLAY_BODY_LEVEL` (600 of 1000), and lit parts are drawn on top.
> Changes in the code: `Screen` gained a `text` level for the SETUP text (target: SETUP, awake, not
> diagnostic); the `body` target is 1000 in SETUP, 600 in PLAY, 0 when idle or in the diagnostic view;
> `UI_IDLE_TIMEOUT_US` is 120 s like the SETUP timeout; `Mirror=Off` hides the body as well as the lit
> parts in PLAY. PLAY at rest costs 3 draw calls instead of 0.
>
> **Second revision, 2026-11-12: rear touch zones.** The rear panel now has its own zone table
> (`zones_default_rear`, optional `rear-zones.txt`), its own latches (`TouchLatches`), a setting
> `RearTouch=Zones|Forward` (default Zones) and its own outlines in the zone hint. In Zones mode the
> rear zone bits are ORed into `buttons` and `rear_num` stays 0 (v2), the rear click flags are not set
> (v1), and no DS4 touchpad contact is produced (USB). This replaces every statement below that the
> rear panel is untouched or has no visuals. The diagnostic view has 19 lines (a "Rear touch" line
> follows the "Front touch" line). `UI_DRAW_MAX` is 1024.

Terms used throughout:

- **Outgoing state**: the buttons word and the four stick bytes that the PC receives in the current frame.
- **Snapshot**: the main thread's own recomputation of the outgoing state, used to draw the mirror (section 6).
- **Part**: one piece of the controller that can light (a face button, a D-pad arm, a stick cap and so on).
- **Sprite**: one rectangle of the atlas PNG with a fixed screen position.
- **Lit**: drawn with its lit sprite (section 8). A part that is not lit is not drawn in PLAY.
- **Level**: an integer from 0 to 1000 (permille) that scales the alpha of one layer. Fades move levels.
- **Zone**: one entry of the touch zone table (section 4). **Contact**: one finger on the front panel, identified by its report `id`.
- **Latch**: the per-caller memory of which zone each contact started in.
- **Touch units**: the 1920x1088 space of `TOUCH_WIDTH` x `TOUCH_HEIGHT` (`Common/protocol.h:23-24`).
- **Tune after capture N**: a default chosen without hardware; replace it after capture N of `UI-DECISIONS.md:85-113`.

---

## 1 Scope and non-goals

In scope:

- Replace the text main screen (`Server/source/main.c:395-434`) with a DualShock 4 mirror built from one PNG atlas: SETUP (no PC: full controller plus setup text) and PLAY (PC connected: black, only pressed parts lit), with a zone hint overlay, an on-demand stats summary, a diagnostic view, idle fade to black.
- Vita-owned front touch zones for L1/R1/L3/R3, data-driven, used by Wi-Fi (v1 and v2 packets) and both USB modes.
- A new identity (title ID, name, data directory, settings file) so the fork installs beside the stock app (D18).
- Host-side unit tests, an SVG-to-atlas export script and a frame preview tool.

Non-goals (do not do these):

- No change to `Client/`, `Common/protocol.h` or `Server/kernel/` (D17). No packet layout change.
- No change to the remap menu screen (`Server/source/remap.c:166-210`) or to the two hold combos (`Server/source/main.c:368-383`).
- No new third-party library. No SVG rendering on the Vita. No redrawing of the artwork; it is extended only (D14, D19).
- No rear-panel or motion visuals (D15). No touchpad visuals (the Vita never sends a touchpad click).
- No import of the stock app's settings file (the user may copy it by hand; the format is identical).

---

## 2 Architecture and file map

### Decision A: module layout

**Decision.** Pure logic (no Vita headers, compiles on the Mac with `cc -std=c99`) lives in six files: `zones`, `mirror`, `screen`, `stats`, `uiplan` and the generated `sprites.h`. Two Vita glue files (`touchzones.c`, `ui.c`) read the hardware, call the pure code and execute vita2d calls. `uiplan` turns the whole UI model into a flat **draw list** (sprites, lines, text with final positions and colours); `ui.c` only executes that list.
**Rationale.** Everything with behaviour (zone hit-testing, latching, mirror mapping, fades, wording, layout, draw order, draw-call budget) becomes testable on the host, and the preview tool renders the exact same draw list the Vita executes.
**Rejected.** One `ui.c` mixing logic and vita2d calls: untestable without hardware, and the preview would have to reimplement the logic.

### File map

| File | Kind | Owns |
|---|---|---|
| `Server/source/padbits.h` | new, pure | Button bit values (`PAD_*`) equal to `SCE_CTRL_*` (`psp2common/ctrl.h:32-51`), masks. |
| `Server/source/zones.h`, `zones.c` | new, pure | Zone table type, built-in default table, `zones.txt` parser, hit test, latch, tap detector, coordinate normalisation. |
| `Server/source/mirror.h`, `mirror.c` | new, pure | Snapshot type, outgoing state to lit parts and stick offsets, D-pad resolution rules. |
| `Server/source/screen.h`, `screen.c` | new, pure | Screen state machine, timing constants, activity detection, fade levels. |
| `Server/source/stats.h`, `stats.c` | new, pure | Frame-time accumulator, summary and diagnostic line formatting. |
| `Server/source/uiplan.h`, `uiplan.c` | new, pure | UI model, layout rectangles, colours, draw list builder, draw-call cost. Defines `SPRITES_IMPLEMENTATION`. |
| `Server/source/sprites.h` | new, generated, committed | Sprite enum, atlas rectangles, screen positions (section 9.4). |
| `Server/source/identity.h` | new, pure | Data directory and display name; checks `APP_TITLE_ID` from CMake. |
| `Server/source/touchzones.h`, `touchzones.c` | new, Vita | Active zone table, `zones.txt` loading, front panel info, `FrontTouch` setting, the function the input threads call. |
| `Server/source/ui.h`, `ui.c` | new, Vita | Atlas loading, per-frame snapshot and model update, draw list execution, frame-time hooks, `Mirror` setting. |
| `Server/assets/ui/dualshock-4.svg` | new | Extended copy of the artwork (section 9.1). |
| `Server/assets/ui/atlas.png` | new, generated, committed | The atlas, packed into the VPK as `app0:ui/atlas.png`. |
| `Server/assets/ui/sprites.json` | new, generated, committed | Same data as `sprites.h`, for the Python preview. Not packed. |
| `Server/tests/Makefile`, `test_ui.c`, `plan_dump.c`, `zones_lint.c` | new, host | Unit tests and two host tools (section 15). |
| `tools/export_sprites.py` | new | SVG to atlas, `sprites.h`, `sprites.json`; `--check` mode. |
| `tools/ui_preview.py`, `tools/ui_scenarios.txt` | new | Composes 960x544 PNG frames from `plan_dump` output. |
| `Server/source/main.c`, `usbmode.c`, `remap.c`, `psbutton.c`, `Server/CMakeLists.txt` | edited | Exact edits in section 14. |

`inputrate.h` (`Server/source/inputrate.h:1-15`), `usbmode.h`, `psbutton.h` and `Server/kernel/fastbuttons.h:1-20` include only `<stdint.h>`, so pure files may include them for their types and enums. Pure files must not include `<psp2/...>` or `<vita2d.h>`.

### Thread ownership

| Thread | Created at | New code it runs |
|---|---|---|
| main | `main.c:309` | `touchzones_init` (startup, file I/O), `ui_init` (startup, PNG load), `ui_update`, `ui_draw`, frame hooks. |
| server (`handle_client`) | `main.c:342` | `touchzones_buttons()` with its own stack `ZoneLatch`. |
| stream | `main.c:346` | `touchzones_buttons()` with its own stack `ZoneLatch`. |
| USB | `usbmode.c:225` | `touchzones_buttons()` with a file-static `ZoneLatch` only it touches. |

`touchzones_buttons()` reads a table that is written once by `touchzones_init()` before any of these threads exist (`usb_init` at `main.c:320`, server threads at `main.c:342-347`). It does no allocation, no lock, no I/O, and runs in bounded time (at most 8 contacts x 16 zones). This satisfies hard constraint 3.

---

## 3 Shared logic API (pure C)

All pure headers start with `#pragma once` and include only `<stdint.h>`, `<stddef.h>` and the pure project headers listed above. Code style: tabs, `snake_case`, C99, comments explain why.

### 3.1 `padbits.h`

```c
#pragma once
#include <stdint.h>

// Same values as SCE_CTRL_* (psp2common/ctrl.h), so pure code builds on the host.
// touchzones.c checks them with _Static_assert.
#define PAD_SELECT   0x00000001u
#define PAD_L3       0x00000002u
#define PAD_R3       0x00000004u
#define PAD_START    0x00000008u
#define PAD_UP       0x00000010u
#define PAD_RIGHT    0x00000020u
#define PAD_DOWN     0x00000040u
#define PAD_LEFT     0x00000080u
#define PAD_LTRIGGER 0x00000100u
#define PAD_RTRIGGER 0x00000200u
#define PAD_L1       0x00000400u
#define PAD_R1       0x00000800u
#define PAD_TRIANGLE 0x00001000u
#define PAD_CIRCLE   0x00002000u
#define PAD_CROSS    0x00004000u
#define PAD_SQUARE   0x00008000u
#define PAD_PS       0x00010000u

// Bits the client turns into DualShock 4 parts (Client/ViGEm.cpp:141-220); anything else is ignored by the mirror
#define PAD_MIRROR_MASK 0x0001FFFFu
// Bits a zone may produce
#define PAD_ZONE_MASK (PAD_L1 | PAD_R1 | PAD_L3 | PAD_R3)
```

### 3.2 `zones.h`

```c
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
```

### 3.3 `mirror.h`

```c
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
```

### 3.4 `screen.h`

```c
#pragma once
#include <stdint.h>
#include "mirror.h"

// All timing in microseconds of process time (sceKernelGetProcessTimeWide). Section 7.2.
#define UI_IDLE_TIMEOUT_US        (30 * 1000 * 1000)   // PLAY
#define UI_IDLE_TIMEOUT_SETUP_US  (120 * 1000 * 1000)  // SETUP: long enough to walk to the PC and start the client
#define UI_FADE_IN_US             (150 * 1000)
#define UI_FADE_OUT_US            (600 * 1000)
#define UI_HINT_SHOW_US           (2500 * 1000)
#define UI_HINT_REARM_QUIET_US    (30 * 1000 * 1000)
#define UI_STATS_SHOW_US          (5 * 1000 * 1000)
#define UI_DIAG_SHOW_US           (60 * 1000 * 1000)
#define UI_WIFI_LOST_GRACE_US     (5 * 1000 * 1000)
#define UI_USB_LOST_GRACE_US      (1 * 1000 * 1000)
#define UI_MAX_STEP_US            (100 * 1000)
#define UI_STICK_ACTIVITY_DELTA   24   // tune after capture 3

#define UI_LEVEL_MAX 1000

enum { SCREEN_SETUP = 0, SCREEN_PLAY };
enum { STATS_HIDDEN = 0, STATS_SUMMARY, STATS_DIAG };

typedef struct {
	uint64_t now_us;
	int wifi_link;     // main.c `connected` (main.c:36)
	int usb_link;      // usb_state() == USB_STATE_CONNECTED (usbmode.c:255-258)
	int remap_open;    // remap_menu_open (remap.c:45)
	int screen_off;    // main.c screen_off (main.c:351)
	int activity;      // from screen_activity()
	int new_contacts;  // from the UI's zones_resolve()
	int front_count;   // front contacts this frame
	int stats_tap;     // from zones_tap_update()
} ScreenInput;

typedef struct {
	int mode;            // SCREEN_*
	int stats;           // STATS_* currently selected
	int stats_drawn;     // view to draw while the stats level fades out (STATS_SUMMARY or STATS_DIAG)
	int front_seen;      // a front contact was ever seen
	int paused;          // remap menu or screen off last frame
	uint64_t last_us, last_activity_us, last_wifi_us, last_usb_us, last_front_us;
	uint64_t hint_until_us, stats_until_us;
	uint16_t body, lit, hint, stats_level; // levels 0..UI_LEVEL_MAX
} Screen;

typedef struct {
	int valid;
	uint32_t buttons;
	uint8_t sticks[4];
} ActivityRef;

void screen_init(Screen *s, uint64_t now_us);
void screen_update(Screen *s, const ScreenInput *in);
// 1 if the snapshot or the front panel counts as activity (section 7.4); updates ref
int screen_activity(ActivityRef *ref, const PadSnapshot *snap, int new_contacts);
```

### 3.5 `stats.h`

```c
#pragma once
#include <stdint.h>
#include "inputrate.h"
#include "fastbuttons.h"

#define FRAME_WINDOW_US (1000 * 1000)
#define FRAME_LATE_US 17500   // a period this long means at least one vblank was missed
#define STATS_LINE_MAX 120
#define STATS_SUMMARY_LINES 4
#define STATS_DIAG_LINES 18

typedef struct {
	uint64_t window_start_us, last_swap_us;
	uint32_t frames, late, sum_period_us, max_period_us, sum_work_us, max_work_us;
	int valid;                                  // one full window done
	uint32_t avg_period_us, peak_period_us, avg_work_us, peak_work_us, late_frames; // last full window
} FrameTime;

typedef struct {
	InputRates rates;
	int fast_ok;            // fast_stats() returned nonzero
	int fast_enabled;       // fast_enabled()
	FastButtonsStats fast;
	int battery;            // 0..100
	int wifi_link, usb_link;
	int zones_owned;        // FrontTouch=Zones
	int zones_source;       // 0 built-in, 1 zones.txt, -1 zones.txt invalid (built-in used)
	int zones_count;
	int mirror_enabled;
	int art_loaded;
} StatsInput;

void frametime_init(FrameTime *f, uint64_t now_us);
void frametime_rendered(FrameTime *f, uint64_t now_us); // after vita2d_wait_rendering_done
void frametime_swapped(FrameTime *f, uint64_t now_us);  // after vita2d_swap_buffers
int stats_summary(const StatsInput *in, const FrameTime *f, char lines[STATS_SUMMARY_LINES][STATS_LINE_MAX]);
int stats_diag(const StatsInput *in, const FrameTime *f, char lines[STATS_DIAG_LINES][STATS_LINE_MAX]);
```

### 3.6 `uiplan.h`

```c
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
```

`uiplan.c` is the only translation unit that defines `SPRITES_IMPLEMENTATION` before including `sprites.h`.

`uiplan_build` never writes past `UI_DRAW_MAX`: once the list is full, further items are dropped silently. `DrawList` objects are large (about 70 KB), so they are always `static`, never on a thread stack.

### 3.7 Vita glue headers

```c
// touchzones.h
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
```

```c
// ui.h
#pragma once
#include <stdint.h>
#include <psp2/ctrl.h>
#include <vita2d.h>

void ui_init(vita2d_pgf *font);  // loads app0:ui/atlas.png; a missing atlas leaves a text-only UI
// Every frame, after fast_update() (main.c:387), also while the remap menu is open or the screen is off
void ui_update(const SceCtrlData *pad, const char *ip, int wifi_link, int battery, int screen_off);
void ui_draw(void);              // between vita2d_start_drawing and vita2d_end_drawing, main screen only
void ui_frame_rendered(void);    // right after vita2d_wait_rendering_done()
void ui_frame_swapped(void);     // right after vita2d_swap_buffers()
int ui_mirror_enabled(void);
void ui_set_mirror_enabled(int enabled);
```

---

## 4 Touch zones

### Decision B: data-driven zones, latched to the starting zone

**Decision.** Zones are a table of up to 16 rectangles or ellipses in touch units, evaluated first-match in table order. A contact is latched to the zone it lands in and keeps producing that zone's button until it lifts, wherever it slides. The built-in default reproduces today's four quadrants with a centre ellipse cut out for the stats tap. A file `ux0:data/VitaPadNG/zones.txt` replaces the table at startup without code changes.
**Rationale.** Thumbs roll during a hold; a latch means a press never jumps from L1 to L3 mid-action, and a thumb that lands in the dead centre never presses anything. The table shape lets capture 2 produce thumb-shaped zones as data.
**Rejected.** Stateless per-sample hit-testing (today's behaviour, `usbmode.c:48-53`, `Client/ViGEm.cpp:58-77`): a slide across the middle line switches buttons mid-press, and a slide out of the dead centre presses a button.

### 4.1 Default table (ships now)

| # | Kind | Shape | a | b | c | d | Screen equivalent (pixels) |
|---|---|---|---|---|---|---|---|
| 0 | Stats | ellipse | 960 | 544 | 240 | 200 | centre (480, 272), radii 120 x 100 |
| 1 | L1 | rect | 0 | 0 | 960 | 544 | top-left quadrant |
| 2 | R1 | rect | 960 | 0 | 1920 | 544 | top-right quadrant |
| 3 | L3 | rect | 0 | 544 | 960 | 1088 | bottom-left quadrant |
| 4 | R3 | rect | 960 | 544 | 1920 | 1088 | bottom-right quadrant |

The rectangle split at x = 960 and y = 544 with half-open bounds matches the client's `x < TOUCH_WIDTH / 2`, `y < TOUCH_HEIGHT / 2` test (`Client/ViGEm.cpp:60-61`), so L1/R1/L3/R3 stay where they are today except for the centre ellipse. The ellipse covers the speaker, the PS button and the lower touchpad on screen, between the two sticks. Tune after capture 2.

### 4.2 Hit test

- Rect: `x >= a && x < c && y >= b && y < d`.
- Ellipse: with `dx = x - a`, `dy = y - b` as `int64_t`: inside when `dx*dx*d*d + dy*dy*c*c <= c*c*d*d` (boundary inclusive).
- `zones_hit` returns the first matching index, so earlier entries cut holes in later ones. A `None` zone is a dead area that blocks later zones and produces nothing.

### 4.3 Contacts, the two-point limit, sliding

- The callers read every front report: `n = min(reportNum, ZONE_MAX_CONTACTS)` (the panel reports at most 6 on the front, `psp2/touch.h:24`). Each is normalised with `zones_normalise` against the front `SceTouchPanelInfo` exactly as `fill_touch` does (`main.c:65-74`).
- Today `fill_packet_v2` truncates to two points (`main.c:128`) and the USB path reads two (`usbmode.c:48`). In Zones mode the two-point limit no longer applies to zones: all contacts count, and no points are sent (section 5), so the protocol's two-entry array (`Common/protocol.h:59-63`) is irrelevant. In Forward mode the two-point truncation stays exactly as today.
- `zones_resolve` algorithm, bounded by 8 x 8 + 8 x 16 steps:
  1. Build a new latch: for each current contact, if its `id` is in the old latch, copy its zone; else `zone = zones_hit(x, y)` and count it as new.
  2. Contacts missing from the current sample are dropped (lifted).
  3. Return the OR of `buttons` over latched zones whose `action == ZONE_ACTION_BUTTON`.
- Two contacts in the same zone produce the bit once (OR). A contact that starts in the stats ellipse and slides into L1 produces nothing until it lifts.
- Each caller keeps its own latch: the server thread and the stream thread each have one on their stack, the USB thread has a file-static one, and the UI has one. Because they sample at slightly different times, the UI latch and a network latch can disagree only if a finger lands and crosses a zone boundary between two samples (under one 16 ms frame); the PC's value wins and the mirror may differ for one frame. Accepted.
- While the remap menu is open the network paths return before reading touch (`main.c:88-93`, `main.c:119-122`, `usbmode.c:84`), so their latches are not updated; a finger that stays down through the menu keeps the zone it had before the menu opened, and a finger that lands while the menu is open is treated as a new contact when it closes (it presses the zone it is in at that moment).

### 4.4 Replacing the table after capture 2: `zones.txt`

Location `ux0:data/VitaPadNG/zones.txt`, read once by `touchzones_init()` on the main thread at startup (at most 2047 bytes). Grammar:

- One zone per line: `<kind> <shape> <a> <b> <c> <d>`, separated by spaces or tabs.
- `kind` is one of `L1`, `R1`, `L3`, `R3`, `Stats`, `None` (case-sensitive). `shape` is `rect` or `ellipse`.
- Numbers are decimal integers. Rect: `0 <= a < c <= 1920`, `0 <= b < d <= 1088`. Ellipse: `0 <= a < 1920`, `0 <= b < 1088`, `1 <= c <= 1920`, `1 <= d <= 1088`.
- `#` starts a comment to end of line; blank lines are skipped; at most 16 zones; at least 1.
- Any error (bad token, out of range, too many lines, file longer than 2047 bytes) rejects the whole file: the built-in table is used and the diagnostic view says so (`touchzones_source() == -1`).

The default table above, written in this format, is the reference example and is the content of the `zones_lint` self-test. To apply a capture, copy a new `zones.txt` to the Vita and restart the app; validate it first on the Mac with `Server/tests/out/zones_lint zones.txt`, which prints the parsed table or the first error and its line.

### 4.5 Stats tap

`zones_tap_update` (UI thread only) tracks each contact's start time, start position, maximum squared drift and whether it started in a `Stats` zone. When a contact lifts, it reports a tap if it started in a `Stats` zone, lasted at most `ZONE_TAP_MAX_US` and drifted at most `ZONE_TAP_SLOP` (squared drift `<= 64*64`). One lift reports at most one tap per call.

---

## 5 Outgoing packet changes

### Decision C: zones as buttons, no front points

**Decision.** In Zones mode (`FrontTouch=Zones`, the default), `fill_packet_v2` ORs the zone bits into `buttons` and leaves `front_num = 0` and `front[]` zeroed. The legacy `fill_packet` ORs the same bits and sends no front touch. USB always uses zones. `FrontTouch=Forward` restores today's front-touch bytes exactly for Wi-Fi.
**Rationale.** With zero front points every front-touch branch of the unmodified client is a no-op, so the zone press arrives exactly once whatever `VIGEM_FRONT_TOUCH` says (proof below), and the Vita alone decides the zones (D3).
**Rejected.** Sending synthetic front points placed in today's quadrants: the result would then depend on the client setting (`VIGEM_FRONT_TOUCH=2` would make them touchpad touches) and could not express thumb-shaped zones.

### 5.1 `fill_packet_v2` (Wi-Fi, v2 and stream)

| Field | Zones mode | Forward mode |
|---|---|---|
| `buttons` | `remap_buttons(fast_apply(pad.buttons)) \| ps_buttons() \| touchzones_buttons(&front, latch)` | as today (`main.c:123`) |
| `front_num` | 0 (left by the `memset` at `main.c:115`) | `min(reportNum, 2)` (`main.c:128`) |
| `front[]` | all zero | as today (`main.c:130`) |
| everything else | unchanged (`main.c:124-139`) | unchanged |
| remap menu open | neutral as today (`main.c:119-122`), no zone bits | same |

Consequences: front touch no longer causes stream sends by itself (the change test at `main.c:257` sees no point movement), while a zone press or release is a `buttons` change and is sent at once.

### 5.2 Proof that the unmodified client produces L1/R1/L3/R3 exactly once

Packet: `buttons` has `SCE_CTRL_L1` (0x400) set, `front_num == 0`. DualShock 4 path, `vgSubmit` (`Client/ViGEm.cpp:131-241`):

| Client line | What it does with this packet | Result |
|---|---|---|
| `ViGEm.cpp:190` | `buttons & SCE_CTRL_L1` sets `DS4_BUTTON_SHOULDER_LEFT` | L1 pressed (the one and only source) |
| `ViGEm.cpp:196-197` | `VIGEM_FRONT_TOUCH == 1` (buttons): `touchToButtons(..., packet->front, 0, ...)`; the loop at `ViGEm.cpp:58` runs 0 times | nothing added |
| `ViGEm.cpp:104-105` | `VIGEM_FRONT_TOUCH == 2` (touchpad): loop `i < packet->front_num` runs 0 times | no touchpad finger, no button |
| `ViGEm.cpp:200-201` | `VIGEM_FRONT_TOUCH == 3` (touchpad click): requires `front_num > 0`, false | no click |
| `VIGEM_FRONT_TOUCH == 0` | no front branch runs | nothing added |

The same holds for R1, L3, R3 (`ViGEm.cpp:191-193`). `VIGEM_SWAP_SHOULDERS` only affects the Vita L/R bits (`ViGEm.cpp:149-172`) and the client's own quadrant mapping (`ViGEm.cpp:63-67`, unreachable with 0 points), so zone L1 stays `SHOULDER_LEFT` either way. Other outputs:

- Xbox 360 (`Client/xbox.cpp:42` maps `SCE_CTRL_L1` to LB; `xbox.cpp:60` runs `touchToButtons` with 0 points).
- vJoy (`Client/main.cpp:876-879`) and keyboard (`Client/main.cpp:981`, via `toLegacyPacket` at `Client/main.cpp:784-805`, which sets no mouse movement when `front_num == 0`, `Client/main.cpp:793`).

Accepted losses in Zones mode, all recoverable with `FrontTouch=Forward`: the front panel cannot act as the DS4 touchpad or as the mouse in keyboard mode, `--monitor` shows no front touches (`Client/main.cpp:1104`), and with `VIGEM_SWAP_SHOULDERS=1` (or the Xbox equivalent) L2/R2 become unreachable: the client then maps Vita L/R to L1/R1 and expects the upper touch corners to be L2/R2 (`Client/ViGEm.cpp:63-67`), but a zone can only send the L1/R1 bits. The mirror assumes swap is off. Thumb captures should therefore use the stock app (installed beside the fork, D18) or Forward mode.

### 5.3 Legacy `fill_packet` (v1)

Zones mode: `buttons = remap_buttons(fast_apply(pad.buttons)) | touchzones_buttons(&front, latch)`; `tx = ty = 0`; `MOUSE_MOV` is never set; the rear click flags (`main.c:100-103`) are unchanged; `ps_buttons()` stays absent as today (`main.c:95`). Forward mode: byte-identical to today (`main.c:94-104`). The explicit `tx = ty = 0` matters because `pkg` is not cleared in `handle_client` (`main.c:152`).

### 5.4 USB mode (`usbmode.c`)

`touch_buttons()` (`usbmode.c:43-55`) is deleted. `read_input` peeks the front panel and ORs `touchzones_buttons(&front, &zone_latch)` into `in->buttons` with the remap and PS bits (`usbmode.c:88`); the existing table maps `SCE_CTRL_L1/R1/L3/R3` to `USB_BTN_*` (`usbmode.c:76`, `usbmode.c:79`), so the separate OR of `usbmode.c:91` is deleted. USB ignores `FrontTouch` (there is no client to forward to). Side fix: the USB path now normalises with the front panel info instead of assuming raw 1920x1088 coordinates (`usbmode.c:49-50`).

---

## 6 Mirror mapping table

### Decision D: the snapshot is recomputed on the main thread

**Decision.** Every frame, `ui_update` computes the snapshot from the main loop's pad sample (`main.c:369`):

```c
if (remap_menu_open) mirror_neutral(&snap);
else {
	snap.buttons = remap_buttons(fast_apply(pad->buttons)) | ps_buttons();
	if (usb_connection() != CONNECTION_WIFI || touchzones_owns_front()) snap.buttons |= zone_bits; // UI latch
	snap.lx = pad->lx; snap.ly = pad->ly; snap.rx = pad->rx; snap.ry = pad->ry;
}
```

This is the expression of `main.c:123` and `usbmode.c:88` plus zone bits (D6). It runs after `ps_update()` and `fast_update()`, so PS and fast-button state are this frame's.
**Rationale.** It shares only pure, lock-free functions with the input threads (`remap_buttons`, `remap.c:113-119`; `fast_apply`, `fastinput.c:27-32`; `ps_buttons`, `psbutton.c:140-142`) and adds nothing to them. It works the same for TCP polling, UDP streaming and USB.
**Rejected.** The stream thread publishing its last packet through a shared buffer: it adds stores and memory barriers to the input path and does not exist for the poll or USB paths.

In Forward mode on Wi-Fi the Vita cannot know what the client does with front points, so the mirror shows no zone presses.

### 6.1 Bits to parts

| Outgoing bit | DS4 part (client source) | Part enum | Sprite(s) lit |
|---|---|---|---|
| `SCE_CTRL_SELECT` | Share (`ViGEm.cpp:141-144`) | `PART_SHARE` | `SPR_LIT_SHARE` |
| `SCE_CTRL_START` | Options (`ViGEm.cpp:145-148`) | `PART_OPTIONS` | `SPR_LIT_OPTIONS` |
| `SCE_CTRL_LTRIGGER` (Vita L) | L2, swap off (`ViGEm.cpp:149-160`) | `PART_L2` | `SPR_LIT_L2` |
| `SCE_CTRL_RTRIGGER` (Vita R) | R2, swap off (`ViGEm.cpp:161-172`) | `PART_R2` | `SPR_LIT_R2` |
| `SCE_CTRL_TRIANGLE` / `CIRCLE` / `CROSS` / `SQUARE` | same (`ViGEm.cpp:173-188`) | `PART_TRIANGLE` ... | `SPR_LIT_TRIANGLE` ... |
| `SCE_CTRL_L1` / `SCE_CTRL_R1` | L1 / R1 (`ViGEm.cpp:190-191`) | `PART_L1` / `PART_R1` | `SPR_LIT_L1` / `SPR_LIT_R1` |
| `SCE_CTRL_L3` / `SCE_CTRL_R3` | stick clicks (`ViGEm.cpp:192-193`) | `PART_L3` / `PART_R3` | `SPR_LIT_STICK_L_RING` + `SPR_LIT_STICK_L_CAP` at full alpha (R likewise) |
| `SCE_CTRL_PSBUTTON` | PS (`ViGEm.cpp:194`) | `PART_PS` | `SPR_LIT_PS` |
| D-pad bits | resolved direction (6.2) | `PART_DPAD_*` | one or two `SPR_LIT_DPAD_*` |
| `lx, ly` / `rx, ry` | sticks (`ViGEm.cpp:136-139`) | cap offset (8.3) | `SPR_LIT_STICK_*_CAP` at `UI_STICK_DEFLECT_ALPHA` when deflected |

Any other bit (headphone, volume, power; `psp2common/ctrl.h:52-55`) is masked out by `PAD_MIRROR_MASK`. USB DS4 and USB generic modes use the same table (their maps at `usbmode.c:73-81` name the same parts). The mirror assumes `VIGEM_SWAP_SHOULDERS=0` (assumed default, `UI-DECISIONS.md:60-61`).

### 6.2 D-pad resolution

The client and USB resolve impossible combinations (opposite directions, set by remapping) differently, and the mirror follows the active transport (`usb_connection() == CONNECTION_WIFI` selects the client rule).

- `MIRROR_DPAD_DS4_CLIENT` (`ViGEm.cpp:206-220`): `dir = none`; assign in order UP, RIGHT, DOWN, LEFT (last present wins); then in order NE (up and right), SE (right and down), SW (down and left), NW (left and up), last present wins.
- `MIRROR_DPAD_USB_HAT` (`usbmode.c:57-69`): first match in order NE, SE, SW, NW, UP, RIGHT, DOWN, LEFT.
- A diagonal lights its two arms (NE = up + right). None lights nothing.

### 6.3 Neutral state

While the remap menu is open the snapshot is `mirror_neutral`: buttons 0, sticks 128, so nothing is lit and both caps rest. The remap menu covers the screen anyway (`main.c:391-393`), so this only matters for the first frame after it closes.

---

## 7 Screen states, timing constants and transitions

### Decision E: one pure state machine driven by process time

**Decision.** `screen_update` owns the mode (SETUP or PLAY), the stats view, the hint timer and four fade levels, all from `sceKernelGetProcessTimeWide()` microseconds passed in by `ui_update`. The remap menu and manual screen-off stay in `main.c` and pause the machine.
**Rationale.** Frame counts drift whenever a frame is late (D7 already makes 60 fps a requirement, but the instrument in section 11 exists to catch the frames that are not); process time keeps every timeout honest and makes the machine testable with synthetic clocks.
**Rejected.** Frame-counted timers like the hold combos (`main.c:370-383`): those two stay frame-counted as required, nothing else does.

### 7.1 "PC connected"

- Wi-Fi: `wifi_link = connected` (`main.c:36`), set while `handle_client` runs (`main.c:150`, `main.c:166`). The client keeps TCP alive while streaming (`Client/main.cpp:1396-1402`).
- USB (both modes): `usb_link = (usb_state() == USB_STATE_CONNECTED)` (`usbmode.c:255-258`, `usbmode.h:17`).
- PC connected = `wifi_link || usb_link` (USB mode still runs the Wi-Fi server, so either path counts).
- PLAY holds while `now - last_wifi_us < UI_WIFI_LOST_GRACE_US` or `now - last_usb_us < UI_USB_LOST_GRACE_US`, where `last_*_us` is the last frame the link was up. The Wi-Fi grace exceeds the 3 s `CLIENT_TIMEOUT` (`main.c:27`) so one stalled poll never flashes the full body.

### 7.2 Timing constants

All in microseconds of process time. "Tune" marks values to revisit after hardware use.

| Constant | Default | Meaning |
|---|---|---|
| `UI_IDLE_TIMEOUT_US` | 30 000 000 | PLAY: no activity for this long and every layer fades to 0 (D9). Tune after capture 5. |
| `UI_IDLE_TIMEOUT_SETUP_US` | 120 000 000 | SETUP: same rule with a longer timeout, so the IP stays readable while the PC client is started. Tune after capture 5. |
| `UI_FADE_IN_US` | 150 000 | Time for a level to go 0 to 1000. |
| `UI_FADE_OUT_US` | 600 000 | Time for a level to go 1000 to 0. |
| `UI_HINT_SHOW_US` | 2 500 000 | Zone hint at full level after a trigger, then fades out (D12). |
| `UI_HINT_REARM_QUIET_US` | 30 000 000 | A new front contact after this long with no front contact re-shows the hint. |
| `UI_STATS_SHOW_US` | 5 000 000 | Summary visible after a tap, then fades (D16). |
| `UI_DIAG_SHOW_US` | 60 000 000 | Diagnostic view lifetime without another tap. |
| `UI_WIFI_LOST_GRACE_US` | 5 000 000 | Wi-Fi link down this long before SETUP returns (assumed default). |
| `UI_USB_LOST_GRACE_US` | 1 000 000 | USB link down this long before SETUP returns. |
| `UI_MAX_STEP_US` | 100 000 | Largest time step used for fades (after sleep or a stall). |
| `ZONE_TAP_MAX_US` | 300 000 | Longest stats tap. Tune after capture 2. |
| `FRAME_WINDOW_US` | 1 000 000 | Frame-time averaging window. |
| `FRAME_LATE_US` | 17 500 | Frame period counted as late. |

Unchanged frame-counted timers: `SCREEN_TOGGLE_FRAMES` 60 for both combos (`main.c:31`, `main.c:376`).

### 7.3 States and transitions

| State or overlay | Shown when | Entry | Exit |
|---|---|---|---|
| SETUP | no PC (after grace) | boot; PC lost for the grace time | PC connected (immediate) |
| PLAY | PC connected | PC link rises | link down for the grace time |
| Zone hint overlay | SETUP or PLAY, not in DIAG | SETUP to PLAY; or a new front contact after `UI_HINT_REARM_QUIET_US` without contact (also the very first contact after boot) | `UI_HINT_SHOW_US` after its trigger, then fade out |
| Stats summary | SETUP or PLAY | stats tap while hidden | `UI_STATS_SHOW_US` after the tap; or tap (goes to DIAG) |
| Diagnostic view (DIAG) | replaces the mirror and text | stats tap while summary shown | tap (hidden); or `UI_DIAG_SHOW_US` after entering |
| Remap menu | existing screen (`remap.c:166-210`) | L+R+START held 60 frames (`main.c:375-383`) | START in the menu or the combo again |
| Manual screen off | black frame, nothing drawn | L+R+SELECT held 60 frames (`main.c:370-372`) | same combo; opening the remap menu also clears it (`main.c:380`) |
| Idle | any state: levels fade to 0 | no activity for `UI_IDLE_TIMEOUT_US` (PLAY) or `UI_IDLE_TIMEOUT_SETUP_US` (SETUP) | any activity (fade in) |

Per-frame algorithm of `screen_update`:

1. `paused = remap_open || screen_off`. If paused: store `last_us = now`, keep levels and mode, set `s->paused = 1`, return. If it was paused last frame, treat this frame as activity (the user just closed the menu or turned the screen on).
2. `step = clamp(now - last_us, 0, UI_MAX_STEP_US)`; `last_us = now` (a clock that goes backwards gives step 0).
3. Link bookkeeping (7.1); compute the new mode. On SETUP to PLAY set `hint_until_us = now + UI_HINT_SHOW_US`.
4. Activity: if `in->activity`, `last_activity_us = now`. `awake = now - last_activity_us < timeout`, where `timeout` is `UI_IDLE_TIMEOUT_SETUP_US` in SETUP and `UI_IDLE_TIMEOUT_US` in PLAY (the mode computed in step 3). A SETUP to PLAY or PLAY to SETUP switch also sets `last_activity_us = now`, so the new screen always starts awake.
5. Front: if `new_contacts > 0` and (`!front_seen` or `now - last_front_us >= UI_HINT_REARM_QUIET_US`) set `hint_until_us = now + UI_HINT_SHOW_US`. If `front_count > 0`: `last_front_us = now`, `front_seen = 1`.
6. Stats: on `stats_tap` cycle HIDDEN to SUMMARY (`stats_until_us = now + UI_STATS_SHOW_US`) to DIAG (`now + UI_DIAG_SHOW_US`) to HIDDEN. If `stats != HIDDEN && now >= stats_until_us`, set HIDDEN. Whenever `stats != HIDDEN`, `stats_drawn = stats`.
7. Targets: `body = (mode == SETUP && awake && stats != DIAG)`, `lit = (awake && stats != DIAG)`, `hint = (now < hint_until_us && stats != DIAG)`, `stats_level = (stats != HIDDEN)`, each as 0 or 1000.
8. Move each level toward its target by `ceil(step * 1000 / UI_FADE_IN_US)` up or `ceil(step * 1000 / UI_FADE_OUT_US)` down, clamped to 0..1000.

Boot: `screen_init` sets SETUP, all levels 0, `last_activity_us = now` (so SETUP fades in over `UI_FADE_IN_US`).

### 7.4 Activity

`screen_activity(ref, snap, new_contacts)` returns 1 when any of these holds, then stores the snapshot in `ref`:

- `ref` is not valid yet (first frame).
- `(snap->buttons & PAD_MIRROR_MASK) != ref->buttons`: any press or release reaching the PC, zone presses included. A held button is not activity, so a part held for `UI_IDLE_TIMEOUT_US` fades out (burn-in guard for held triggers).
- Any stick axis differs from `ref->sticks` by `>= UI_STICK_ACTIVITY_DELTA`. The reference only moves when activity fires, so slow rest noise below the delta never accumulates into activity.
- `new_contacts > 0` on the front panel.

Not activity: motion sensors, rear touch, held unchanged front contacts, frame count, link changes (a link change switches mode, and the hint covers the user's attention). Remap close and screen-on count through step 1 above.

---

## 8 Sprites and visual language

### Decision F: opaque lit silhouettes, L2 is the existing sliver

**Decision.** A lit sprite is an opaque shape in a dark blue-grey fill with a lighter edge (face buttons keep their symbol ink), so it reads the same alone on black and on top of the body. In the front view the existing `#trigger` sliver is L2; L1 is a new lit-only band traced along the shell's shoulder edge just below it. A stick cap translates up to 12 pixels with the stick; deflection shows the cap at reduced alpha, a click adds a ring around the well and shows the cap at full alpha.
**Rationale.** Physically the bumper sits in front of and below the trigger, so the higher, rearward sliver is the trigger; drawing L1 only when lit leaves the reviewed artwork untouched. Opaque fills avoid the double-exposure look that translucent glows give over a textured body, and the low-luminance palette keeps the lit area dim on the OLED.
**Rejected.** Splitting each existing sliver into left and right halves for L1 and L2 (no physical meaning), and additive glows (`vita2d_set_blend_mode_add`), which brighten whatever is under them and read differently over black and over the body.

### 8.1 Sprite list

Enum order is this table's order (`SPR_*` index 0 to 21). "Use": S = SETUP base layer, P = PLAY, B = both (lit layer, drawn in SETUP over the body and in PLAY over black).

| # | Sprite id | Enum | Depicts | Use |
|---|---|---|---|---|
| 0 | `spr-body` | `SPR_BODY` | Whole controller without the two stick caps: L2 slivers, shell, touchpad, D-pad, face buttons, Share/Options, stick wells, speaker, PS button, connector edge | S |
| 1 | `spr-stick-l-cap` | `SPR_STICK_L_CAP` | Left stick cap (unlit), at rest | S |
| 2 | `spr-stick-r-cap` | `SPR_STICK_R_CAP` | Right stick cap (unlit), at rest | S |
| 3 | `spr-lit-l2` | `SPR_LIT_L2` | Left trigger sliver, lit | B |
| 4 | `spr-lit-r2` | `SPR_LIT_R2` | Right trigger sliver, lit | B |
| 5 | `spr-lit-l1` | `SPR_LIT_L1` | Left bumper band along the shell shoulder edge | B |
| 6 | `spr-lit-r1` | `SPR_LIT_R1` | Right bumper band | B |
| 7 | `spr-lit-share` | `SPR_LIT_SHARE` | Share button face | B |
| 8 | `spr-lit-options` | `SPR_LIT_OPTIONS` | Options button face | B |
| 9 | `spr-lit-dpad-up` | `SPR_LIT_DPAD_UP` | D-pad up arm | B |
| 10 | `spr-lit-dpad-right` | `SPR_LIT_DPAD_RIGHT` | D-pad right arm | B |
| 11 | `spr-lit-dpad-down` | `SPR_LIT_DPAD_DOWN` | D-pad down arm | B |
| 12 | `spr-lit-dpad-left` | `SPR_LIT_DPAD_LEFT` | D-pad left arm | B |
| 13 | `spr-lit-triangle` | `SPR_LIT_TRIANGLE` | Triangle button with its symbol | B |
| 14 | `spr-lit-circle` | `SPR_LIT_CIRCLE` | Circle button with its symbol | B |
| 15 | `spr-lit-cross` | `SPR_LIT_CROSS` | Cross button with its symbol | B |
| 16 | `spr-lit-square` | `SPR_LIT_SQUARE` | Square button with its symbol | B |
| 17 | `spr-lit-ps` | `SPR_LIT_PS` | Lit ring plus the unchanged PS button artwork | B |
| 18 | `spr-lit-stick-l-ring` | `SPR_LIT_STICK_L_RING` | Ring around the left well (L3 click) | B |
| 19 | `spr-lit-stick-r-ring` | `SPR_LIT_STICK_R_RING` | Ring around the right well (R3 click) | B |
| 20 | `spr-lit-stick-l-cap` | `SPR_LIT_STICK_L_CAP` | Left cap, lit, at rest position | B |
| 21 | `spr-lit-stick-r-cap` | `SPR_LIT_STICK_R_CAP` | Right cap, lit, at rest position | B |

Part to sprite: `PART_L2` 3, `PART_R2` 4, `PART_L1` 5, `PART_R1` 6, `PART_SHARE` 7, `PART_OPTIONS` 8, `PART_DPAD_UP..LEFT` 9-12, `PART_TRIANGLE..SQUARE` 13-16, `PART_PS` 17, `PART_L3` 18 + 20, `PART_R3` 19 + 21.

### 8.2 Colours

| Name | Value | Used for |
|---|---|---|
| `LIT_FILL` | `#22324A` | Fill of every lit shape |
| `LIT_EDGE` | `#6F93C8` | Edge stroke of lit shapes, L1/R1 bands, rings |
| Triangle ink | `#62BDB3` | Symbol stroke on `spr-lit-triangle` (darker stop of `triangle-ink`) |
| Circle ink | `#D28385` | `spr-lit-circle` |
| Cross ink | `#89A9DB` | `spr-lit-cross` |
| Square ink | `#BD8DBB` | `spr-lit-square` |
| `UI_COL_VALUE` | `UI_RGBA(0xC3, 0xCA, 0xD6, a)` | Text values (IP, numbers) |
| `UI_COL_LABEL` | `UI_RGBA(0x7D, 0x86, 0x96, a)` | Text labels, credits |
| `UI_COL_TITLE` | `UI_RGBA(0x56, 0x60, 0x70, a)` | SETUP title |
| `UI_COL_HINT_LINE` | `UI_RGBA(0x3A, 0x4A, 0x62, a)` | Zone outlines |
| `UI_COL_HINT_LABEL` | `UI_RGBA(0x6F, 0x93, 0xC8, a)` | Zone labels |

Rules: no pure white anywhere in the new UI; sprites are drawn with tint `UI_RGBA(0xFF, 0xFF, 0xFF, a)` where `a = level * 255 / 1000` (times `UI_STICK_DEFLECT_ALPHA / 255` for a deflected, unclicked cap). Brightness budget, checked by the export script: the sum of opaque pixel areas of sprites 3 to 21 must stay at or below 8% of 960x544 (41 779 pixels); the script prints each lit sprite's mean relative luminance for review.

### 8.3 Stick caps

- `mirror_stick_offset(v)`: `d = v - 128`; `off = (d * 12 + (d > 0 ? 63 : -63)) / 127` with C truncation, clamped to [-12, 12]. So 0 maps to -12, 128 to 0, 255 to +12. Screen y grows downward and `ly = 0` is up, so the same formula applies to both axes.
- Deflected: `|lx - 128| > MIRROR_STICK_DEADBAND || |ly - 128| > MIRROR_STICK_DEADBAND`. Inside the dead band both offsets are 0 and the cap counts as at rest (no jump at the dead band edge because offsets use the raw values once outside).
- Positions are whole pixels: a cap sprite is drawn at `(sx + dx, sy + dy)` of its own rectangle. SETUP draws the base cap there (the well stays in the body sprite); both modes draw the lit cap there when deflected or clicked.
- 12 px keeps the 45.5 px cap radius inside the 70 px housing (`analog-stick` ellipse `rx 60.6` units = 70.1 px) and clear of the PS button (section 9.2 coordinates).

| Stick condition | PLAY | SETUP |
|---|---|---|
| at rest, not clicked | nothing | base cap at rest |
| deflected, not clicked | lit cap at offset, alpha x 160/255 | base cap + lit cap at offset, alpha x 160/255 |
| clicked (L3/R3), any deflection | ring + lit cap at offset, full alpha | base cap + ring + lit cap at offset, full alpha |

---

## 9 Asset contract (SVG ids, render geometry, atlas, `sprites.h`)

### Decision G: full-canvas renders cropped to integer rectangles

**Decision.** Every sprite is rendered alone onto a full 960x544 canvas whose viewBox centres the controller exactly, then cropped to its alpha bounding box. The crop's top-left is the sprite's screen position.
**Rationale.** Drawing each crop unscaled at its integer position reproduces the full-canvas render pixel for pixel, so composites are exact, there is never a half-pixel offset, and no positions are typed by hand.
**Rejected.** Rendering at height 544 with the original viewBox (857 px wide, centred at x = 51.5) and per-sprite manual anchors: half-pixel centring and hand-maintained coordinates.

### 9.1 SVG extension rules

Source of truth: `Server/assets/ui/dualshock-4.svg`, a copy of `~/Documents/Codex/2026-11-12/generate-a-svg-of-a-playstation/outputs/dualshock-4.svg` extended as follows. Existing shapes, gradients, colours and the logo are not changed; the root keeps `width="1480" height="940" viewBox="130 50 740 470"`, `<title>`, `<desc>` and comments.

1. **Top level.** Allowed top-level elements: `title`, `desc`, `defs`, and `g` elements whose `id` starts with `spr-`. Every drawable element lives inside exactly one `spr-` group. No `spr-` group nests another. Lit groups carry `display="none"` so the file still previews as the plain controller.
2. **Shared art in `<defs>`.** Anything used by more than one `spr-` group must be in `<defs>`, because the exporter deletes the other `spr-` groups before rendering one.
3. **Name the unnamed uses** (ids are plain, not `spr-`): in `shoulder-buttons`, the untransformed `use` becomes `id="trigger-l2"`, the mirrored one `id="trigger-r2"`. In `directional-pad`, the four `use` of `#dpad-key` become `dpad-up` (no rotation), `dpad-right` (`rotate(90)`), `dpad-down` (`rotate(180)`), `dpad-left` (`rotate(270)`). In `share-and-options`, `share-button` (x 362.5) and `options-button` (x 637.5).
4. **Split the stick.** Replace `#analog-stick` in `<defs>` with `#analog-stick-well` (its `ellipse` cx 1 shadow, the housing `ellipse`, the grain `ellipse`, the seat `circle` r 42.35) and `#analog-stick-cap` (the circles r 39.3, 37.8, 37.2, 35.55, 29.05, 28.5 and the two highlight paths), children copied verbatim in their original order. In `analog-sticks`, the two uses reference `#analog-stick-well` and get ids `stick-l-well`, `stick-r-well`.
5. **PS button into defs.** Move the children of `playstation-button` verbatim into `<defs>` as `<g id="ps-button-art">`. The `playstation-button` group keeps its id and `transform="translate(500 295)"` and contains only `<use xlink:href="#ps-button-art"/>`.
6. **Wrap the body.** Wrap `shoulder-buttons`, `body`, `touchpad`, `directional-pad`, `action-buttons`, `share-and-options`, `analog-sticks`, `speaker`, `playstation-button`, `connector-edge`, in this existing order, in `<g id="spr-body">`.
7. **Add the caps** after `spr-body`:

```xml
<g id="spr-stick-l-cap" clip-path="url(#shell-clip)"><use xlink:href="#analog-stick-cap" transform="translate(392.8 291.3)"/></g>
<g id="spr-stick-r-cap" clip-path="url(#shell-clip)"><use xlink:href="#analog-stick-cap" transform="translate(607.2 291.3)"/></g>
```

Moving the caps above the speaker, PS button and connector edge changes nothing at rest: the caps span x 353.5 to 432.1 and 567.9 to 646.5, the PS button x 479 to 521, the speaker x 472.5 to 527.5 (user units).

8. **Add the lit groups** after the caps, in table order, each with `display="none"`. The markup below is the baseline: it fixes each lit sprite's id, position and footprint. The artist extending the SVG may make a lit sprite richer than this baseline (for example reuse the part's own gradients and highlights with a lit rim, so it matches the photorealistic body) as long as: the id and order are unchanged; the sprite stays inside the footprint of the part it depicts plus at most 4 px of rim on the 960x544 canvas; it reads clearly both alone on black and over the body; the lit area budget of 8.2 holds; no filter, font, embedded raster or blend mode is introduced (gradients, patterns, masks and clip paths are allowed, as in the source file). `LIT_FILL` and `LIT_EDGE` remain the reference palette for fills and rims.

```xml
<g id="spr-lit-l2" display="none"><path d="M252.5 96 L253.6 86.6 C254.1 81.9 274.4 75.4 291.6 75.4 C309.5 74.1 329.5 79 336 84.2 C338.4 86.2 338.9 91.6 340 96Z" fill="#22324A" stroke="#6F93C8" stroke-width="1.35"/></g>
<g id="spr-lit-r2" display="none" transform="translate(1000 0) scale(-1 1)"><path d="(same as spr-lit-l2)" fill="#22324A" stroke="#6F93C8" stroke-width="1.35"/></g>
<g id="spr-lit-l1" display="none"><path d="M250 92 C275 88.4 320 87.5 337 90 C347 91.5 347.5 96 357 97.3" fill="none" stroke="#6F93C8" stroke-width="4" stroke-linecap="round"/></g>
<g id="spr-lit-r1" display="none" transform="translate(1000 0) scale(-1 1)"><path d="(same as spr-lit-l1)" fill="none" stroke="#6F93C8" stroke-width="4" stroke-linecap="round"/></g>
<g id="spr-lit-share" display="none"><rect x="-9.55" y="-18.65" width="19.1" height="34.6" rx="9.25" transform="translate(362.5 132.6) rotate(-1.9)" fill="#22324A" stroke="#6F93C8" stroke-width="1"/></g>
<g id="spr-lit-options" display="none"><rect x="-9.55" y="-18.65" width="19.1" height="34.6" rx="9.25" transform="translate(637.5 132.6) rotate(1.9)" fill="#22324A" stroke="#6F93C8" stroke-width="1"/></g>
<g id="spr-lit-dpad-up" display="none"><use xlink:href="#dpad-key-face" transform="translate(291 199.6)" fill="#22324A" stroke="#6F93C8" stroke-width="1.12"/></g>
<!-- dpad-right / -down / -left: same with "translate(291 199.6) rotate(90)", "rotate(180)", "rotate(270)" -->
<g id="spr-lit-triangle" display="none" transform="translate(709 150.3)"><circle r="21" fill="#22324A" stroke="#6F93C8" stroke-width="1"/><path d="M0-14.1 L14 10.9 H-14Z" fill="none" stroke="#62BDB3" stroke-width="2.45" stroke-linejoin="round"/></g>
<g id="spr-lit-circle" display="none" transform="translate(758.3 199.6)"><circle r="21" fill="#22324A" stroke="#6F93C8" stroke-width="1"/><circle r="14.18" fill="none" stroke="#D28385" stroke-width="2.45"/></g>
<g id="spr-lit-cross" display="none" transform="translate(709 248.9)"><circle r="21" fill="#22324A" stroke="#6F93C8" stroke-width="1"/><path d="M-11.7-11.7L11.7 11.7 M11.7-11.7L-11.7 11.7" fill="none" stroke="#89A9DB" stroke-width="2.45" stroke-linecap="round"/></g>
<g id="spr-lit-square" display="none" transform="translate(659.7 199.6)"><circle r="21" fill="#22324A" stroke="#6F93C8" stroke-width="1"/><rect x="-11.6" y="-11.6" width="23.2" height="23.2" rx=".65" fill="none" stroke="#BD8DBB" stroke-width="2.45"/></g>
<g id="spr-lit-ps" display="none" transform="translate(500 295)"><circle r="21" fill="none" stroke="#6F93C8" stroke-width="2.4"/><use xlink:href="#ps-button-art"/></g>
<g id="spr-lit-stick-l-ring" display="none" transform="translate(392.8 291.3)"><circle cy="-.1" r="45.5" fill="none" stroke="#6F93C8" stroke-width="3"/></g>
<g id="spr-lit-stick-r-ring" display="none" transform="translate(607.2 291.3)"><circle cy="-.1" r="45.5" fill="none" stroke="#6F93C8" stroke-width="3"/></g>
<g id="spr-lit-stick-l-cap" display="none" transform="translate(392.8 291.3)"><circle cy="-1.7" r="37.8" fill="#22324A" stroke="#6F93C8" stroke-width="1.1"/><circle cy="-2.2" r="29.05" fill="none" stroke="#6F93C8" stroke-opacity=".6" stroke-width="1"/></g>
<g id="spr-lit-stick-r-cap" display="none" transform="translate(607.2 291.3)"><!-- same children as spr-lit-stick-l-cap --></g>
```

The face-button centres come from `action-buttons` `translate(709 199.6)` plus each button's offset of 49.3 units (`dualshock-4.svg:526-552`).

### 9.2 Render geometry

Scale `s = 544 / 470 = 1.157446809`. The canvas viewBox keeps the original height and the controller's symmetry axis x = 500 (every mirrored element uses `translate(1000 0) scale(-1 1)`):

- Canvas: `width="960" height="544" viewBox="85.29411765 50 829.41176471 470"` (width `960 / s`, left `500 - 480 / s`).
- User unit (x, y) to screen pixel: `X = (x - 85.29411765) * s`, `Y = (y - 50) * s`. Unit x = 500 lands on pixel x = 480.
- Measured with resvg on this canvas: controller alpha bounding box x 93 to 867, y 28 to 513 (774 x 485), 93 px free on each side.

Reference screen coordinates (pixels) used by the layout and tests:

| Item | Screen |
|---|---|
| Touchpad face | x 355.6 to 604.4, y 79.9 to 194.5 |
| D-pad centre / action centre | (238.0, 173.2) / (721.9, 173.2) |
| Stick centres | (355.9, 279.3) and (604.1, 279.3); cap radius 45.5 |
| PS button | centre (480.0, 283.6), radius 24.3 |
| L2 slivers | x 193.5 to 294.8 and 665.2 to 766.5, y 28.0 to 53.2 |
| Bridge lower edge | y 337.7 to 358.4 between x 297 and 663 |

### 9.3 Export pipeline and atlas (`tools/export_sprites.py`)

Requirements: Python 3, Pillow, the `resvg` CLI on `PATH` (no other renderer; the decision record measured a 1.4% difference between renderers). Steps:

1. Parse the SVG with `xml.etree.ElementTree`. Validate rule 9.1.1, the exact required set and order of `spr-` ids (the table in 8.1, embedded in the script as `SPRITES`), and that no id is missing or extra. Exit 1 with a message on any violation.
2. For each sprite: copy the tree, delete every other top-level `spr-` group, remove `display` from the target, set the canvas root attributes of 9.2, write a temporary file, render with `resvg tmp.svg tmp.png`.
3. Crop to the bounding box of pixels with alpha > 0. Set RGB to 0 where alpha is 0. An empty render is an error. `sx, sy` = crop top-left on the canvas.
4. Pack: sort by height descending, then width descending, then id; shelf-pack left to right into width 1024 with 2 px transparent padding on every side of every sprite (4 px between neighbours); atlas height = packed height rounded up to a multiple of 8; error if it exceeds 1024. The generated atlas is 1024 x 616.
5. Write `Server/assets/ui/atlas.png` (RGBA8, straight alpha, non-interlaced), `Server/source/sprites.h` (9.4) and `Server/assets/ui/sprites.json` (`{"atlas": [w, h], "canvas": [960, 544], "sprites": [{"id", "enum", "ax", "ay", "w", "h", "sx", "sy"}]}`).
6. Checks printed and enforced: lit area budget (8.2); composite check: `spr-body` + both base caps composited at their positions must equal the original, unmodified SVG rendered on the same canvas within 2 levels per channel (proves 9.1 changed nothing visible); atlas bytes `w * h * 4`.
7. `--check`: regenerate into a temporary directory and compare `sprites.h` and `sprites.json` byte for byte and `atlas.png` pixel for pixel with the committed files; exit 1 on difference.

Filtering: the atlas is drawn only at integer positions and scale 1, so `ui_init` sets `SCE_GXM_TEXTURE_FILTER_POINT` for both min and mag; the padding still protects against bleeding if linear filtering is ever enabled.

### 9.4 Generated `sprites.h`

Exact format (numbers illustrative):

```c
// Generated by tools/export_sprites.py from Server/assets/ui/dualshock-4.svg. Do not edit.
// resvg <version>, canvas 960x544, viewBox 85.29411765 50 829.41176471 470
#pragma once
#include <stdint.h>

#define SPRITES_ATLAS_PATH "app0:ui/atlas.png"
#define SPRITES_ATLAS_W 1024
#define SPRITES_ATLAS_H 616
#define SPRITES_CANVAS_W 960
#define SPRITES_CANVAS_H 544
#define SPRITES_PAD 2

typedef struct {
	uint16_t ax, ay; // top-left in the atlas
	uint16_t w, h;   // size in pixels
	int16_t sx, sy;  // top-left on the 960x544 screen (rest position for stick caps)
} SpriteRect;

typedef enum {
	SPR_BODY = 0,
	SPR_STICK_L_CAP,
	/* ... one per row of section 8.1, in that order ... */
	SPR_LIT_STICK_R_CAP,
	SPR_COUNT
} SpriteId;

extern const SpriteRect sprite_rects[SPR_COUNT];
extern const char *const sprite_names[SPR_COUNT];

#ifdef SPRITES_IMPLEMENTATION
const SpriteRect sprite_rects[SPR_COUNT] = {
	[SPR_BODY] = { 2, 2, 774, 485, 93, 28 },
	/* ... */
};
const char *const sprite_names[SPR_COUNT] = {
	[SPR_BODY] = "spr-body",
	/* ... */
};
#endif
```

Enum names: `SPR_` plus the id without `spr-`, upper-cased, `-` replaced by `_`. The arrays are behind `SPRITES_IMPLEMENTATION` because GCC's `-Wall` warns on unused `static const` variables in C, which header-defined static arrays would trigger in every other unit.

### 9.5 Locations

| Item | Repo path | VPK / device path |
|---|---|---|
| Extended SVG | `Server/assets/ui/dualshock-4.svg` | not packed |
| Atlas | `Server/assets/ui/atlas.png` | `app0:ui/atlas.png` |
| Sprite data | `Server/source/sprites.h`, `Server/assets/ui/sprites.json` | compiled in |
| Zones file | none | `ux0:data/VitaPadNG/zones.txt` (optional) |

CMake line added inside `vita_create_vpk` (paths are relative to `Server/`, like `Server/CMakeLists.txt:50-53`):

```cmake
  FILE assets/ui/atlas.png ui/atlas.png
```

The Vita build never runs Python or resvg; generated files are committed.

### 9.6 LiveArea assets (`tools/export_livearea.py`)

The fork installs beside the stock app (D18), so its bubble must look different. `tools/export_livearea.py` renders the unmodified controller (the `spr-body` group plus both base caps of `Server/assets/ui/dualshock-4.svg`, lit groups hidden) with resvg and writes three files over the existing ones, which `Server/CMakeLists.txt:50-52` already packs:

| File | Size | Content |
|---|---|---|
| `Server/build/sce_sys/icon0.png` | 128x128 | Controller centred on a `#0B0E14` rounded square, about 8 px margin |
| `Server/build/sce_sys/livearea/contents/bg.png` | 840x500 | Controller centred on `#0B0E14`, about 60% of the width |
| `Server/build/sce_sys/livearea/contents/startup.png` | 280x158 | `#0B0E14` with the controller at the left and nothing else (no text: fonts are not reproducible) |

Every file must be an 8-bit indexed PNG (IHDR colour type 3), non-interlaced, exactly the size above, and at most 420 KB; quantise with Pillow `Image.quantize(256, method=Image.Quantize.FASTOCTREE)` after flattening onto the background colour. The script verifies those properties after writing and exits 1 on any violation. `template.xml` is not changed. `--check` regenerates into a temporary directory and compares pixel for pixel.

---

## 10 Rendering rules and budgets

### Decision H: one draw list, per-item alpha, no overlay quads

**Decision.** Every fade is applied as the alpha of each item's tint or text colour; nothing is ever drawn just to darken. Items whose alpha computes to 0 are omitted from the list. The clear colour stays black (`main.c:325`).
**Rationale.** Over a black clear, scaling each item's alpha is exactly a fade to black and costs no extra draw call or fill; a full-screen blended quad would touch all 522 240 pixels every frame.
**Rejected.** A full-screen black quad with varying alpha (forbidden in PLAY; also needless elsewhere).

### 10.1 Draw order

`uiplan_build` emits, in this order (each step only when its alpha > 0):

DIAG (`stats_drawn == STATS_DIAG` and `stats_level > 0`, including its fade-out): diagnostic text only (section 11.3), alpha from `stats_level`, and nothing else. The mirror reappears once the diagnostic text has faded out.

SETUP and PLAY:

1. `SPR_BODY` at its position, alpha `body`.
2. `SPR_STICK_L_CAP`, `SPR_STICK_R_CAP` at rest plus offsets, alpha `body`.
3. Lit sprites in `PART_*` order (L2, R2, L1, R1, Share, Options, D-pad up, right, down, left, triangle, circle, cross, square, PS, then L ring, R ring, then L cap, R cap), alpha `lit` (times 160/255 for a deflected unclicked cap). In PLAY skipped entirely when `Mirror=Off`.
4. SETUP text (section 12), alpha `body`; the info block uses `body * (1000 - stats_level) / 1000` so the summary replaces it.
5. Zone hint, alpha `hint`: for each zone, a rect as 4 `DRAW_LINE` from `(a/2, b/2)` to `(c/2 - 1, d/2 - 1)`; an ellipse as 24 line segments around `(a/2, b/2)` with radii `(c/2, d/2)`; then a label at the zone's centre (rect centre, or ellipse centre) for every non-`None` zone: `L1`, `R1`, `L3`, `R3`, or `stats`, scale 0.9, centred, `UI_COL_HINT_LABEL`.
6. Stats summary (`stats_drawn == STATS_SUMMARY`), alpha `stats_level` (section 11.2).

Remap menu and screen-off frames are drawn by `main.c` as today; `ui_draw` is not called.

Executor (`ui.c`): `DRAW_SPRITE` uses `vita2d_draw_texture_tint_part(atlas, x, y, ax, ay, w, h, color)`; `DRAW_LINE` uses `vita2d_draw_line`; `DRAW_TEXT` uses `vita2d_pgf_draw_text(font, x', y, color, scale', text)` where for `ALIGN_CENTER` `x' = x - width / 2` with `width = vita2d_pgf_text_width(font, scale', text)`, and when `x1 > 0` and the width exceeds `x1` the scale is reduced in steps of 0.05 down to 0.7 until it fits. If the atlas failed to load, `art_loaded = 0` and only text and lines are emitted.

### 10.2 Draw-call budget

Counted with `uiplan_cost` (1 per sprite, 1 per line, 1 per non-space glyph; the clear is not counted). Built-in zone table.

| Frame | Sprites | Lines | Glyphs (max) | Total max |
|---|---|---|---|---|
| PLAY at rest | 0 | 0 | 0 | 0 |
| PLAY, everything pressed, both sticks clicked | 19 | 0 | 0 | 19 |
| PLAY + hint | 19 | 16 + 24 | 13 | 72 |
| PLAY + summary | 19 | 0 | 4 x 40 | 179 |
| SETUP, nothing pressed | 3 | 0 | 14 + 3 x 30 + 5 x 40 | 307 |
| SETUP worst (all pressed, hint) | 22 | 40 | 304 + 13 | 379 |
| DIAG | 0 | 0 | about 1 000 | <= 1 200 |

Limits enforced by tests: `UI_DRAW_BUDGET` (500) for every non-diagnostic frame with the built-in table; `UI_DRAW_BUDGET_DIAG` (1 200) for the diagnostic view and for any frame with 16 ellipses in `zones.txt` (16 x 24 lines + labels). Today's main screen issues roughly 1 100 glyph draws per frame (`main.c:396-432`), so every new frame costs at most what v1.9.2 already draws.

### 10.3 Video memory

| Item | Size |
|---|---|
| Atlas, as generated: 1024 x 616 RGBA8 | 2 523 136 bytes (2.4 MiB) |
| Atlas, hard maximum 1024 x 1024 | 4 194 304 bytes (4 MiB) |
| PGF font | unchanged from today |

No render targets, no extra textures. The atlas is loaded once in `ui_init` and never freed.

---

## 11 Stats and frame-time instrument

### Decision I: summary on a centre tap, diagnostic on a second tap

**Decision.** A tap in the stats zone shows a four-line health summary for 5 s; a second tap while it is visible switches to a full-screen diagnostic view that keeps today's readout plus frame time and the credits; a third tap closes it. Values come only from `input_rate_get` (`inputrate.c:65-67`), `fast_stats` (`fastinput.c:42-46`), `fast_enabled`, the battery read at `main.c:360`, the link flags and the new frame-time figure.
**Rationale.** In Zones mode a tap in the stats zone is not a button and sends no touch point, so it never reaches the PC (D16); in `FrontTouch=Forward` on Wi-Fi the same touch is also forwarded to the PC, and the zone hint is never shown there, and the diagnostic view keeps the v1.9.2 numbers for A/B comparison (D20).
**Rejected.** A long press for the diagnostic view: harder to discover and it conflicts with resting thumbs.

`ui_update` calls `fast_stats()` and `input_rate_get()` only on frames where the stats level is above 0, so the kernel call is not made every frame otherwise.

### 11.1 Frame-time instrument

- Measured: frame period = time between two consecutive `ui_frame_swapped()` calls; work time = `ui_frame_rendered()` minus the previous `ui_frame_swapped()` (CPU work plus waiting for the GPU, before the vblank wait). Their difference shows whether `vita2d_swap_buffers` blocks on vblank.
- Window: `FRAME_WINDOW_US` (1 s) of process time. At the end of each window: average and maximum period, average and maximum work, and the number of periods above `FRAME_LATE_US`. `valid` becomes 1 after the first full window.
- Cost: two `sceKernelGetProcessTimeWide()` calls and a few integer additions per frame, on the main thread only.
- The instrument runs in every state, including remap menu and screen off, because `ui_frame_*` are called around the swap for every frame.

### 11.2 Summary wording (exact, ASCII only)

Drawn centred at x = 480 in the info block (section 12), baselines 388, 410, 432, 454, scale 0.85, `UI_COL_VALUE`, max width 340.

| Line | Condition | Text |
|---|---|---|
| 1 | `rates.streamed > 0` | `Stream: %d pkt/s` |
| 1 | else `wifi_link` | `Polls: %d/s, %d with new data` (`rates.polls`, `rates.fresh`) |
| 1 | else `usb_link` | `USB: connected` |
| 1 | else | `PC: not connected` |
| 2 | `!fast_ok` | `Fast buttons: unavailable` |
| 2 | else `!fast_enabled` | `Fast buttons: off` |
| 2 | else `!fast.available` | `Fast buttons: unavailable` |
| 2 | else `!fast.running \|\| fast.learned < 12` | `Fast buttons: starting` |
| 2 | else | `Fast buttons: %.1f ms sooner, %d fallbacks` (`fast.avg_us / 1000.0f`, `fast.fallbacks`) |
| 3 | `!valid` | `Frame: measuring` |
| 3 | else | `Frame: %.1f ms, max %.1f, %d late` (`avg_period_us / 1000.0f`, `peak_period_us / 1000.0f`, `late_frames`) |
| 4 | always | `Battery: %d%%` |

The fast-button conditions follow the order of `main.c:426-432`.

### 11.3 Diagnostic view

Left-aligned at x = 16, baselines `28 + 21 * i` for i = 0 to 17, scale 0.85. Lines 0 to 7 in `UI_COL_VALUE`, credits in `UI_COL_LABEL`.

| i | Text |
|---|---|
| 0 | `VitaPad NG 2.0 diagnostics - tap the centre to close` |
| 1 | `Input updates/s: buttons & sticks %d, touch %d, motion %d` (`main.c:422`) |
| 2 | `Streaming to the PC: %d packets/s (UDP)` or `PC polls/s: %d (%d with new button & stick data)` (`main.c:423-424`), else `USB: connected`, else `PC: not connected` |
| 3 | The four fast-button strings of `main.c:428-432`, verbatim; `Fast buttons: need Unsafe Homebrew` when `!fast_ok` |
| 4 | `Frame: %.2f ms avg, %.2f ms max, work %.2f ms avg, %.2f ms max, %d late` or `Frame: measuring` |
| 5 | `Battery: %d%%` |
| 6 | `Front touch: %d zones from built-in defaults` / `from zones.txt` / `from built-in defaults (zones.txt is invalid)`; or `Front touch: forwarded to the PC (FrontTouch=Forward)` |
| 7 | `Mirror: on` or `Mirror: off (Mirror=Off)`, followed by ` - controller art missing` when `!art_loaded` |
| 8 | empty |
| 9-17 | The credit strings of `main.c:396` and `main.c:412-419`, verbatim |

---

## 12 SETUP screen layout

### Decision J: PGF system font, text inside the controller's flat areas

**Decision.** Use the PGF system font already loaded (`main.c:326`) and place text on the touchpad face, in the strip above the touchpad, and in the free area between the grips under the bridge.
**Rationale.** No new asset, glyph cache or font licence, and the look matches the kept remap menu. The three regions are flat and dark in the artwork, so text stays legible on the body and leaves the controller silhouette intact.
**Rejected.** A bundled TTF through the linked freetype (`Server/CMakeLists.txt:40`): nicer typography, but it adds a file to the VPK, a glyph texture, load time, and a licence question for the font.

### 12.1 Regions (960x544 pixels, x, y, w, h)

| Region | Rect | Why it is free |
|---|---|---|
| Controller bounding box | (93, 28, 774, 485) | measured, 9.2 |
| `R_TITLE` | (300, 4, 360, 24) | above the shell; the L2 slivers start at y 28 outside x 300 to 660 |
| `R_CONN` | (364, 86, 232, 104) | inside the touchpad face (x 355.6 to 604.4, y 79.9 to 194.5) |
| `R_INFO` | (310, 366, 340, 170) | under the bridge (lowest at y 358.4); the grips' inner edges are at x <= 296 and x >= 664 for y >= 366 |

All text is centred at x = 480.

### 12.2 Lines and wording

`R_TITLE`, baseline 22, scale 0.8, `UI_COL_TITLE`: `VitaPad NG 2.0`.

`R_CONN`, max width 224:

| Case | Line 1 (baseline 120, scale 1.2, `UI_COL_VALUE`) | Line 2 (baseline 146, scale 0.85, `UI_COL_LABEL`) | Line 3 (baseline 168, scale 0.85, `UI_COL_LABEL`) |
|---|---|---|---|
| Wi-Fi, has IP | `%s:%d` (IP, `GAMEPAD_PORT`) | `Waiting for the PC client` | none |
| Wi-Fi, no IP | `No Wi-Fi` | `Waiting for a Wi-Fi connection` | none |
| USB DS4 or generic, `USB_STATE_WAITING` | `USB: DualShock 4` or `USB: standard gamepad` | `Plug the Vita into a computer` | `Wi-Fi: %s:%d` if it has an IP |
| USB, `USB_STATE_OFF`, PS module available | same as above | `Starting USB mode...` | same |
| USB selected, PS module unavailable | same as above | `USB needs Unsafe Homebrew` | same |

`R_INFO`, baselines 388, 410, 432, 454, 476, scale 0.85, `UI_COL_LABEL`, max width 340:

| Line | Text |
|---|---|
| 1 | `PS button: double-tap for the LiveArea` / `PS button: hold for the LiveArea` / `PS button: normal` / `PS button: needs Unsafe Homebrew` (from `ps_available()`, `ps_mode()`) |
| 2 | `No buttons remapped` / `1 button remapped` / `%d buttons remapped` (`remap_changed_count()`) |
| 3 | `Hold L + R + SELECT 1 s: screen off` |
| 4 | `Hold L + R + START 1 s: remap menu` |
| 5 | `Tap the centre: stats` |

Dropped from the main screen: the status line (`main.c:399`, now shown by the mode itself), the input-rate and fast-button lines (`main.c:420-433`, now in the summary and diagnostic view) and the credits (`main.c:396`, `main.c:412-419`, now in the diagnostic view).

---

## 13 Identity and settings

### Decision K: `VPNG00001`, own data directory, two new keys

**Decision.** Title ID `VPNG00001`, LiveArea name `VitaPad NG`, version `02.00`, data directory `ux0:data/VitaPadNG`, settings file `ux0:data/VitaPadNG/settings.txt` (same `Key=Value` format as today), restart flag and zones file in the same directory. The title ID is defined once in CMake and passed to C as `APP_TITLE_ID`. The kernel module keeps its name `VitaPadKernel5` (`psbutton.c:16`, `Server/kernel/exports.yml:1`) and is not modified; its load path follows the title ID automatically (`psbutton.c:17`).
**Rationale.** A separate directory means the stock app and the fork never rewrite each other's settings (the stock save drops unknown keys, `remap.c:101-111`), and one definition of the ID removes the duplicated literal of `CMakeLists.txt:15` and `psbutton.c:14`.
**Rejected.** Keeping `ux0:data/VitaPad`: the stock app would erase the new keys on its next save.

Places that change:

| Where | Old | New |
|---|---|---|
| `Server/CMakeLists.txt:14` | `"VitaPad"` | `"VitaPad NG"` |
| `Server/CMakeLists.txt:15` | `"VPAD00001"` | `"VPNG00001"` |
| `Server/CMakeLists.txt:16` | `"01.90"` | `"02.00"` |
| `Server/source/psbutton.c:14` | `"VPAD00001"` | `APP_TITLE_ID` (used by the module path at `psbutton.c:17` and `go_home` at `psbutton.c:87`) |
| `Server/source/psbutton.c:19` | `"ux0:data/VitaPad/restarted.flag"` | `APP_DATA_DIR "/restarted.flag"` |
| `Server/source/psbutton.c:71` | `"ux0:data/VitaPad"` | `APP_DATA_DIR` |
| `Server/source/remap.c:11-12` | `"ux0:data/VitaPad"`, `"/remap.txt"` | `APP_DATA_DIR`, `"/settings.txt"` |
| new `touchzones.c` | | `APP_DATA_DIR "/zones.txt"` |

Not changed: socket and thread names (`main.c:170`, `main.c:342-346`; debug labels only), `README.md:54` and `TESTING.md:67` (they document the stock app; outside the allowed scope). `NAME ${VITA_APP_NAME}` at `CMakeLists.txt:49` needs no edit: an unquoted CMake variable splits only on semicolons, so the space survives (verify in the build, section 15.3).

`identity.h`:

```c
#pragma once

// The fork installs beside the stock VitaPad (VPAD00001): own title ID and data directory.
// APP_TITLE_ID comes from VITA_TITLEID in Server/CMakeLists.txt so the two can't disagree.
#ifndef APP_TITLE_ID
#error "APP_TITLE_ID must be defined by Server/CMakeLists.txt"
#endif
#define APP_DATA_DIR "ux0:data/VitaPadNG"
```

New settings keys, written after `Connection=` by `remap_save` and read by `remap_load`; unknown values fall back to the default:

| Key | Values | Default | Effect |
|---|---|---|---|
| `FrontTouch` | `Zones`, `Forward` | `Zones` | Wi-Fi only: Zones = section 5; Forward = today's front-touch bytes. Read once at startup. |
| `Mirror` | `On`, `Off` | `On` | Off: PLAY draws nothing (pure black); SETUP, hint, stats unchanged. |

Both keys exist so each new cost can be switched off for the latency A/B (`UI-DECISIONS.md:45`). They are edited in the file (the remap menu stays as is) and survive the menu's save because `remap_save` writes them.

---

## 14 Exact edits to existing files

Line numbers refer to the unmodified files. Apply bottom-up within a file, or match on the quoted text.

### 14.1 `Server/source/main.c`

| # | Lines | Edit |
|---|---|---|
| M1 | after 22 | Add `#include "touchzones.h"` and `#include "ui.h"`. |
| M2 | 81 | `static void fill_packet(PadPacket *pkg){` becomes `static void fill_packet(PadPacket *pkg, ZoneLatch *latch){`. |
| M3 | 95-99 | Replace with the block below (the rear lines 100-104 stay). |
| M4 | 108 | `static uint64_t fill_packet_v2(PadPacketV2 *pkg){` becomes `static uint64_t fill_packet_v2(PadPacketV2 *pkg, ZoneLatch *latch){`. |
| M5 | 128 | Replace with the block below. Lines 123 and 130 stay. |
| M6 | after 153 | Add `ZoneLatch latch;` and `zones_latch_reset(&latch);` (one latch per TCP client). |
| M7 | 158 | `input_rate_poll(fill_packet_v2(&pkg_v2, &latch));` |
| M8 | 161 | `fill_packet(&pkg, &latch);` |
| M9 | after 228 | Add `ZoneLatch latch;` and `zones_latch_reset(&latch);` (fresh latch per stream socket). |
| M10 | 254 | `fill_packet_v2(&pkg, &latch);` |
| M11 | between 317 and 318 | Add `touchzones_init();` (before `remap_load`, `usb_init` and every thread). |
| M12 | after 326 | Add `ui_init(debug_font);`. |
| M13 | 327 | Delete `uint32_t text_color = ...;` (it would become unused and warn under `-Wall`). |
| M14 | after 387 | Add `ui_update(&pad, has_ip ? vita_ip : NULL, connected, battery, screen_off);` |
| M15 | 395-434 | Replace the whole block with `else if (!screen_off) ui_draw();` (the comment at 394 stays). |
| M16 | after 436 | Add `ui_frame_rendered();` |
| M17 | after 437 | Add `ui_frame_swapped();` |

M3 block:

```c
	pkg->buttons = remap_buttons(fast_apply(pad.buttons));
	uint8_t flags = NO_INPUT;
	if (touchzones_owns_front()){
		// The Vita owns the front panel: zones become buttons and no touch reaches the PC
		pkg->buttons |= touchzones_buttons(&front, latch);
		pkg->tx = pkg->ty = 0;
	}else{
		pkg->tx = front.report[0].x;
		pkg->ty = front.report[0].y;
		if (front.reportNum > 0) flags += MOUSE_MOV;
	}
```

M5 block:

```c
	if (touchzones_owns_front()){
		// The Vita owns the front panel: zones become buttons and no point is sent (front_num stays 0
		// from the memset), so no client setting can map them a second time
		pkg->buttons |= touchzones_buttons(&front, latch);
	}else pkg->front_num = front.reportNum > 2 ? 2 : front.reportNum;
```

The per-frame order of the loop (`main.c:358-387`: IP and battery, power tick, screen combo, remap combo, remap update, `ps_update`, `fast_update`) is unchanged; `ui_update` runs after all of it.

### 14.2 `Server/source/usbmode.c`

| # | Lines | Edit |
|---|---|---|
| U1 | after 13 | Add `#include "touchzones.h"`. |
| U2 | after 32 | Add `// Only the USB thread uses it (read_input)` and `static ZoneLatch zone_latch;` (zero = empty latch). |
| U3 | 43-55 | Delete `touch_buttons()` and its comment. |
| U4 | 88 | Replace with the block below. |
| U5 | 91 | Delete `in->usb_buttons \|= touch_buttons();` (the map at 73-81 now covers the zone bits). |

```c
	SceTouchData front;
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) < 0) front.reportNum = 0;
	// Front touch zones, the same table as Wi-Fi (USB has no client to forward touches to)
	in->buttons = remap_buttons(fast_apply(pad.buttons)) | ps_buttons() | touchzones_buttons(&front, &zone_latch);
```

### 14.3 `Server/source/remap.c`

| # | Lines | Edit |
|---|---|---|
| R1 | after 9 | Add `#include "identity.h"`, `#include "touchzones.h"`, `#include "ui.h"`. |
| R2 | 11 | `#define REMAP_DIR APP_DATA_DIR` |
| R3 | 12 | `#define REMAP_FILE REMAP_DIR "/settings.txt"` |
| R4 | after 93 | Add the parse block below. |
| R5 | after 109 | Add `fprintf(f, "FrontTouch=%s\n", touchzones_owns_front() ? "Zones" : "Forward");` and `fprintf(f, "Mirror=%s\n", ui_mirror_enabled() ? "On" : "Off");` |

```c
		if (strcmp(line, "FrontTouch") == 0){
			touchzones_set_owns_front(strcmp(eq + 1, "Forward") != 0);
			continue;
		}
		if (strcmp(line, "Mirror") == 0){
			ui_set_mirror_enabled(strcmp(eq + 1, "Off") != 0);
			continue;
		}
```

The menu (`remap.c:127-210`) and `reset_mapping` (`remap.c:54-58`) are not touched.

### 14.4 `Server/source/psbutton.c`

| # | Lines | Edit |
|---|---|---|
| P1 | after 12 | Add `#include "identity.h"`. |
| P2 | 14 | `#define TITLE_ID APP_TITLE_ID` |
| P3 | 19 | `#define RESTART_FLAG APP_DATA_DIR "/restarted.flag"` |
| P4 | 71 | `sceIoMkdir(APP_DATA_DIR, 0777);` |

### 14.5 `Server/CMakeLists.txt`

| # | Lines | Edit |
|---|---|---|
| C1 | 14-16 | `set(VITA_APP_NAME "VitaPad NG")`, `set(VITA_TITLEID "VPNG00001")`, `set(VITA_VERSION "02.00")` |
| C2 | 28 (after `source/fastinput.c`) | Add `source/zones.c`, `source/mirror.c`, `source/screen.c`, `source/stats.c`, `source/uiplan.c`, `source/touchzones.c`, `source/ui.c`, one per line. |
| C3 | after 31 | `target_compile_definitions(VitaPad PRIVATE "APP_TITLE_ID=\"${VITA_TITLEID}\"")` |
| C4 | after 53 | `  FILE assets/ui/atlas.png ui/atlas.png` |
| C5 | 40 (after `png16 z`) | Add `m` again on its own line: libpng needs `pow()`, and the linker only resolves backwards, so `m` must also come after `png16`. |

### 14.6 New Vita glue, required behaviour

`touchzones.c`: `_Static_assert` that every `PAD_*` equals its `SCE_CTRL_*`; `touchzones_init` gets the front `SceTouchPanelInfo`, fills the built-in table, then reads `zones.txt` with `fopen`/`fread` into a static 2048-byte buffer and replaces the table only on a successful parse. The table and source are plain statics written only there; `owns_front` is `static volatile int` defaulting to 1.

`ui.c` per frame (`ui_update`): read `now`; peek the front panel; `touchzones_read`; `zones_resolve` with the UI latch (gives `zone_bits`, `new_contacts`); `zones_tap_update`; build the snapshot (section 6); `mirror_compute`; `screen_activity`; fill `ScreenInput` (`wifi_link`, `usb_link = usb_state() == USB_STATE_CONNECTED`, `remap_menu_open`, `screen_off`) and `screen_update`; fill `SetupInfo` and, when the stats level is above 0, `StatsInput`. `ui_draw` calls `uiplan_build` on a static `DrawList` and executes it. `ui_init` loads `SPRITES_ATLAS_PATH`, checks the texture is `SPRITES_ATLAS_W` x `SPRITES_ATLAS_H` (otherwise treats the art as missing), and sets POINT filtering.

---

## 15 Test plan

### 15.1 Host unit tests (`Server/tests/test_ui.c`)

Build: `make -C Server/tests test` compiles the pure files with `cc -std=c99 -Wall -Wextra -Werror -I../source -I../kernel -I../../Common` and runs `out/test_ui`, which prints one line per failed check and exits non-zero on any failure. No test framework; a `CHECK(cond)` macro.

Zones:

1. Default table: 5 zones, order and values of 4.1.
2. Hit test: (0,0) is L1, (600,300) is L1, (1300,300) is R1, (600,800) is L3, (959,543), (960,543) and (959,544) are stats (inside the centre ellipse, which is tested first), (1919,1087) is R3, (960,544) is stats, (960,344) is stats (ellipse top, inclusive), (960,343) is R1, (720,544) is stats (ellipse left end), (719,544) is L3.
3. Parse: the default table text gives the same table; comments, blank lines and tabs accepted; each of: unknown kind, unknown shape, 5 numbers, negative, `x0 >= x1`, `c > 1920`, ellipse radius 0, 17 zones, empty file, returns -1 and leaves `*out` untouched.
4. Normalise: matches `fill_touch` (`main.c:65-74`) for min, max, below min (0), above max (range - 1), zero-width panel (raw minus min, then clamped).
5. Resolve: one contact in L1 gives `PAD_L1`; the same id moved into R3 still gives `PAD_L1` (latch); lift and land again in R3 gives `PAD_R3`; a contact starting in the stats ellipse gives 0 and still 0 after sliding into L1; six contacts in four zones give all four bits; eight contacts handled; `new_contacts` counts only unseen ids; a zero-initialised latch works; contacts missing from the sample are dropped.
6. Tap: lift after 200 ms within 30 units in stats zone is a tap; after 400 ms is not; drift 100 units is not; starting in L1 is not; two taps in one frame report 1.

Mirror:

7. Each single bit of the table in 6.1 lights exactly its part; bits outside `PAD_MIRROR_MASK` (0x80000, 0x100000) light nothing.
8. D-pad both rules for all 16 combinations of the four bits, against a reference table written from `ViGEm.cpp:206-220` and `usbmode.c:57-69` (for example UP + DOWN: client lights down only, USB lights up only).
9. Stick offset: 0 gives -12, 1 gives -12, 64 gives -6, 128 gives 0, 192 gives 6, 255 gives 12; deflection false for (144, 112) and true for (145, 128).
10. L3 with a deflected stick: ring and cap lit, cap offset kept. Neutral snapshot lights nothing.

Screen:

11. Boot: SETUP, body level reaches 1000 after `UI_FADE_IN_US`.
12. Wi-Fi link rises: PLAY at once, hint until +2.5 s, body reaches 0 after 600 ms.
13. Link drops for 4.9 s and returns: stays PLAY, no second hint. Drops for 5.0 s: SETUP. USB grace is 1.0 s.
14. Idle in PLAY: no activity 29.9 s keeps levels; at 30 s levels fall and reach 0 600 ms later; a button change restores them in 150 ms. Idle in SETUP: the body level holds at 119.9 s and falls from 120 s. A mode switch after a long idle starts the new mode awake.
15. Activity: stick moving by 23 is not activity; by 24 is; slow drift of 1 per frame for 100 frames is activity at most once per 24 units; a held button is not activity after its first frame; rear touch and motion are not inputs at all.
16. Hint rearm: contact at t0, none for 30 s, new contact shows the hint; after only 29 s it does not; the first contact after boot shows it.
17. Stats cycle: tap shows summary; 5 s later hidden; tap, tap gives DIAG with body and lit targets 0; 60 s later hidden; third tap hides at once; `stats_drawn` keeps the last view during the fade.
18. Pause: with `remap_open` or `screen_off`, levels and mode freeze; the first unpaused frame counts as activity; a clock going backwards gives step 0; a 10 s step changes a level by at most the `UI_MAX_STEP_US` amount.

Stats:

19. Frame time: 60 periods of 16 667 us in a window give avg 16.7 ms, 0 late; one 33 333 us period gives late 1 and max 33.3 ms; `valid` only after one full window.
20. Summary wording: every row of 11.2 by its condition, compared as exact strings, including `Battery: 100%`.
21. Diagnostic: line count 18; lines 9 to 17 equal the credit strings of `main.c:396` and `main.c:412-419`; each line fits `STATS_LINE_MAX`.

Plan and sprites:

22. `sprites.h`: `SPR_COUNT == 22`; names follow 9.4; every rectangle lies inside the atlas and on the 960x544 canvas; no two atlas rectangles (grown by `SPRITES_PAD`) overlap; base and lit cap rectangles share a centre within 1 px.
23. Draw order: for SETUP and PLAY scenarios the item sequence follows 10.1; PLAY at rest emits 0 items; `Mirror=Off` PLAY emits no sprites; `art_loaded = 0` emits no sprites.
24. Budget: the scenarios of 10.2 stay within `UI_DRAW_BUDGET`; DIAG and a 16-ellipse table stay within `UI_DRAW_BUDGET_DIAG`; no list exceeds `UI_DRAW_MAX` items.
25. Alpha: an item's alpha equals `level * 255 / 1000` of its layer; no item is emitted with alpha 0.
26. Layout: every text item of SETUP lies in its region of 12.1 (by baseline and declared max width).

### 15.2 Preview tool

- `Server/tests/out/plan_dump <scenario> [--zones FILE]` (built by the same Makefile) prints the draw list of a named scenario as JSON: `{"scenario", "cost", "items": [{"kind", "sprite", "x", "y", "x1", "y1", "color", "scale", "align", "text"}]}`. `plan_dump --list` prints the scenario names. Scenarios are defined in `plan_dump.c` from fixed models and fixed clocks: `setup_wifi`, `setup_no_wifi`, `setup_usb_waiting`, `setup_cross_l1`, `setup_half_faded`, `play_rest`, `play_all`, `play_sticks`, `play_l3`, `play_hint`, `play_summary`, `play_mirror_off`, `diag`.
- `tools/ui_scenarios.txt` lists the scenarios to render, one per line.
- `python3 tools/ui_preview.py --out OUTDIR [--zones FILE]` runs `plan_dump` for each scenario, composes a 960x544 RGB frame per scenario on black from `Server/assets/ui/atlas.png` and `sprites.json` (straight-alpha "over", alpha from the item colour), draws lines, and draws text with Pillow's default font marked approximate (PGF metrics are not available on the Mac). Outputs `OUTDIR/<scenario>.png`, `OUTDIR/contact-sheet.png` (all frames at half size with name and cost) and prints each frame's cost and mean luminance.
- `python3 tools/export_sprites.py --check` is part of the test run.

### 15.3 Build check

```bash
cd Server && VITASDK=/usr/local/vitasdk PATH=/usr/local/vitasdk/bin:$PATH cmake -S . -B out && cmake --build out 2>&1 | tee out/build.log
grep -ci warning out/build.log   # must print 0
```

Then confirm `unzip -l out/VitaPad.vpk` lists `ui/atlas.png`, and that the generated `out/VitaPad.vpk_param.sfo` contains `VPNG00001` and `VitaPad NG` (`strings` is enough).

---

## 16 On-Vita verification checklist

To run when the PCH-1000 is available; captures 1 to 6 of `UI-DECISIONS.md:85-113` come first on the stock app.

1. Install the VPK beside the stock app; both icons exist; the fork's settings appear in `ux0:data/VitaPadNG/settings.txt` after a remap save, and the stock `remap.txt` is untouched.
2. First launch with Unsafe Homebrew: the kernel module loads, the app restarts once, PS capture works (`restarted.flag` under the new directory).
3. SETUP: body centred, no half-pixel blur, text legible on the near-black body (capture 4 question), wording as section 12, IP and port correct.
4. Wi-Fi client connects: PLAY within one frame, hint for 2.5 s then fade, rest state pure black.
5. Every button and both sticks light the right part; diagonals; L3/R3 ring; cap travel looks proportional; hold L+R+SELECT and L+R+START still work after 60 frames.
6. Zones: L1/R1/L3/R3 reach the PC exactly once with `VIGEM_FRONT_TOUCH` 0, 1, 2 and 3; sliding across the middle keeps the first zone; the centre tap shows stats and sends nothing (`--monitor` shows no button).
7. `FrontTouch=Forward`: front touch behaves exactly like v1.9.2 in `--monitor`.
8. USB DS4 and USB generic: zones work, mirror lights, SETUP returns 1 s after unplugging.
9. Wi-Fi dropout: closing the client returns SETUP after about 5 s plus the 3 s flag delay; a reconnect inside the grace shows no SETUP flash.
10. Idle: after 30 s without input the screen fades to black; any button or new touch brings it back; held trigger fades after 30 s.
11. Frame time in the diagnostic view: 16.7 ms average, 0 late in SETUP, PLAY, hint, summary and diagnostic. Compare with `Mirror=Off`.
12. Latency budget (`UI-DECISIONS.md:39-45`): packets per second and longest gap in `--monitor`, fast-button fallbacks 0, compared with the capture 1 baseline and with `FrontTouch=Forward`.
13. Atlas load time at startup (stopwatch), free video memory unaffected (no crash with other apps suspended).
14. Remap menu unchanged; closing it returns to the mirror with a fade-in.
15. Suspend and resume: state recovers, PS relock still works, no stuck lit parts.
16. Tune and record: `MIRROR_STICK_DEADBAND`, `UI_STICK_ACTIVITY_DELTA` (capture 3), zone table and `ZONE_TAP_*` (capture 2), `UI_IDLE_TIMEOUT_US` against system dimming (capture 5), lit colours on the OLED.

---

## 17 Open risks and unknowns

| Risk | Impact | Mitigation |
|---|---|---|
| PGF glyph draws are slower than assumed | missed frames in DIAG | budget capped at today's proven ~1 100 glyphs; frame instrument shows it; shorten DIAG if needed |
| Atlas PNG decode time at startup | slower launch | measured on the Vita (16.13); atlas can shrink to 8-bit palette later without API change |
| Front panel active area differs from 1920x1088 | zones offset | normalisation uses `SceTouchPanelInfo`, like `fill_touch`; tune zones after capture 2 |
| Thumb capture may leave no centre dead area | stats trigger collides with zones | the table makes the stats zone data; it can move to any free spot |
| UI latch disagrees with a network latch for one frame | mirror off by one frame | accepted (4.3) |
| Users relying on the front panel as DS4 touchpad or mouse | lost feature in Zones mode | `FrontTouch=Forward` |
| `VIGEM_SWAP_SHOULDERS=1` on the PC | mirror shows L2 where the PC gets L1 | documented assumption; the Vita cannot see client settings |
| `SCE_CTRL_INTERCEPTED` equals `SCE_CTRL_PSBUTTON` (`psp2common/ctrl.h:50-51`) and passes through `remap_buttons` (`remap.c:115`) | PC (and mirror) may see PS while input is intercepted | existing behaviour, unchanged; watch for it on hardware |
| `connected` may not drop on some reconnects (`UI-DECISIONS.md:121`) | SETUP never returns, or returns late | grace logic only needs the flag to drop eventually; verify (16.9) |
| Near-black body illegible on the OLED (capture 4) | SETUP hard to read | text colours independent of the body; body brightness can be tinted up in `ui.c` |
| resvg version changes antialiasing | `--check` fails after a tool upgrade | version recorded in `sprites.h`; regenerate and review |
| `NAME` with a space in `vita_create_vpk` | LiveArea name truncated | checked in the build (15.3); fallback name `VitaPadNG` |
| Process clock behaviour across sleep | instant SETUP after resume | expected and harmless; fades clamp to `UI_MAX_STEP_US` |
