#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>

#include "remap.h"
#include "psbutton.h"
#include "usbmode.h"
#include "fastinput.h"
#include "identity.h"
#include "touchzones.h"
#include "ui.h"

#define REMAP_DIR APP_DATA_DIR
#define REMAP_FILE REMAP_DIR "/settings.txt"

typedef struct {
	const char *name;
	uint32_t mask;
} Button;

// Physical Vita buttons that can be remapped
static const Button sources[] = {
	{ "Cross", SCE_CTRL_CROSS }, { "Circle", SCE_CTRL_CIRCLE }, { "Square", SCE_CTRL_SQUARE }, { "Triangle", SCE_CTRL_TRIANGLE },
	{ "Up", SCE_CTRL_UP }, { "Down", SCE_CTRL_DOWN }, { "Left", SCE_CTRL_LEFT }, { "Right", SCE_CTRL_RIGHT },
	{ "L", SCE_CTRL_LTRIGGER }, { "R", SCE_CTRL_RTRIGGER }, { "Start", SCE_CTRL_START }, { "Select", SCE_CTRL_SELECT },
};
#define SOURCES_NUM (sizeof(sources) / sizeof(sources[0]))

// What they can send: any Vita button, the DualShock 4 buttons the Vita lacks, or nothing
static const Button targets[] = {
	{ "Cross", SCE_CTRL_CROSS }, { "Circle", SCE_CTRL_CIRCLE }, { "Square", SCE_CTRL_SQUARE }, { "Triangle", SCE_CTRL_TRIANGLE },
	{ "Up", SCE_CTRL_UP }, { "Down", SCE_CTRL_DOWN }, { "Left", SCE_CTRL_LEFT }, { "Right", SCE_CTRL_RIGHT },
	{ "L", SCE_CTRL_LTRIGGER }, { "R", SCE_CTRL_RTRIGGER }, { "Start", SCE_CTRL_START }, { "Select", SCE_CTRL_SELECT },
	{ "L1", SCE_CTRL_L1 }, { "R1", SCE_CTRL_R1 }, { "L3", SCE_CTRL_L3 }, { "R3", SCE_CTRL_R3 },
	{ "None", 0 },
};
#define TARGETS_NUM (sizeof(targets) / sizeof(targets[0]))

// The menu has one row per source plus the settings
#define ROWS_NUM (SOURCES_NUM + 3)
#define PS_ROW SOURCES_NUM
#define CONNECTION_ROW (SOURCES_NUM + 1)
#define FAST_ROW (SOURCES_NUM + 2)

#define ROW_Y(row) (116 + (row) * 28)

volatile int remap_menu_open = 0;

// mapping[source] = target index. The first SOURCES_NUM targets are the sources themselves, so identity is the default
static volatile int mapping[SOURCES_NUM];
static uint32_t sources_mask = 0;

static int selected = 0;
static uint32_t old_buttons = 0;

static void reset_mapping(void){
	for (int i = 0; i < (int)SOURCES_NUM; i++) mapping[i] = i;
	ps_set_mode(PS_MODE_DOUBLE_TAP);
	fast_set_enabled(1);
}

static int find(const Button *list, int num, const char *name){
	for (int i = 0; i < num; i++)
		if (strcmp(list[i].name, name) == 0) return i;
	return -1;
}

void remap_load(void){
	sources_mask = 0;
	for (int i = 0; i < (int)SOURCES_NUM; i++) sources_mask |= sources[i].mask;
	reset_mapping();

	// One "Source=Target" per line, e.g. "Select=L3"
	FILE *f = fopen(REMAP_FILE, "r");
	if (f == NULL) return;
	char line[64];
	while (fgets(line, sizeof(line), f)){
		line[strcspn(line, "\r\n")] = 0;
		char *eq = strchr(line, '=');
		if (eq == NULL) continue;
		*eq = 0;
		if (strcmp(line, "Connection") == 0){
			for (int c = 0; c < CONNECTIONS_NUM; c++)
				if (strcmp(eq + 1, usb_connection_id(c)) == 0) usb_set_connection(c);
			continue;
		}
		if (strcmp(line, "FastButtons") == 0){
			fast_set_enabled(strcmp(eq + 1, "Off") != 0);
			continue;
		}
		if (strcmp(line, "PSButton") == 0){
			for (int m = 0; m < PS_MODES_NUM; m++)
				if (strcmp(eq + 1, ps_mode_id(m)) == 0) ps_set_mode(m);
			continue;
		}
		if (strcmp(line, "FrontTouch") == 0){
			touchzones_set_owns_front(strcmp(eq + 1, "Forward") != 0);
			continue;
		}
		if (strcmp(line, "RearTouch") == 0){
			touchzones_set_owns_rear(strcmp(eq + 1, "Forward") != 0);
			continue;
		}
		if (strcmp(line, "Mirror") == 0){
			ui_set_mirror_enabled(strcmp(eq + 1, "Off") != 0);
			continue;
		}
		int src = find(sources, SOURCES_NUM, line);
		int dst = find(targets, TARGETS_NUM, eq + 1);
		if (src >= 0 && dst >= 0) mapping[src] = dst;
	}
	fclose(f);
}

