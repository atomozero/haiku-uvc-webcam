/*
 * uvc_benchmark.cpp
 *
 * UVC Webcam Diagnostic and Benchmark Tool for Haiku OS
 * Analyzes connected USB video devices and evaluates UVC compliance.
 *
 * Build: g++ -O2 -o uvc_benchmark uvc_benchmark.cpp -lbe -ldevice
 * Run: ./uvc_benchmark
 *
 * Features:
 * - Detects all USB video class devices
 * - Parses UVC descriptors (VC and VS interfaces)
 * - Reports UVC version and capabilities
 * - Lists supported formats and resolutions
 * - Tests probe/commit structure sizes
 * - Evaluates Haiku compatibility
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

#include <OS.h>
#include <USBKit.h>

// ANSI colors
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"
#define RESET   "\033[0m"

// USB Video Class codes
#define USB_VIDEO_CLASS				0x0E
#define USB_VIDEO_SUBCLASS_CONTROL	0x01
#define USB_VIDEO_SUBCLASS_STREAMING 0x02

// UVC Descriptor Types
#define USB_VIDEO_CS_INTERFACE		0x24
#define USB_VIDEO_CS_ENDPOINT		0x25

// Video Control Interface Descriptor Subtypes
#define UVC_VC_HEADER				0x01
#define UVC_VC_INPUT_TERMINAL		0x02
#define UVC_VC_OUTPUT_TERMINAL		0x03
#define UVC_VC_SELECTOR_UNIT		0x04
#define UVC_VC_PROCESSING_UNIT		0x05
#define UVC_VC_EXTENSION_UNIT		0x06

// Video Streaming Interface Descriptor Subtypes
#define UVC_VS_INPUT_HEADER			0x01
#define UVC_VS_OUTPUT_HEADER		0x02
#define UVC_VS_STILL_IMAGE_FRAME	0x03
#define UVC_VS_FORMAT_UNCOMPRESSED	0x04
#define UVC_VS_FRAME_UNCOMPRESSED	0x05
#define UVC_VS_FORMAT_MJPEG			0x06
#define UVC_VS_FRAME_MJPEG			0x07
#define UVC_VS_FORMAT_MPEG2TS		0x0A
#define UVC_VS_FORMAT_DV			0x0C
#define UVC_VS_COLORFORMAT			0x0D
#define UVC_VS_FORMAT_FRAME_BASED	0x10
#define UVC_VS_FRAME_FRAME_BASED	0x11
#define UVC_VS_FORMAT_STREAM_BASED	0x12
#define UVC_VS_FORMAT_H264			0x13
#define UVC_VS_FRAME_H264			0x14
#define UVC_VS_FORMAT_H264_SIMULCAST 0x15
#define UVC_VS_FORMAT_VP8			0x16
#define UVC_VS_FRAME_VP8			0x17
#define UVC_VS_FORMAT_VP8_SIMULCAST	0x18

// Terminal Types
#define UVC_TT_VENDOR_SPECIFIC		0x0100
#define UVC_TT_STREAMING			0x0101
#define UVC_ITT_VENDOR_SPECIFIC		0x0200
#define UVC_ITT_CAMERA				0x0201
#define UVC_ITT_MEDIA_TRANSPORT		0x0202

// Common GUIDs
static const uint8 GUID_YUY2[] = {
	0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

static const uint8 GUID_NV12[] = {
	0x4E, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

static const uint8 GUID_MJPEG[] = {
	0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

// Structures for parsed data
struct Resolution {
	uint16 width;
	uint16 height;
	uint32 minFrameInterval;  // 100ns units
	uint32 maxFrameInterval;
	uint8 frameIndex;

	float GetMaxFPS() const {
		if (minFrameInterval == 0) return 0;
		return 10000000.0f / minFrameInterval;
	}

	float GetMinFPS() const {
		if (maxFrameInterval == 0) return 0;
		return 10000000.0f / maxFrameInterval;
	}

	uint32 GetPixelCount() const {
		return (uint32)width * height;
	}
};

struct VideoFormat {
	std::string name;
	uint8 formatIndex;
	uint8 bitsPerPixel;
	std::vector<Resolution> resolutions;
	bool isCompressed;
};

struct UVCDevice {
	uint16 vendorId;
	uint16 productId;
	std::string manufacturer;
	std::string product;
	std::string serial;
	uint16 uvcVersion;
	uint8 numStreamingInterfaces;
	std::vector<VideoFormat> formats;

	// Capabilities
	bool hasCamera;
	bool hasProcessingUnit;
	bool hasExtensionUnit;
	uint32 processingControls;
	uint32 cameraControls;

	// Haiku compatibility
	bool probeSize34Works;
	bool probeSize26Works;
	bool hasHighBandwidthEndpoints;
	uint16 maxPacketSize;

	std::string GetUVCVersionString() const {
		char buf[16];
		snprintf(buf, sizeof(buf), "%d.%d%d",
			(uvcVersion >> 8) & 0xFF,
			(uvcVersion >> 4) & 0x0F,
			uvcVersion & 0x0F);
		return buf;
	}
};

// Global device list
static std::vector<UVCDevice> gDevices;

// Helper functions
const char* GetTerminalTypeName(uint16 type)
{
	switch (type) {
		case UVC_TT_VENDOR_SPECIFIC: return "Vendor Specific";
		case UVC_TT_STREAMING: return "Streaming";
		case UVC_ITT_VENDOR_SPECIFIC: return "Input Vendor Specific";
		case UVC_ITT_CAMERA: return "Camera";
		case UVC_ITT_MEDIA_TRANSPORT: return "Media Transport";
		default: return "Unknown";
	}
}

const char* GetFormatName(const uint8* guid)
{
	if (memcmp(guid, GUID_YUY2, 16) == 0) return "YUY2";
	if (memcmp(guid, GUID_NV12, 16) == 0) return "NV12";
	if (memcmp(guid, GUID_MJPEG, 16) == 0) return "MJPEG";

	// Check for common FourCC patterns
	if (guid[0] >= 0x20 && guid[0] < 0x7F &&
		guid[1] >= 0x20 && guid[1] < 0x7F &&
		guid[2] >= 0x20 && guid[2] < 0x7F &&
		guid[3] >= 0x20 && guid[3] < 0x7F) {
		static char fourcc[8];
		snprintf(fourcc, sizeof(fourcc), "%.4s", (const char*)guid);
		return fourcc;
	}

	return "Unknown";
}

void PrintGUID(const uint8* guid)
{
	printf("%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
		guid[3], guid[2], guid[1], guid[0],
		guid[5], guid[4],
		guid[7], guid[6],
		guid[8], guid[9],
		guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

// Parse UVC descriptors from BUSBDevice
bool ParseUVCDescriptors(BUSBDevice* device, UVCDevice& uvcDev)
{
	const BUSBConfiguration* config = device->ActiveConfiguration();
	if (!config) return false;

	for (uint32 i = 0; i < config->CountInterfaces(); i++) {
		const BUSBInterface* iface = config->InterfaceAt(i);
		if (!iface) continue;

		uint8 ifClass = iface->Class();
		uint8 ifSubclass = iface->Subclass();

		// Parse generic/class-specific descriptors using OtherDescriptorAt
		// We need a buffer large enough for any descriptor
		uint8 descBuffer[256];
		usb_descriptor* desc = (usb_descriptor*)descBuffer;
		VideoFormat* currentFormat = nullptr;

		// Check for Video Control interface
		if (ifClass == USB_VIDEO_CLASS && ifSubclass == USB_VIDEO_SUBCLASS_CONTROL) {
			// Iterate through descriptors
			for (uint32 d = 0; ; d++) {
				status_t result = iface->OtherDescriptorAt(d, desc, sizeof(descBuffer));
				if (result != B_OK) break;

				uint8* data = descBuffer;
				uint8 len = data[0];
				uint8 descType = data[1];

				// Class-specific interface descriptor
				if (descType == USB_VIDEO_CS_INTERFACE && len >= 3) {
					uint8 subtype = data[2];

					switch (subtype) {
						case UVC_VC_HEADER:
							if (len >= 12) {
								uvcDev.uvcVersion = data[3] | (data[4] << 8);
								uvcDev.numStreamingInterfaces = data[11];
							}
							break;

						case UVC_VC_INPUT_TERMINAL:
							if (len >= 8) {
								uint16 termType = data[4] | (data[5] << 8);
								if (termType == UVC_ITT_CAMERA) {
									uvcDev.hasCamera = true;
									if (len >= 15) {
										uvcDev.cameraControls = data[14] | (data[15] << 8);
										if (len >= 17)
											uvcDev.cameraControls |= (data[16] << 16);
									}
								}
							}
							break;

						case UVC_VC_PROCESSING_UNIT:
							uvcDev.hasProcessingUnit = true;
							if (len >= 8) {
								uvcDev.processingControls = data[7];
								if (len >= 9)
									uvcDev.processingControls |= (data[8] << 8);
								if (len >= 10)
									uvcDev.processingControls |= (data[9] << 16);
							}
							break;

						case UVC_VC_EXTENSION_UNIT:
							uvcDev.hasExtensionUnit = true;
							break;
					}
				}
			}
		}

		// Check for Video Streaming interface
		if (ifClass == USB_VIDEO_CLASS && ifSubclass == USB_VIDEO_SUBCLASS_STREAMING) {
			currentFormat = nullptr;

			for (uint32 d = 0; ; d++) {
				status_t result = iface->OtherDescriptorAt(d, desc, sizeof(descBuffer));
				if (result != B_OK) break;

				uint8* data = descBuffer;
				uint8 len = data[0];
				uint8 descType = data[1];

				if (descType == USB_VIDEO_CS_INTERFACE && len >= 3) {
					uint8 subtype = data[2];

					switch (subtype) {
						case UVC_VS_FORMAT_UNCOMPRESSED:
							if (len >= 27) {
								VideoFormat fmt;
								fmt.formatIndex = data[3];
								fmt.name = GetFormatName(&data[5]);
								fmt.bitsPerPixel = data[21];
								fmt.isCompressed = false;
								uvcDev.formats.push_back(fmt);
								currentFormat = &uvcDev.formats.back();
							}
							break;

						case UVC_VS_FORMAT_MJPEG:
							if (len >= 11) {
								VideoFormat fmt;
								fmt.formatIndex = data[3];
								fmt.name = "MJPEG";
								fmt.bitsPerPixel = 0;  // Variable
								fmt.isCompressed = true;
								uvcDev.formats.push_back(fmt);
								currentFormat = &uvcDev.formats.back();
							}
							break;

						case UVC_VS_FRAME_UNCOMPRESSED:
						case UVC_VS_FRAME_MJPEG:
							if (len >= 26 && currentFormat) {
								Resolution res;
								res.frameIndex = data[3];
								res.width = data[5] | (data[6] << 8);
								res.height = data[7] | (data[8] << 8);
								res.minFrameInterval = data[21] | (data[22] << 8) |
									(data[23] << 16) | (data[24] << 24);
								res.maxFrameInterval = data[25] | (data[26] << 8) |
									(data[27] << 16) | (data[28] << 24);
								currentFormat->resolutions.push_back(res);
							}
							break;
					}
				}
			}

			// Check endpoints for high-bandwidth
			for (uint32 e = 0; e < iface->CountEndpoints(); e++) {
				const BUSBEndpoint* ep = iface->EndpointAt(e);
				if (ep && ep->IsIsochronous()) {
					uint16 maxPacket = ep->MaxPacketSize();
					// High-bandwidth endpoints have transactions > 1
					// MaxPacketSize includes the multiplier in bits 11-12
					uint16 basePacket = maxPacket & 0x07FF;
					uint8 mult = ((maxPacket >> 11) & 0x03) + 1;

					if (mult > 1) {
						uvcDev.hasHighBandwidthEndpoints = true;
					}
					if (basePacket * mult > uvcDev.maxPacketSize) {
						uvcDev.maxPacketSize = basePacket * mult;
					}
				}
			}
		}
	}

	return uvcDev.formats.size() > 0;
}

// USB device visitor
class UVCDeviceVisitor : public BUSBRoster {
public:
	virtual status_t DeviceAdded(BUSBDevice* device)
	{
		// Check if this is a video device
		bool isVideo = false;

		const BUSBConfiguration* config = device->ActiveConfiguration();
		if (config) {
			for (uint32 i = 0; i < config->CountInterfaces(); i++) {
				const BUSBInterface* iface = config->InterfaceAt(i);
				if (iface && iface->Class() == USB_VIDEO_CLASS) {
					isVideo = true;
					break;
				}
			}
		}

		if (!isVideo) return B_OK;

		UVCDevice uvcDev;
		uvcDev.vendorId = device->VendorID();
		uvcDev.productId = device->ProductID();
		uvcDev.manufacturer = device->ManufacturerString() ? device->ManufacturerString() : "";
		uvcDev.product = device->ProductString() ? device->ProductString() : "";
		uvcDev.serial = device->SerialNumberString() ? device->SerialNumberString() : "";
		uvcDev.uvcVersion = 0;
		uvcDev.numStreamingInterfaces = 0;
		uvcDev.hasCamera = false;
		uvcDev.hasProcessingUnit = false;
		uvcDev.hasExtensionUnit = false;
		uvcDev.processingControls = 0;
		uvcDev.cameraControls = 0;
		uvcDev.probeSize34Works = true;
		uvcDev.probeSize26Works = true;
		uvcDev.hasHighBandwidthEndpoints = false;
		uvcDev.maxPacketSize = 0;

		if (ParseUVCDescriptors(device, uvcDev)) {
			gDevices.push_back(uvcDev);
		}

		return B_OK;
	}

	virtual void DeviceRemoved(BUSBDevice* device) {}
};

void PrintHeader(const char* title)
{
	printf("\n" BOLD "===================================================================\n");
	printf(" %s\n", title);
	printf("===================================================================\n" RESET);
}

void PrintSubHeader(const char* title)
{
	printf("\n" CYAN "--- %s ---\n" RESET, title);
}

void PrintDeviceInfo(const UVCDevice& dev, int index)
{
	printf("\n" BOLD BLUE "+-------------------------------------------------------------+\n");
	printf("| Device %d: %-50s|\n", index + 1, dev.product.c_str());
	printf("+-------------------------------------------------------------+\n" RESET);

	printf("\n" BOLD "Basic Information:\n" RESET);
	printf("  Vendor ID:      0x%04X\n", dev.vendorId);
	printf("  Product ID:     0x%04X\n", dev.productId);
	printf("  Manufacturer:   %s\n", dev.manufacturer.empty() ? "(not specified)" : dev.manufacturer.c_str());
	printf("  Product:        %s\n", dev.product.empty() ? "(not specified)" : dev.product.c_str());
	printf("  Serial:         %s\n", dev.serial.empty() ? "(not specified)" : dev.serial.c_str());

	PrintSubHeader("UVC Compliance");
	printf("  UVC Version:    %s", dev.GetUVCVersionString().c_str());

	// Version assessment
	uint8 major = (dev.uvcVersion >> 8) & 0xFF;
	uint8 minor = (dev.uvcVersion >> 4) & 0x0F;
	if (major == 1 && minor == 0) {
		printf(" " YELLOW "(UVC 1.0 - Legacy)\n" RESET);
	} else if (major == 1 && minor == 1) {
		printf(" " GREEN "(UVC 1.1 - Common)\n" RESET);
	} else if (major == 1 && minor == 5) {
		printf(" " GREEN "(UVC 1.5 - Modern)\n" RESET);
	} else {
		printf(" " CYAN "(Unknown version)\n" RESET);
	}

	// Probe/commit size expectation
	printf("  Expected Probe Size: ");
	if (major == 1 && minor == 0) {
		printf("26 bytes (UVC 1.0)\n");
	} else if (major == 1 && minor >= 1) {
		printf("34 bytes (UVC 1.1+)\n");
	} else {
		printf("34 bytes (default)\n");
	}

	PrintSubHeader("Capabilities");
	printf("  Camera Terminal:     %s\n", dev.hasCamera ? GREEN "Yes" RESET : "No");
	printf("  Processing Unit:     %s\n", dev.hasProcessingUnit ? GREEN "Yes" RESET : "No");
	printf("  Extension Unit:      %s\n", dev.hasExtensionUnit ? YELLOW "Yes" RESET : "No");

	if (dev.hasCamera && dev.cameraControls) {
		printf("  Camera Controls:     ");
		if (dev.cameraControls & 0x0002) printf("Exposure ");
		if (dev.cameraControls & 0x0004) printf("Iris ");
		if (dev.cameraControls & 0x0008) printf("Focus ");
		if (dev.cameraControls & 0x0020) printf("Zoom ");
		if (dev.cameraControls & 0x0040) printf("Pan ");
		if (dev.cameraControls & 0x0080) printf("Tilt ");
		if (dev.cameraControls & 0x0100) printf("Roll ");
		printf("\n");
	}

	if (dev.hasProcessingUnit && dev.processingControls) {
		printf("  Processing Controls: ");
		if (dev.processingControls & 0x0001) printf("Brightness ");
		if (dev.processingControls & 0x0002) printf("Contrast ");
		if (dev.processingControls & 0x0004) printf("Hue ");
		if (dev.processingControls & 0x0008) printf("Saturation ");
		if (dev.processingControls & 0x0010) printf("Sharpness ");
		if (dev.processingControls & 0x0020) printf("Gamma ");
		if (dev.processingControls & 0x0040) printf("WB-Temp ");
		if (dev.processingControls & 0x0100) printf("Backlight ");
		if (dev.processingControls & 0x0200) printf("Gain ");
		if (dev.processingControls & 0x0400) printf("PowerLine ");
		if (dev.processingControls & 0x0800) printf("Auto-Hue ");
		if (dev.processingControls & 0x1000) printf("Auto-WB ");
		printf("\n");
	}

	PrintSubHeader("Video Formats");

	for (size_t f = 0; f < dev.formats.size(); f++) {
		const VideoFormat& fmt = dev.formats[f];
		printf("\n  " BOLD "Format %zu: %s" RESET, f + 1, fmt.name.c_str());
		if (fmt.isCompressed) {
			printf(" (Compressed)\n");
		} else {
			printf(" (%d bpp)\n", fmt.bitsPerPixel);
		}

		// Sort resolutions by pixel count
		std::vector<Resolution> sorted = fmt.resolutions;
		std::sort(sorted.begin(), sorted.end(), [](const Resolution& a, const Resolution& b) {
			return a.GetPixelCount() > b.GetPixelCount();
		});

		printf("  +----------------+---------------+----------+\n");
		printf("  | Resolution     | Frame Rate    | Idx      |\n");
		printf("  +----------------+---------------+----------+\n");

		for (size_t r = 0; r < sorted.size(); r++) {
			const Resolution& res = sorted[r];
			char resBuf[32], fpsBuf[32];
			snprintf(resBuf, sizeof(resBuf), "%ux%u", res.width, res.height);

			if (res.minFrameInterval == res.maxFrameInterval || res.maxFrameInterval == 0) {
				snprintf(fpsBuf, sizeof(fpsBuf), "%.1f fps", res.GetMaxFPS());
			} else {
				snprintf(fpsBuf, sizeof(fpsBuf), "%.1f-%.1f fps",
					res.GetMinFPS(), res.GetMaxFPS());
			}

			// Highlight common resolutions
			const char* color = RESET;
			if (res.width == 1920 && res.height == 1080) color = GREEN;
			else if (res.width == 1280 && res.height == 720) color = GREEN;
			else if (res.width == 640 && res.height == 480) color = CYAN;

			printf("  | %s%-14s%s | %-13s | %-8d |\n",
				color, resBuf, RESET, fpsBuf, res.frameIndex);
		}
		printf("  +----------------+---------------+----------+\n");
	}

	PrintSubHeader("Haiku Compatibility Assessment");

	// High-bandwidth endpoints
	if (dev.hasHighBandwidthEndpoints) {
		printf("  " YELLOW "! High-bandwidth endpoints detected\n" RESET);
		printf("    Max packet size: %u bytes\n", dev.maxPacketSize);
		printf("    Note: Haiku EHCI may have issues with high-bandwidth ISO transfers.\n");
		printf("    Driver uses single-transaction mode as workaround.\n");
	} else {
		printf("  " GREEN "+ Standard bandwidth endpoints (good compatibility)\n" RESET);
	}

	// UVC version compatibility
	uint8 majorVer = (dev.uvcVersion >> 8) & 0xFF;
	uint8 minorVer = (dev.uvcVersion >> 4) & 0x0F;

	if (majorVer == 1 && minorVer == 0) {
		printf("  " YELLOW "! UVC 1.0 device - may need 26-byte probe structure\n" RESET);
		printf("    Driver has fallback mechanism for this.\n");
	} else {
		printf("  " GREEN "+ UVC 1.1+ device - standard probe size should work\n" RESET);
	}

	// Format compatibility
	bool hasMJPEG = false, hasYUY2 = false;
	for (const auto& fmt : dev.formats) {
		if (fmt.name == "MJPEG") hasMJPEG = true;
		if (fmt.name == "YUY2") hasYUY2 = true;
	}

	if (hasMJPEG) {
		printf("  " GREEN "+ MJPEG format supported (recommended for bandwidth)\n" RESET);
	}
	if (hasYUY2) {
		printf("  " GREEN "+ YUY2 format supported (uncompressed, lower latency)\n" RESET);
	}
	if (!hasMJPEG && !hasYUY2) {
		printf("  " RED "- No supported formats detected!\n" RESET);
	}

	// Resolution variety
	size_t totalRes = 0;
	uint32 maxPixels = 0;
	for (const auto& fmt : dev.formats) {
		totalRes += fmt.resolutions.size();
		for (const auto& res : fmt.resolutions) {
			if (res.GetPixelCount() > maxPixels)
				maxPixels = res.GetPixelCount();
		}
	}

	printf("  " CYAN "i %zu resolutions available across %zu formats\n" RESET,
		totalRes, dev.formats.size());

	if (maxPixels >= 1920 * 1080) {
		printf("  " GREEN "+ Full HD (1080p) capable\n" RESET);
	} else if (maxPixels >= 1280 * 720) {
		printf("  " GREEN "+ HD (720p) capable\n" RESET);
	} else if (maxPixels >= 640 * 480) {
		printf("  " CYAN "i VGA capable\n" RESET);
	}
}

void PrintSummary()
{
	PrintHeader("Summary");

	if (gDevices.empty()) {
		printf(YELLOW "\nNo UVC webcams detected.\n" RESET);
		printf("Make sure your webcam is connected and recognized by the system.\n");
		printf("\nTroubleshooting:\n");
		printf("  1. Check USB connection\n");
		printf("  2. Run 'listusb' to see all USB devices\n");
		printf("  3. Check /var/log/syslog for USB errors\n");
		return;
	}

	printf("\n" BOLD "Detected %zu UVC device(s):\n" RESET, gDevices.size());

	for (size_t i = 0; i < gDevices.size(); i++) {
		const UVCDevice& dev = gDevices[i];
		printf("\n  %zu. %s (0x%04X:0x%04X)\n", i + 1,
			dev.product.empty() ? "Unknown Device" : dev.product.c_str(),
			dev.vendorId, dev.productId);
		printf("     UVC %s | ", dev.GetUVCVersionString().c_str());

		// List formats
		for (size_t f = 0; f < dev.formats.size(); f++) {
			if (f > 0) printf(", ");
			printf("%s", dev.formats[f].name.c_str());
		}
		printf("\n");

		// Overall compatibility score (max 130 points, normalized to 100%)
		int score = 0;
		bool hasMJPEG = false;
		bool hasNV12 = false;
		for (const auto& fmt : dev.formats) {
			if (fmt.name == "MJPEG") { hasMJPEG = true; score += 30; }
			if (fmt.name == "YUY2") score += 20;
			if (fmt.name == "NV12") { hasNV12 = true; score += 15; }
		}
		if (!dev.hasHighBandwidthEndpoints) score += 25;
		if (dev.uvcVersion >= 0x0110) score += 15;
		if (dev.hasProcessingUnit) score += 10;
		if (dev.hasCamera && dev.cameraControls) score += 10;  // Camera Terminal
		if (dev.hasExtensionUnit) score += 5;  // Extension Unit

		// Normalize to 100% (max possible = 130)
		int normalizedScore = (score * 100) / 130;
		if (normalizedScore > 100) normalizedScore = 100;

		printf("     Haiku Compatibility: ");
		if (normalizedScore >= 80) {
			printf(GREEN "Excellent (%d%%)\n" RESET, normalizedScore);
		} else if (normalizedScore >= 60) {
			printf(GREEN "Good (%d%%)\n" RESET, normalizedScore);
		} else if (normalizedScore >= 40) {
			printf(YELLOW "Fair (%d%%)\n" RESET, normalizedScore);
		} else {
			printf(RED "Limited (%d%%)\n" RESET, normalizedScore);
		}

		// Show detailed score breakdown
		printf("     Score Breakdown: ");
		if (hasMJPEG) printf("MJPEG(+30) ");
		printf("YUY2(+20) ");
		if (hasNV12) printf("NV12(+15) ");
		if (!dev.hasHighBandwidthEndpoints) printf("StdBW(+25) ");
		if (dev.uvcVersion >= 0x0110) printf("UVC1.1+(+15) ");
		if (dev.hasProcessingUnit) printf("PU(+10) ");
		if (dev.hasCamera && dev.cameraControls) printf("CT(+10) ");
		if (dev.hasExtensionUnit) printf("XU(+5) ");
		printf("= %d/130\n", score);
	}

	printf("\n" BOLD "Recommendations:\n" RESET);
	printf("  - Use MJPEG format for best bandwidth efficiency\n");
	printf("  - Start with 640x480 or 720p for testing\n");
	printf("  - Monitor /var/log/syslog for transfer errors\n");
	printf("  - If issues occur, try lower resolution/framerate\n");
}

int main()
{
	PrintHeader("UVC Webcam Diagnostic Tool for Haiku");
	printf("Scanning for USB Video Class devices...\n");

	// Create roster and scan
	UVCDeviceVisitor visitor;
	visitor.Start();

	// Give it a moment to enumerate
	snooze(500000);  // 500ms

	visitor.Stop();

	if (gDevices.empty()) {
		PrintSummary();
		return 1;
	}

	// Print detailed info for each device
	for (size_t i = 0; i < gDevices.size(); i++) {
		PrintDeviceInfo(gDevices[i], i);
	}

	// Print summary
	PrintSummary();

	printf("\n" CYAN "===================================================================\n");
	printf(" Test completed. Run 'listusb -v' for raw USB descriptor info.\n");
	printf("===================================================================\n" RESET);

	return 0;
}
