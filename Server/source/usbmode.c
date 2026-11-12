#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/motion.h>
#include <psp2/power.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "usbmode.h"
#include "psbutton.h"
#include "remap.h"
#include "usb.h"
#include "fastinput.h"
#include "touchzones.h"

// From the kernel module (weak imports: resolved only once the module is loaded)
int vitapadKernelUsbStart(int mode);
int vitapadKernelUsbStop(void);
int vitapadKernelUsbSetReport(const void *report, unsigned int size);
int vitapadKernelUsbGetState(void);

// The report is refreshed this often (the computer polls every 1 ms)
#define UPDATE_US (4 * 1000)
// After a failed start (or after the kernel gave USB back while we were in the background), retry this often
#define RETRY_US (1000 * 1000)
#define BATTERY_US (5 * 1000 * 1000)

#define RAD_TO_DEG 57.2957795f
#define DS4_GYRO_PER_DEG_S 16.0f
#define DS4_ACCEL_PER_G 8192.0f

static volatile int connection = CONNECTION_WIFI;
static SceTouchPanelInfo rear_panel;
// Only the USB thread uses them (read_input)
static ZoneLatch zone_latch;
static ZoneLatch rear_latch;

// Everything read from the Vita for one report
typedef struct {
	uint32_t buttons;       // Vita buttons after remapping, plus PS
	uint16_t usb_buttons;   // In USB_BTN_* order, front touch corners included
	uint8_t lx, ly, rx, ry;
	int rear_num;           // Rear touch fingers (DS4 touchpad)
	int rear_x[2], rear_y[2], rear_id[2];
} Input;

static uint8_t hat(uint32_t buttons){
	int up = buttons & SCE_CTRL_UP, down = buttons & SCE_CTRL_DOWN;
	int left = buttons & SCE_CTRL_LEFT, right = buttons & SCE_CTRL_RIGHT;
	if (up && right) return 1;
	if (right && down) return 3;
	if (down && left) return 5;
	if (left && up) return 7;
	if (up) return 0;
	if (right) return 2;
	if (down) return 4;
	if (left) return 6;
	return USB_HAT_NEUTRAL;
}

// Returns 0 while the remap menu is open: the computer gets a neutral pad then
static int read_input(Input *in){
	static const struct { uint32_t vita; uint16_t usb; } map[] = {
		{ SCE_CTRL_SQUARE, USB_BTN_SQUARE }, { SCE_CTRL_CROSS, USB_BTN_CROSS },
		{ SCE_CTRL_CIRCLE, USB_BTN_CIRCLE }, { SCE_CTRL_TRIANGLE, USB_BTN_TRIANGLE },
		{ SCE_CTRL_L1, USB_BTN_L1 }, { SCE_CTRL_R1, USB_BTN_R1 },
		{ SCE_CTRL_LTRIGGER, USB_BTN_L2 }, { SCE_CTRL_RTRIGGER, USB_BTN_R2 },
		{ SCE_CTRL_SELECT, USB_BTN_SHARE }, { SCE_CTRL_START, USB_BTN_OPTIONS },
		{ SCE_CTRL_L3, USB_BTN_L3 }, { SCE_CTRL_R3, USB_BTN_R3 },
		{ SCE_CTRL_PSBUTTON, USB_BTN_PS },
	};
	memset(in, 0, sizeof(*in));
	in->lx = in->ly = in->rx = in->ry = 128;
	if (remap_menu_open) return 0;

	SceCtrlData pad;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	SceTouchData front;
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) < 0) front.reportNum = 0;
	// Front touch zones, the same table as Wi-Fi (USB has no client to forward touches to)
	in->buttons = remap_buttons(fast_apply(pad.buttons)) | ps_buttons() | touchzones_buttons(&front, &zone_latch);
	// The rear panel is read before the button map below, so its zone bits reach the USB buttons too.
	// A failed read counts as no contact
	SceTouchData rear;
	const int rear_ok = sceTouchPeek(SCE_TOUCH_PORT_BACK, &rear, 1) >= 0;
	if (!rear_ok) rear.reportNum = 0;
	const int rear_zones = touchzones_owns_rear();
	if (rear_zones) in->buttons |= touchzones_rear_buttons(&rear, &rear_latch);
	for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
		if (in->buttons & map[i].vita) in->usb_buttons |= map[i].usb;
	in->lx = pad.lx;
	in->ly = pad.ly;
	in->rx = pad.rx;
	in->ry = pad.ry;

	// With the rear zones owning the panel no point reaches the DS4 touchpad (rear_num stays 0)
	if (!rear_zones && rear_ok){
		int w = rear_panel.maxAaX - rear_panel.minAaX, h = rear_panel.maxAaY - rear_panel.minAaY;
		for (int i = 0; i < (int)rear.reportNum && i < 2; i++){
			int x = rear.report[i].x - rear_panel.minAaX, y = rear.report[i].y - rear_panel.minAaY;
			if (w > 0) x = x * (DS4_TOUCHPAD_WIDTH - 1) / w;
			if (h > 0) y = y * (DS4_TOUCHPAD_HEIGHT - 1) / h;
			if (x < 0) x = 0;
			if (x > DS4_TOUCHPAD_WIDTH - 1) x = DS4_TOUCHPAD_WIDTH - 1;
			if (y < 0) y = 0;
			if (y > DS4_TOUCHPAD_HEIGHT - 1) y = DS4_TOUCHPAD_HEIGHT - 1;
			in->rear_x[i] = x;
			in->rear_y[i] = y;
			in->rear_id[i] = rear.report[i].id;
			in->rear_num = i + 1;
		}
	}
	return 1;
}

