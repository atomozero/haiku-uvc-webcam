/*
 * Sonix SN9C20x Bridge Probe Tool
 * Reads bridge registers and probes I2C bus to identify chip and sensor.
 * Uses vendor-specific USB control transfers (same protocol as Linux sn9c20x).
 *
 * Build: g++ -O2 -o sonix_probe sonix_probe.cpp -lbe -ldevice
 * Run:   sonix_probe
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <USBKit.h>


// Sonix vendor USB control transfer helpers
static int
sonix_reg_read(BUSBDevice& dev, uint16 index, uint8* buf, uint16 len)
{
	return dev.ControlTransfer(
		USB_REQTYPE_DEVICE_IN | USB_REQTYPE_VENDOR,
		0x00,	// bRequest: read register
		0x00,	// wValue
		index,	// wIndex: register address
		len,	// wLength
		buf);
}


static int
sonix_reg_write(BUSBDevice& dev, uint16 index, const uint8* buf, uint16 len)
{
	return dev.ControlTransfer(
		USB_REQTYPE_DEVICE_OUT | USB_REQTYPE_VENDOR,
		0x08,	// bRequest: write register
		0x00,	// wValue
		index,	// wIndex: register address
		len,	// wLength
		(void*)buf);
}


// I2C read via Sonix bridge
static int
sonix_i2c_read(BUSBDevice& dev, uint8 slave, uint8 reg, uint8* val)
{
	uint8 row[8];
	memset(row, 0, sizeof(row));

	// Write register address
	row[0] = 0x81 | (1 << 4);	// I2C write 1 byte
	row[1] = slave;
	row[2] = reg;
	sonix_reg_write(dev, 0x10c0, row, 8);
	snooze(50000);

	// Read result
	uint8 result = 0;
	sonix_reg_read(dev, 0x10c2, &result, 1);
	*val = result;
	return 0;
}


class SonixProbeRoster : public BUSBRoster {
public:
	virtual status_t DeviceAdded(BUSBDevice* device)
	{
		if (device->VendorID() != 0x0c45)
			return B_ERROR;

		printf("=== Sonix Device Found ===\n");
		printf("VID:PID = %04x:%04x\n", device->VendorID(), device->ProductID());
		printf("Product: %s\n", device->ProductString());
		printf("Manufacturer: %s\n", device->ManufacturerString());
		printf("Configs: %d\n", device->CountConfigurations());

		// Try to read bridge chip ID registers
		printf("\n--- Bridge Register Probe ---\n");

		// Key Sonix registers
		struct { uint16 reg; const char* name; } regs[] = {
			{0x1000, "Chip ID high"},
			{0x1001, "Chip ID low"},
			{0x1002, "System control"},
			{0x1006, "USB type"},
			{0x1007, "System clock"},
			{0x1020, "GPIO direction"},
			{0x1040, "Sensor type/flags"},
			{0x1061, "Sensor clock"},
			{0x1065, "Sensor I2C"},
			{0x10c0, "I2C control"},
			{0x10e0, "Format control"},
			{0x10f0, "Frame size high"},
			{0x10f1, "Frame size low"},
		};

		for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
			uint8 val = 0;
			int ret = sonix_reg_read(*device, regs[i].reg, &val, 1);
			if (ret >= 0)
				printf("  [0x%04x] %-20s = 0x%02x (%d)\n",
					regs[i].reg, regs[i].name, val, val);
			else
				printf("  [0x%04x] %-20s = READ FAILED (%d)\n",
					regs[i].reg, regs[i].name, ret);
		}

		// Read chip ID block (16 bytes from 0x1000)
		printf("\n--- Chip ID Block (0x1000-0x100f) ---\n");
		uint8 idBlock[16];
		int ret = sonix_reg_read(*device, 0x1000, idBlock, 16);
		if (ret >= 0) {
			printf("  ");
			for (int i = 0; i < 16; i++)
				printf("%02x ", idBlock[i]);
			printf("\n");
		}

		// Probe known I2C sensor addresses
		printf("\n--- I2C Sensor Probe ---\n");
		uint8 sensorAddrs[] = {
			0x30,	// OV7670, OV9650, OV9655
			0x21,	// OV7660, OV7670 (alt)
			0x5c,	// MT9V011
			0x5d,	// MT9V111, MT9V112
			0x48,	// MT9M001, MT9M111
			0x11,	// HV7131R
			0x60,	// SOI968
			0x20,	// Generic
		};

		const char* sensorNames[] = {
			"OV9650/OV9655",
			"OV7660/OV7670",
			"MT9V011",
			"MT9V111/MT9V112",
			"MT9M001/MT9M111",
			"HV7131R",
			"SOI968",
			"Generic",
		};

		for (size_t i = 0; i < sizeof(sensorAddrs) / sizeof(sensorAddrs[0]); i++) {
			uint8 val = 0;
			// Try reading sensor register 0x00 (chip ID on most sensors)
			uint8 row[8] = {0};
			row[0] = 0x81 | (1 << 4);
			row[1] = sensorAddrs[i];
			row[2] = 0x00;	// Register 0 = chip ID

			sonix_reg_write(*device, 0x10c0, row, 8);
			snooze(50000);

			uint8 status = 0;
			sonix_reg_read(*device, 0x10c0, &status, 1);

			uint8 result = 0;
			sonix_reg_read(*device, 0x10c2, &result, 1);

			printf("  I2C addr 0x%02x (%s): status=0x%02x data=0x%02x %s\n",
				sensorAddrs[i], sensorNames[i], status, result,
				(status & 0x04) ? "" : "*** RESPONDED ***");
		}

		// List all interfaces and endpoints
		printf("\n--- USB Interfaces ---\n");
		const BUSBConfiguration* config = device->ActiveConfiguration();
		if (config) {
			for (uint32 i = 0; i < config->CountInterfaces(); i++) {
				const BUSBInterface* iface = config->InterfaceAt(i);
				if (!iface) continue;
				printf("  Interface %u: class=%u sub=%u proto=%u alts=%u\n",
					i, iface->Class(), iface->Subclass(),
					iface->Protocol(), iface->CountAlternates());
				for (uint32 e = 0; e < iface->CountEndpoints(); e++) {
					const BUSBEndpoint* ep = iface->EndpointAt(e);
					if (!ep) continue;
					printf("    EP%u: %s %s maxPkt=%u\n",
						e, ep->IsInput() ? "IN" : "OUT",
						ep->IsBulk() ? "BULK" :
						ep->IsIsochronous() ? "ISO" :
						ep->IsInterrupt() ? "INT" : "???",
						ep->MaxPacketSize());
				}
			}
		}

		printf("\n=== Probe Complete ===\n");
		fProbed = true;
		return B_OK;
	}

	virtual void DeviceRemoved(BUSBDevice* device) {}

	bool fProbed = false;
};


int
main()
{
	printf("Sonix SN9C20x Bridge Probe\n");
	printf("Scanning for Microdia (0c45) devices...\n\n");

	SonixProbeRoster roster;
	roster.Start();

	// Wait up to 3 seconds for device
	for (int i = 0; i < 30 && !roster.fProbed; i++)
		snooze(100000);

	if (!roster.fProbed)
		printf("No Microdia device found.\n");

	roster.Stop();
	return roster.fProbed ? 0 : 1;
}
