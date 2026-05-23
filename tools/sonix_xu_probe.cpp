/*
 * Sonix XU (Extension Unit) Probe via UVC standard SET_CUR/GET_CUR
 * Reads chip registers via the Sonix ASIC_RW XU control.
 * This uses UVC Extension Unit protocol, NOT vendor-specific USB transfers.
 *
 * Build: g++ -O2 -o sonix_xu_probe sonix_xu_probe.cpp -lbe -ldevice
 * Run:   sonix_xu_probe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <USBKit.h>

// UVC request codes
#define UVC_SET_CUR		0x01
#define UVC_GET_CUR		0x81

// Sonix XU selectors
#define XU_SONIX_SYS_ASIC_RW		0x01
#define XU_SONIX_SYS_FLASH_CTRL	0x03
#define XU_SONIX_SYS_FRAME_INFO	0x06
#define XU_SONIX_SYS_H264_CTRL		0x07
#define XU_SONIX_SYS_MJPG_CTRL		0x08

// Sonix chip IDs
#define SONIX_SN9C291_CHIPID	0x10
#define SONIX_SN9C292_CHIPID	0x20


// UVC XU control transfer: SET_CUR
static int
xu_set_cur(BUSBDevice& dev, uint8 unitId, uint8 selector,
	uint8* data, uint16 size, uint16 ifIndex)
{
	return dev.ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
		UVC_SET_CUR,
		(selector << 8),
		(unitId << 8) | ifIndex,
		size,
		data);
}


// UVC XU control transfer: GET_CUR
static int
xu_get_cur(BUSBDevice& dev, uint8 unitId, uint8 selector,
	uint8* data, uint16 size, uint16 ifIndex)
{
	return dev.ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		UVC_GET_CUR,
		(selector << 8),
		(unitId << 8) | ifIndex,
		size,
		data);
}


// Read a Sonix ASIC register via XU
static int
sonix_asic_read(BUSBDevice& dev, uint16 addr, uint8* value,
	uint8 unitId, uint16 ifIndex)
{
	uint8 data[4];
	data[0] = addr & 0xFF;
	data[1] = (addr >> 8) & 0xFF;
	data[2] = 0x00;
	data[3] = 0xFF;	// Dummy write flag

	int ret = xu_set_cur(dev, unitId, XU_SONIX_SYS_ASIC_RW, data, 4, ifIndex);
	if (ret < 0) return ret;

	snooze(10000);

	ret = xu_get_cur(dev, unitId, XU_SONIX_SYS_ASIC_RW, data, 4, ifIndex);
	if (ret < 0) return ret;

	*value = data[2];
	return 0;
}


class SonixXUProbeRoster : public BUSBRoster {
public:
	virtual status_t DeviceAdded(BUSBDevice* device)
	{
		if (device->VendorID() != 0x0c45)
			return B_ERROR;

		printf("=== Sonix XU Probe: %04x:%04x ===\n",
			device->VendorID(), device->ProductID());
		printf("Product: %s\n", device->ManufacturerString());

		// Find the VideoControl interface index
		const BUSBConfiguration* config = device->ActiveConfiguration();
		if (!config) {
			printf("No active configuration!\n");
			return B_ERROR;
		}

		uint16 vcIfIndex = 0;
		bool foundVC = false;

		for (uint32 i = 0; i < config->CountInterfaces(); i++) {
			const BUSBInterface* iface = config->InterfaceAt(i);
			if (iface && iface->Class() == 14 && iface->Subclass() == 1) {
				vcIfIndex = i;
				foundVC = true;
				printf("VideoControl interface: %u\n", i);
				break;
			}
		}

		if (!foundVC) {
			printf("No VideoControl interface found!\n");
			return B_ERROR;
		}

		// Try different XU unit IDs (commonly 3 or 4 for Sonix)
		printf("\n--- Trying XU Unit ID 3 (System) ---\n");
		_ProbeUnit(*device, 3, vcIfIndex);

		printf("\n--- Trying XU Unit ID 4 (User) ---\n");
		_ProbeUnit(*device, 4, vcIfIndex);

		// Try to identify chip via known register addresses
		printf("\n--- Chip Identification ---\n");
		for (uint8 unitId = 3; unitId <= 4; unitId++) {
			uint8 chipId = 0;
			// Read chip ID register (address 0x0000)
			int ret = sonix_asic_read(*device, 0x0000, &chipId, unitId, vcIfIndex);
			if (ret >= 0) {
				printf("  Unit %d: Chip ID register 0x0000 = 0x%02x", unitId, chipId);
				if (chipId == SONIX_SN9C291_CHIPID)
					printf(" (SN9C291 series)");
				else if (chipId == SONIX_SN9C292_CHIPID)
					printf(" (SN9C292 series)");
				printf("\n");
			} else {
				printf("  Unit %d: Read FAILED (ret=%d)\n", unitId, ret);
			}
		}

		fProbed = true;
		printf("\n=== Probe Complete ===\n");
		return B_OK;
	}

	virtual void DeviceRemoved(BUSBDevice* device) {}

	bool fProbed = false;

private:
	void _ProbeUnit(BUSBDevice& dev, uint8 unitId, uint16 ifIndex)
	{
		// Try reading ASIC registers at key addresses
		struct { uint16 addr; const char* name; } regs[] = {
			{0x0000, "Chip ID"},
			{0x0001, "Chip Rev"},
			{0x0100, "System Clock"},
			{0x0203, "DRAM Config"},
			{0x0716, "DRAM Size"},
			{0x1000, "USB Config"},
			{0x1061, "Sensor Clock"},
			{0x10e0, "Format Control"},
			{0x10f0, "Frame Size Hi"},
			{0x10f1, "Frame Size Lo"},
		};

		for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
			uint8 val = 0;
			int ret = sonix_asic_read(dev, regs[i].addr, &val, unitId, ifIndex);
			if (ret >= 0) {
				printf("  [0x%04x] %-18s = 0x%02x (%3d)\n",
					regs[i].addr, regs[i].name, val, val);
			} else {
				printf("  [0x%04x] %-18s = FAILED (%d)\n",
					regs[i].addr, regs[i].name, ret);
			}
		}
	}
};


int
main()
{
	printf("Sonix XU (Extension Unit) Probe\n");
	printf("Using UVC SET_CUR/GET_CUR protocol (not vendor USB)\n\n");

	SonixXUProbeRoster roster;
	roster.Start();

	for (int i = 0; i < 30 && !roster.fProbed; i++)
		snooze(100000);

	if (!roster.fProbed)
		printf("No Microdia device found.\n");

	roster.Stop();
	return roster.fProbed ? 0 : 1;
}