static void remap_save(void){
	sceIoMkdir("ux0:data", 0777);
	sceIoMkdir(REMAP_DIR, 0777);
	FILE *f = fopen(REMAP_FILE, "w");
	if (f == NULL) return;
	for (int i = 0; i < (int)SOURCES_NUM; i++) fprintf(f, "%s=%s\n", sources[i].name, targets[mapping[i]].name);
	fprintf(f, "PSButton=%s\n", ps_mode_id(ps_mode()));
	fprintf(f, "FastButtons=%s\n", fast_enabled() ? "On" : "Off");
	fprintf(f, "Connection=%s\n", usb_connection_id(usb_connection()));
	fprintf(f, "FrontTouch=%s\n", touchzones_owns_front() ? "Zones" : "Forward");
	fprintf(f, "RearTouch=%s\n", touchzones_owns_rear() ? "Zones" : "Forward");
	fprintf(f, "Mirror=%s\n", ui_mirror_enabled() ? "On" : "Off");
	fclose(f);
}

uint32_t remap_buttons(uint32_t buttons){
	// Buttons that aren't remappable (e.g. L1/R1/L3/R3 from a PS TV controller) pass through
	uint32_t out = buttons & ~sources_mask;
	for (int i = 0; i < (int)SOURCES_NUM; i++)
		if (buttons & sources[i].mask) out |= targets[mapping[i]].mask;
	return out;
}

int remap_changed_count(void){
	int count = 0;
	for (int i = 0; i < (int)SOURCES_NUM; i++) count += mapping[i] != i;
	return count;
}

void remap_menu_open_now(void){
	remap_menu_open = 1;
	// The buttons of the combo that opened the menu shouldn't count as presses
	SceCtrlData pad;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	old_buttons = pad.buttons;
}

int remap_menu_update(uint32_t buttons){
	uint32_t pressed = buttons & ~old_buttons;
	old_buttons = buttons;

	if (pressed & SCE_CTRL_UP) selected = (selected + ROWS_NUM - 1) % ROWS_NUM;
	if (pressed & SCE_CTRL_DOWN) selected = (selected + 1) % ROWS_NUM;
	if (selected == FAST_ROW){
		if ((pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) && ps_available()) fast_set_enabled(!fast_enabled());
	}else if (selected == CONNECTION_ROW){
		if (ps_available()){
			if (pressed & SCE_CTRL_LEFT) usb_set_connection((usb_connection() + CONNECTIONS_NUM - 1) % CONNECTIONS_NUM);
			if (pressed & SCE_CTRL_RIGHT) usb_set_connection((usb_connection() + 1) % CONNECTIONS_NUM);
		}
	}else if (selected == PS_ROW){
		if (ps_available()){
			if (pressed & SCE_CTRL_LEFT) ps_set_mode((ps_mode() + PS_MODES_NUM - 1) % PS_MODES_NUM);
			if (pressed & SCE_CTRL_RIGHT) ps_set_mode((ps_mode() + 1) % PS_MODES_NUM);
		}
	}else{
		if (pressed & SCE_CTRL_LEFT) mapping[selected] = (mapping[selected] + TARGETS_NUM - 1) % TARGETS_NUM;
		if (pressed & SCE_CTRL_RIGHT) mapping[selected] = (mapping[selected] + 1) % TARGETS_NUM;
	}
	if (pressed & SCE_CTRL_TRIANGLE) reset_mapping();
	if (pressed & SCE_CTRL_START){
		remap_save();
		remap_menu_open = 0;
		return 1;
	}
	return 0;
}

