#include <vita2d.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/types.h>
#include <psp2/motion.h>
#include <psp2/power.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include "protocol.h"
#include "remap.h"
#include "psbutton.h"
#include "usbmode.h"
#include "inputrate.h"
#include "fastinput.h"
#include "touchzones.h"
#include "ui.h"

#define NET_INIT_SIZE 1*1024*1024

// A client that stops polling for this long is considered gone
#define CLIENT_TIMEOUT (3 * 1000 * 1000)

// Hold this combo to turn the screen off/on
#define SCREEN_TOGGLE_COMBO (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_SELECT)
#define SCREEN_TOGGLE_FRAMES 60

// Hold this combo to open/close the button remapping menu
#define REMAP_MENU_COMBO (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START)

volatile int connected = 0;
volatile uint8_t battery = 100;

static SceTouchPanelInfo panel_info[2];

static int recv_all(int s, void *buf, unsigned int len){
	uint8_t *p = (uint8_t *)buf;
	while (len > 0){
		int ret = sceNetRecv(s, p, len, 0);
		if (ret <= 0) return -1;
		p += ret;
		len -= ret;
	}
	return 0;
}

static int send_all(int s, const void *buf, unsigned int len){
	const uint8_t *p = (const uint8_t *)buf;
	while (len > 0){
		int ret = sceNetSend(s, p, len, 0);
		if (ret <= 0) return -1;
		p += ret;
		len -= ret;
	}
	return 0;
}

// Maps a touch report to the TOUCH_WIDTH x TOUCH_HEIGHT range
static void fill_touch(TouchPoint *out, const SceTouchReport *report, const SceTouchPanelInfo *info){
	int x = report->x - info->minAaX;
	int y = report->y - info->minAaY;
	int w = info->maxAaX - info->minAaX;
	int h = info->maxAaY - info->minAaY;
	if (w > 0) x = x * (TOUCH_WIDTH - 1) / w;
	if (h > 0) y = y * (TOUCH_HEIGHT - 1) / h;
	if (x < 0) x = 0;
	if (x > TOUCH_WIDTH - 1) x = TOUCH_WIDTH - 1;
	if (y < 0) y = 0;
	if (y > TOUCH_HEIGHT - 1) y = TOUCH_HEIGHT - 1;
	out->x = x;
	out->y = y;
	out->id = report->id;
	out->reserved = 0;
}

static void fill_packet(PadPacket *pkg, ZoneLatch *latch){
	SceCtrlData pad;
	SceTouchData front, retro;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	// A failed read must not turn stack garbage into zone presses
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) < 0) memset(&front, 0, sizeof(front));
	sceTouchPeek(SCE_TOUCH_PORT_BACK, &retro, 1);
	input_rate_poll(pad.timeStamp);
	// While the remap menu is open the PC gets a neutral pad
	if (remap_menu_open){
		memset(pkg, 0, sizeof(PadPacket));
		pkg->lx = pkg->ly = pkg->rx = pkg->ry = 128;
		return;
	}
	memcpy(pkg, &pad.buttons, 8); // Buttons + analogs state
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
	if (retro.reportNum > 0){
		if (retro.report[0].x > 960) flags += RIGHT_CLICK;
		else flags += LEFT_CLICK;
	}
	pkg->click = flags;
}

