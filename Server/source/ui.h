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