void remap_menu_draw(vita2d_pgf *font){
	uint32_t white = RGBA8(0xFF, 0xFF, 0xFF, 0xFF);
	uint32_t dim = RGBA8(0x90, 0x96, 0xA8, 0xFF);
	uint32_t accent = RGBA8(0x4D, 0xA3, 0xFF, 0xFF);
	uint32_t changed = RGBA8(0xFF, 0xD1, 0x66, 0xFF);

	vita2d_pgf_draw_text(font, 20, 34, white, 1.2, "Button remapping");
	vita2d_pgf_draw_text(font, 20, 62, dim, 0.9, "Up/Down: choose button   Left/Right: change   Triangle: reset all   START: save and close");
	vita2d_pgf_draw_text(font, 20, 84, dim, 0.9, "The PC receives no input while this menu is open.  L1/R1/L3/R3 need ViGEm or vJoy mode.");

	for (int i = 0; i < (int)SOURCES_NUM; i++){
		int y = ROW_Y(i);
		if (i == selected) vita2d_draw_rectangle(12, y - 21, 560, 28, RGBA8(0x2B, 0x30, 0x40, 0xFF));
		vita2d_pgf_draw_text(font, 30, y, i == selected ? accent : white, 1.0, sources[i].name);
		vita2d_pgf_draw_text(font, 200, y, dim, 1.0, "->");
		const char *target = targets[mapping[i]].name;
		if (i == selected) vita2d_pgf_draw_textf(font, 260, y, accent, 1.0, "<  %s  >", target);
		else vita2d_pgf_draw_text(font, 260, y, mapping[i] != i ? changed : white, 1.0, target);
	}

	int y = ROW_Y(PS_ROW);
	int available = ps_available();
	const char *value = available ? ps_mode_label(ps_mode()) : "Unavailable: enable Unsafe Homebrew in HENkaku settings";
	if (selected == PS_ROW) vita2d_draw_rectangle(12, y - 21, 936, 28, RGBA8(0x2B, 0x30, 0x40, 0xFF));
	vita2d_pgf_draw_text(font, 30, y, selected == PS_ROW ? accent : white, 1.0, "PS button");
	vita2d_pgf_draw_text(font, 200, y, dim, 1.0, "->");
	if (selected == PS_ROW && available) vita2d_pgf_draw_textf(font, 260, y, accent, 1.0, "<  %s  >", value);
	else vita2d_pgf_draw_text(font, 260, y, available ? white : dim, 1.0, value);

	y = ROW_Y(CONNECTION_ROW);
	const char *connection = !available ? "Wi-Fi (USB mode needs Unsafe Homebrew)" : usb_connection_label(usb_connection());
	if (selected == CONNECTION_ROW) vita2d_draw_rectangle(12, y - 21, 936, 28, RGBA8(0x2B, 0x30, 0x40, 0xFF));
	vita2d_pgf_draw_text(font, 30, y, selected == CONNECTION_ROW ? accent : white, 1.0, "Connection");
	vita2d_pgf_draw_text(font, 200, y, dim, 1.0, "->");
	if (selected == CONNECTION_ROW && available) vita2d_pgf_draw_textf(font, 260, y, accent, 1.0, "<  %s  >", connection);
	else vita2d_pgf_draw_text(font, 260, y, available ? white : dim, 1.0, connection);

	y = ROW_Y(FAST_ROW);
	const char *fast = !available ? "Normal (fast buttons need Unsafe Homebrew)" : fast_enabled() ? "Fast: read directly, about 10 ms sooner" : "Normal: once per frame";
	if (selected == FAST_ROW) vita2d_draw_rectangle(12, y - 21, 936, 28, RGBA8(0x2B, 0x30, 0x40, 0xFF));
	vita2d_pgf_draw_text(font, 30, y, selected == FAST_ROW ? accent : white, 1.0, "Buttons");
	vita2d_pgf_draw_text(font, 200, y, dim, 1.0, "->");
	if (selected == FAST_ROW && available) vita2d_pgf_draw_textf(font, 260, y, accent, 1.0, "<  %s  >", fast);
	else vita2d_pgf_draw_text(font, 260, y, available ? white : dim, 1.0, fast);
}