// Returns the timestamp of the buttons/sticks sample used
static uint64_t fill_packet_v2(PadPacketV2 *pkg, ZoneLatch *latch){
	SceCtrlData pad;
	SceTouchData front, retro;
	SceMotionSensorState motion;
	sceCtrlPeekBufferPositive(0, &pad, 1);
	// A failed read must not turn stack garbage into zone presses
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1) < 0) memset(&front, 0, sizeof(front));
	sceTouchPeek(SCE_TOUCH_PORT_BACK, &retro, 1);
	memset(pkg, 0, sizeof(PadPacketV2));
	pkg->timestamp = (uint32_t)sceKernelGetProcessTimeWide();
	pkg->battery = battery;
	// While the remap menu is open the PC gets a neutral pad
	if (remap_menu_open){
		pkg->lx = pkg->ly = pkg->rx = pkg->ry = 128;
		return pad.timeStamp;
	}
	pkg->buttons = remap_buttons(fast_apply(pad.buttons)) | ps_buttons();
	pkg->lx = pad.lx;
	pkg->ly = pad.ly;
	pkg->rx = pad.rx;
	pkg->ry = pad.ry;
	if (touchzones_owns_front()){
		// The Vita owns the front panel: zones become buttons and no point is sent (front_num stays 0
		// from the memset), so no client setting can map them a second time
		pkg->buttons |= touchzones_buttons(&front, latch);
	}else pkg->front_num = front.reportNum > 2 ? 2 : front.reportNum;
	pkg->rear_num = retro.reportNum > 2 ? 2 : retro.reportNum;
	for (int i = 0; i < pkg->front_num; i++) fill_touch(&pkg->front[i], &front.report[i], &panel_info[SCE_TOUCH_PORT_FRONT]);
	for (int i = 0; i < pkg->rear_num; i++) fill_touch(&pkg->rear[i], &retro.report[i], &panel_info[SCE_TOUCH_PORT_BACK]);
	if (sceMotionGetSensorState(&motion, 1) == 0){
		pkg->accel[0] = motion.accelerometer.x;
		pkg->accel[1] = motion.accelerometer.y;
		pkg->accel[2] = motion.accelerometer.z;
		pkg->gyro[0] = motion.gyro.x;
		pkg->gyro[1] = motion.gyro.y;
		pkg->gyro[2] = motion.gyro.z;
	}
	return pad.timeStamp;
}

static void handle_client(int client){
	int one = 1;
	int timeout = CLIENT_TIMEOUT;
	sceNetSetsockopt(client, SCE_NET_IPPROTO_TCP, SCE_NET_TCP_NODELAY, &one, sizeof(one));
	sceNetSetsockopt(client, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &timeout, sizeof(timeout));
	sceNetSetsockopt(client, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &timeout, sizeof(timeout));

	connected = 1;
	char request[REQUEST_SIZE];
	PadPacket pkg;
	PadPacketV2 pkg_v2;
	ZoneLatch latch;
	zones_latch_reset(&latch);
	for (;;){
		if (recv_all(client, request, REQUEST_SIZE) < 0) break;
		int ret;
		if (memcmp(request, REQUEST_V2, REQUEST_SIZE) == 0){
			input_rate_poll(fill_packet_v2(&pkg_v2, &latch));
			ret = send_all(client, &pkg_v2, sizeof(PadPacketV2));
		}else{
			fill_packet(&pkg, &latch);
			ret = send_all(client, &pkg, sizeof(PadPacket));
		}
		if (ret < 0) break;
	}
	connected = 0;
}

static int create_socket(int type, int protocol, int port){
	int fd = sceNetSocket("VitaPad", SCE_NET_AF_INET, type, protocol);
	if (fd < 0) return fd;
	int one = 1;
	sceNetSetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR, &one, sizeof(one));
	SceNetSockaddrIn addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = SCE_NET_AF_INET;
	addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
	addr.sin_port = sceNetHtons(port);
	if (sceNetBind(fd, (SceNetSockaddr *)&addr, sizeof(addr)) < 0){
		sceNetSocketClose(fd);
		return -1;
	}
	return fd;
}

// Server thread
static int server_thread(unsigned int args, void* argp){
	for (;;){
		int fd = create_socket(SCE_NET_SOCK_STREAM, 0, GAMEPAD_PORT);
		if (fd < 0 || sceNetListen(fd, 128) < 0){
			if (fd >= 0) sceNetSocketClose(fd);
			sceKernelDelayThread(1000 * 1000);
			continue;
		}
		for (;;){
			SceNetSockaddrIn clientaddr;
			unsigned int addrlen = sizeof(clientaddr);
			int client = sceNetAccept(fd, (SceNetSockaddr *)&clientaddr, &addrlen);
			// The listening socket can break (e.g. after the Vita resumes from sleep), recreate it
			if (client < 0) break;
			handle_client(client);
			sceNetSocketClose(client);
		}
		sceNetSocketClose(fd);
		sceKernelDelayThread(500 * 1000);
	}
	return 0;
}