void build_generic_report(const Input *in, UsbGamepadReport *report){
	memset(report, 0, sizeof(*report));
	report->buttons = in->usb_buttons;
	report->hat = hat(in->buttons);
	report->lx = in->lx;
	report->ly = in->ly;
	report->rx = in->rx;
	report->ry = in->ry;
}

static int16_t to_int16(float value){
	if (value > 32767.0f) return 32767;
	if (value < -32768.0f) return -32768;
	return (int16_t)value;
}

static void set_touch_point(Ds4TouchPoint *point, int touching, int id, int x, int y){
	point->contact = touching ? (id & 0x7F) : 0x80;
	point->x_lo = x & 0xFF;
	point->x_hi_y_lo = ((x >> 8) & 0x0F) | ((y & 0x0F) << 4);
	point->y_hi = (y >> 4) & 0xFF;
}

// motion: may be NULL (no motion data); battery: 0-100; timestamp_us: time of the report
void build_ds4_report(const Input *in, const SceMotionSensorState *motion, int battery, uint64_t timestamp_us, Ds4Report *report){
	static uint8_t counter = 0;
	static uint8_t touch_counter = 0;
	memset(report, 0, sizeof(*report));
	report->report_id = 0x01;
	report->lx = in->lx;
	report->ly = in->ly;
	report->rx = in->rx;
	report->ry = in->ry;
	report->buttons[0] = hat(in->buttons) | ((in->usb_buttons & 0x0F) << 4);
	report->buttons[1] = (in->usb_buttons >> 4) & 0xFF;
	report->buttons[2] = ((in->usb_buttons >> 12) & 0x03) | ((counter++ & 0x3F) << 2);
	report->l2 = (in->usb_buttons & USB_BTN_L2) ? 255 : 0;
	report->r2 = (in->usb_buttons & USB_BTN_R2) ? 255 : 0;
	report->timestamp = (uint16_t)(timestamp_us * 3 / 16);

	if (motion){
		// Vita axes: X right, Y towards the top of the screen, Z out of the screen; gravity in G, rotation in rad/s.
		// DS4 axes: X right, Y out of the face, Z towards the player; reaction to gravity, rotation in deg/s.
		const float gyro = RAD_TO_DEG * DS4_GYRO_PER_DEG_S;
		report->gyro[0] = to_int16(motion->gyro.x * gyro);
		report->gyro[1] = to_int16(motion->gyro.z * gyro);
		report->gyro[2] = to_int16(-motion->gyro.y * gyro);
		report->accel[0] = to_int16(-motion->accelerometer.x * DS4_ACCEL_PER_G);
		report->accel[1] = to_int16(-motion->accelerometer.z * DS4_ACCEL_PER_G);
		report->accel[2] = to_int16(motion->accelerometer.y * DS4_ACCEL_PER_G);
	}else{
		report->accel[1] = (int16_t)DS4_ACCEL_PER_G; // Lying flat
	}

	int level = battery / 10;
	if (level > 10) level = 10;
	if (level < 0) level = 0;
	report->status[0] = 0x10 | level; // Cable connected (USB)

	// Rear touchpad = DS4 touchpad
	report->num_touch_reports = 1;
	report->touch[0].timestamp = touch_counter++;
	for (int i = 0; i < 2; i++)
		set_touch_point(&report->touch[0].points[i], i < in->rear_num, in->rear_id[i], in->rear_x[i], in->rear_y[i]);
	for (int t = 1; t < 3; t++)
		for (int i = 0; i < 2; i++) set_touch_point(&report->touch[t].points[i], 0, 0, 0, 0);
}

