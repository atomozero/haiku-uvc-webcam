/*
 * UVC Controls Probe - discovers which camera/processing controls a webcam supports
 * Queries GET_INFO for each standard UVC control to determine capabilities.
 *
 * Build: g++ -O2 -o uvc_controls_probe uvc_controls_probe.cpp -lbe -ldevice
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <USBKit.h>

#define UVC_GET_CUR  0x81
#define UVC_GET_MIN  0x82
#define UVC_GET_MAX  0x83
#define UVC_GET_INFO 0x86
#define UVC_GET_DEF  0x87

struct ControlInfo {
	uint8 selector;
	const char* name;
	uint8 size; // data size in bytes
};

// Processing Unit controls (Unit ID typically 2)
static ControlInfo sPUControls[] = {
	{0x01, "PU Backlight Compensation", 2},
	{0x02, "PU Brightness", 2},
	{0x03, "PU Contrast", 2},
	{0x04, "PU Gain", 2},
	{0x05, "PU Power Line Frequency", 1},
	{0x06, "PU Hue", 2},
	{0x07, "PU Saturation", 2},
	{0x08, "PU Sharpness", 2},
	{0x09, "PU Gamma", 2},
	{0x0A, "PU White Balance Temperature", 2},
	{0x0B, "PU White Balance Temperature Auto", 1},
	{0x0C, "PU White Balance Component", 4},
	{0x0D, "PU White Balance Component Auto", 1},
	{0x0E, "PU Digital Multiplier", 2},
	{0x0F, "PU Digital Multiplier Limit", 2},
	{0x10, "PU Hue Auto", 1},
	{0x11, "PU Analog Video Standard", 1},
	{0x12, "PU Analog Lock Status", 1},
	{0x13, "PU Contrast Auto", 1},
};

// Camera Terminal controls (Unit ID typically 1)
static ControlInfo sCTControls[] = {
	{0x01, "CT Scanning Mode", 1},
	{0x02, "CT Auto-Exposure Mode", 1},
	{0x03, "CT Auto-Exposure Priority", 1},
	{0x04, "CT Exposure Time (Absolute)", 4},
	{0x05, "CT Exposure Time (Relative)", 1},
	{0x06, "CT Focus (Absolute)", 2},
	{0x07, "CT Focus (Relative)", 2},
	{0x08, "CT Iris (Absolute)", 2},
	{0x09, "CT Iris (Relative)", 1},
	{0x0A, "CT Zoom (Absolute)", 2},
	{0x0B, "CT Zoom (Relative)", 3},
	{0x0C, "CT PanTilt (Absolute)", 8},
	{0x0D, "CT PanTilt (Relative)", 4},
	{0x0E, "CT Roll (Absolute)", 2},
	{0x0F, "CT Roll (Relative)", 2},
	{0x11, "CT Focus Auto", 1},
	{0x12, "CT Privacy", 1},
	{0x13, "CT Focus Simple", 1},
	{0x14, "CT Window", 4},
	{0x15, "CT Region of Interest", 10},
};


static int
uvc_get(BUSBDevice& dev, uint8 request, uint8 unitId, uint8 selector,
	uint8* data, uint16 size, uint16 ifIndex)
{
	return dev.ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		request,
		(selector << 8),
		(unitId << 8) | ifIndex,
		size,
		data);
}


static void
probe_controls(BUSBDevice& dev, const char* unitName, uint8 unitId,
	uint16 ifIndex, ControlInfo* controls, int count)
{
	printf("\n=== %s (Unit %d) ===\n", unitName, unitId);
	int supported = 0;

	for (int i = 0; i < count; i++) {
		uint8 info = 0;
		int ret = uvc_get(dev, UVC_GET_INFO, unitId, controls[i].selector,
			&info, 1, ifIndex);

		if (ret < 0)
			continue; // not supported

		bool supGet = (info & 0x01) != 0;
		bool supSet = (info & 0x02) != 0;
		bool supAuto = (info & 0x04) != 0;

		printf("  [0x%02x] %-35s GET=%d SET=%d AUTO=%d",
			controls[i].selector, controls[i].name,
			supGet, supSet, supAuto);

		// Try to read current, min, max values
		uint8 cur[16] = {}, mn[16] = {}, mx[16] = {}, def[16] = {};
		int sz = controls[i].size;

		if (supGet) {
			uvc_get(dev, UVC_GET_CUR, unitId, controls[i].selector, cur, sz, ifIndex);
			uvc_get(dev, UVC_GET_MIN, unitId, controls[i].selector, mn, sz, ifIndex);
			uvc_get(dev, UVC_GET_MAX, unitId, controls[i].selector, mx, sz, ifIndex);
			uvc_get(dev, UVC_GET_DEF, unitId, controls[i].selector, def, sz, ifIndex);

			if (sz == 1)
				printf("  cur=%d min=%d max=%d def=%d", cur[0], mn[0], mx[0], def[0]);
			else if (sz == 2) {
				int16 c = cur[0] | (cur[1] << 8);
				int16 m = mn[0] | (mn[1] << 8);
				int16 x = mx[0] | (mx[1] << 8);
				int16 d = def[0] | (def[1] << 8);
				printf("  cur=%d min=%d max=%d def=%d", c, m, x, d);
			} else if (sz == 4) {
				int32 c = cur[0] | (cur[1]<<8) | (cur[2]<<16) | (cur[3]<<24);
				int32 m = mn[0] | (mn[1]<<8) | (mn[2]<<16) | (mn[3]<<24);
				int32 x = mx[0] | (mx[1]<<8) | (mx[2]<<16) | (mx[3]<<24);
				int32 d = def[0] | (def[1]<<8) | (def[2]<<16) | (def[3]<<24);
				printf("  cur=%d min=%d max=%d def=%d", c, m, x, d);
			}
		}

		printf("\n");
		supported++;
	}

	printf("  Total: %d controls supported\n", supported);
}


class Probe : public BUSBRoster {
public:
	bool done = false;
	uint16 targetPID;

	Probe(uint16 pid) : targetPID(pid) {}

	status_t DeviceAdded(BUSBDevice* dev) {
		if (dev->ProductID() != targetPID)
			return B_ERROR;

		printf("=== UVC Controls Probe: %04x:%04x %s ===\n",
			dev->VendorID(), dev->ProductID(), dev->ProductString());

		// Find VideoControl interface
		const BUSBConfiguration* cfg = dev->ActiveConfiguration();
		if (!cfg) return B_ERROR;

		uint16 vcIf = 0;
		for (uint32 i = 0; i < cfg->CountInterfaces(); i++) {
			const BUSBInterface* iface = cfg->InterfaceAt(i);
			if (iface && iface->Class() == 14 && iface->Subclass() == 1) {
				vcIf = i;
				break;
			}
		}

		// Try common unit IDs for Processing Unit and Camera Terminal
		// PU is usually 2 or 3, CT is usually 1
		for (uint8 puId = 2; puId <= 5; puId++) {
			uint8 info = 0;
			if (uvc_get(*dev, UVC_GET_INFO, puId, 0x02, &info, 1, vcIf) >= 0) {
				probe_controls(*dev, "Processing Unit", puId, vcIf,
					sPUControls, sizeof(sPUControls)/sizeof(sPUControls[0]));
				break;
			}
		}

		for (uint8 ctId = 1; ctId <= 4; ctId++) {
			uint8 info = 0;
			if (uvc_get(*dev, UVC_GET_INFO, ctId, 0x02, &info, 1, vcIf) >= 0) {
				probe_controls(*dev, "Camera Terminal", ctId, vcIf,
					sCTControls, sizeof(sCTControls)/sizeof(sCTControls[0]));
				break;
			}
		}

		done = true;
		return B_OK;
	}

	void DeviceRemoved(BUSBDevice*) {}
};


int main(int argc, char* argv[])
{
	uint16 pid = 0x0001; // AUKEY default
	if (argc > 1) pid = strtol(argv[1], NULL, 16);

	printf("Probing USB device with PID 0x%04x...\n", pid);

	Probe p(pid);
	p.Start();
	for (int i = 0; i < 30 && !p.done; i++) snooze(100000);
	p.Stop();

	if (!p.done) printf("Device not found.\n");
	return p.done ? 0 : 1;
}