// Streams input over UDP to the client that sends hellos (see protocol.h): each change goes out right
// away instead of waiting for the next poll, and a lost packet costs one sample instead of a TCP
// retransmission stall
#define STREAM_CHECK_US 1000
static int stream_thread(unsigned int args, void* argp){
	char buf[32];
	for (;;){
		int fd = create_socket(SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP, STREAM_PORT);
		if (fd < 0){
			sceKernelDelayThread(1000 * 1000);
			continue;
		}
		SceNetSockaddrIn target;
		uint64_t last_hello = 0, last_send = 0;
		PadPacketV2 last;
		StreamPacket packet;
		memset(&last, 0, sizeof(last));
		memset(&packet, 0, sizeof(packet));
		memcpy(packet.magic, STREAM_MAGIC, 4);
		ZoneLatch latch;
		zones_latch_reset(&latch);
		for (;;){
			// Hellos: who to stream to, and whether they still want it
			SceNetSockaddrIn from;
			unsigned int fromlen = sizeof(from);
			int len;
			while ((len = sceNetRecvfrom(fd, buf, sizeof(buf), SCE_NET_MSG_DONTWAIT, (SceNetSockaddr *)&from, &fromlen)) > 0){
				if (len >= (int)sizeof(STREAM_HELLO) && memcmp(buf, STREAM_HELLO, sizeof(STREAM_HELLO)) == 0){
					target = from;
					last_hello = sceKernelGetProcessTimeWide();
				}
				fromlen = sizeof(from);
			}
			// The socket can break (e.g. after the Vita resumes from sleep), recreate it
			if (len < 0 && len != SCE_NET_ERROR_EAGAIN) break;

			uint64_t now = sceKernelGetProcessTimeWide();
			if (!last_hello || now - last_hello > STREAM_TIMEOUT_MS * 1000ULL){
				last_hello = 0;
				sceKernelDelayThread(10 * 1000);
				continue;
			}

			// Anything new? Buttons (fast buttons included), sticks, touch and motion all end up in the packet;
			// the timestamp always changes so it isn't compared
			PadPacketV2 pkg;
			fill_packet_v2(&pkg, &latch);
			uint32_t timestamp = pkg.timestamp;
			pkg.timestamp = last.timestamp;
			int changed = memcmp(&pkg, &last, sizeof(pkg)) != 0;
			pkg.timestamp = timestamp;
			if (changed || now - last_send >= STREAM_MAX_INTERVAL_MS * 1000ULL){
				packet.seq++;
				packet.pad = pkg;
				if (sceNetSendto(fd, &packet, sizeof(packet), 0, (SceNetSockaddr *)&target, sizeof(target)) >= 0) input_rate_stream();
				last = pkg;
				last_send = now;
			}
			sceKernelDelayThread(STREAM_CHECK_US);
		}
		sceNetSocketClose(fd);
		sceKernelDelayThread(500 * 1000);
	}
	return 0;
}

// Answers discovery broadcasts so the PC client can find the Vita without typing its IP
static int discovery_thread(unsigned int args, void* argp){
	char buf[64];
	for (;;){
		int fd = create_socket(SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP, DISCOVERY_PORT);
		if (fd < 0){
			sceKernelDelayThread(1000 * 1000);
			continue;
		}
		for (;;){
			SceNetSockaddrIn from;
			unsigned int fromlen = sizeof(from);
			int len = sceNetRecvfrom(fd, buf, sizeof(buf), 0, (SceNetSockaddr *)&from, &fromlen);
			if (len < 0) break;
			if (len >= (int)strlen(DISCOVERY_REQUEST) && memcmp(buf, DISCOVERY_REQUEST, strlen(DISCOVERY_REQUEST)) == 0)
				sceNetSendto(fd, DISCOVERY_REPLY, strlen(DISCOVERY_REPLY), 0, (SceNetSockaddr *)&from, fromlen);
		}
		sceNetSocketClose(fd);
		sceKernelDelayThread(500 * 1000);
	}
	return 0;
}

