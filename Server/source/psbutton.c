#include <stdio.h>
#include <string.h>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/shellutil.h>
#include <psp2/vshbridge.h>
#include <taihen.h>

#include "psbutton.h"
#include "identity.h"

#define TITLE_ID APP_TITLE_ID
// Must match the module name in kernel/exports.yml (it carries the ABI version)
#define KERNEL_MODULE_NAME "VitaPadKernel5"
#define KERNEL_MODULE_PATH "ux0:app/" TITLE_ID "/module/vitapad_kernel.skprx"
// Written right before restarting after loading the module, so a failure can't cause a restart loop
#define RESTART_FLAG APP_DATA_DIR "/restarted.flag"

// Two presses within this time go back to the LiveArea (like Adrenaline)
#define DOUBLE_TAP_US (300 * 1000)
// Holding this long goes back to the LiveArea in hold mode
#define HOLD_US (800 * 1000)
// After a double tap the PS button stays unlocked this long, so the system can act on it
#define UNLOCK_US (3 * 1000 * 1000)
// A gap this long between frames means the Vita slept or VitaPad was in the background
#define RESUME_GAP_US (1 * 1000 * 1000)

// From the kernel module (weak import: resolved only once the module is loaded)
int vitapadKernelGetButtons(uint32_t *buttons);

// The quick menu is locked too, so holding PS doesn't open it by accident
#define PS_LOCK (SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2 | SCE_SHELL_UTIL_LOCK_TYPE_QUICK_MENU)

static int available = 0;
static volatile int mode = PS_MODE_DOUBLE_TAP;
static int locked = 0;
static volatile uint32_t ps_state = 0;
static uint64_t last_press = 0;
static uint64_t press_start = 0;
static int hold_done = 0;
static uint64_t unlocked_until = 0;
static uint64_t last_frame = 0;
static uint32_t old_ps = 0;

static void lock(void){
	sceShellUtilLock(PS_LOCK);
	locked = 1;
}

static void unlock(void){
	sceShellUtilUnlock(PS_LOCK);
	locked = 0;
}

void ps_init(void){
	int search_buf[2] = { 0, 0 };
	if (_vshKernelSearchModuleByName(KERNEL_MODULE_NAME, search_buf) < 0){
		// Already restarted once and the module still isn't there: give up, the PS button stays native
		SceUID flag = sceIoOpen(RESTART_FLAG, SCE_O_RDONLY, 0);
		if (flag >= 0){
			sceIoClose(flag);
			sceIoRemove(RESTART_FLAG);
			return;
		}
		// Fails without "Enable Unsafe Homebrew": the PS button stays native
		if (taiLoadStartKernelModule(KERNEL_MODULE_PATH, 0, NULL, 0) < 0) return;
		// Our imports of the module only resolve when the app starts: restart
		sceIoMkdir("ux0:data", 0777);
		sceIoMkdir(APP_DATA_DIR, 0777);
		flag = sceIoOpen(RESTART_FLAG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
		if (flag >= 0) sceIoClose(flag);
		sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
		return;
	}
	sceIoRemove(RESTART_FLAG);
	available = 1;
	sceShellUtilInitEvents(0);
	if (mode != PS_MODE_NORMAL) lock();
}

// Gives the PS button back to the system for a moment and goes to the LiveArea
static void go_home(uint64_t now){
	unlock();
	unlocked_until = now + UNLOCK_US;
	sceAppMgrLaunchAppByName2(TITLE_ID, NULL, NULL);
}

void ps_update(void){
	if (!available) return;
	uint64_t now = sceKernelGetProcessTimeWide();

	// The lock doesn't survive sleep or going to the LiveArea: take it again
	if (last_frame && now - last_frame > RESUME_GAP_US){
		unlocked_until = 0;
		if (mode != PS_MODE_NORMAL) lock();
	}
	last_frame = now;

	if (mode == PS_MODE_NORMAL){
		if (locked) unlock();
		ps_state = 0;
		return;
	}

	uint32_t buttons = 0;
	if (vitapadKernelGetButtons(&buttons) < 0) buttons = 0;
	uint32_t ps = buttons & SCE_CTRL_PSBUTTON;
	int pressed = ps && !old_ps;
	old_ps = ps;

	if (mode == PS_MODE_DOUBLE_TAP){
		ps_state = ps;
		if (pressed){
			if (last_press && now - last_press < DOUBLE_TAP_US){
				last_press = 0;
				go_home(now);
			}else last_press = now;
		}
	}else{
		// Hold mode: taps go to the PC, holding goes to the LiveArea (and stops being sent)
		if (pressed){
			press_start = now;
			hold_done = 0;
		}
		if (ps && !hold_done && now - press_start >= HOLD_US){
			hold_done = 1;
			go_home(now);
		}
		ps_state = (ps && !hold_done) ? ps : 0;
	}

	if (unlocked_until && now > unlocked_until){
		unlocked_until = 0;
		lock();
	}
}

uint32_t ps_buttons(void){
	return (available && mode != PS_MODE_NORMAL) ? ps_state : 0;
}

int ps_available(void){
	return available;
}

int ps_mode(void){
	return mode;
}

void ps_set_mode(int value){
	if (value < 0 || value >= PS_MODES_NUM) value = PS_MODE_DOUBLE_TAP;
	mode = value;
	last_press = 0;
	hold_done = 0;
	unlocked_until = 0;
	if (!available) return;
	if (mode != PS_MODE_NORMAL && !locked) lock();
	if (mode == PS_MODE_NORMAL && locked) unlock();
}

const char *ps_mode_id(int value){
	static const char *ids[PS_MODES_NUM] = { "DoubleTap", "Hold", "Off" };
	return ids[value];
}

const char *ps_mode_label(int value){
	static const char *labels[PS_MODES_NUM] = {
		"Send to PC, double-tap for the LiveArea",
		"Send to PC, hold for the LiveArea",
		"Normal",
	};
	return labels[value];
}