static int kernel_mode(int value){
	return value == CONNECTION_USB_GENERIC ? USB_MODE_GENERIC : USB_MODE_DS4;
}

static int usb_thread(SceSize args, void *argp){
	uint64_t last_try = 0, last_battery = 0;
	int battery = 100;
	for (;;){
		int current = connection;
		if (current != CONNECTION_WIFI && ps_available()){
			uint64_t now = sceKernelGetProcessTimeWide();
			if (vitapadKernelUsbGetState() == USB_STATE_OFF && now - last_try > RETRY_US){
				last_try = now;
				vitapadKernelUsbStart(kernel_mode(current));
			}
			Input in;
			int live = read_input(&in);
			if (current == CONNECTION_USB_GENERIC){
				UsbGamepadReport report;
				build_generic_report(&in, &report);
				vitapadKernelUsbSetReport(&report, sizeof(report));
			}else{
				if (now - last_battery > BATTERY_US || !last_battery){
					last_battery = now;
					battery = scePowerGetBatteryLifePercent();
				}
				SceMotionSensorState motion;
				int has_motion = live && sceMotionGetSensorState(&motion, 1) == 0;
				Ds4Report report;
				build_ds4_report(&in, has_motion ? &motion : NULL, battery, now, &report);
				vitapadKernelUsbSetReport(&report, sizeof(report));
			}
		}
		sceKernelDelayThread(UPDATE_US);
	}
	return 0;
}

void usb_init(void){
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &rear_panel);
	SceUID thread = sceKernelCreateThread("VitaPad USB Thread", usb_thread, 0x10000100, 0x4000, 0, 0, NULL);
	if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
}

int usb_connection(void){
	return connection;
}

void usb_set_connection(int value){
	if (value < 0 || value >= CONNECTIONS_NUM) value = CONNECTION_WIFI;
	if (value == connection) return;
	connection = value;
	// Stop USB mode; the thread starts it again in the new mode if needed
	if (ps_available()) vitapadKernelUsbStop();
}

const char *usb_connection_id(int value){
	static const char *ids[CONNECTIONS_NUM] = { "WiFi", "USB", "USBGeneric" };
	return ids[value];
}

const char *usb_connection_label(int value){
	static const char *labels[CONNECTIONS_NUM] = {
		"Wi-Fi (VitaPad PC client)",
		"USB, as a DualShock 4",
		"USB, as a standard gamepad",
	};
	return labels[value];
}

int usb_state(void){
	if (connection == CONNECTION_WIFI || !ps_available()) return USB_STATE_OFF;
	return vitapadKernelUsbGetState();
}