// Returns 1 and fills ip if the Vita is connected to a network
static int get_ip(char *ip, size_t size){
	int state;
	if (sceNetCtlInetGetState(&state) < 0 || state != SCE_NETCTL_STATE_CONNECTED) return 0;
	SceNetCtlInfo info;
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) < 0) return 0;
	snprintf(ip, size, "%s", info.ip_address);
	return 1;
}

vita2d_pgf* debug_font;

int main(){

	// Enabling analog, touch and motion support
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, 1);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, 1);
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &panel_info[SCE_TOUCH_PORT_FRONT]);
	sceTouchGetPanelInfo(SCE_TOUCH_PORT_BACK, &panel_info[SCE_TOUCH_PORT_BACK]);
	sceMotionStartSampling();
	touchzones_init();
	remap_load();
	ps_init();
	usb_init();
	input_rate_init();

	// Initializing graphics stuffs
	vita2d_init();
	vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
	debug_font = vita2d_load_default_pgf();
	ui_init(debug_font);

	// Initializing network stuffs
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	int ret = sceNetShowNetstat();
	if (ret == SCE_NET_ERROR_ENOTINIT) {
		SceNetInitParam initparam;
		initparam.memory = malloc(NET_INIT_SIZE);
		initparam.size = NET_INIT_SIZE;
		initparam.flags = 0;
		sceNetInit(&initparam);
	}
	sceNetCtlInit();

	// Starting server threads
	SceUID thread = sceKernelCreateThread("VitaPad Thread", &server_thread, 0x10000100, 0x10000, 0, 0, NULL);
	sceKernelStartThread(thread, 0, NULL);
	SceUID discovery = sceKernelCreateThread("VitaPad Discovery Thread", &discovery_thread, 0x10000100, 0x4000, 0, 0, NULL);
	sceKernelStartThread(discovery, 0, NULL);
	SceUID stream = sceKernelCreateThread("VitaPad Stream Thread", &stream_thread, 0x10000100, 0x4000, 0, 0, NULL);
	sceKernelStartThread(stream, 0, NULL);

	char vita_ip[32];
	int has_ip = 0;
	int screen_off = 0;
	int combo_frames = 0;
	int menu_combo_frames = 0;
	unsigned int frame = 0;
	for (;;){

		// Refreshing IP and battery once per second, the Wi-Fi connection may not be up yet or may change
		if (frame % 60 == 0){
			has_ip = get_ip(vita_ip, sizeof(vita_ip));
			battery = scePowerGetBatteryLifePercent();
		}
		frame++;

		// Keeping the Vita from going to sleep (and dropping the connection) while a client is connected
		if (connected) sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);

		// Screen toggle
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if ((pad.buttons & SCREEN_TOGGLE_COMBO) == SCREEN_TOGGLE_COMBO){
			if (++combo_frames == SCREEN_TOGGLE_FRAMES) screen_off = !screen_off;
		}else combo_frames = 0;

		// Remap menu toggle
		if ((pad.buttons & REMAP_MENU_COMBO) == REMAP_MENU_COMBO){
			if (++menu_combo_frames == SCREEN_TOGGLE_FRAMES){
				if (remap_menu_open) remap_menu_open = 0;
				else {
					remap_menu_open_now();
					screen_off = 0;
				}
			}
		}else menu_combo_frames = 0;
		if (remap_menu_open) remap_menu_update(pad.buttons);
		ps_update();

		fast_update();
		ui_update(&pad, has_ip ? vita_ip : NULL, connected, battery, screen_off);

		vita2d_start_drawing();
		vita2d_clear_screen();
		if (remap_menu_open){
			remap_menu_draw(debug_font);
		}
		// With the screen off we draw a plain black frame: OLED pixels are off, so no burn-in
		else if (!screen_off) ui_draw();
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		ui_frame_rendered();
		vita2d_swap_buffers();
		ui_frame_swapped();

	}

}
