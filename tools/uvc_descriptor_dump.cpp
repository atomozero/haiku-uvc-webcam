/*
 * UVC descriptor dump tool.
 *
 * Prints the raw class-specific USB descriptors (VideoControl / VideoStreaming)
 * of the connected UVC camera(s) as C byte arrays, so real device data can be
 * captured and stored as regression fixtures for the descriptor validators in
 * tests/test_descriptors.cpp (turning the reconstructed corpus into true
 * captures).
 *
 * Build: g++ -O2 -o uvc_descriptor_dump uvc_descriptor_dump.cpp -lbe -ldevice
 * Run:   uvc_descriptor_dump [PID_hex]    (no arg: dump every UVC camera found)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <OS.h>
#include <USB3.h>
#include <USBKit.h>


static void
dump_descriptor(const uint8* d, size_t len, const char* tag, uint32 idx)
{
	printf("\t// %s[%u]  bLength=%u bDescriptorType=0x%02x bDescriptorSubtype=0x%02x\n",
		tag, (unsigned)idx, (unsigned)(len >= 1 ? d[0] : 0),
		(unsigned)(len >= 2 ? d[1] : 0), (unsigned)(len >= 3 ? d[2] : 0));
	printf("\tstatic const uint8 %s_%u[] = {", tag, (unsigned)idx);
	for (size_t i = 0; i < len; i++) {
		if (i % 12 == 0)
			printf("\n\t\t");
		printf("0x%02x, ", d[i]);
	}
	printf("\n\t};\n");
}


static void
dump_alternate(const BUSBInterface* alt, uint32 cfgIdx, uint32 ifIdx)
{
	if (alt == NULL)
		return;

	uint8 buffer[512];
	usb_descriptor* generic = (usb_descriptor*)buffer;
	char tag[80];
	snprintf(tag, sizeof(tag), "cfg%u_if%u_cls%u_sub%u", (unsigned)cfgIdx,
		(unsigned)ifIdx, (unsigned)alt->Class(), (unsigned)alt->Subclass());

	for (uint32 k = 0;
			alt->OtherDescriptorAt(k, generic, sizeof(buffer)) == B_OK; k++) {
		size_t len = generic->generic.length;
		if (len < 2 || len > sizeof(buffer))
			continue;
		dump_descriptor((const uint8*)generic, len, tag, k);
	}
}


class Dump : public BUSBRoster {
public:
					Dump(uint16 pid) : fTargetPID(pid), fFound(false) {}

	bool			Found() const { return fFound; }

	status_t DeviceAdded(BUSBDevice* dev)
	{
		if (fTargetPID != 0 && dev->ProductID() != fTargetPID)
			return B_ERROR;

		// Only UVC devices: at least one VideoControl interface (class 14 /
		// subclass 1).
		const BUSBConfiguration* active = dev->ActiveConfiguration();
		if (active == NULL)
			return B_ERROR;
		bool isUVC = false;
		for (uint32 i = 0; i < active->CountInterfaces(); i++) {
			const BUSBInterface* iface = active->InterfaceAt(i);
			if (iface != NULL && iface->Class() == 14 && iface->Subclass() == 1) {
				isUVC = true;
				break;
			}
		}
		if (!isUVC)
			return B_ERROR;

		printf("// === %04x:%04x  %s ===\n", dev->VendorID(), dev->ProductID(),
			dev->ProductString() != NULL ? dev->ProductString() : "?");

		for (uint32 c = 0; c < dev->CountConfigurations(); c++) {
			const BUSBConfiguration* cfg = dev->ConfigurationAt(c);
			if (cfg == NULL)
				continue;
			for (uint32 i = 0; i < cfg->CountInterfaces(); i++) {
				const BUSBInterface* iface = cfg->InterfaceAt(i);
				if (iface == NULL)
					continue;
				// Class-specific descriptors (formats/frames) live on the
				// alternate settings; dump each.
				for (uint32 a = 0; a < iface->CountAlternates(); a++)
					dump_alternate(iface->AlternateAt(a), c, i);
			}
		}

		fFound = true;
		return B_OK;
	}

	void DeviceRemoved(BUSBDevice*) {}

private:
	uint16	fTargetPID;
	bool	fFound;
};


int
main(int argc, char* argv[])
{
	uint16 pid = 0;	// 0 = dump every UVC camera
	if (argc > 1)
		pid = (uint16)strtol(argv[1], NULL, 16);

	Dump dump(pid);
	dump.Start();
	for (int i = 0; i < 30 && !dump.Found(); i++)
		snooze(100000);
	dump.Stop();

	if (!dump.Found()) {
		fprintf(stderr, "No UVC camera found%s.\n",
			pid != 0 ? " with that PID" : "");
		return 1;
	}
	return 0;
}
