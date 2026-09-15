/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2009, Ithamar Adema, <ithamar.adema@team-embedded.nl>.
 * Distributed under the terms of the MIT License.
 */


#include "UVCCamDevice.h"
#include "UVCDeframer.h"
#include "UVCQuirks.h"
#include "UVCDescriptors.h"
#include "CamDebug.h"
#include "CamConfig.h"

#include <new>
#include <stdio.h>
#include <Autolock.h>
#include <stdlib.h>
#include <syslog.h>
#include <Notification.h>
#include <ParameterWeb.h>
#include <String.h>
#include <media/Buffer.h>

#undef TRACE
#define TRACE(x...) do {} while(0)
//#define TRACE(x...) printf(x)

// Descriptor-dump gate: every printf() in this file is enumeration noise
// from the VS/VC descriptor parsers (~100 lines per device start).
// Route them through the debug level so the default log stays quiet;
// parsing itself is unaffected.
#include <stdarg.h>
static inline void UVC_Dump(const char* format, ...)
{
	if (gWebcamDebugLevel < WEBCAM_DEBUG_VERBOSE)
		return;
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}
#define printf UVC_Dump


usb_webcam_support_descriptor kSupportedDevices[] = {
	// Specific VID/PID devices first (higher priority than generic class match)
	// Device list imported from Linux UVC driver (drivers/media/usb/uvc/uvc_driver.c)

	// HP
	{{ 0, 0, 0, 0x03f0, 0xe207, }, "HP",            "Webcam HD 2300",                  "??" },

	// Quanta/Acer
	{{ 0, 0, 0, 0x0408, 0x4033, }, "Quanta",        "HD User Facing Camera",           "??" },
	{{ 0, 0, 0, 0x0408, 0x4035, }, "Quanta",        "HD User Facing Camera",           "??" },

	// LogiLink
	{{ 0, 0, 0, 0x0416, 0xa91a, }, "LogiLink",      "Wireless Webcam",                 "??" },

	// Genius
	{{ 0, 0, 0, 0x0458, 0x706e, }, "Genius",        "eFace 2025",                      "??" },

	// Microsoft
	{{ 0, 0, 0, 0x045e, 0x00f8, }, "Microsoft",     "Lifecam NX-6000",                 "??" },
	{{ 0, 0, 0, 0x045e, 0x0721, }, "Microsoft",     "Lifecam NX-3000",                 "??" },
	{{ 0, 0, 0, 0x045e, 0x0723, }, "Microsoft",     "Lifecam VX-7000",                 "??" },

	// Logitech
	{{ 0, 0, 0, 0x046d, 0x0821, }, "Logitech",      "HD Pro Webcam C910",              "??" },
	{{ 0, 0, 0, 0x046d, 0x0823, }, "Logitech",      "Webcam B910",                     "??" },
	{{ 0, 0, 0, 0x046d, 0x082d, }, "Logitech",      "HD Pro Webcam C920",              "??" },
	{{ 0, 0, 0, 0x046d, 0x085c, }, "Logitech",      "HD Pro Webcam C922",              "??" },
	{{ 0, 0, 0, 0x046d, 0x087c, }, "Logitech",      "Rally Bar Huddle",                "??" },
	{{ 0, 0, 0, 0x046d, 0x089b, }, "Logitech",      "Rally Bar",                       "??" },
	{{ 0, 0, 0, 0x046d, 0x08c1, }, "Logitech",      "QuickCam Fusion",                 "??" },
	{{ 0, 0, 0, 0x046d, 0x08c2, }, "Logitech",      "QuickCam Orbit MP",               "??" },
	{{ 0, 0, 0, 0x046d, 0x08c3, }, "Logitech",      "QuickCam Pro for Notebook",       "??" },
	{{ 0, 0, 0, 0x046d, 0x08c5, }, "Logitech",      "QuickCam Pro 5000",               "??" },
	{{ 0, 0, 0, 0x046d, 0x08c6, }, "Logitech",      "QuickCam OEM Dell Notebook",      "??" },
	{{ 0, 0, 0, 0x046d, 0x08c7, }, "Logitech",      "QuickCam OEM Cisco VT Camera II", "??" },
	{{ 0, 0, 0, 0x046d, 0x08d3, }, "Logitech",      "Rally Bar Mini",                  "??" },

	// Chicony
	{{ 0, 0, 0, 0x04f2, 0xb071, }, "Chicony",       "CNF7129 (Asus EEE 100HE)",        "??" },
	{{ 0, 0, 0, 0x04f2, 0xb119, }, "Chicony",       "CNF8111",                         "??" },
	{{ 0, 0, 0, 0x04f2, 0xb40a, }, "Chicony",       "HD UVC WebCam",                   "??" },

	// Alcor Micro
	{{ 0, 0, 0, 0x058f, 0x3820, }, "Alcor Micro",   "AU3820 PC USB Webcam",            "??" },
	{{ 0, 0, 0, 0x058f, 0x5608, }, "Alcor Micro",   "USB 2.0 Camera",                  "??" },

	// OmniVision
	{{ 0, 0, 0, 0x05a9, 0x2640, }, "OmniVision",    "Dell XPS m1530",                  "??" },
	{{ 0, 0, 0, 0x05a9, 0x2641, }, "OmniVision",    "Dell SP2008WFP Monitor",          "??" },
	{{ 0, 0, 0, 0x05a9, 0x2643, }, "OmniVision",    "Dell Alienware X51",              "??" },
	{{ 0, 0, 0, 0x05a9, 0x264a, }, "OmniVision",    "Dell Studio Hybrid 140g",         "??" },
	{{ 0, 0, 0, 0x05a9, 0x7670, }, "OmniVision",    "Dell XPS M1330",                  "??" },

	// Apple
	{{ 0, 0, 0, 0x05ac, 0x8501, }, "Apple",         "Built-In iSight",                 "??" },
	{{ 0, 0, 0, 0x05ac, 0x8514, }, "Apple",         "FaceTime HD Camera",              "??" },
	{{ 0, 0, 0, 0x05ac, 0x8600, }, "Apple",         "Built-In iSight via iBridge",     "??" },

	// Foxlink
	{{ 0, 0, 0, 0x05c8, 0x0403, }, "Foxlink",       "HP Webcam (HP Mini 5103)",        "??" },

	// Genesys Logic
	{{ 0, 0, 0, 0x05e3, 0x0505, }, "Genesys Logic", "USB 2.0 PC Camera",               "??" },

	// Hercules
	{{ 0, 0, 0, 0x06f8, 0x300c, }, "Hercules",      "Classic Silver",                  "??" },

	// ViMicro
	{{ 0, 0, 0, 0x0ac8, 0x332d, }, "ViMicro",       "Vega",                            "??" },
	{{ 0, 0, 0, 0x0ac8, 0x3410, }, "ViMicro",       "Minoru3D",                        "??" },
	{{ 0, 0, 0, 0x0ac8, 0x3420, }, "ViMicro",       "Venus Minoru3D",                  "??" },

	// Ophir Optronics
	{{ 0, 0, 0, 0x0bd3, 0x0555, }, "Ophir Optronics", "SPCAM 620U",                    "??" },

	// Realtek
	// NOTE: 0x5843 has issues with Haiku's xHCI driver (Missed service errors)
	// May work better on USB 2.0 ports (EHCI) or with external USB 2.0 hub
	{{ 0, 0, 0, 0x0bda, 0x5843, }, "Realtek",        "USB Camera",                     "??" },

	// Microdia/Sonix
	// NOTE: Some Sonix devices have issues with Haiku's xHCI driver (Missed service errors)
	// May work better on USB 2.0 ports (EHCI) or with external USB 2.0 hub
	{{ 0, 0, 0, 0x0c45, 0x6340, }, "Sonix",         "USB 2.0 Camera",                  "??" },
	{{ 0, 0, 0, 0x0c45, 0x6366, }, "Sonix",         "292A IPC AR0330",                 "??" },
	{{ 0, 0, 0, 0x0c45, 0x6409, }, "Microdia",      "Motion Eye",                      "??" },
	{{ 0, 0, 0, 0x0c45, 0x64ab, }, "Sonix",         "MT9M114 Integrated Camera",       "??" },
	{{ 0, 0, 0, 0x0c45, 0x6720, }, "Microdia",      "Integrated Webcam HD",            "??" },

	// MediaTek
	{{ 0, 0, 0, 0x0e8d, 0x0004, }, "MediaTek",      "MT6227",                          "??" },

	// IMC Networks
	{{ 0, 0, 0, 0x13d3, 0x5103, }, "IMC Networks",  "Medion Akoya",                    "??" },

	// JMicron
	{{ 0, 0, 0, 0x152d, 0x0310, }, "JMicron",       "USB 2.0 XGA WebCam",              "??" },

	// Kurokesu
	{{ 0, 0, 0, 0x16d0, 0x0ed1, }, "Kurokesu",      "C1 PRO",                          "??" },

	// Syntek
	{{ 0, 0, 0, 0x174f, 0x5212, }, "Syntek",        "HP Spartan",                      "??" },
	{{ 0, 0, 0, 0x174f, 0x5931, }, "Syntek",        "Samsung Q310",                    "??" },
	{{ 0, 0, 0, 0x174f, 0x8a12, }, "Syntek",        "Packard Bell EasyNote MX52",      "??" },
	{{ 0, 0, 0, 0x174f, 0x8a31, }, "Syntek",        "Asus F9SG",                       "??" },
	{{ 0, 0, 0, 0x174f, 0x8a33, }, "Syntek",        "Asus U3S",                        "??" },
	{{ 0, 0, 0, 0x174f, 0x8a34, }, "Syntek",        "JAOtech Smart Terminal",          "??" },

	// Miricle
	{{ 0, 0, 0, 0x17dc, 0x0202, }, "Miricle",       "307K",                            "??" },

	// Lenovo
	{{ 0, 0, 0, 0x17ef, 0x480b, }, "Lenovo",        "Thinkpad SL400/SL500",            "??" },

	// Aveo Technology
	{{ 0, 0, 0, 0x1871, 0x0306, }, "Aveo",          "USB 2.0 Camera",                  "??" },
	{{ 0, 0, 0, 0x1871, 0x0516, }, "Aveo",          "USB 2.0 Camera (Tasco Microscope)", "??" },

	// Ecamm
	{{ 0, 0, 0, 0x18cd, 0xcafe, }, "Ecamm",         "Pico iMage",                      "??" },

	// Arkmicro/FSC/Manta
	{{ 0, 0, 0, 0x18ec, 0x3188, }, "Manta",         "MM-353 Plako",                    "??" },
	{{ 0, 0, 0, 0x18ec, 0x3288, }, "FSC",           "WebCam V30S",                     "??" },
	{{ 0, 0, 0, 0x18ec, 0x3290, }, "Arkmicro",      "USB Web Camera",                  "??" },

	// Generic USB Camera (Philips/NXP chipset)
	// NOTE: Has issues with Haiku's xHCI driver (Missed service errors)
	// May work better on USB 2.0 ports (EHCI) or with external USB 2.0 hub
	{{ 0, 0, 0, 0x1908, 0x2310, }, "Generic",       "USB2.0 PC Camera",                "??" },

	// Imaging Source
	{{ 0, 0, 0, 0x199e, 0x8102, }, "Imaging Source", "USB CCD Camera",                 "??" },

	// Bodelin
	{{ 0, 0, 0, 0x19ab, 0x1000, }, "Bodelin",       "ProScopeHR",                      "??" },

	// MSI
	{{ 0, 0, 0, 0x1b3b, 0x2951, }, "MSI",           "StarCam 370i",                    "??" },

	// Generalplus
	{{ 0, 0, 0, 0x1b3f, 0x2002, }, "Generalplus",   "808 Camera",                      "??" },

	// AUKEY / Shenzhen Aoni
	{{ 0, 0, 0, 0x1bcf, 0x0001, }, "AUKEY",         "PC-LM1E",                         "??" },
	{{ 0, 0, 0, 0x1bcf, 0x0b40, }, "Shenzhen Aoni", "2K FHD Camera",                   "??" },

	// SiGma Micro
	{{ 0, 0, 0, 0x1c4f, 0x3000, }, "SiGma Micro",   "USB Web Camera",                  "??" },

	// Actions Microelectronics
	{{ 0, 0, 0, 0x1de1, 0xf105, }, "Actions Micro", "Display capture-UVC05",           "??" },

	// NXP Semiconductors
	{{ 0, 0, 0, 0x1fc9, 0x009b, }, "NXP",           "IR VIDEO",                        "??" },

	// Oculus VR
	{{ 0, 0, 0, 0x2833, 0x0201, }, "Oculus VR",     "Positional Tracker DK2",          "??" },
	{{ 0, 0, 0, 0x2833, 0x0211, }, "Oculus VR",     "Rift Sensor",                     "??" },

	// GEO Semiconductor
	{{ 0, 0, 0, 0x29fe, 0x4d53, }, "GEO Semi",      "GC6500",                          "??" },

	// Insta360
	{{ 0, 0, 0, 0x2e1a, 0x4c01, }, "Insta360",      "Link",                            "??" },

	// SunplusIT / Bison Electronics
	// NOTE: Requires xHCI high-bandwidth workaround on Haiku (SetAlternate bug)
	{{ 0, 0, 0, 0x5986, 0x2113, }, "SunplusIT",     "Integrated Camera",               "??" },

	// Intel RealSense
	{{ 0, 0, 0, 0x8086, 0x0ad2, }, "Intel",         "RealSense D410",                  "??" },
	{{ 0, 0, 0, 0x8086, 0x0ad3, }, "Intel",         "RealSense D415",                  "??" },
	{{ 0, 0, 0, 0x8086, 0x0ad4, }, "Intel",         "RealSense D430",                  "??" },
	{{ 0, 0, 0, 0x8086, 0x0b03, }, "Intel",         "RealSense D4M",                   "??" },
	{{ 0, 0, 0, 0x8086, 0x0b07, }, "Intel",         "RealSense D435",                  "??" },
	{{ 0, 0, 0, 0x8086, 0x0b3a, }, "Intel",         "RealSense D435i",                 "??" },
	{{ 0, 0, 0, 0x8086, 0x0b5b, }, "Intel",         "RealSense D405",                  "??" },
	{{ 0, 0, 0, 0x8086, 0x0b5c, }, "Intel",         "RealSense D455",                  "??" },
	{{ 0, 0, 0, 0x8086, 0x1155, }, "Intel",         "RealSense D421",                  "??" },

	// Generic class-based matching (fallback for unknown devices).
	// Subclass is left as wildcard (0) so the matcher catches IAD composite
	// devices (Logitech C920/C922, OBSBOT, Razer Kiyo, etc.) where the
	// VideoControl interface is buried under Class=0xEF at the device
	// descriptor. Any interface that exposes Class=0x0E (USB Video) is
	// treated as a candidate UVC device; per-format validation happens in
	// UVCCamDevice::Init.
	{{ USB_VIDEO_DEVICE_CLASS, 0, 0, 0, 0 }, "Generic UVC", "Video Class", "??" },
	{{ 0xEF, 0x02, 0, 0, 0 }, "Miscellaneous device", "Interface association", "??" },
	{{ 0, 0, 0, 0, 0}, NULL, NULL, NULL }
};

/* Table 2-1 Compression Formats of USB Video Payload Uncompressed */
usbvc_guid kYUY2Guid = {0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kNV12Guid = {0x4e, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
// UYVY: same as YUY2 but byte order is U Y0 V Y1
usbvc_guid kUYVYGuid = {0x55, 0x59, 0x56, 0x59, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
// NV21: same as NV12 but VU interleaved instead of UV
usbvc_guid kNV21Guid = {0x4e, 0x56, 0x32, 0x31, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
// YV12: planar Y-V-U (4:2:0)
usbvc_guid kYV12Guid = {0x59, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
// I420 / IYUV: planar Y-U-V (4:2:0)
usbvc_guid kI420Guid = {0x49, 0x34, 0x32, 0x30, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kIYUVGuid = {0x49, 0x59, 0x55, 0x56, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
// GREY / Y800: 8-bit monochrome
usbvc_guid kGREYGuid = {0x47, 0x52, 0x45, 0x59, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kY800Guid = {0x59, 0x38, 0x30, 0x30, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};


// Frame-based codec GUIDs (UVC 1.5 Payload Frame Based, vendor-extended).
// All share the suffix 0000-0010-8000-00AA00389B71; only the FourCC differs.
usbvc_guid kH264Guid = {0x48, 0x32, 0x36, 0x34, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kH265Guid = {0x48, 0x45, 0x56, 0x43, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kVP8Guid = {0x56, 0x50, 0x38, 0x30, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kMJPG2KGuid = {0x4d, 0x4a, 0x32, 0x43, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};


// UVC 1.5 spec section 3.1.2.3 - VS_FORMAT_FRAME_BASED descriptor.
// 28 bytes total.
struct uvc_format_frame_based_descriptor {
	uint8	length;
	uint8	descriptor_type;
	uint8	descriptor_subtype;
	uint8	format_index;
	uint8	num_frame_descriptors;
	uint8	guid_format[16];
	uint8	bits_per_pixel;
	uint8	default_frame_index;
	uint8	aspect_ratio_x;
	uint8	aspect_ratio_y;
	uint8	interlace_flags;
	uint8	copy_protect;
	uint8	variable_size;
} _PACKED;


// UVC 1.5 spec section 3.1.2.4 - VS_FRAME_FRAME_BASED descriptor.
// Note: dwBytesPerLine sits between bFrameIntervalType and the interval list,
// which is a layout difference vs FRAME_MJPEG / FRAME_UNCOMPRESSED.
struct uvc_frame_frame_based_descriptor {
	uint8	length;
	uint8	descriptor_type;
	uint8	descriptor_subtype;
	uint8	frame_index;
	uint8	capabilities;
	uint16	width;
	uint16	height;
	uint32	min_bit_rate;
	uint32	max_bit_rate;
	uint32	default_frame_interval;
	uint8	frame_interval_type;
	uint32	bytes_per_line;
	// Followed by either continuous {min, max, step} (12 bytes) or
	// frame_interval_type discrete intervals (4 bytes each).
} _PACKED;


// Identify frame-based codec from its descriptor GUID.
static uvc_frame_based_codec
identify_frame_based_codec(const uint8* guid)
{
	if (!memcmp(guid, kH264Guid, sizeof(usbvc_guid)))
		return UVC_CODEC_H264;
	if (!memcmp(guid, kH265Guid, sizeof(usbvc_guid)))
		return UVC_CODEC_H265;
	if (!memcmp(guid, kVP8Guid, sizeof(usbvc_guid)))
		return UVC_CODEC_VP8;
	if (!memcmp(guid, kMJPG2KGuid, sizeof(usbvc_guid)))
		return UVC_CODEC_MJPEG2000;
	return UVC_CODEC_UNKNOWN;
}


// Identify uncompressed format from the GUID field of a UVC format descriptor.
static uvc_uncompressed_format
identify_uncompressed_format(const usbvc_guid guid)
{
	if (!memcmp(guid, kYUY2Guid, sizeof(usbvc_guid)))
		return UVC_FMT_YUY2;
	if (!memcmp(guid, kUYVYGuid, sizeof(usbvc_guid)))
		return UVC_FMT_UYVY;
	if (!memcmp(guid, kNV12Guid, sizeof(usbvc_guid)))
		return UVC_FMT_NV12;
	if (!memcmp(guid, kNV21Guid, sizeof(usbvc_guid)))
		return UVC_FMT_NV21;
	if (!memcmp(guid, kYV12Guid, sizeof(usbvc_guid)))
		return UVC_FMT_YV12;
	if (!memcmp(guid, kI420Guid, sizeof(usbvc_guid))
			|| !memcmp(guid, kIYUVGuid, sizeof(usbvc_guid)))
		return UVC_FMT_I420;
	if (!memcmp(guid, kGREYGuid, sizeof(usbvc_guid))
			|| !memcmp(guid, kY800Guid, sizeof(usbvc_guid)))
		return UVC_FMT_GREY;
	return UVC_FMT_UNKNOWN;
}


// P40: post a desktop notification when a UVC camera is recognised at the
// USB level (Sniff matched) but the driver cannot stream from it. Without
// this the user only sees the device disappear from the media settings list
// with no hint why, and has to chase the cause in /var/log/syslog.
static void
notify_init_failure(uint16 vid, uint16 pid, const char* shortReason)
{
	BNotification note(B_ERROR_NOTIFICATION);
	note.SetGroup("USB Webcam");
	note.SetTitle("Webcam not usable");
	BString content;
	content.SetToFormat("%04x:%04x — %s. Run with WEBCAM_DEBUG=verbose and "
		"check syslog for details.", vid, pid,
		shortReason != NULL ? shortReason : "init failed");
	note.SetContent(content);
	note.Send();
}


// Score a VS interface by walking its class-specific descriptors and adding
// points for each decodable format. Higher is better. Used by the constructor
// to pick the "color" stream on multi-VS cameras (Intel RealSense, Sonix
// SN9C292, stereo cameras) where the previous "last-VS-wins" policy could
// land on a depth/IR pipe with no MJPEG/YUY2 frames.
//
// Scoring weights:
//   MJPEG format                                : 100 (preferred — already decoded)
//   Recognized uncompressed format (YUY2 ... GREY): 50
//   Unrecognized uncompressed (raw Bayer, depth) :  5 (better than nothing)
//   Frame-based encoded (H.264/H.265/VP8)        :  1 (we don't decode yet)
static int
score_streaming_interface(const BUSBInterface* iface, uint8* scratch,
	size_t scratchSize, uint8* outMjpegCount = NULL,
	uint8* outUncompressedCount = NULL, uint8* outFrameBasedCount = NULL)
{
	if (outMjpegCount != NULL)
		*outMjpegCount = 0;
	if (outUncompressedCount != NULL)
		*outUncompressedCount = 0;
	if (outFrameBasedCount != NULL)
		*outFrameBasedCount = 0;
	if (iface == NULL)
		return -1;
	int score = 0;
	usb_descriptor* generic = (usb_descriptor*)scratch;
	for (uint32 k = 0; iface->OtherDescriptorAt(k, generic, scratchSize)
			== B_OK; k++) {
		if (generic->generic.descriptor_type
				!= (USB_REQTYPE_CLASS | USB_DESCRIPTOR_INTERFACE))
			continue;
		const usbvc_class_descriptor* d
			= (const usbvc_class_descriptor*)generic;
		switch (d->descriptorSubtype) {
			case USB_VIDEO_VS_FORMAT_MJPEG:
				score += 100;
				if (outMjpegCount != NULL && *outMjpegCount < 255)
					(*outMjpegCount)++;
				break;
			case USB_VIDEO_VS_FORMAT_UNCOMPRESSED:
			{
				// Read the 16-byte GUID through the bounds-safe validator so a
				// short descriptor can't have us identify a format from stale
				// scratch memory.
				const usbvc_class_descriptor* cd
					= (const usbvc_class_descriptor*)generic;
				UVCUncompressedFormatCheck fmt
					= UVCCheckUncompressedFormatDescriptor(
						(const uint8*)generic, cd->length);
				if (fmt.valid
						&& identify_uncompressed_format(fmt.guid) != UVC_FMT_UNKNOWN)
					score += 50;
				else
					score += 5;
				if (outUncompressedCount != NULL && *outUncompressedCount < 255)
					(*outUncompressedCount)++;
				break;
			}
			case USB_VIDEO_VS_FORMAT_FRAME_BASED:
			case USB_VIDEO_VS_FORMAT_H264:
			case USB_VIDEO_VS_FORMAT_VP8:
				score += 1;
				if (outFrameBasedCount != NULL && *outFrameBasedCount < 255)
					(*outFrameBasedCount)++;
				break;
			default:
				break;
		}
	}
	return score;
}


// =============================================================================
// Global YUV to RGB Lookup Tables
// =============================================================================
// Pre-computed tables eliminate per-pixel multiplications in color conversion.
// Uses BT.601 coefficients: R = 1.164(Y-16) + 1.596(V-128)
//                           G = 1.164(Y-16) - 0.391(U-128) - 0.813(V-128)
//                           B = 1.164(Y-16) + 2.018(U-128)

yuv_rgb_lookup_tables gYuvRgbTables;


void
yuv_rgb_lookup_tables::Initialize()
{
	if (initialized)
		return;

	for (int i = 0; i < 256; i++) {
		// Y contribution (same for R, G, B)
		// y_table[i] = 298 * (i - 16), unshifted for combining with U/V
		// Max value: 298 * 239 = 71222 (requires int32)
		y_table[i] = 298 * (i - 16);

		// U contribution to B: 516 * (u - 128)
		// Range: -66048 to +65532 (requires int32)
		u_b_table[i] = 516 * (i - 128);

		// U contribution to G: -100 * (u - 128)
		u_g_table[i] = -100 * (i - 128);

		// V contribution to R: 409 * (v - 128)
		v_r_table[i] = 409 * (i - 128);

		// V contribution to G: -208 * (v - 128)
		v_g_table[i] = -208 * (i - 128);
	}

	initialized = true;
	syslog(LOG_INFO, "UVCCamDevice: YUV-RGB lookup tables initialized (~5KB)\n");
}


static void
print_guid(const usbvc_guid guid)
{
	switch (identify_uncompressed_format(guid)) {
		case UVC_FMT_YUY2: printf("YUY2"); return;
		case UVC_FMT_UYVY: printf("UYVY"); return;
		case UVC_FMT_NV12: printf("NV12"); return;
		case UVC_FMT_NV21: printf("NV21"); return;
		case UVC_FMT_YV12: printf("YV12"); return;
		case UVC_FMT_I420: printf("I420"); return;
		case UVC_FMT_GREY: printf("GREY"); return;
		default: break;
	}
	printf("%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
		"%02x:%02x:%02x:%02x", guid[0], guid[1], guid[2], guid[3], guid[4],
		guid[5], guid[6], guid[7], guid[8], guid[9], guid[10], guid[11],
		guid[12], guid[13], guid[14], guid[15]);
}


UVCCamDevice::UVCCamDevice(CamDeviceAddon& _addon, BUSBDevice* _device)
	: CamDevice(_addon, _device),
	fHeaderDescriptor(NULL),
	fInterruptIn(NULL),
	fCurrentVideoAlternate(0),
	fActiveStreamIdx(-1),
	fUncompressedFormatIndex(1),
	fUncompressedFrameIndex(1),
	fMJPEGFormatIndex(1),
	fMJPEGFrameIndex(1),		// Initialize to 1, will be updated by AcceptVideoFrame
	fDefaultMJPEGFrameIndex(0),
	fDefaultUncompressedFrameIndex(0),
	fMaxVideoFrameSize(0),
	fMaxPayloadTransferSize(0),
	fProbeCommitSize(34),		// Default UVC 1.1+ size, will be auto-detected
	fJpegDecompressor(NULL),
	fIsMJPEG(false),
	fIsNV12(false),
	fUncompressedPixelFormat(UVC_FMT_UNKNOWN),
	fMicrodiaQuirk(false),
	fQuirks(0),
	fFrameBasedCodec(UVC_CODEC_UNKNOWN),
	fFrameBasedFormatIndex(0),
	fFrameBasedBitsPerPixel(0),
	// FIX BUG 6: Init per-instance diagnostic counters
	fFillFrameCount(0),
	fFillFrameSuccess(0),
	fFillFrameTimeout(0),
	fMjpegAttempts(0),
	fMjpegSuccess(0),
	fMjpegNoSOI(0),
	fMjpegDecompressErrors(0),
	fHasAudio(false),
	fAudioControlIndex(0),
	fAudioStreamingIndex(0),
	fCurrentAudioAlternate(0),
	fAudioIsoIn(NULL),
	fAudioMaxPacketSize(0),
	fAudioChannels(0),
	fAudioBitResolution(0),
	fAudioSubFrameSize(0),
	fAudioSampleRate(0),
	fAudioTerminalID(0),
	fAudioFeatureUnitID(0),
	fAudioTransferRunning(false),
	fAudioPumpThread(-1),
	fAudioBuffer(NULL),
	fAudioBufferLen(0),
	fAudioRingBuffer(NULL),
	fAudioRingSize(0),
	fAudioRingHead(0),
	fAudioRingTail(0),
	fSelectedResolutionIndex(0),
	fResolutionParameterID(0),
	fResolutionTransitionStart(0),
	// Frame rate selection (P2 Feature)
	fSelectedFrameIntervalIndex(0),
	fFrameRateParameterID(0),
	fXULedParameterID(0),
	fXULedState(false),
	fExposureRelID(-1),
	fFocusRelID(-1),
	fZoomRelID(-1),
	fPanRelID(-1),
	fTiltRelID(-1),
	fNumFrameIntervals(0),
	fSelectedFrameInterval(333333),  // Default 30fps (10000000/30)
	fAudioRingSem(-1),
	// Frame validation state (Feature 1)
	fConsecutiveBadFrames(0),
	// Processing Unit controls (Feature 2)
	fProcessingUnitID(0),
	fControlsInitialized(false),
	// Camera Terminal controls (CT)
	fCameraTerminalID(0),
	fCameraTerminalControls(0),
	fHasCameraTerminal(false),
	fAutoExposureMode(2),		// Default: Auto
	fExposureTimeAbs(333),		// Default: ~33ms (30fps)
	fAutoFocus(true),
	fFocusAbsolute(0),
	fZoomAbsolute(100),			// 100 = 1x zoom
	fPanAbsolute(0),
	fTiltAbsolute(0),
	fPrivacyEnabled(false),
	fAutoExposureModeID(-1),
	fExposureTimeID(-1),
	fAutoFocusID(-1),
	fFocusAbsoluteID(-1),
	fZoomAbsoluteID(-1),
	fPanTiltID(-1),
	// Extension Unit support (XU)
	fHasExtensionUnits(false),
	// EHCI host system error recovery
	fEHCIRecoveryInProgress(false),
	// Still image capture support
	fStillCaptureMethod(STILL_CAPTURE_NONE),
	fHasStillCapture(false),
	fTriggerSupport(false),
	fTriggerUsage(false),
	// Resolution fallback state (Feature 3)
	fCurrentResolutionLevel(0),
	fTargetResolutionLevel(0),
	fLastFallbackTime(0),
	fStableStartTime(0),
	fEvalWindowPackets(0),
	fEvalWindowErrors(0),
	fEvalWindowStartTime(0),
	fFallbackActive(false),
	fFallbackWarningShown(false),
	fLastPacketSuccessCount(0),
	fLastPacketErrorCount(0),
	// Sorted resolution indices
	fSortedMJPEGCount(0),
	fSortedUncompressedCount(0),
	// High-bandwidth auto-detection state
	fHighBandwidthTested(false),
	fHighBandwidthWorks(true),		// Assume it works until proven otherwise
	fHighBandwidthFailures(0),
	fUsingHighBandwidth(false),
	// Bandwidth estimation (cached)
	fMaxAvailableBandwidth(0),
	fBandwidthCalculated(false),
	// USB controller detection
	fControllerDetected(false),
	// MJPEG frame size monitoring
	fMJPEGFrameSizeSum(0),
	fMJPEGFrameSizeCount(0),
	fExpectedMJPEGMinSize(0),
	fLastFrameSizeCheck(0)
{
	// Initialize controller info to unknown
	memset(&fControllerInfo, 0, sizeof(fControllerInfo));
	fControllerInfo.type = USB_HC_UNKNOWN;
	fControllerInfo.device_speed = USB_SPEED_UNKNOWN;
	fControllerInfo.type_name = "unknown";
	// Initialize frame validation stats
	memset(&fValidationStats, 0, sizeof(fValidationStats));
	// Initialize frame intervals array (P2 Feature)
	memset(fCurrentFrameIntervals, 0, sizeof(fCurrentFrameIntervals));
	// Initialize sorted resolution indices
	memset(fSortedMJPEGIndices, 0, sizeof(fSortedMJPEGIndices));
	memset(fSortedUncompressedIndices, 0, sizeof(fSortedUncompressedIndices));
	// Initialize still image info
	memset(&fStillImageInfo, 0, sizeof(fStillImageInfo));

	// Initialize fallback config with defaults
	_InitializeFallbackConfig();

	// Initialize YUV-RGB lookup tables (once, shared across all instances)
	gYuvRgbTables.Initialize();

	fDeframer = new UVCDeframer(this);
	SetDataInput(fDeframer);

	const BUSBConfiguration* config;
	const BUSBInterface* interface;
	usb_descriptor* generic;
	uint8 buffer[1024];

	generic = (usb_descriptor*)buffer;

	// P2: pre-pass to find the configuration that actually owns the UVC
	// VideoControl interface. Composite devices (Logitech firmwares with
	// CONFIG 1=HID + CONFIG 2=UVC, some webcam+mic combos) used to have
	// SetConfiguration called once per config in the main loop, which
	// resets the device state on Haiku and reliably broke streaming on
	// the second iteration. Now we pick the right config up front, call
	// SetConfiguration at most once, and parse only that config.
	uint32 uvcConfigIndex = 0;
	int bestUvcScore = -1;
	for (uint32 i = 0; i < _device->CountConfigurations(); i++) {
		const BUSBConfiguration* probe = _device->ConfigurationAt(i);
		if (probe == NULL)
			continue;
		int score = 0;
		for (uint32 j = 0; j < probe->CountInterfaces(); j++) {
			const BUSBInterface* intf = probe->InterfaceAt(j);
			if (intf == NULL)
				continue;
			if (intf->Class() != USB_VIDEO_DEVICE_CLASS)
				continue;
			if (intf->Subclass()
					== USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS)
				score += 10;
			else if (intf->Subclass()
					== USB_VIDEO_INTERFACE_VIDEOSTREAMING_SUBCLASS)
				score += 5;
		}
		if (score > bestUvcScore) {
			bestUvcScore = score;
			uvcConfigIndex = i;
		}
	}

	for (uint32 i = 0; i < _device->CountConfigurations(); i++) {
		if (i != uvcConfigIndex)
			continue;
		config = _device->ConfigurationAt(i);
		if (config == NULL)
			continue;
		if (_device->ActiveConfiguration() != config)
			_device->SetConfiguration(config);

		// P3 Phase A: pre-pass to pick the best VideoStreaming interface in
		// this configuration. Multi-stream cameras (Intel RealSense,
		// SN9C292, stereo cameras) expose several VS interfaces — one per
		// pipe (color, depth, IR). Previously the parser kept the last one
		// it saw, which on RealSense lands on a depth/IR pipe with no
		// MJPEG/YUY2 frames and breaks all downstream code.
		//
		// P3 Phase B: also stash a uvc_vs_stream entry for every VS we
		// see, so the media addon can later emit one flavor per stream.
		// The pre-pass walks descriptors once per VS, so collecting the
		// metadata here costs nothing extra.
		int32 bestVSInterfaceIdx = -1;
		int bestVSScore = -1;
		uint32 vsInterfaceCount = 0;
		for (uint32 j = 0; j < config->CountInterfaces(); j++) {
			const BUSBInterface* candidate = config->InterfaceAt(j);
			if (candidate == NULL)
				continue;
			if (candidate->Class() != USB_VIDEO_DEVICE_CLASS
					|| candidate->Subclass()
						!= USB_VIDEO_INTERFACE_VIDEOSTREAMING_SUBCLASS) {
				continue;
			}
			vsInterfaceCount++;
			uint8 mjpegCount = 0, uncompCount = 0, frameBasedCount = 0;
			int score = score_streaming_interface(candidate, buffer,
				sizeof(buffer), &mjpegCount, &uncompCount, &frameBasedCount);

			uvc_vs_stream* meta = new(std::nothrow) uvc_vs_stream;
			if (meta != NULL) {
				meta->interface_index = candidate->Index();
				meta->score = score;
				meta->alternates_count = candidate->CountAlternates();
				meta->mjpeg_count = mjpegCount;
				meta->uncompressed_count = uncompCount;
				meta->frame_based_count = frameBasedCount;
				// FIX: AddItem can fail on OOM; do not leak meta.
				if (!fVSStreams.AddItem(meta))
					delete meta;
			}

			syslog(LOG_INFO, "UVCCamDevice: VS scan cfg=%u intf=%u "
				"score=%d alternates=%u\n",
				i, j, score, (unsigned)candidate->CountAlternates());
			if (score > bestVSScore) {
				bestVSScore = score;
				bestVSInterfaceIdx = (int32)j;
			}
		}
		if (vsInterfaceCount > 1) {
			syslog(LOG_INFO, "UVCCamDevice: %u VS interfaces in cfg=%u; "
				"selecting intf=%d (best score %d)\n",
				vsInterfaceCount, i, (int)bestVSInterfaceIdx, bestVSScore);
		}

		// P3 Phase B: locate the chosen VS inside fVSStreams so the addon
		// can later resolve "active stream" → list index.
		for (int32 si = 0; si < fVSStreams.CountItems(); si++) {
			const uvc_vs_stream* s
				= (const uvc_vs_stream*)fVSStreams.ItemAt(si);
			if (s != NULL && (int32)s->interface_index == bestVSInterfaceIdx) {
				fActiveStreamIdx = si;
				break;
			}
		}

		for (uint32 j = 0; j < config->CountInterfaces(); j++) {
			interface = config->InterfaceAt(j);
			if (interface == NULL)
				continue;

			if (interface->Class() == USB_VIDEO_DEVICE_CLASS && interface->Subclass()
				== USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS) {
				syslog(LOG_INFO, "UVCCamDevice: (%" B_PRIu32 ",%" B_PRIu32 "): Found Video Control "
					"interface.\n", i, j);

				// look for class specific interface descriptors and parse them
				for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
					sizeof(buffer)) == B_OK; k++) {
					if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
						| USB_DESCRIPTOR_INTERFACE))
						continue;
					fControlIndex = interface->Index();
					_ParseVideoControl((const usbvc_class_descriptor*)generic,
						generic->generic.length);
				}
				for (uint32 k = 0; k < interface->CountEndpoints(); k++) {
					const BUSBEndpoint* e = interface->EndpointAt(k);  // FIX BUG 1: was 'i', fixed to 'k'
					if (e && e->IsInterrupt() && e->IsInput()) {
						fInterruptIn = e;
						break;
					}
				}
				// FIX BUG 3: fInitStatus moved after full parse (see ctor end)
			} else if (interface->Class() == USB_VIDEO_DEVICE_CLASS && interface->Subclass()
				== USB_VIDEO_INTERFACE_VIDEOSTREAMING_SUBCLASS) {
				// P3 Phase A: only parse the winning VS interface; skip
				// secondary streams (depth, IR, etc.) for now.
				if ((int32)j != bestVSInterfaceIdx) {
					syslog(LOG_INFO,
						"UVCCamDevice: cfg=%u intf=%u: skipping VS "
						"interface (not selected by Phase A picker)\n",
						i, j);
					continue;
				}
				syslog(LOG_INFO, "UVCCamDevice: (%" B_PRIu32 ",%" B_PRIu32 "): Found Video Streaming "
					"interface, #alternates=%u.\n", i, j, (unsigned)interface->CountAlternates());

				fStreamingIndex = interface->Index();

				// Parse VS class-specific descriptors.
				// Try base interface first, then alternates if needed.
				// Retry with increasing delays if USB stack hasn't
				// populated descriptors yet (common on device hotplug).
				uint32 descCount = 0;
				for (int attempt = 0; attempt < 3; attempt++) {
					if (attempt > 0) {
						snooze(200000 * attempt);
						syslog(LOG_INFO, "UVCCamDevice: VS descriptor retry %d\n",
							attempt);
					}

					descCount = 0;
					for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
						sizeof(buffer)) == B_OK; k++) {
						descCount++;
						if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
							| USB_DESCRIPTOR_INTERFACE))
							continue;
						_ParseVideoStreaming(
							(const usbvc_class_descriptor*)generic,
							generic->generic.length);
					}

					// Also check alternate interfaces
					if (descCount == 0) {
						for (uint32 alt = 0;
							alt < interface->CountAlternates(); alt++) {
							const BUSBInterface* alternate
								= interface->AlternateAt(alt);
							if (alternate == NULL)
								continue;
							for (uint32 k = 0;
								alternate->OtherDescriptorAt(k, generic,
									sizeof(buffer)) == B_OK; k++) {
								descCount++;
								if (generic->generic.descriptor_type
									!= (USB_REQTYPE_CLASS
										| USB_DESCRIPTOR_INTERFACE))
									continue;
								_ParseVideoStreaming(
									(const usbvc_class_descriptor*)generic,
									generic->generic.length);
							}
							if (fUncompressedFrames.CountItems() > 0
								|| fMJPEGFrames.CountItems() > 0)
								break;
						}
					}

					if (fUncompressedFrames.CountItems() > 0
						|| fMJPEGFrames.CountItems() > 0)
						break;
				}
				syslog(LOG_INFO, "UVCCamDevice: Found %u VS descriptors, "
					"uncompressed=%d mjpeg=%d\n", descCount,
					(int)fUncompressedFrames.CountItems(),
					(int)fMJPEGFrames.CountItems());

				syslog(LOG_INFO, "UVCCamDevice: Total frames found: uncompressed=%d, mjpeg=%d\n",
					(int)fUncompressedFrames.CountItems(), (int)fMJPEGFrames.CountItems());

				// P4: the VS base interface (alternate 0) is the
				// zero-bandwidth alternate, which by UVC spec has zero
				// endpoints. The previous scan here always left fIsoIn
				// either NULL or pointing into a stale alternate, but the
				// real endpoint is later picked by _SelectBestAlternate()
				// on the alt we actually switch to. The dead loop has
				// been removed; fIsoIn is initialised to NULL via the
				// ctor and assigned in _SelectBestAlternate().
			} else if (interface->Class() == USB_AUDIO_DEVICE_CLASS
				&& interface->Subclass() == USB_AUDIO_INTERFACE_AUDIOCONTROL) {
				// Found Audio Control interface
				fAudioControlIndex = interface->Index();
				fHasAudio = true;

				// Parse audio control descriptors
				for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
					sizeof(buffer)) == B_OK; k++) {
					if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
						| USB_DESCRIPTOR_INTERFACE))
						continue;
					_ParseAudioControl((const usb_audio_class_descriptor*)generic,
						generic->generic.length);
				}
			} else if (interface->Class() == USB_AUDIO_DEVICE_CLASS
				&& interface->Subclass() == USB_AUDIO_INTERFACE_AUDIOSTREAMING) {
				// Found Audio Streaming interface
				fAudioStreamingIndex = interface->Index();

				// Parse audio streaming descriptors from alternates 1+
				// Alternate 0 is zero-bandwidth and has no format descriptors
				for (uint32 alt = 1; alt < interface->CountAlternates(); alt++) {
					const BUSBInterface* alternate = interface->AlternateAt(alt);
					if (alternate == NULL)
						continue;

					for (uint32 k = 0; alternate->OtherDescriptorAt(k, generic,
						sizeof(buffer)) == B_OK; k++) {
						if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
							| USB_DESCRIPTOR_INTERFACE))
							continue;
						_ParseAudioStreaming((const usb_audio_class_descriptor*)generic,
							generic->generic.length);
					}

					// Found format info - no need to check more alternates
					if (fAudioSampleRate > 0)
						break;
				}

				// Endpoint will be set by _SelectAudioAlternate when starting transfer
			}
		}
	}

	// TASK 1: Fallback for AUKEY PC-LM1E (VID:0x1BCF PID:0x0001)
	// If USB descriptor parsing failed, hardcode the known resolutions
	if (fMJPEGFrames.CountItems() == 0 && fUncompressedFrames.CountItems() == 0) {
		syslog(LOG_WARNING, "UVCCamDevice: USB descriptor parsing found no frames, using hardcoded fallback\n");
		uint16 vendorID = fDevice->VendorID();
		uint16 productID = fDevice->ProductID();

		syslog(LOG_INFO, "UVCCamDevice: No frames parsed, checking for known device (VID:0x%04X PID:0x%04X)\n",
			vendorID, productID);

		// AUKEY PC-LM1E Camera
		if (vendorID == 0x1BCF && productID == 0x0001) {
			syslog(LOG_INFO, "UVCCamDevice: Detected AUKEY PC-LM1E, using hardcoded resolutions\n");

			// Helper to create frame descriptors
			// frame_interval is in 100ns units: 30fps = 333333, 25fps = 400000, etc.
			struct FrameInfo {
				uint16 width;
				uint16 height;
				uint32 default_interval;  // Default fps as interval
				uint32 min_interval;      // Max fps as interval
			};

			// MJPEG frames - ORDER MUST MATCH USB DESCRIPTOR ORDER!
			// Camera's MJPEG format index is 2 (not 1!)
			// Frame indices from USB descriptor: 1=1280x720, 2=320x240, 3=800x600, etc.
			FrameInfo mjpegFrames[] = {
				{1280, 720,  333333, 333333},   // frame_index 1: 720p @ 30fps
				{320,  240,  333333, 333333},   // frame_index 2: QVGA @ 30fps
				{800,  600,  333333, 333333},   // frame_index 3: SVGA @ 30fps
				{1024, 768,  333333, 333333},   // frame_index 4: XGA @ 30fps
				{640,  480,  333333, 333333},   // frame_index 5: VGA @ 30fps
				{1920, 1080, 333333, 333333},   // frame_index 6: 1080p @ 30fps
			};

			fMJPEGFormatIndex = 2;  // Actual camera MJPEG format index
			for (size_t i = 0; i < sizeof(mjpegFrames)/sizeof(mjpegFrames[0]); i++) {
				usb_video_frame_descriptor* desc
					= new (std::nothrow) usb_video_frame_descriptor;
				if (desc == NULL)
					continue;
				memset(desc, 0, sizeof(*desc));
				desc->frame_index = i + 1;
				desc->capabilities = 0;
				desc->width = mjpegFrames[i].width;
				desc->height = mjpegFrames[i].height;
				desc->min_bit_rate = mjpegFrames[i].width * mjpegFrames[i].height * 16 * 15;  // Estimate
				desc->max_bit_rate = mjpegFrames[i].width * mjpegFrames[i].height * 16 * 30;
				desc->max_video_frame_buffer_size = mjpegFrames[i].width * mjpegFrames[i].height * 2;
				desc->default_frame_interval = mjpegFrames[i].default_interval;
				desc->frame_interval_type = 1;  // Discrete
				desc->discrete_frame_intervals[0] = mjpegFrames[i].min_interval;
				// FIX: AddItem fails on OOM; do not leak desc.
				if (!fMJPEGFrames.AddItem(desc))
					delete desc;
				else {
					syslog(LOG_INFO, "UVCCamDevice: Added MJPEG %ux%u\n",
						desc->width, desc->height);
				}
			}

			// YUY2/Uncompressed frames - ORDER MUST MATCH USB DESCRIPTOR ORDER!
			// Camera's YUY2 format index is 1 (not 2!)
			// Frame indices from USB descriptor: 1=1280x720@10fps, 2=320x240@30fps, etc.
			FrameInfo yuy2Frames[] = {
				{1280, 720,  1000000, 1000000},  // frame_index 1: 720p @ 10fps
				{320,  240,  333333,  333333},   // frame_index 2: QVGA @ 30fps
				{800,  600,  500000,  500000},   // frame_index 3: SVGA @ 20fps
				{1024, 768,  666666,  666666},   // frame_index 4: XGA @ 15fps
				{640,  480,  333333,  333333},   // frame_index 5: VGA @ 30fps
				{1920, 1080, 2000000, 2000000},  // frame_index 6: 1080p @ 5fps
			};

			fUncompressedFormatIndex = 1;  // Actual camera YUY2 format index
			for (size_t i = 0; i < sizeof(yuy2Frames)/sizeof(yuy2Frames[0]); i++) {
				usb_video_frame_descriptor* desc
					= new (std::nothrow) usb_video_frame_descriptor;
				if (desc == NULL)
					continue;
				memset(desc, 0, sizeof(*desc));
				desc->frame_index = i + 1;
				desc->capabilities = 0;
				desc->width = yuy2Frames[i].width;
				desc->height = yuy2Frames[i].height;
				desc->min_bit_rate = yuy2Frames[i].width * yuy2Frames[i].height * 16 * 5;
				desc->max_bit_rate = yuy2Frames[i].width * yuy2Frames[i].height * 16 * 30;
				desc->max_video_frame_buffer_size = yuy2Frames[i].width * yuy2Frames[i].height * 2;
				desc->default_frame_interval = yuy2Frames[i].default_interval;
				desc->frame_interval_type = 1;
				desc->discrete_frame_intervals[0] = yuy2Frames[i].min_interval;
				// FIX: AddItem fails on OOM; do not leak desc.
				if (!fUncompressedFrames.AddItem(desc))
					delete desc;
				else {
					syslog(LOG_INFO, "UVCCamDevice: Added YUY2 %ux%u\n",
						desc->width, desc->height);
				}
			}

			syslog(LOG_INFO, "UVCCamDevice: Hardcoded %d MJPEG + %d YUY2 frames\n",
				(int)fMJPEGFrames.CountItems(), (int)fUncompressedFrames.CountItems());
		}

		// Microdia Integrated Webcam HD (Dell XPS, etc.)
		// From listusb: VID 0x0c45, PID 0x6720
		else if (vendorID == 0x0c45 && productID == 0x6720) {
			syslog(LOG_INFO, "UVCCamDevice: Detected Microdia Integrated Webcam HD, using hardcoded resolutions\n");

			struct FrameInfo {
				uint16 width;
				uint16 height;
				uint32 default_interval;
				uint32 min_interval;
			};

			// MJPEG frames - from listusb descriptor (Format Index 0x01)
			// Frame indices: 1=1280x720, 2=960x540, 3=848x480, 4=640x480, 5=640x360
			FrameInfo mjpegFrames[] = {
				{1280, 720,  333333, 333333},   // frame_index 1: 720p @ 30fps
				{960,  540,  333333, 333333},   // frame_index 2: qHD @ 30fps
				{848,  480,  333333, 333333},   // frame_index 3: FWVGA @ 30fps
				{640,  480,  333333, 333333},   // frame_index 4: VGA @ 30fps
				{640,  360,  333333, 333333},   // frame_index 5: nHD @ 30fps
			};

			fMJPEGFormatIndex = 1;  // MJPEG format index from descriptor
			for (size_t i = 0; i < sizeof(mjpegFrames)/sizeof(mjpegFrames[0]); i++) {
				usb_video_frame_descriptor* desc
					= new (std::nothrow) usb_video_frame_descriptor;
				if (desc == NULL)
					continue;
				memset(desc, 0, sizeof(*desc));
				desc->frame_index = i + 1;
				desc->capabilities = 0;
				desc->width = mjpegFrames[i].width;
				desc->height = mjpegFrames[i].height;
				desc->min_bit_rate = mjpegFrames[i].width * mjpegFrames[i].height * 16 * 15;
				desc->max_bit_rate = mjpegFrames[i].width * mjpegFrames[i].height * 16 * 30;
				desc->max_video_frame_buffer_size = mjpegFrames[i].width * mjpegFrames[i].height * 2;
				desc->default_frame_interval = mjpegFrames[i].default_interval;
				desc->frame_interval_type = 1;
				desc->discrete_frame_intervals[0] = mjpegFrames[i].min_interval;
				fMJPEGFrames.AddItem(desc);
				syslog(LOG_INFO, "UVCCamDevice: Added MJPEG %ux%u\n", desc->width, desc->height);
			}

			// YUY2/Uncompressed frames - from listusb descriptor (Format Index 0x02)
			// Note: 1280x720 limited to 10fps due to USB bandwidth
			FrameInfo yuy2Frames[] = {
				{1280, 720,  1000000, 1000000}, // frame_index 1: 720p @ 10fps (bandwidth limited)
				{640,  480,  333333,  333333},  // frame_index 2: VGA @ 30fps
				{640,  360,  333333,  333333},  // frame_index 3: nHD @ 30fps
				{424,  240,  333333,  333333},  // frame_index 4: WQVGA @ 30fps
				{320,  240,  333333,  333333},  // frame_index 5: QVGA @ 30fps
				{320,  180,  333333,  333333},  // frame_index 6: @ 30fps
				{160,  120,  333333,  333333},  // frame_index 7: QQVGA @ 30fps
			};

			fUncompressedFormatIndex = 2;  // YUY2 format index from descriptor
			for (size_t i = 0; i < sizeof(yuy2Frames)/sizeof(yuy2Frames[0]); i++) {
				usb_video_frame_descriptor* desc
					= new (std::nothrow) usb_video_frame_descriptor;
				if (desc == NULL)
					continue;
				memset(desc, 0, sizeof(*desc));
				desc->frame_index = i + 1;
				desc->capabilities = 0;
				desc->width = yuy2Frames[i].width;
				desc->height = yuy2Frames[i].height;
				desc->min_bit_rate = yuy2Frames[i].width * yuy2Frames[i].height * 16 * 5;
				desc->max_bit_rate = yuy2Frames[i].width * yuy2Frames[i].height * 16 * 30;
				desc->max_video_frame_buffer_size = yuy2Frames[i].width * yuy2Frames[i].height * 2;
				desc->default_frame_interval = yuy2Frames[i].default_interval;
				desc->frame_interval_type = 1;
				desc->discrete_frame_intervals[0] = yuy2Frames[i].min_interval;
				// FIX: AddItem fails on OOM; do not leak desc.
				if (!fUncompressedFrames.AddItem(desc))
					delete desc;
				else {
					syslog(LOG_INFO, "UVCCamDevice: Added YUY2 %ux%u\n",
						desc->width, desc->height);
				}
			}

			syslog(LOG_INFO, "UVCCamDevice: Hardcoded %d MJPEG + %d YUY2 frames for Microdia 0x6720\n",
				(int)fMJPEGFrames.CountItems(), (int)fUncompressedFrames.CountItems());
		}
	}

	// Choose a sensible default resolution.
	//
	// P15: UVC requires the host to honour bDefaultFrameIndex advertised by
	// the camera in each format descriptor. Some firmwares only allow probe
	// negotiation to start from the default frame and reject everything
	// else until a successful probe has established baseline. We try the
	// camera's default first, then fall back to "nearest target pixel
	// count" if the default is invalid or missing.
	//
	// Target must match SuggestVideoFrame() (MJPEG: 640x480, YUY2: 320x240)
	// to avoid configuring the camera twice during stream startup.
	{
		const bool prefersMJPEG = (fMJPEGFrames.CountItems() > 0);
		BList* defaultList = prefersMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
		uint8 cameraDefault = prefersMJPEG
			? fDefaultMJPEGFrameIndex : fDefaultUncompressedFrameIndex;
		uint32 targetPixels = prefersMJPEG ? (640 * 480) : (320 * 240);
		int32 bestIndex = -1;

		// 1) Honour the camera's bDefaultFrameIndex if present and valid.
		if (cameraDefault != 0) {
			for (int32 i = 0; i < defaultList->CountItems(); i++) {
				const usb_video_frame_descriptor* desc =
					(const usb_video_frame_descriptor*)defaultList->ItemAt(i);
				if (desc != NULL && desc->frame_index == cameraDefault) {
					bestIndex = i;
					syslog(LOG_INFO, "UVCCamDevice: Default resolution from "
						"camera bDefaultFrameIndex=%u: %ux%u (list index %d)\n",
						cameraDefault, desc->width, desc->height, (int)i);
					break;
				}
			}
		}

		// 2) Fall back to nearest-target heuristic.
		if (bestIndex < 0) {
			uint32 bestDiff = UINT32_MAX;
			bestIndex = 0;
			for (int32 i = 0; i < defaultList->CountItems(); i++) {
				const usb_video_frame_descriptor* desc =
					(const usb_video_frame_descriptor*)defaultList->ItemAt(i);
				if (desc == NULL) continue;
				uint32 pixels = (uint32)desc->width * desc->height;
				uint32 diff = (pixels > targetPixels)
					? (pixels - targetPixels) : (targetPixels - pixels);
				if (diff < bestDiff) {
					bestDiff = diff;
					bestIndex = i;
				}
			}
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)defaultList->ItemAt(bestIndex);
			if (desc) {
				syslog(LOG_INFO, "UVCCamDevice: Default resolution "
					"(target-nearest fallback): %ux%u (index %d)\n",
					desc->width, desc->height, (int)bestIndex);
			}
		}

		fSelectedResolutionIndex = bestIndex;
	}

	// Initialize TurboJPEG decompressor
	fJpegDecompressor = tjInitDecompress();
	/* FIX: Check if TurboJPEG initialization failed */
	if (fJpegDecompressor == NULL) {
		syslog(LOG_WARNING, "UVCCamDevice: tjInitDecompress failed - MJPEG disabled\n");
		/* Continue anyway - YUY2 format will still work */
	}

	// Detect USB controller type for XHCI optimizations
	_DetectControllerType();
	_LogControllerCapabilities();

	// Log frame-based (encoded) capability for diagnostics.
	// The driver does not currently decode H.264/H.265/VP8/M-JPEG2000 streams,
	// so MJPEG or uncompressed remains the active streaming format.
	if (fFrameBasedFrames.CountItems() > 0
			|| fFrameBasedCodec != UVC_CODEC_UNKNOWN) {
		syslog(LOG_INFO,
			"UVCCamDevice: Frame-based stream detected: codec=%s formatIdx=%u "
			"bpp=%u resolutions=%d (not currently decoded by this driver)\n",
			_FrameBasedCodecName(fFrameBasedCodec),
			(unsigned)fFrameBasedFormatIndex,
			(unsigned)fFrameBasedBitsPerPixel,
			(int)fFrameBasedFrames.CountItems());
		for (int32 i = 0; i < fFrameBasedFrames.CountItems(); i++) {
			const uvc_frame_based_resolution* r =
				(const uvc_frame_based_resolution*)
					fFrameBasedFrames.ItemAt(i);
			if (r != NULL) {
				syslog(LOG_INFO,
					"UVCCamDevice:   frame-based[%d] %ux%u interval=%u\n",
					(int)i, r->width, r->height,
					(unsigned)r->default_frame_interval);
			}
		}
	}

	// FIX BUG 3: Set fInitStatus only after full parse
	// Minimum: at least one video format available
	// (interfaces can use index 0, so do not check > 0)
	const bool hasMJPEG = (fMJPEGFrames.CountItems() > 0);
	const bool hasUncompressed = (fUncompressedFrames.CountItems() > 0);
	const bool canDecodeMJPEG = (fJpegDecompressor != NULL);

	// P22: MJPEG-only camera with no JPEG decoder. The driver would otherwise
	// report Init OK, then every FillFrameBuffer would silently produce a
	// blue placeholder. Fail loud and early so the user knows libturbojpeg is
	// missing (or failed to load) and which camera is affected.
	if (hasMJPEG && !hasUncompressed && !canDecodeMJPEG) {
		syslog(LOG_ERR, "UVCCamDevice: Init FAILED - camera %04x:%04x exposes "
			"only MJPEG but libturbojpeg is unavailable; install/repair "
			"libturbojpeg.so or use a camera that also exposes YUY2.\n",
			fDevice->VendorID(), fDevice->ProductID());
		notify_init_failure(fDevice->VendorID(), fDevice->ProductID(),
			"MJPEG-only camera, libturbojpeg missing");
	} else if (hasUncompressed || hasMJPEG) {
		fInitStatus = B_OK;

		// FIX: Initialize fIsMJPEG based on available formats
		// Prefer MJPEG for better bandwidth usage (compressed vs raw YUY2)
		if (hasMJPEG && canDecodeMJPEG)
			fIsMJPEG = true;
		else
			fIsMJPEG = false;

		// Resolve device quirks from the data tables (per-device entry quirks
		// OR-ed with vendor-wide quirks) instead of hard-coding VID checks in
		// the streaming path. See addons/uvc/UVCQuirks.{h,cpp}. The runtime
		// gate in _ConvertYUY2toRGB32 (srcSize > expectedSize) still prevents
		// the stride compensation from firing on correctly-sized frames.
		fQuirks = ResolveWebcamQuirks(fDevice->VendorID(), fDevice->ProductID(),
			MatchedEntryQuirks());
		if ((fQuirks & UVC_QUIRK_SONIX_STRIDE) != 0) {
			fMicrodiaQuirk = true;
			syslog(LOG_INFO, "UVCCamDevice: Sonix stride quirk armed for "
				"%04x:%04x (applied at runtime only when srcSize > expected)\n",
				fDevice->VendorID(), fDevice->ProductID());
		}

		const char* uncompressedName
			= _UncompressedFormatName(fUncompressedPixelFormat);
		syslog(LOG_INFO, "UVCCamDevice: Init OK - ctrl=%u stream=%u frames=%d+%d format=%s\n",
			fControlIndex, fStreamingIndex,
			(int)fUncompressedFrames.CountItems(), (int)fMJPEGFrames.CountItems(),
			fIsMJPEG ? "MJPEG" : uncompressedName);
		syslog(LOG_INFO, "UVCCamDevice: Format indices: MJPEG=%d, Uncompressed=%d (%s)\n",
			fMJPEGFormatIndex, fUncompressedFormatIndex, uncompressedName);

		// Build sorted resolution list for proper fallback ordering
		// (level 0 = highest, level N = lowest)
		_BuildSortedResolutionList();

		// Log frame indices for current format (raw USB order, for debugging)
		BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
		WEBCAM_VERBOSE("UVCCamDevice: Raw %s frame list (%d entries):\n",
			fIsMJPEG ? "MJPEG" : uncompressedName, (int)frameList->CountItems());
		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc)
				WEBCAM_VERBOSE("UVCCamDevice:   raw[%d] %ux%u frame_index=%u\n",
					(int)i, desc->width, desc->height, desc->frame_index);
		}
	} else if (fFrameBasedFrames.CountItems() > 0) {
		syslog(LOG_ERR,
			"UVCCamDevice: Init FAILED - camera only exposes encoded "
			"frame-based stream (%s, %d resolutions). This driver does not "
			"yet decode encoded streams; expose an MJPEG or uncompressed "
			"format in the camera firmware to stream on Haiku.\n",
			_FrameBasedCodecName(fFrameBasedCodec),
			(int)fFrameBasedFrames.CountItems());
		BString reason;
		reason.SetToFormat("%s-only stream, no software decoder",
			_FrameBasedCodecName(fFrameBasedCodec));
		notify_init_failure(fDevice->VendorID(), fDevice->ProductID(),
			reason.String());
	} else {
		syslog(LOG_ERR, "UVCCamDevice: Init FAILED - no video frames available\n");
		notify_init_failure(fDevice->VendorID(), fDevice->ProductID(),
			"USB descriptors did not expose any video format");
	}
}


UVCCamDevice::~UVCCamDevice()
{
	syslog(LOG_INFO, "UVCCamDevice::~UVCCamDevice() - Destroying device\n");

	// Stop video first so the pump cannot use the jpeg handle below.
	// Bounded join, on timeout the device is stalled and we leak.
	StopTransfer();

	// Stop audio transfer if running
	// On timeout the pump still runs, skip frees below.
	bool audioWedged = false;
	if (fAudioTransferRunning) {
		if (StopAudioTransfer() == B_TIMED_OUT)
			audioWedged = true;
	}
	if (audioWedged || IsStalled())
		return;

	// Cleanup audio resources
	if (fAudioBuffer) {
		free(fAudioBuffer);
		fAudioBuffer = NULL;
	}
	if (fAudioRingBuffer) {
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
	}
	if (fAudioRingSem >= 0) {
		delete_sem(fAudioRingSem);
		fAudioRingSem = -1;
	}

	// Cleanup TurboJPEG decompressor
	if (fJpegDecompressor) {
		tjDestroy(fJpegDecompressor);
		fJpegDecompressor = NULL;
		syslog(LOG_INFO, "UVCCamDevice: TurboJPEG decompressor destroyed\n");
	}

	// CRITICAL FIX: Free frame descriptors allocated with new
	// These were allocated in lines 251 and 254, must be freed to avoid memory leak
	for (int32 i = 0; i < fUncompressedFrames.CountItems(); i++) {
		delete (usb_video_frame_descriptor*)fUncompressedFrames.ItemAt(i);
	}
	fUncompressedFrames.MakeEmpty();

	for (int32 i = 0; i < fMJPEGFrames.CountItems(); i++) {
		delete (usb_video_frame_descriptor*)fMJPEGFrames.ItemAt(i);
	}
	fMJPEGFrames.MakeEmpty();

	for (int32 i = 0; i < fFrameBasedFrames.CountItems(); i++) {
		delete (uvc_frame_based_resolution*)fFrameBasedFrames.ItemAt(i);
	}
	fFrameBasedFrames.MakeEmpty();

	for (int32 i = 0; i < fVSStreams.CountItems(); i++) {
		delete (uvc_vs_stream*)fVSStreams.ItemAt(i);
	}
	fVSStreams.MakeEmpty();

	// Cleanup processing controls (Feature 2)
	for (int32 i = 0; i < fProcessingControls.CountItems(); i++) {
		delete (camera_control_info*)fProcessingControls.ItemAt(i);
	}
	fProcessingControls.MakeEmpty();

	// Cleanup Extension Units (Feature XU)
	for (int32 i = 0; i < fExtensionUnits.CountItems(); i++) {
		delete (extension_unit_info*)fExtensionUnits.ItemAt(i);
	}
	fExtensionUnits.MakeEmpty();

	free(fHeaderDescriptor);
}


void
UVCCamDevice::_ParseVideoStreaming(const usbvc_class_descriptor* _descriptor,
	size_t len)
{
	// Need at least length, type and subtype to switch.
	if (_descriptor == NULL || len < 3)
		return;
	switch (_descriptor->descriptorSubtype) {
		case USB_VIDEO_VS_INPUT_HEADER:
		{
			const usb_video_class_specific_vs_interface_input_header_descriptor* descriptor
				= (const usb_video_class_specific_vs_interface_input_header_descriptor*)_descriptor;
			if (_descriptor->length < kUVCVSInputHeaderFixedLen) {
				syslog(LOG_WARNING, "UVCCamDevice: skipping short VS_INPUT_HEADER "
					"(bLength=%u)\n", _descriptor->length);
				break;
			}
			printf("VS_INPUT_HEADER:\t#fmts=%d,ept=0x%x (%s)\n", descriptor->num_formats,
				descriptor->_endpoint_address.endpoint_number,
				descriptor->_endpoint_address.direction ? "IN" : "OUT");
			if (descriptor->_info.dynamic_format_change_support)
				printf("\tDynamic Format Change supported\n");
			printf("\toutput terminal id=%d\n", descriptor->terminal_link);

			// Store still capture method and trigger info
			fStillCaptureMethod = (still_capture_method)descriptor->still_capture_method;
			fTriggerSupport = descriptor->trigger_support;
			fTriggerUsage = descriptor->trigger_usage;
			if (fStillCaptureMethod != STILL_CAPTURE_NONE) {
				printf("\tstill capture: method=%d (%s)\n",
					fStillCaptureMethod, _GetStillCaptureMethodName(fStillCaptureMethod));
			}
			if (descriptor->trigger_support) {
				printf("\ttrigger button fixed to still capture=%s\n",
					descriptor->trigger_usage ? "no" : "yes");
			}
			// Bounds-safe iteration of bmaControls[num_formats][control_size]:
			// only entries whose control byte fits within the descriptor, so a
			// large num_formats/control_size can't walk us past bLength.
			const uint8* inHdrBytes = (const uint8*)_descriptor;
			const uint8 inHdrLen = _descriptor->length;
			const uint8 inSafeFormats = UVCVSHeaderSafeFormatCount(
				descriptor->num_formats, descriptor->control_size,
				kUVCVSInputHeaderFixedLen, inHdrLen);
			if (inSafeFormats < descriptor->num_formats) {
				syslog(LOG_WARNING, "UVCCamDevice: VS_INPUT_HEADER advertises %u "
					"formats but only %u fit bLength=%u; ignoring the rest\n",
					descriptor->num_formats, inSafeFormats, inHdrLen);
			}
			for (uint8 i = 0; i < inSafeFormats; i++) {
				const uint8 ctl = UVCDescByte(inHdrBytes, inHdrLen,
					kUVCVSInputHeaderFixedLen
						+ (size_t)i * descriptor->control_size);
				printf("\tfmt%d: %s %s %s %s - %s %s\n", i,
					(ctl & 0x01) ? "wKeyFrameRate" : "",
					(ctl & 0x02) ? "wPFrameRate" : "",
					(ctl & 0x04) ? "wCompQuality" : "",
					(ctl & 0x08) ? "wCompWindowSize" : "",
					(ctl & 0x10) ? "<Generate Key Frame>" : "",
					(ctl & 0x20) ? "<Update Frame Segment>" : "");
			}
			break;
		}
		case USB_VIDEO_VS_FORMAT_UNCOMPRESSED:
		{
			const usbvc_format_descriptor* descriptor
				= (const usbvc_format_descriptor*)_descriptor;
			// Bounds-safe extraction (UVCDescriptors): reject a short or lying
			// descriptor instead of identifying a format from a stale 16-byte
			// GUID. avail = the descriptor's own bLength.
			const UVCUncompressedFormatCheck fmt =
				UVCCheckUncompressedFormatDescriptor(
					(const uint8*)_descriptor, _descriptor->length);
			if (!fmt.valid) {
				syslog(LOG_WARNING, "UVCCamDevice: rejecting malformed "
					"VS_FORMAT_UNCOMPRESSED descriptor (bLength=%u)\n",
					_descriptor->length);
				break;
			}
			uvc_uncompressed_format detected
				= identify_uncompressed_format(fmt.guid);

			// Format selection across multiple uncompressed descriptors:
			//   - First format wins if no recognized format has been seen yet
			//   - Once a recognized format is selected, only switch to a
			//     "more preferred" recognized format (preference order:
			//     YUY2 > UYVY > NV12 > NV21 > I420 > YV12 > GREY).
			// This avoids being trapped on an UNKNOWN format when the camera
			// also advertises a supported one.
			auto preferenceRank = [](uvc_uncompressed_format f) -> int {
				switch (f) {
					case UVC_FMT_YUY2: return 7;
					case UVC_FMT_UYVY: return 6;
					case UVC_FMT_NV12: return 5;
					case UVC_FMT_NV21: return 4;
					case UVC_FMT_I420: return 3;
					case UVC_FMT_YV12: return 2;
					case UVC_FMT_GREY: return 1;
					default: return 0;
				}
			};
			if (fUncompressedPixelFormat == UVC_FMT_UNKNOWN
					|| preferenceRank(detected)
						> preferenceRank(fUncompressedPixelFormat)) {
				fUncompressedPixelFormat = detected;
				fUncompressedFormatIndex = fmt.formatIndex;
				fDefaultUncompressedFrameIndex = fmt.defaultFrameIndex;
				fIsNV12 = (detected == UVC_FMT_NV12);
			}
			printf("VS_FORMAT_UNCOMPRESSED:\tbFormatIdx=%d,#frmdesc=%d,guid=",
				descriptor->formatIndex, descriptor->numFrameDescriptors);
			print_guid(descriptor->uncompressed.format);
			printf("\n\t#bpp=%d,optfrmidx=%d,aspRX=%d,aspRY=%d\n",
				descriptor->uncompressed.bytesPerPixel,
				descriptor->uncompressed.defaultFrameIndex,
				descriptor->uncompressed.aspectRatioX,
				descriptor->uncompressed.aspectRatioY);
			printf("\tbmInterlaceFlags:\n");
			if (descriptor->uncompressed.interlaceFlags & 1)
				printf("\tInterlaced stream or variable\n");
			printf("\t%d fields per frame\n",
				(descriptor->uncompressed.interlaceFlags & 2) ? 1 : 2);
			if (descriptor->uncompressed.interlaceFlags & 4)
				printf("\tField 1 first\n");
			printf("\tField Pattern: ");
			switch ((descriptor->uncompressed.interlaceFlags & 0x30) >> 4) {
				case 0: printf("Field 1 only\n"); break;
				case 1: printf("Field 2 only\n"); break;
				case 2: printf("Regular pattern of fields 1 and 2\n"); break;
				case 3: printf("Random pattern of fields 1 and 2\n"); break;
			}
			if (descriptor->uncompressed.copyProtect)
				printf("\tRestrict duplication\n");

			if (fUncompressedPixelFormat != UVC_FMT_UNKNOWN
					&& descriptor->formatIndex == fUncompressedFormatIndex) {
				printf("\tSelected uncompressed format: %s\n",
					_UncompressedFormatName(fUncompressedPixelFormat));
			}
			break;
		}
		case USB_VIDEO_VS_FRAME_MJPEG:
		case USB_VIDEO_VS_FRAME_UNCOMPRESSED:
		{
			const usb_video_frame_descriptor* descriptor
				= (const usb_video_frame_descriptor*)_descriptor;

			// P10: reject descriptors whose values are clearly garbage so
			// later code (probe/commit, FillFrameBuffer) doesn't allocate
			// huge buffers or trigger asserts. Skipping the AddItem is
			// safe: a camera that advertises corrupt data here can still
			// expose other usable resolutions on the same VS interface.
			//
			// Validation lives in the bounds-safe, fuzzed helper
			// UVCCheckFrameDescriptor() (addons/uvc/UVCDescriptors) — it reads
			// only within `avail` and rejects an inconsistent bLength or a
			// frame_interval_type that would run the interval loop past the
			// descriptor. Passing descriptor->length keeps today's contract
			// (the USB kit already sized the blob to bLength).
			const UVCFrameDescCheck frameChk = UVCCheckFrameDescriptor(
				(const uint8*)descriptor, descriptor->length);
			const bool descSane = frameChk.valid;

			const char* tag = (_descriptor->descriptorSubtype
					== USB_VIDEO_VS_FRAME_UNCOMPRESSED)
				? "VS_FRAME_UNCOMPRESSED" : "VS_FRAME_MJPEG";
			printf("%s:", tag);

			if (!descSane) {
				syslog(LOG_WARNING,
					"UVCCamDevice: rejecting %s descriptor: %ux%u maxbuf=%"
					B_PRIu32 " (camera %04x:%04x — corrupted descriptor?)\n",
					tag, descriptor->width, descriptor->height,
					descriptor->max_video_frame_buffer_size,
					fDevice->VendorID(), fDevice->ProductID());
			} else {
				// The struct stores discrete intervals in a fixed-size union:
				// discrete_frame_intervals[0] is a flexible array member that
				// overlaps the 12-byte `continuous` variant, so a plain copy of
				// the descriptor can only hold kIntervalsInCopy intervals.
				// Clamp the stored count so every downstream
				// `i < frame_interval_type` loop over this copy stays in bounds.
				// FIX-H1: copy only bLength bytes (zero-padded) instead of
				// sizeof(*descriptor) so a short-but-valid descriptor (e.g.
				// 30B, 1 interval) cannot over-read the kit blob.
				const uint8 kIntervalsInCopy = (uint8)(
					sizeof(usb_video_frame_descriptor::continuous)
						/ sizeof(uint32));
				usb_video_frame_descriptor* copy
					= new (std::nothrow) usb_video_frame_descriptor;
				if (copy == NULL)
					break;
				memset(copy, 0, sizeof(*copy));
				size_t copyLen = descriptor->length;
				if (copyLen > sizeof(*copy))
					copyLen = sizeof(*copy);
				memcpy(copy, descriptor, copyLen);
				if (copy->frame_interval_type > kIntervalsInCopy)
					copy->frame_interval_type = kIntervalsInCopy;

				bool added = false;
				if (_descriptor->descriptorSubtype
						== USB_VIDEO_VS_FRAME_UNCOMPRESSED)
					added = fUncompressedFrames.AddItem(copy);
				else
					added = fMJPEGFrames.AddItem(copy);
				if (!added)
					delete copy;
			}
			printf("\tbFrameIdx=%d,stillsupported=%s,"
				"fixedframerate=%s\n", descriptor->frame_index,
				(descriptor->capabilities & 1) ? "yes" : "no",
				(descriptor->capabilities & 2) ? "yes" : "no");
			if (descSane) {
				printf("\twidth=%u,height=%u,min/max bitrate=%" B_PRIu32
					"/%" B_PRIu32 ", maxbuf=%" B_PRIu32 "\n",
					descriptor->width, descriptor->height,
					descriptor->min_bit_rate, descriptor->max_bit_rate,
					descriptor->max_video_frame_buffer_size);
			}

			if (!descSane)
				break;

			printf("\tdefault frame interval: %" B_PRIu32 ", #intervals(0=cont): %d\n",
				descriptor->default_frame_interval, frameChk.frameIntervalType);
			if (frameChk.frameIntervalType == 0) {
				printf("min/max frame interval=%" B_PRIu32 "/%" B_PRIu32 ", step=%" B_PRIu32 "\n",
					descriptor->continuous.min_frame_interval,
					descriptor->continuous.max_frame_interval,
					descriptor->continuous.frame_interval_step);
			} else for (uint8 i = 0; i < frameChk.frameIntervalType; i++) {
				printf("\tdiscrete frame interval: %" B_PRIu32 "\n",
					descriptor->discrete_frame_intervals[i]);
			}
			break;
		}
		case USB_VIDEO_VS_COLORFORMAT:
		{
			const usb_video_color_matching_descriptor* descriptor
				= (const usb_video_color_matching_descriptor*)_descriptor;
			printf("VS_COLORFORMAT:\n\tbColorPrimaries: ");
			switch (descriptor->color_primaries) {
				case 0: printf("Unspecified\n"); break;
				case 1: printf("BT.709,sRGB\n"); break;
				case 2: printf("BT.470-2(M)\n"); break;
				case 3: printf("BT.470-2(B,G)\n"); break;
				case 4: printf("SMPTE 170M\n"); break;
				case 5: printf("SMPTE 240M\n"); break;
				default: printf("Invalid (%d)\n", descriptor->color_primaries);
			}
			printf("\tbTransferCharacteristics: ");
			switch (descriptor->transfer_characteristics) {
				case 0: printf("Unspecified\n"); break;
				case 1: printf("BT.709\n"); break;
				case 2: printf("BT.470-2(M)\n"); break;
				case 3: printf("BT.470-2(B,G)\n"); break;
				case 4: printf("SMPTE 170M\n"); break;
				case 5: printf("SMPTE 240M\n"); break;
				case 6: printf("Linear (V=Lc)\n"); break;
				case 7: printf("sRGB\n"); break;
				default: printf("Invalid (%d)\n",
					descriptor->transfer_characteristics);
			}
			printf("\tbMatrixCoefficients: ");
			switch (descriptor->matrix_coefficients) {
				case 0: printf("Unspecified\n"); break;
				case 1: printf("BT.709\n"); break;
				case 2: printf("FCC\n"); break;
				case 3: printf("BT.470-2(B,G)\n"); break;
				case 4: printf("SMPTE 170M (BT.601)\n"); break;
				case 5: printf("SMPTE 240M\n"); break;
				default: printf("Invalid (%d)\n", descriptor->matrix_coefficients);
			}
			break;
		}
		case USB_VIDEO_VS_OUTPUT_HEADER:
		{
			const usb_video_class_specific_vs_interface_output_header_descriptor* descriptor
				= (const usb_video_class_specific_vs_interface_output_header_descriptor*)_descriptor;
			if (_descriptor->length < kUVCVSOutputHeaderFixedLen) {
				syslog(LOG_WARNING, "UVCCamDevice: skipping short VS_OUTPUT_HEADER "
					"(bLength=%u)\n", _descriptor->length);
				break;
			}
			printf("VS_OUTPUT_HEADER:\t#fmts=%d,ept=0x%x (%s)\n",
				descriptor->num_formats, descriptor->_endpoint_address.endpoint_number,
				descriptor->_endpoint_address.direction ? "IN" : "OUT");
			printf("\toutput terminal id=%d\n", descriptor->terminal_link);
			// Bounds-safe iteration of bmaControls[num_formats][control_size].
			const uint8* outHdrBytes = (const uint8*)_descriptor;
			const uint8 outHdrLen = _descriptor->length;
			const uint8 outSafeFormats = UVCVSHeaderSafeFormatCount(
				descriptor->num_formats, descriptor->control_size,
				kUVCVSOutputHeaderFixedLen, outHdrLen);
			if (outSafeFormats < descriptor->num_formats) {
				syslog(LOG_WARNING, "UVCCamDevice: VS_OUTPUT_HEADER advertises %u "
					"formats but only %u fit bLength=%u; ignoring the rest\n",
					descriptor->num_formats, outSafeFormats, outHdrLen);
			}
			for (uint8 i = 0; i < outSafeFormats; i++) {
				const uint8 ctl = UVCDescByte(outHdrBytes, outHdrLen,
					kUVCVSOutputHeaderFixedLen
						+ (size_t)i * descriptor->control_size);
				printf("\tfmt%d: %s %s %s %s\n", i,
					(ctl & 0x01) ? "wKeyFrameRate" : "",
					(ctl & 0x02) ? "wPFrameRate" : "",
					(ctl & 0x04) ? "wCompQuality" : "",
					(ctl & 0x08) ? "wCompWindowSize" : "");
			}
			break;
		}
		case USB_VIDEO_VS_STILL_IMAGE_FRAME:
		{
			const usb_video_still_image_frame_descriptor* descriptor
				= (const usb_video_still_image_frame_descriptor*)_descriptor;
			_ParseStillImageFrame(descriptor, len);
			break;
		}
		case USB_VIDEO_VS_FORMAT_MJPEG:
		{
			// VS_FORMAT_MJPEG is a fixed 11-byte descriptor; reject a short one
			// rather than read its fields (format/default-frame index) from
			// stale scratch memory.
			if (_descriptor->length < 11) {
				syslog(LOG_WARNING, "UVCCamDevice: skipping short VS_FORMAT_MJPEG "
					"(bLength=%u)\n", _descriptor->length);
				break;
			}
			const usbvc_format_descriptor* descriptor
				= (const usbvc_format_descriptor*)_descriptor;
			fMJPEGFormatIndex = descriptor->formatIndex;
			fDefaultMJPEGFrameIndex = descriptor->mjpeg.defaultFrameIndex;
			printf("VS_FORMAT_MJPEG:\tbFormatIdx=%d,#frmdesc=%d\n",
				descriptor->formatIndex, descriptor->numFrameDescriptors);
			printf("\t#flgs=%d,optfrmidx=%d,aspRX=%d,aspRY=%d\n",
				descriptor->mjpeg.flags,
				descriptor->mjpeg.defaultFrameIndex,
				descriptor->mjpeg.aspectRatioX,
				descriptor->mjpeg.aspectRatioY);
			printf("\tbmInterlaceFlags:\n");
			if (descriptor->mjpeg.interlaceFlags & 1)
				printf("\tInterlaced stream or variable\n");
			printf("\t%d fields per frame\n",
				(descriptor->mjpeg.interlaceFlags & 2) ? 1 : 2);
			if (descriptor->mjpeg.interlaceFlags & 4)
				printf("\tField 1 first\n");
			printf("\tField Pattern: ");
			switch ((descriptor->mjpeg.interlaceFlags & 0x30) >> 4) {
				case 0: printf("Field 1 only\n"); break;
				case 1: printf("Field 2 only\n"); break;
				case 2: printf("Regular pattern of fields 1 and 2\n"); break;
				case 3: printf("Random pattern of fields 1 and 2\n"); break;
			}
			if (descriptor->mjpeg.copyProtect)
				printf("\tRestrict duplication\n");
			break;
		}
		case USB_VIDEO_VS_FORMAT_MPEG2TS:
			printf("VS_FORMAT_MPEG2TS:\t\n");
			break;
		case USB_VIDEO_VS_FORMAT_DV:
			printf("VS_FORMAT_DV:\t\n");
			break;
		case USB_VIDEO_VS_FORMAT_FRAME_BASED:
		{
			if (len < sizeof(uvc_format_frame_based_descriptor)) {
				printf("VS_FORMAT_FRAME_BASED: truncated (len=%zu)\n", len);
				break;
			}
			const uvc_format_frame_based_descriptor* descriptor
				= (const uvc_format_frame_based_descriptor*)_descriptor;
			uvc_frame_based_codec codec
				= identify_frame_based_codec(descriptor->guid_format);

			printf("VS_FORMAT_FRAME_BASED:\tbFormatIdx=%d,#frmdesc=%d,guid=",
				descriptor->format_index, descriptor->num_frame_descriptors);
			print_guid(descriptor->guid_format);
			printf("\n\tcodec=%s,bpp=%d,optfrmidx=%d,aspRX=%d,aspRY=%d\n",
				_FrameBasedCodecName(codec),
				descriptor->bits_per_pixel,
				descriptor->default_frame_index,
				descriptor->aspect_ratio_x,
				descriptor->aspect_ratio_y);
			printf("\tvariableSize=%d\n", descriptor->variable_size);

			// Detection only - keep the first recognized codec we see.
			// Future work (P8 phase 2): expose this stream via B_MEDIA_ENCODED_VIDEO.
			if (fFrameBasedCodec == UVC_CODEC_UNKNOWN
					&& codec != UVC_CODEC_UNKNOWN) {
				fFrameBasedCodec = codec;
				fFrameBasedFormatIndex = descriptor->format_index;
				fFrameBasedBitsPerPixel = descriptor->bits_per_pixel;
			}
			break;
		}
		case USB_VIDEO_VS_FRAME_FRAME_BASED:
		{
			if (len < sizeof(uvc_frame_frame_based_descriptor)) {
				printf("VS_FRAME_FRAME_BASED: truncated (len=%zu)\n", len);
				break;
			}
			const uvc_frame_frame_based_descriptor* descriptor
				= (const uvc_frame_frame_based_descriptor*)_descriptor;
			printf("VS_FRAME_FRAME_BASED:\tbFrameIdx=%d,%ux%u,bytesPerLine=%"
				B_PRIu32 "\n",
				descriptor->frame_index, descriptor->width, descriptor->height,
				descriptor->bytes_per_line);
			printf("\tmin/max bitrate=%" B_PRIu32 "/%" B_PRIu32
				",defaultInterval=%" B_PRIu32 ",#intervals(0=cont)=%d\n",
				descriptor->min_bit_rate, descriptor->max_bit_rate,
				descriptor->default_frame_interval,
				descriptor->frame_interval_type);

			uvc_frame_based_resolution* entry = new(std::nothrow)
				uvc_frame_based_resolution;
			if (entry != NULL) {
				entry->frame_index = descriptor->frame_index;
				entry->width = descriptor->width;
				entry->height = descriptor->height;
				entry->default_frame_interval
					= descriptor->default_frame_interval;
				entry->bytes_per_line = descriptor->bytes_per_line;
				// FIX: AddItem fails on OOM; do not leak entry.
				if (!fFrameBasedFrames.AddItem(entry))
					delete entry;
			}
			break;
		}
		case USB_VIDEO_VS_FORMAT_STREAM_BASED:
			printf("VS_FORMAT_STREAM_BASED:\t\n");
			break;
		default:
			// P41: surface unknown VS subtypes to syslog. UVC 1.5 and
			// vendor extensions keep adding new subtypes (Microsoft H.264
			// extension, UVC 2.0 simulcast); without a log line in syslog
			// users have no way to spot that the firmware exposes
			// something we don't understand yet.
			printf("INVALID STREAM UNIT TYPE=%d!\n",
				_descriptor->descriptorSubtype);
			syslog(LOG_INFO,
				"UVCCamDevice: unknown VS descriptor subtype=0x%02x "
				"on camera %04x:%04x (please report)\n",
				_descriptor->descriptorSubtype,
				fDevice->VendorID(), fDevice->ProductID());
	}
}


void
UVCCamDevice::_ParseVideoControl(const usbvc_class_descriptor* _descriptor,
	size_t len)
{
	// Need at least length, type and subtype to switch.
	if (_descriptor == NULL || len < 3)
		return;
	switch (_descriptor->descriptorSubtype) {
		case USB_VIDEO_VC_HEADER:
		{
			if (fHeaderDescriptor != NULL) {
				// P11: duplicate VC_HEADER. UVC requires exactly one per
				// VideoControl interface; a second one usually means the
				// camera exposes more than one VC, or that we're seeing
				// the same one twice through the alternate-scan retry
				// loop. Skip silently for the alternate-scan case but
				// surface to syslog so unusual composite devices show up.
				syslog(LOG_WARNING, "UVCCamDevice: Multiple VC_HEADER on "
					"camera %04x:%04x, keeping first (v%x.%02x)\n",
					fDevice->VendorID(), fDevice->ProductID(),
					fHeaderDescriptor->version >> 8,
					fHeaderDescriptor->version & 0xff);
				break;
			}
			// FIX-H3: len is the device-controlled bLength. A truncated
			// header (or numInterfaces beyond len) over-read the heap in
			// the loop below. Reject before malloc/memcpy.
			if (len < 12)
				break;
			// Read byte 11 as raw data, the struct is 3 bytes long.
			// Index on the struct type would step by struct size.
			const uint8* rawBytes = (const uint8*)_descriptor;
			uint8 numIf = rawBytes[11];
			if ((size_t)numIf > len - 12)
				break;
			fHeaderDescriptor = (usbvc_interface_header_descriptor*)malloc(len);
			if (fHeaderDescriptor == NULL)
				break;
			memcpy(fHeaderDescriptor, _descriptor, len);
			printf("VC_HEADER:\tUVC v%x.%02x, clk %.5f MHz\n",
				fHeaderDescriptor->version >> 8,
				fHeaderDescriptor->version & 0xff,
				fHeaderDescriptor->clockFrequency / 1000000.0);
			for (uint8 i = 0; i < fHeaderDescriptor->numInterfacesNumbers; i++) {
				printf("\tStreaming Interface %d\n",
					fHeaderDescriptor->interfaceNumbers[i]);
			}
			break;
		}
		case USB_VIDEO_VC_INPUT_TERMINAL:
		{
			// FIX-H4: an 8-byte non-camera terminal with a spoofed
			// terminalType read the 15-byte camera layout out of bounds.
			if (len < 8)
				break;
			const usbvc_input_terminal_descriptor* descriptor
				= (const usbvc_input_terminal_descriptor*)_descriptor;
			printf("VC_INPUT_TERMINAL:\tid=%d,type=%04x,associated terminal="
				"%d\n", descriptor->terminalID, descriptor->terminalType,
				descriptor->associatedTerminal);
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->terminal));
			if (descriptor->terminalType == USB_VIDEO_CAMERA_IN) {
				// FIX-H4: the camera layout needs 15 bytes + controls.
				if (len < 15)
					break;
				const usb_video_camera_terminal_descriptor* desc
					= (const usb_video_camera_terminal_descriptor*)descriptor;
				if ((size_t)desc->control_size > len - 15)
					break;
				printf("\tObjectiveFocalLength Min/Max %d/%d\n",
					desc->objective_focal_length_min,
					desc->objective_focal_length_max);
				printf("\tOcularFocalLength %d\n", desc->ocular_focal_length);
				printf("\tControlSize %d\n", desc->control_size);

				// Store Camera Terminal info for CT controls
				fCameraTerminalID = desc->terminal_id;
				fHasCameraTerminal = true;

				// Build controls bitmap from descriptor
				fCameraTerminalControls = 0;
				if (desc->control_size >= 1) {
					fCameraTerminalControls |= desc->_controls._control_a.scanning_mode ? (1 << 0) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.auto_exposure_mode ? (1 << 1) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.auto_exposure_priority ? (1 << 2) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.exposure_time_absolute ? (1 << 3) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.exposure_time_relative ? (1 << 4) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.focus_absolute ? (1 << 5) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.focus_relative ? (1 << 6) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.iris_absolute ? (1 << 7) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.iris_relative ? (1 << 8) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.zoom_absolute ? (1 << 9) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.zoom_relative ? (1 << 10) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.pan_tilt_absolute ? (1 << 11) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.pan_tilt_relative ? (1 << 12) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.roll_absolute ? (1 << 13) : 0;
					fCameraTerminalControls |= desc->_controls._control_a.roll_relative ? (1 << 14) : 0;
				}
				if (desc->control_size >= 3) {
					// Note: _contorl_b is a typo in Haiku's USB_video.h header
					fCameraTerminalControls |= desc->_controls._contorl_b.focus_auto ? (1 << 17) : 0;
					fCameraTerminalControls |= desc->_controls._contorl_b.privacy ? (1 << 18) : 0;
				}

				// Log supported CT controls
				printf("\tCamera Terminal Controls: 0x%08x\n", (unsigned)fCameraTerminalControls);
				if (fCameraTerminalControls & (1 << 1)) printf("\t  Auto Exposure Mode\n");
				if (fCameraTerminalControls & (1 << 3)) printf("\t  Exposure Time Absolute\n");
				if (fCameraTerminalControls & (1 << 5)) printf("\t  Focus Absolute\n");
				if (fCameraTerminalControls & (1 << 17)) printf("\t  Focus Auto\n");
				if (fCameraTerminalControls & (1 << 9)) printf("\t  Zoom Absolute\n");
				if (fCameraTerminalControls & (1 << 11)) printf("\t  Pan/Tilt Absolute\n");
				if (fCameraTerminalControls & (1 << 18)) printf("\t  Privacy\n");
			}
			break;
		}
		case USB_VIDEO_VC_OUTPUT_TERMINAL:
		{
			// FIX-H8: truncated 2-byte blob over-read terminal fields.
			if (len < 9)
				break;
			const usb_video_output_terminal_descriptor* descriptor
				= (const usb_video_output_terminal_descriptor*)_descriptor;
			printf("VC_OUTPUT_TERMINAL:\tid=%d,type=%04x,associated terminal="
				"%d, src id=%d\n", descriptor->terminal_id,
				descriptor->terminal_type, descriptor->associated_terminal,
				descriptor->source_id);
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->terminal));
			break;
		}
		case USB_VIDEO_VC_SELECTOR_UNIT:
		{
			// FIX-H6: num_input_pins is device-controlled; bound the
			// source_id walk and the Selector() index to bLength.
			if (len < 6)
				break;
			const usb_video_selector_unit_descriptor* descriptor
				= (const usb_video_selector_unit_descriptor*)_descriptor;
			if ((size_t)descriptor->num_input_pins > len - 6)
				break;
			printf("VC_SELECTOR_UNIT:\tid=%d,#pins=%d\n",
				descriptor->unit_id, descriptor->num_input_pins);
			printf("\t");
			for (uint8 i = 0; i < descriptor->num_input_pins; i++)
				printf("%d ", descriptor->source_id[i]);
			printf("\n");
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->Selector()));
			break;
		}
		case USB_VIDEO_VC_PROCESSING_UNIT:
		{
			// FIX-H7: controls[] and Processing()/VideoStandards() index
			// past bLength when control_size lies. Require the full
			// 8 + control_size + 2 tail before touching any of them.
			if (len < 8)
				break;
			const usb_video_processing_unit_descriptor* descriptor
				= (const usb_video_processing_unit_descriptor*)_descriptor;
			if ((size_t)descriptor->control_size > len - 8
				|| len < (size_t)8 + descriptor->control_size + 2) {
				break;
			}
			fControlRequestIndex = fControlIndex + (descriptor->unit_id << 8);
			fProcessingUnitID = descriptor->unit_id;
			{
				uint8 controls = descriptor->control_size >= 1
					? descriptor->controls[0] : 0;
				syslog(LOG_INFO, "UVCCamDevice: Processing Unit id=%d controls=0x%02x"
					" (%s%s%s%s%s%s%s)\n",
					descriptor->unit_id, controls,
					(controls & 0x01) ? "Brightness " : "",
					(controls & 0x02) ? "Contrast " : "",
					(controls & 0x04) ? "Hue " : "",
					(controls & 0x08) ? "Saturation " : "",
					(controls & 0x10) ? "Sharpness " : "",
					(controls & 0x20) ? "Gamma " : "",
					(controls & 0x40) ? "WB-Temp " : "");
			}
			printf("VC_PROCESSING_UNIT:\t unit id=%d,src id=%d, digmul=%d\n",
				descriptor->unit_id, descriptor->source_id,
				descriptor->max_multiplier);
			printf("\tbControlSize=%d\n", descriptor->control_size);
			if (descriptor->control_size >= 1) {
				if (descriptor->controls[0] & 1)
					printf("\tBrightness\n");
				if (descriptor->controls[0] & 2)
					printf("\tContrast\n");
				if (descriptor->controls[0] & 4)
					printf("\tHue\n");
				if (descriptor->controls[0] & 8)
					printf("\tSaturation\n");
				if (descriptor->controls[0] & 16)
					printf("\tSharpness\n");
				if (descriptor->controls[0] & 32)
					printf("\tGamma\n");
				if (descriptor->controls[0] & 64)
					printf("\tWhite Balance Temperature\n");
				if (descriptor->controls[0] & 128)
					printf("\tWhite Balance Component\n");
			}
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 1)
					printf("\tBacklight Compensation\n");
				if (descriptor->controls[1] & 2)
					printf("\tGain\n");
				if (descriptor->controls[1] & 4)
					printf("\tPower Line Frequency\n");
				if (descriptor->controls[1] & 8)
					printf("\t[AUTO] Hue\n");
				if (descriptor->controls[1] & 16)
					printf("\t[AUTO] White Balance Temperature\n");
				if (descriptor->controls[1] & 32)
					printf("\t[AUTO] White Balance Component\n");
				if (descriptor->controls[1] & 64)
					printf("\tDigital Multiplier\n");
				if (descriptor->controls[1] & 128)
					printf("\tDigital Multiplier Limit\n");
			}
			if (descriptor->control_size >= 3) {
				if (descriptor->controls[2] & 1)
					printf("\tAnalog Video Standard\n");
				if (descriptor->controls[2] & 2)
					printf("\tAnalog Video Lock Status\n");
			}
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->Processing()));
			if (descriptor->VideoStandards()._video_standards.ntsc_525_60)
				printf("\tNTSC  525/60\n");
			if (descriptor->VideoStandards()._video_standards.pal_625_50)
				printf("\tPAL   625/50\n");
			if (descriptor->VideoStandards()._video_standards.secam_625_50)
				printf("\tSECAM 625/50\n");
			if (descriptor->VideoStandards()._video_standards.ntsc_625_50)
				printf("\tNTSC  625/50\n");
			if (descriptor->VideoStandards()._video_standards.pal_525_60)
				printf("\tPAL   525/60\n");
			break;
		}
		case USB_VIDEO_VC_EXTENSION_UNIT:
		{
			const usb_video_extension_unit_descriptor* descriptor
				= (const usb_video_extension_unit_descriptor*)_descriptor;
			_ParseExtensionUnit(descriptor);
			break;
		}
		default:
			printf("Unknown control %d\n", _descriptor->descriptorSubtype);
	}
}


void
UVCCamDevice::_ParseAudioControl(const usb_audio_class_descriptor* _descriptor,
	size_t len)
{
	// Need at least length, type and subtype to switch.
	if (_descriptor == NULL || len < 3)
		return;
	switch (_descriptor->descriptorSubtype) {
		case USB_AUDIO_AC_HEADER:
			break;

		case USB_AUDIO_AC_INPUT_TERMINAL:
		{
			// FIX-H8: truncated blob over-read terminal fields.
			if (len < 12)
				break;
			const usb_audio_input_terminal_descriptor* descriptor
				= (const usb_audio_input_terminal_descriptor*)_descriptor;
			fAudioTerminalID = descriptor->terminalID;
			fAudioChannels = descriptor->numChannels;
			break;
		}

		case USB_AUDIO_AC_OUTPUT_TERMINAL:
			break;

		case USB_AUDIO_AC_FEATURE_UNIT:
		{
			// FIX-H8: unitID sits at offset 5; reject short blobs.
			if (len < 7)
				break;
			const usb_audio_feature_unit_descriptor* descriptor
				= (const usb_audio_feature_unit_descriptor*)_descriptor;
			fAudioFeatureUnitID = descriptor->unitID;
			break;
		}

		default:
			break;
	}
}


void
UVCCamDevice::_ParseAudioStreaming(const usb_audio_class_descriptor* _descriptor,
	size_t len)
{
	// Need at least length, type and subtype to switch.
	if (_descriptor == NULL || len < 3)
		return;
	switch (_descriptor->descriptorSubtype) {
		case USB_AUDIO_AS_GENERAL:
			break;

		case USB_AUDIO_AS_FORMAT_TYPE:
		{
			// FIX-H5: sampleFreqType is device-controlled (up to 255
			// frequencies = 765 bytes). Bound every 3-byte read to the
			// readable bytes instead of walking off the scratch buffer.
			if (len < 8)
				break;
			const usb_audio_format_type_i_descriptor* descriptor
				= (const usb_audio_format_type_i_descriptor*)_descriptor;
			if (descriptor->sampleFreqType == 0) {
				if (len < 14)
					break;
			} else if (len < (size_t)8
				+ (size_t)descriptor->sampleFreqType * 3) {
				break;
			}
			if (descriptor->formatType == USB_AUDIO_FORMAT_TYPE_I) {
				fAudioChannels = descriptor->numChannels;
				fAudioSubFrameSize = descriptor->subFrameSize;
				fAudioBitResolution = descriptor->bitResolution;

				// Parse sample frequencies
				if (descriptor->sampleFreqType == 0) {
					// Continuous range - use max frequency
					uint32 maxFreq = usb_audio_get_sample_rate(
						&descriptor->sampleFrequencies[3]);
					fAudioSampleRate = maxFreq;
				} else {
					// Discrete frequencies - use highest rate
					for (uint8 i = 0; i < descriptor->sampleFreqType; i++) {
						uint32 freq = usb_audio_get_sample_rate(
							&descriptor->sampleFrequencies[i * 3]);
						if (i == 0 || freq > fAudioSampleRate)
							fAudioSampleRate = freq;
					}
				}

				syslog(LOG_INFO, "UVCCamDevice: Audio format: %d ch, %d-bit, %d Hz\n",
					fAudioChannels, fAudioBitResolution, (int)fAudioSampleRate);
			}
			break;
		}

		default:
			break;
	}
}


bool
UVCCamDevice::SupportsIsochronous()
{
	return true;
}


void
UVCCamDevice::Unplugged()
{
	// Stop audio first, base stops video and clears USB pointers.
	// Bounded join, on timeout the pump is stalled and leaked.
	if (fAudioTransferRunning)
		StopAudioTransfer();
	CamDevice::Unplugged();
}


status_t
UVCCamDevice::StartTransfer()
{
	// Entry marker: makes it unambiguous in syslog whether a consumer actually
	// reached the streaming path (vs. getting stuck during parameter setup).
	syslog(LOG_INFO, "UVCCamDevice: StartTransfer requested (ctrlIf=%d streamIf=%d)\n",
		fControlIndex, fStreamingIndex);

	status_t err = _ProbeCommitFormat();
	if (err != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice: StartTransfer aborted - Probe/Commit failed (%s)\n",
			strerror(err));
		return err;
	}

	err = _SelectBestAlternate();
	if (err != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice: StartTransfer aborted - alternate selection failed (%s)\n",
			strerror(err));
		return err;
	}

	syslog(LOG_INFO, "UVCCamDevice: StartTransfer - starting data pump\n");
	return CamDevice::StartTransfer();
}


status_t
UVCCamDevice::StopTransfer()
{
	// CRITICAL FIX: Stop the data pump thread FIRST, before changing USB interface.
	// Previous code called _SelectIdleAlternate() before CamDevice::StopTransfer(),
	// which caused KDL panic "USB object did not become idle!" because SetAlternate()
	// was called while the pump thread was still performing IsochronousTransfer().
	//
	// Correct sequence:
	// 1. Stop the pump thread and wait for it to exit (CamDevice::StopTransfer)
	// 2. Then switch to alternate 0 to turn off LED (_SelectIdleAlternate)

	status_t result = CamDevice::StopTransfer();

	// Now that pump thread is stopped, it's safe to change USB interface
	_SelectIdleAlternate();

	return result;
}


status_t
UVCCamDevice::SuggestVideoFrame(uint32& width, uint32& height)
{
	syslog(LOG_INFO, "UVCCamDevice::SuggestVideoFrame(%" B_PRIu32 ", %" B_PRIu32 ")\n", width, height);

	// Safe mode: start with lowest resolution to avoid bandwidth issues
	// Useful for systems with USB problems or EHCI controllers
	const char* safeMode = getenv("WEBCAM_SAFE_MODE");
	if (safeMode != NULL && (strcmp(safeMode, "1") == 0 || strcmp(safeMode, "yes") == 0)) {
		// Use lowest resolution available
		BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
		int32 count = frameList->CountItems();
		if (count > 0) {
			// Find smallest resolution
			int32 bestIndex = 0;
			uint32 smallestPixels = UINT32_MAX;
			for (int32 i = 0; i < count; i++) {
				const usb_video_frame_descriptor* desc =
					(const usb_video_frame_descriptor*)frameList->ItemAt(i);
				if (desc == NULL)
					continue;
				// Use 64-bit math, 16-bit fields can wrap signed int.
				uint64 pixels = (uint64)desc->width * (uint64)desc->height;
				if (pixels < smallestPixels) {
					smallestPixels = (uint32)pixels;
					bestIndex = i;
				}
			}
			fSelectedResolutionIndex = bestIndex;
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(bestIndex);
			if (desc == NULL)
				return B_ERROR;
			width = desc->width;
			height = desc->height;
			syslog(LOG_INFO, "UVCCamDevice: SAFE MODE - using lowest resolution %ux%u\n",
				width, height);
			AcceptVideoFrame(width, height);
			return B_OK;
		}
	}

	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

	// First check if fIsMJPEG needs to be initialized.
	// P22: only prefer MJPEG when libturbojpeg is actually available;
	// otherwise fall back to the uncompressed list so the user gets video
	// instead of a silent blue placeholder.
	if (fMJPEGFrames.CountItems() > 0 && fJpegDecompressor != NULL)
		fIsMJPEG = true;
	else if (fUncompressedFrames.CountItems() > 0)
		fIsMJPEG = false;

	// Re-select the frame list after determining format
	frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

	// Suggest a resolution that fits the available bandwidth.
	// For MJPEG: prefer 640x480 (good quality/bandwidth balance on USB 2.0).
	// For YUY2: prefer 320x240 (uncompressed needs more bandwidth).
	uint32 targetW = fIsMJPEG ? 640 : 320;
	uint32 targetH = fIsMJPEG ? 480 : 240;

	if (frameList->CountItems() > 0) {
		// Find the resolution closest to target
		int32 bestIndex = 0;
		uint32 bestDiff = UINT32_MAX;
		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc == NULL)
				continue;
			// Use 64-bit math, 16-bit fields can wrap signed int.
			uint64 pixels = (uint64)desc->width * (uint64)desc->height;
			uint64 target = (uint64)targetW * (uint64)targetH;
			uint64 gap = pixels > target ? pixels - target : target - pixels;
			uint32 diff = gap > UINT32_MAX ? UINT32_MAX : (uint32)gap;
			if (diff < bestDiff) {
				bestDiff = diff;
				bestIndex = i;
			}
		}

		fSelectedResolutionIndex = bestIndex;
		const usb_video_frame_descriptor* descriptor
			= (const usb_video_frame_descriptor*)frameList->ItemAt(bestIndex);
		if (descriptor != NULL) {
			width  = descriptor->width;
			height = descriptor->height;
			syslog(LOG_INFO, "UVCCamDevice: SuggestVideoFrame %ux%u (%s, index %d)\n",
				width, height, fIsMJPEG ? "MJPEG" : "YUY2", (int)bestIndex);
			AcceptVideoFrame(width, height);
			return B_OK;
		}
	}

	// Fallback to 320x240 if no frames available
	syslog(LOG_INFO, "UVCCamDevice::SuggestVideoFrame: No frames available, using fallback 320x240\n");
	width = 320;
	height = 240;

	// Try to accept 320x240
	AcceptVideoFrame(width, height);
	return B_OK;
}


status_t
UVCCamDevice::AcceptVideoFrame(uint32& width, uint32& height)
{
	int32 uncompressedCount = fUncompressedFrames.CountItems();
	int32 mjpegCount = fMJPEGFrames.CountItems();

	// Prefer MJPEG over YUY2 for USB webcams
	// Prefer MJPEG (better bandwidth usage) over uncompressed.
	// P22: only choose MJPEG when libturbojpeg is loaded — otherwise the
	// camera would stream encoded JPEGs we can't decode.
	if (mjpegCount > 0 && fJpegDecompressor != NULL)
		fIsMJPEG = true;
	else if (uncompressedCount > 0)
		fIsMJPEG = false;
	else {
		// FALLBACK: If USB descriptor parsing failed (common on Haiku),
		// accept any format with hardcoded 320x240 resolution.
		// This allows video to work even when OtherDescriptorAt() doesn't
		// return UVC class-specific descriptors.
		syslog(LOG_INFO, "UVCCamDevice::AcceptVideoFrame: No frames parsed, using fallback 320x240\n");
		if (width == 0 || height == 0) {
			width = 320;
			height = 240;
		}
		// Try MJPEG first (less bandwidth), then fall back to uncompressed.
		// P22: only when libturbojpeg is available — otherwise force YUY2
		// even in this no-descriptors-parsed fallback path so the producer
		// gets data it can render.
		fIsMJPEG = (fJpegDecompressor != NULL);
		fMJPEGFormatIndex = 1;
		fMJPEGFrameIndex = 1;
		fUncompressedFormatIndex = 1;
		fUncompressedFrameIndex = 1;
		SetVideoFrame(BRect(0, 0, width - 1, height - 1));
		return B_OK;
	}

	// Search in the appropriate frame list
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	int32 frameCount = frameList->CountItems();

	// Use fSelectedResolutionIndex if width/height not specified
	if (width == 0 || height == 0) {
		int32 index = fSelectedResolutionIndex;
		if (index >= 0 && index < frameCount) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(index);
			if (desc) {
				width = desc->width;
				height = desc->height;
				syslog(LOG_INFO, "UVCCamDevice::AcceptVideoFrame: Using selected resolution %ux%u (index %d)\n",
					width, height, index);
			}
		}
		// Fallback to 320x240 if index invalid
		if (width == 0 || height == 0) {
			width = 320;
			height = 240;
		}
	}

	for (int32 i = 0; i < frameCount; i++) {
		const usb_video_frame_descriptor* descriptor
			= (const usb_video_frame_descriptor*)frameList->ItemAt(i);
		if (descriptor == NULL)
			continue;
		if (descriptor->width == width && descriptor->height == height) {
			// Check if resolution is supportable with available bandwidth
			// Auto-fallback to lower resolution if bandwidth is insufficient
			if (!_IsResolutionSupportable(width, height, fIsMJPEG)) {
				float maxFps = _EstimateMaxFps(width, height, fIsMJPEG);
				syslog(LOG_WARNING, "UVCCamDevice: %ux%u limited to %.1f fps - "
					"auto-selecting lower resolution\n", width, height, maxFps);
				syslog(LOG_INFO, "UVCCamDevice: Enable high-bandwidth with: "
					"export WEBCAM_FORCE_HIGH_BANDWIDTH=1\n");

				// Find next lower resolution in sorted list
				int32* sortedIndices = fIsMJPEG ? fSortedMJPEGIndices : fSortedUncompressedIndices;
				int32 sortedCount = fIsMJPEG ? fSortedMJPEGCount : fSortedUncompressedCount;

				// Find current position in sorted list and try next lower
				for (int32 level = 0; level < sortedCount - 1; level++) {
					int32 idx = sortedIndices[level];
					if (idx >= 0 && idx < frameCount) {
						usb_video_frame_descriptor* desc =
							(usb_video_frame_descriptor*)frameList->ItemAt(idx);
						if (desc && desc->width == width && desc->height == height) {
							// Found current, get next lower
							int32 nextIdx = sortedIndices[level + 1];
							if (nextIdx >= 0 && nextIdx < frameCount) {
								usb_video_frame_descriptor* nextDesc =
									(usb_video_frame_descriptor*)frameList->ItemAt(nextIdx);
								if (nextDesc) {
									syslog(LOG_INFO, "UVCCamDevice: Falling back to %ux%u\n",
										nextDesc->width, nextDesc->height);
									width = nextDesc->width;
									height = nextDesc->height;
									// Continue to accept this resolution
									descriptor = nextDesc;
									break;
								}
							}
						}
					}
				}
			}

			/* FIX BUG 11: Use descriptor->frame_index instead of i+1.
			 * The frame_index in the USB descriptor is the value for
			 * Probe/Commit, NOT the position in the list.
			 * If the list is not sorted by frame_index, i+1 is wrong.
			 */
			if (fIsMJPEG) {
				fMJPEGFrameIndex = descriptor->frame_index;
				// MJPEG frames are variable size - use FID/EOF for boundaries
				if (fDeframer)
					((UVCDeframer*)fDeframer)->SetExpectedFrameSize(0);
			} else {
				fUncompressedFrameIndex = descriptor->frame_index;
				// Set expected frame size based on the uncompressed pixel format.
				// 4:2:2 packed (YUY2/UYVY) = 2 bytes/pixel; 4:2:0 planar
				// (NV12/NV21/I420/YV12) = 1.5 bytes/pixel; GREY = 1 byte/pixel.
				if (fDeframer) {
					size_t frameBytes = _UncompressedFrameSize(
						fUncompressedPixelFormat, width, height);
					((UVCDeframer*)fDeframer)->SetExpectedFrameSize(frameBytes);
				}
			}

			// Update current resolution level for correct fallback direction
			// This ensures fallback goes DOWN (to lower resolutions) not UP
			int32 level = _FindResolutionLevel(width, height);
			if (level >= 0) {
				fCurrentResolutionLevel = level;
				syslog(LOG_DEBUG, "UVCCamDevice: Set resolution level to %d for %ux%u\n",
					level, width, height);
			}

			SetVideoFrame(BRect(0, 0, width - 1, height - 1));
			return B_OK;
		}
	}

	return B_ERROR;
}


// PHASE 4: Resolution fallback implementation
status_t
UVCCamDevice::ReduceResolution()
{
	// Get the list of available frames
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	if (frameList->CountItems() <= 1) {
		syslog(LOG_WARNING, "UVCCamDevice::ReduceResolution: "
			"Already at minimum resolution (only 1 resolution available)\n");
		return B_ERROR;
	}

	// Find current resolution in the list
	BRect currentFrame = VideoFrame();
	uint32 currentWidth = (uint32)(currentFrame.Width() + 1);
	uint32 currentHeight = (uint32)(currentFrame.Height() + 1);

	// Find a lower resolution
	uint32 bestWidth = 0;
	uint32 bestHeight = 0;
	int32 bestIndex = -1;
	uint32 currentPixels = currentWidth * currentHeight;

	for (int32 i = 0; i < frameList->CountItems(); i++) {
		const usb_video_frame_descriptor* frameDesc =
			(const usb_video_frame_descriptor*)frameList->ItemAt(i);
		if (frameDesc == NULL)
			continue;

		uint32 width = frameDesc->width;
		uint32 height = frameDesc->height;
		uint32 pixels = width * height;

		// Look for the largest resolution that's smaller than current
		if (pixels < currentPixels && pixels > bestWidth * bestHeight) {
			bestWidth = width;
			bestHeight = height;
			bestIndex = i;
		}
	}

	if (bestIndex < 0) {
		syslog(LOG_WARNING, "UVCCamDevice::ReduceResolution: "
			"No lower resolution found (current: %ux%u)\n",
			currentWidth, currentHeight);
		return B_ERROR;
	}

	syslog(LOG_INFO, "UVCCamDevice::ReduceResolution: "
		"Reducing resolution from %ux%u to %ux%u due to high packet loss\n",
		currentWidth, currentHeight, bestWidth, bestHeight);

	// Apply the new resolution
	uint32 newWidth = bestWidth;
	uint32 newHeight = bestHeight;
	status_t result = AcceptVideoFrame(newWidth, newHeight);

	if (result == B_OK) {
		// Reset packet statistics after resolution change
		ResetPacketStatistics();

		// Reset MJPEG frame size tracking for new resolution
		fMJPEGFrameSizeSum = 0;
		fMJPEGFrameSizeCount = 0;
		fExpectedMJPEGMinSize = 0;  // Will be recalculated for new resolution

		fFallbackActive = true;
		fLastFallbackTime = system_time();
	}

	return result;
}


status_t
UVCCamDevice::_ProbeCommitFormat()
{
	if (fDevice == NULL)
		return B_ERROR;

	// INTERFACE PREPARATION: ALWAYS reset streaming interface to alternate 0
	// before probe/commit. This is critical because:
	// 1. The camera's internal state may not match our tracked fCurrentVideoAlternate
	// 2. After USB errors or EHCI issues, the interface state is undefined
	// 3. Many cameras require explicit SetAlternate(0) before accepting probe commands
	// 4. On first call, fCurrentVideoAlternate is 0 but camera may need reset anyway
	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	if (config != NULL) {
		const BUSBInterface* streaming = config->InterfaceAt(fStreamingIndex);
		if (streaming != NULL) {
			syslog(LOG_INFO, "UVCCamDevice: Probe/Commit: resetting interface to alternate 0\n");
			status_t resetResult = ((BUSBInterface*)streaming)->SetAlternate(0);
			if (resetResult == B_OK) {
				fCurrentVideoAlternate = 0;
				// Give the device time to settle after interface reset
				snooze(100000);  // 100ms - increased for EHCI stability
			} else {
				syslog(LOG_WARNING, "UVCCamDevice: Interface reset failed: %s (continuing anyway)\n",
					strerror(resetResult));
				// Even if SetAlternate fails, give device time to recover
				snooze(50000);
			}
		}
	}

	// Additional delay before first control transfer
	// Some cameras need time after initialization before accepting probe
	// Use WEBCAM_PROBE_DELAY environment variable to increase delay (in ms)
	// Increased default to 100ms for better EHCI compatibility
	bigtime_t probeDelay = 100000;  // 100ms default (was 20ms)
	const char* delayEnv = getenv("WEBCAM_PROBE_DELAY");
	if (delayEnv != NULL) {
		int envDelay = atoi(delayEnv);
		if (envDelay > 0 && envDelay <= 2000) {
			probeDelay = envDelay * 1000;
			syslog(LOG_INFO, "UVCCamDevice: Using WEBCAM_PROBE_DELAY=%dms\n", envDelay);
		}
	}
	snooze(probeDelay);

	usb_video_probe_and_commit_controls request;
	memset(&request, 0, sizeof(request));
	// bmHint (UVC spec table 4-46): set bits mean "this field's value in our
	// request is meaningful, don't override it". We pin dwFrameInterval (we
	// chose it deliberately) but leave wKeyFrameRate / wPFrameRate /
	// wCompQuality flexible so the camera can pick sensible encoder
	// defaults. Setting their hint bits on zero would force the camera to
	// zero, which most firmwares reject — Linux uvcvideo uses the same
	// pattern. WEBCAM_MJPEG_QUALITY (0..10000, UVC unit) lets advanced users
	// override the quality for MJPEG streams (P21).
	request._hint.frame_interval = 1;
	const char* qualityEnv = getenv("WEBCAM_MJPEG_QUALITY");
	if (qualityEnv != NULL && fIsMJPEG) {
		int q = atoi(qualityEnv);
		if (q > 0 && q <= 10000) {
			request.comp_quality = (uint16)q;
			request._hint.comp_quality = 1;
			syslog(LOG_INFO, "UVCCamDevice: WEBCAM_MJPEG_QUALITY=%d pinned\n",
				q);
		}
	}

	/* P2 Feature: Use user-selected frame interval, fall back to device default */
	uint32 frameInterval = 333333;  // Default 30 fps
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	uint32 frameIndex = fIsMJPEG ? fMJPEGFrameIndex : fUncompressedFrameIndex;

	/* First priority: use user-selected frame interval if available */
	if (fSelectedFrameInterval > 0) {
		frameInterval = fSelectedFrameInterval;
		syslog(LOG_INFO, "UVCCamDevice: Using user-selected frame interval %u (%.1f fps)\n",
			frameInterval, 10000000.0f / frameInterval);
	} else if (frameIndex > 0 && frameIndex <= (uint32)frameList->CountItems()) {
		const usb_video_frame_descriptor* frameDesc =
			(const usb_video_frame_descriptor*)frameList->ItemAt(frameIndex - 1);
		if (frameDesc != NULL) {
			/* Fall back to default_frame_interval from descriptor */
			/* Zero means broken firmware, keep the 30 fps default. */
			if (frameDesc->default_frame_interval != 0) {
				frameInterval = frameDesc->default_frame_interval;
				syslog(LOG_INFO, "UVCCamDevice: Using device default frame interval %u (%.1f fps)\n",
					frameInterval, 10000000.0f / frameInterval);
			}
		}
	}

	/* For YUY2 (uncompressed), check bandwidth and adapt if needed */
	if (!fIsMJPEG && frameIndex > 0 && frameIndex <= (uint32)frameList->CountItems()) {
		const usb_video_frame_descriptor* frameDesc =
			(const usb_video_frame_descriptor*)frameList->ItemAt(frameIndex - 1);
		if (frameDesc != NULL) {
			uint32 maxBandwidth = _GetMaxAvailableBandwidth();
			if (maxBandwidth > 0) {
				// 64-bit math, reject empty frames first.
				uint64 frameSize = (uint64)frameDesc->width
					* (uint64)frameDesc->height * 2;
				if (frameSize == 0)
					return B_ERROR;
				uint64 bytesPerSecond = (uint64)maxBandwidth * 8000;
				float maxFps = (float)bytesPerSecond / (float)frameSize;

				syslog(LOG_INFO, "UVCCamDevice: YUY2 bandwidth check: "
					"max=%u bytes/uframe, frameSize=%llu, maxFps=%.1f\n",
					maxBandwidth, (unsigned long long)frameSize, maxFps);

				// Check if frame descriptor has discrete intervals
				if (frameDesc->frame_interval_type > 0) {
					// DISCRETE INTERVALS: Select a supported interval that fits bandwidth
					syslog(LOG_INFO, "UVCCamDevice: Frame has %d discrete intervals\n",
						frameDesc->frame_interval_type);

					uint32 selectedInterval = 0;
					float selectedFps = 0;
					uint32 slowestValidInterval = 0;

					// Iterate through discrete intervals (typically sorted fastest to slowest)
					// Find the fastest interval that fits within available bandwidth
					for (int i = 0; i < frameDesc->frame_interval_type; i++) {
						uint32 interval = frameDesc->discrete_frame_intervals[i];

						// Validate interval: must be > 0 and reasonable (1-60 fps range)
						// Valid range: 166666 (60fps) to 10000000 (1fps)
						if (interval < 166666 || interval > 10000000) {
							syslog(LOG_DEBUG, "UVCCamDevice: Skipping invalid interval[%d]=%u\n",
								i, interval);
							continue;
						}

						float fps = 10000000.0f / interval;
						uint64 requiredBandwidth = (uint64)((double)frameSize * (double)fps);

						syslog(LOG_INFO, "UVCCamDevice: interval %u (%.1f fps): "
							"need %llu, have %llu\n",
							interval, fps,
							(unsigned long long)requiredBandwidth,
							(unsigned long long)bytesPerSecond);

						// Track slowest valid interval for fallback
						if (interval > slowestValidInterval)
							slowestValidInterval = interval;

						// Select this interval if it fits within bandwidth
						// Use 90% safety margin
						if ((double)requiredBandwidth
							<= (double)bytesPerSecond * 0.9) {
							if (selectedInterval == 0 || interval < selectedInterval) {
								// Prefer faster (smaller interval)
								selectedInterval = interval;
								selectedFps = fps;
							}
						}
					}

					// If no interval fits, try standard UVC intervals as fallback
					// (in case descriptor storage lost some intervals)
					if (selectedInterval == 0) {
						// Standard UVC intervals: 10fps, 5fps, 2fps, 1fps
						static const uint32 standardIntervals[] = {
							1000000,   // 10 fps
							2000000,   // 5 fps
							5000000,   // 2 fps
							10000000   // 1 fps
						};

						syslog(LOG_WARNING, "UVCCamDevice: No stored interval fits bandwidth, trying standard intervals\n");

						for (int i = 0; i < 4; i++) {
							uint32 interval = standardIntervals[i];
							float fps = 10000000.0f / interval;
							uint32 requiredBandwidth = (uint32)(frameSize * fps);

							syslog(LOG_INFO, "UVCCamDevice: Trying standard interval %u (%.1f fps): requires %u, available %u\n",
								interval, fps, requiredBandwidth, bytesPerSecond);

							// Use 75% margin - USB isochronous needs headroom for overhead
							if (requiredBandwidth <= (uint32)(bytesPerSecond * 0.75f)) {
								selectedInterval = interval;
								selectedFps = fps;
								syslog(LOG_INFO, "UVCCamDevice: Selected standard fallback interval %u (%.1f fps)\n",
									selectedInterval, selectedFps);
								break;
							}
						}

						// Ultimate fallback: 1 fps
						if (selectedInterval == 0) {
							selectedInterval = 10000000;
							selectedFps = 1.0f;
							syslog(LOG_WARNING, "UVCCamDevice: Using ultimate fallback: 1 fps\n");
						}
					} else {
						syslog(LOG_INFO, "UVCCamDevice: Selected discrete interval %u (%.1f fps) from %d available\n",
							selectedInterval, selectedFps, frameDesc->frame_interval_type);
					}

					// Use selected interval if it's slower than what was originally requested
					if (selectedInterval > frameInterval) {
						syslog(LOG_INFO, "UVCCamDevice: Bandwidth limit: adapting FPS %.1f -> %.1f (interval %u -> %u)\n",
							10000000.0f / frameInterval, selectedFps, frameInterval, selectedInterval);
						frameInterval = selectedInterval;
					}
				} else {
					// CONTINUOUS INTERVALS: Calculate best interval within range
					float safeFps = maxFps * 0.9f;
					if (safeFps < 1.0f) safeFps = 1.0f;

					uint32 adaptedInterval = (uint32)(10000000.0f / safeFps);

					// Clamp to continuous range if available
					if (frameDesc->continuous.min_frame_interval > 0) {
						if (adaptedInterval < frameDesc->continuous.min_frame_interval)
							adaptedInterval = frameDesc->continuous.min_frame_interval;
						if (adaptedInterval > frameDesc->continuous.max_frame_interval)
							adaptedInterval = frameDesc->continuous.max_frame_interval;
					}

					if (adaptedInterval > frameInterval) {
						syslog(LOG_INFO, "UVCCamDevice: Bandwidth limit (continuous): adapting FPS %.1f -> %.1f (interval %u -> %u)\n",
							10000000.0f / frameInterval, 10000000.0f / adaptedInterval, frameInterval, adaptedInterval);
						frameInterval = adaptedInterval;
					} else {
						syslog(LOG_INFO, "UVCCamDevice: YUY2 bandwidth OK: requesting %.1f fps\n",
							10000000.0f / frameInterval);
					}
				}
			}
		}
	}
	request.frame_interval = frameInterval;

	if (fIsMJPEG) {
		request.format_index = fMJPEGFormatIndex;
		request.frame_index = fMJPEGFrameIndex;
	} else {
		request.format_index = fUncompressedFormatIndex;
		request.frame_index = fUncompressedFrameIndex;
	}

	// Validate frame_index and log the actual resolution being requested
	// This helps debug synchronization issues between Producer and driver
	{
		BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
		bool found = false;
		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc && desc->frame_index == request.frame_index) {
				syslog(LOG_INFO, "UVC Probe: frame_index=%d corresponds to %ux%u\n",
					request.frame_index, desc->width, desc->height);
				found = true;
				break;
			}
		}
		if (!found) {
			syslog(LOG_WARNING, "UVC Probe: frame_index=%d not found in frame list!\n",
				request.frame_index);
			// Try to fix by using the first available frame
			if (frameList->CountItems() > 0) {
				const usb_video_frame_descriptor* desc =
					(const usb_video_frame_descriptor*)frameList->ItemAt(0);
				if (desc) {
					syslog(LOG_WARNING, "UVC Probe: Falling back to frame_index=%d (%ux%u)\n",
						desc->frame_index, desc->width, desc->height);
					request.frame_index = desc->frame_index;
					if (fIsMJPEG)
						fMJPEGFrameIndex = desc->frame_index;
					else
						fUncompressedFrameIndex = desc->frame_index;
				}
			}
		}
	}

	// Probe/commit buffer: union the struct over a 64-byte raw region so the
	// ControlTransfer length can exceed sizeof(usb_video_probe_and_commit_controls).
	// UVC 1.5 probe is 48 bytes; some vendor firmwares advertise even larger
	// payloads via GET_LEN. We zero-pad the trailing bytes — most encoder-side
	// fields (bUsage, bBitDepthLuma, etc.) accept 0 as "use device default".
	union probe_commit_buffer {
		usb_video_probe_and_commit_controls fields;
		uint8 raw[64];
	};
	probe_commit_buffer probeBuf;
	memset(&probeBuf, 0, sizeof(probeBuf));
	probeBuf.fields = request;

	// Determine probe/commit length:
	//   1. UVC 1.1+ GET_LEN query (the spec-correct way)
	//   2. Fall back to a UVC-version guess
	//   3. If SET_CUR fails, sweep a list of known vendor sizes (P19)
	//
	// P12: fHeaderDescriptor can be NULL when VS descriptor parsing failed
	// at construction time but the hardcoded-resolution fallback (AUKEY,
	// Microdia) populated frame lists from a static table. Treat a missing
	// header as UVC 1.0 (0x0100) — the safest assumption since UVC 1.0 has
	// the smallest probe size (26 bytes) and does not require GET_LEN.
	uint16 uvcVersion = (fHeaderDescriptor != NULL)
		? fHeaderDescriptor->version : 0x0100;

	// P34: known firmwares advertise bcdUVC=0x0110 but actually only accept
	// the 26-byte UVC 1.0 probe layout. Without this override the version
	// guess picks 34 bytes, SET_CUR fails, the kProbeSizes sweep eventually
	// finds 26 — but the user pays ~2 s of retry latency every Init.
	const uint16 vid = fDevice->VendorID();
	const uint16 pid = fDevice->ProductID();
	const bool isLogitechUvc10Impostor =
		vid == 0x046d && (pid == 0x0825 /* C270 */
			|| pid == 0x081b /* C310 */);
	if (isLogitechUvc10Impostor && uvcVersion > 0x0100) {
		syslog(LOG_INFO, "UVCCamDevice: %04x:%04x advertises UVC 0x%04x "
			"but uses the UVC 1.0 probe layout — forcing 26-byte probe\n",
			vid, pid, uvcVersion);
		uvcVersion = 0x0100;
	}
	size_t length = 0;
	if (uvcVersion >= 0x0110) {
		uint16 queriedLen = 0;
		size_t got = fDevice->ControlTransfer(
			USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
			USB_VIDEO_RC_GET_LEN,
			USB_VIDEO_VS_PROBE_CONTROL << 8,
			fStreamingIndex, sizeof(queriedLen), &queriedLen);
		if (got == sizeof(queriedLen)
				&& queriedLen >= 22
				&& queriedLen <= sizeof(probeBuf.raw)) {
			length = queriedLen;
			WEBCAM_VERBOSE("UVC Probe: GET_LEN reports %zu bytes\n", length);
		} else if (got > 0) {
			WEBCAM_VERBOSE("UVC Probe: GET_LEN returned %zu bytes "
				"(value=%u out of range, falling back to version guess)\n",
				got, queriedLen);
		}
	}
	if (length == 0) {
		length = uvcVersion > 0x100 ? 34 : 26;
		// Fast-path: a previous negotiation already found a working
		// size on this device, try that last good size first.
		if (fProbeCommitSize >= 22
			&& fProbeCommitSize <= sizeof(probeBuf.raw)
			&& fProbeCommitSize != length) {
			WEBCAM_VERBOSE("UVC Probe: trying last good size %zu first\n",
				fProbeCommitSize);
			length = fProbeCommitSize;
		}
	}

	// P20: query GET_MIN/GET_MAX/GET_DEF on the probe control before SET_CUR.
	// Strict camera firmwares (Imaging Source, HiSense, some industrial)
	// STALL on SET_CUR when our requested frame_interval falls outside the
	// advertised [min, max] range. Logging the bounds also makes negotiation
	// failures much easier to diagnose. GET_DEF gives us the camera's
	// preferred default values, which we keep as a safety net in case
	// SET_CUR fails across every probe size we know.
	probe_commit_buffer minBuf, maxBuf, defBuf;
	memset(&minBuf, 0, sizeof(minBuf));
	memset(&maxBuf, 0, sizeof(maxBuf));
	memset(&defBuf, 0, sizeof(defBuf));
	size_t minLen = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, USB_VIDEO_RC_GET_MIN,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &minBuf);
	size_t maxLen = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, USB_VIDEO_RC_GET_MAX,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &maxBuf);
	size_t defLen = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, USB_VIDEO_RC_GET_DEF,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &defBuf);
	const bool haveMin = (minLen == length);
	const bool haveMax = (maxLen == length);
	const bool haveDef = (defLen == length);

	if (haveMin) {
		WEBCAM_VERBOSE("UVC Probe GET_MIN: frame_interval=%u "
			"max_video_frame_size=%u max_payload=%u\n",
			minBuf.fields.frame_interval,
			minBuf.fields.max_video_frame_size,
			minBuf.fields.max_payload_transfer_size);
	}
	if (haveMax) {
		WEBCAM_VERBOSE("UVC Probe GET_MAX: frame_interval=%u "
			"max_video_frame_size=%u max_payload=%u\n",
			maxBuf.fields.frame_interval,
			maxBuf.fields.max_video_frame_size,
			maxBuf.fields.max_payload_transfer_size);
	}
	if (haveDef) {
		WEBCAM_VERBOSE("UVC Probe GET_DEF: format=%u frame=%u "
			"frame_interval=%u\n",
			defBuf.fields.format_index, defBuf.fields.frame_index,
			defBuf.fields.frame_interval);
	}

	// Clamp our requested frame_interval to the advertised range. UVC stores
	// frame_interval in 100ns units: GET_MIN returns the SHORTEST interval
	// (highest FPS), GET_MAX the LONGEST (lowest FPS).
	if (haveMin && request.frame_interval > 0
			&& request.frame_interval < minBuf.fields.frame_interval) {
		syslog(LOG_WARNING, "UVC Probe: frame_interval %u below MIN %u, "
			"clamping (capped at %.1f fps)\n",
			request.frame_interval, minBuf.fields.frame_interval,
			10000000.0f / minBuf.fields.frame_interval);
		request.frame_interval = minBuf.fields.frame_interval;
		probeBuf.fields.frame_interval = request.frame_interval;
	}
	if (haveMax && request.frame_interval > 0
			&& request.frame_interval > maxBuf.fields.frame_interval) {
		syslog(LOG_WARNING, "UVC Probe: frame_interval %u above MAX %u, "
			"clamping (capped at %.1f fps)\n",
			request.frame_interval, maxBuf.fields.frame_interval,
			10000000.0f / maxBuf.fields.frame_interval);
		request.frame_interval = maxBuf.fields.frame_interval;
		probeBuf.fields.frame_interval = request.frame_interval;
	}

	WEBCAM_VERBOSE("UVC Probe request: format=%d frame=%d interval=%u (MJPEG=%d)\n",
		request.format_index, request.frame_index, request.frame_interval, fIsMJPEG);

	// Try SET_CUR Probe with retry logic and fallback to different sizes
	// Some cameras need multiple attempts before responding to control transfers.
	// EHCI controller errors (0x00080248) often indicate timing issues that
	// can be resolved with retries and increased delays.
	//
	// P19: extended kProbeSizes covers UVC 1.0 (22, 26), UVC 1.1 (34),
	// UVC 1.5 (48) and the in-between values observed in vendor firmwares
	// (28, 30, 32, 36, 38, 40, 44). Spec-conformant sizes are tried first.
	static const size_t kProbeSizes[] = {
		34, 26, 48,			// UVC 1.1, 1.0, 1.5
		28, 30, 32, 36, 38, 40, 44,	// Vendor-extended (Microsoft H.264, Realtek)
		22				// Pre-UVC-1.0 / very old firmwares
	};
	static const int kNumProbeSizes = sizeof(kProbeSizes) / sizeof(kProbeSizes[0]);
	static const int kMaxRetries = 5;  // Increased retries per size for EHCI stability
	static const bigtime_t kRetryDelays[] = { 100000, 200000, 300000, 400000, 500000 };  // 100-500ms delays

	size_t actualLength = 0;
	bool probeSuccess = false;

	// First try the expected size based on UVC version with retries
	WEBCAM_VERBOSE("UVC Probe: trying size %zu (UVC version 0x%04x)\n",
		length, uvcVersion);

	for (int retry = 0; retry < kMaxRetries && !probeSuccess; retry++) {
		if (retry > 0) {
			WEBCAM_VERBOSE("UVC Probe: retry %d with delay %lldms\n",
				retry, kRetryDelays[retry - 1] / 1000);
			snooze(kRetryDelays[retry - 1]);
		}

		actualLength = fDevice->ControlTransfer(
			USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
			USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &probeBuf);

		if (actualLength == length) {
			probeSuccess = true;
			syslog(LOG_INFO, "UVC Probe SET_CUR succeeded with size %zu (attempt %d)\n",
				length, retry + 1);
		}
	}

	if (!probeSuccess) {
		syslog(LOG_WARNING, "UVC Probe SET_CUR failed with size %zu after %d retries, trying alternatives...\n",
			length, kMaxRetries);

		// Try other common sizes with retries
		for (int i = 0; i < kNumProbeSizes && !probeSuccess; i++) {
			size_t trySize = kProbeSizes[i];
			if (trySize == length)
				continue;  // Already tried this one

			WEBCAM_VERBOSE("UVC Probe: trying alternative size %zu\n", trySize);

			for (int retry = 0; retry < kMaxRetries && !probeSuccess; retry++) {
				// Delay before each attempt (including first)
				snooze(kRetryDelays[retry < kMaxRetries - 1 ? retry : kMaxRetries - 2]);

				actualLength = fDevice->ControlTransfer(
					USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
					USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, trySize, &probeBuf);

				if (actualLength == trySize) {
					length = trySize;  // Use this size for subsequent operations
					probeSuccess = true;
					syslog(LOG_INFO, "UVC Probe SET_CUR succeeded with alternative size %zu (attempt %d)\n",
						trySize, retry + 1);
					break;
				}
			}
		}
	}

	// P20: strict-firmware safety net. If SET_CUR with our chosen values
	// failed across every probe size we know, the camera is rejecting our
	// payload (not the size). Retry once using the camera's own GET_DEF
	// values — these are by construction inside the camera's accepted range
	// and known to be self-consistent. We lose the requested format/frame
	// but get a working stream that the user can re-negotiate later via
	// AcceptVideoFrame.
	if (!probeSuccess && haveDef) {
		syslog(LOG_WARNING, "UVC Probe: SET_CUR failed with our values, "
			"retrying with GET_DEF defaults (format=%u frame=%u interval=%u)\n",
			defBuf.fields.format_index, defBuf.fields.frame_index,
			defBuf.fields.frame_interval);
		probe_commit_buffer defAttempt;
		memset(&defAttempt, 0, sizeof(defAttempt));
		defAttempt.fields = defBuf.fields;
		for (int retry = 0; retry < kMaxRetries && !probeSuccess; retry++) {
			if (retry > 0)
				snooze(kRetryDelays[retry - 1]);
			actualLength = fDevice->ControlTransfer(
				USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
				USB_VIDEO_RC_SET_CUR,
				USB_VIDEO_VS_PROBE_CONTROL << 8,
				fStreamingIndex, length, &defAttempt);
			if (actualLength == length) {
				probeSuccess = true;
				probeBuf = defAttempt;
				request = defAttempt.fields;
				// Keep our internal indices in sync with what the camera
				// actually accepted, so subsequent re-probes use the right
				// frame.
				if (fIsMJPEG)
					fMJPEGFrameIndex = defAttempt.fields.frame_index;
				else
					fUncompressedFrameIndex = defAttempt.fields.frame_index;
				syslog(LOG_INFO, "UVC Probe: GET_DEF fallback succeeded "
					"(attempt %d)\n", retry + 1);
			}
		}
	}

	if (!probeSuccess) {
		syslog(LOG_ERR, "UVC Probe SET_CUR failed with all known sizes\n");
		syslog(LOG_ERR, "  Last attempt returned: %zd (expected: %zu)\n",
			(ssize_t)actualLength, length);
		return B_ERROR;
	}

	// Store the working probe size for future use
	fProbeCommitSize = length;

	// GET_CUR Probe (get negotiated values)
	probe_commit_buffer responseBuf;
	memset(&responseBuf, 0, sizeof(responseBuf));
	actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, USB_VIDEO_RC_GET_CUR,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &responseBuf);
	usb_video_probe_and_commit_controls& response = responseBuf.fields;

	// Log negotiated values for debugging
	syslog(LOG_INFO, "UVC Probe negotiated: format=%d frame=%d interval=%u\n",
		response.format_index, response.frame_index, response.frame_interval);
	syslog(LOG_INFO, "UVC Probe: maxVideoFrameSize=%u maxPayloadTransfer=%u\n",
		response.max_video_frame_size, response.max_payload_transfer_size);

	// Validate probe response - camera should return non-zero values
	if (response.max_video_frame_size == 0 || response.max_payload_transfer_size == 0) {
		syslog(LOG_WARNING, "UVC Probe: Camera returned zero frame/payload size! "
			"Requested frame_index=%d may not be supported.\n", request.frame_index);
	}

	// Check if camera changed our requested parameters (indicates negotiation)
	if (response.frame_index != request.frame_index) {
		syslog(LOG_WARNING, "UVC Probe: Camera changed frame_index from %d to %d!\n",
			request.frame_index, response.frame_index);
	}
	if (response.format_index != request.format_index) {
		syslog(LOG_WARNING, "UVC Probe: Camera changed format_index from %d to %d!\n",
			request.format_index, response.format_index);
	}

	// CRITICAL FIX: Commit must use NEGOTIATED values from response, not original request!
	// The device may have modified parameters during probe negotiation.
	// Using request instead of response causes format mismatch and corrupted frames.
	// SET_CUR Commit with negotiated parameters
	actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
		USB_VIDEO_VS_COMMIT_CONTROL << 8, fStreamingIndex, length, &responseBuf);
	if (actualLength != length) {
		syslog(LOG_ERR, "UVC Commit failed: expected %zu, got %zu\n", length, actualLength);
		return B_ERROR;
	}

	// Sanitize the negotiated sizes: the Probe/Commit response is device-
	// controlled and can be garbage (a Microdia was observed reporting ~2 GB
	// frame / ~1 GB payload). Bound them against the raw RGB32 size of the
	// selected resolution and sane ceilings so bandwidth math and any
	// allocation can't be driven by a bogus value.
	BRect vf = VideoFrame();
	uint32 rawFrameSize =
		(uint32)((vf.Width() + 1) * (vf.Height() + 1) * 4);
	UVCProbeSizes sane = UVCSanitizeProbeSizes(response.max_video_frame_size,
		response.max_payload_transfer_size, rawFrameSize);
	if (sane.clamped) {
		syslog(LOG_WARNING, "UVC Probe: implausible negotiated sizes "
			"(frame=%u payload=%u) — clamped to frame=%u payload=%u\n",
			response.max_video_frame_size, response.max_payload_transfer_size,
			sane.maxVideoFrameSize, sane.maxPayloadTransferSize);
	}
	fMaxVideoFrameSize = sane.maxVideoFrameSize;
	fMaxPayloadTransferSize = sane.maxPayloadTransferSize;

	syslog(LOG_INFO, "UVC Commit successful: maxPayload=%u\n", fMaxPayloadTransferSize);
	return B_OK;
}


uint32
UVCCamDevice::_GetMaxAvailableBandwidth()
{
	// Calculate max available bandwidth including high-bandwidth endpoints.
	// High-bandwidth endpoints (mult=2 or mult=3) are now supported with
	// modified EHCI driver.

	if (fDevice == NULL)
		return 0;

	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	if (config == NULL)
		return 0;

	const BUSBInterface* streaming = config->InterfaceAt(fStreamingIndex);
	if (streaming == NULL)
		return 0;

	uint32 maxBandwidth = 0;

	for (uint32 i = 0; i < streaming->CountAlternates(); i++) {
		const BUSBInterface* alternate = streaming->AlternateAt(i);
		if (alternate == NULL)
			continue;

		for (uint32 j = 0; j < alternate->CountEndpoints(); j++) {
			const BUSBEndpoint* endpoint = alternate->EndpointAt(j);
			if (endpoint == NULL)
				continue;

			if (!endpoint->IsIsochronous() || !endpoint->IsInput())
				continue;

			// Decode wMaxPacketSize for USB 2.0 high-bandwidth
			uint32 rawMaxPacketSize = endpoint->MaxPacketSize();
			uint32 basePacketSize = rawMaxPacketSize & 0x7FF;
			uint32 transactions = ((rawMaxPacketSize >> 11) & 0x3) + 1;
			uint32 totalBandwidth = basePacketSize * transactions;

			// Use same auto-detection logic as _SelectBestAlternate
			bool allowHighBandwidth = _ShouldUseHighBandwidth();

			if (transactions > 1 && !allowHighBandwidth)
				continue;

			uint32 effectiveBandwidth = (transactions > 1 && allowHighBandwidth) ? totalBandwidth : basePacketSize;
			if (effectiveBandwidth > maxBandwidth)
				maxBandwidth = effectiveBandwidth;
		}
	}

	return maxBandwidth;
}


float
UVCCamDevice::_EstimateMaxFps(uint32 width, uint32 height, bool isMJPEG)
{
	// Calculate estimated max FPS for a given resolution based on available bandwidth
	// USB 2.0 high-speed: 8000 microframes/second

	uint32 bandwidth = _GetMaxAvailableBandwidth();
	if (bandwidth == 0)
		return 0.0f;

	// 64-bit math, zero size means no valid estimate.
	uint64 pixels = (uint64)width * (uint64)height;
	if (pixels == 0)
		return 0.0f;
	uint64 bytesPerSecond = (uint64)bandwidth * 8000;

	if (isMJPEG) {
		// MJPEG is compressed, typically 1/10 to 1/20 of raw YUY2 size
		// Use conservative estimate of 1/8 compression ratio
		uint64 estimatedFrameSize = (pixels * 2) / 8;
		if (estimatedFrameSize == 0)
			return 0.0f;
		return (float)bytesPerSecond / (float)estimatedFrameSize;
	} else {
		// YUY2 uncompressed: 2 bytes per pixel
		uint64 frameSize = pixels * 2;
		if (frameSize == 0)
			return 0.0f;
		return (float)bytesPerSecond / (float)frameSize;
	}
}


bool
UVCCamDevice::_IsResolutionSupportable(uint32 width, uint32 height, bool isMJPEG)
{
	// Estimate if a resolution can achieve minimum acceptable FPS.
	// If bandwidth is unknown (0), allow the resolution anyway and let
	// Probe/Commit handle negotiation - don't block all resolutions.

	float maxFps = _EstimateMaxFps(width, height, isMJPEG);

	if (maxFps == 0.0f) {
		// Bandwidth unknown (no usable endpoints found yet), allow anyway
		return true;
	}

	const float kMinAcceptableFps = 5.0f;
	if (maxFps < kMinAcceptableFps) {
		syslog(LOG_WARNING, "UVCCamDevice: Resolution %ux%u (%s) limited to %.1f fps\n",
			width, height, isMJPEG ? "MJPEG" : "YUY2", maxFps);
		return false;
	}

	return true;
}


status_t
UVCCamDevice::_SelectBestAlternate()
{
	if (fDevice == NULL)
		return B_ERROR;

	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	// FIX: ActiveConfiguration() can be NULL after a failed SetConfiguration
	// or mid-teardown; dereferencing it crashed Init on exotic devices.
	if (config == NULL)
		return B_NO_INIT;
	const BUSBInterface* streaming = config->InterfaceAt(fStreamingIndex);
	if (streaming == NULL)
		return B_BAD_INDEX;

	/* Task 4: Calculate minimum required bandwidth based on negotiated format */
	uint32 requiredBandwidth = 0;
	if (fMaxPayloadTransferSize > 0) {
		/* Use the negotiated max payload from Probe/Commit */
		requiredBandwidth = fMaxPayloadTransferSize;
		syslog(LOG_INFO, "UVCCamDevice: Required bandwidth from probe: %u bytes\n",
			requiredBandwidth);
	}

	/* Scan all alternates and log bandwidth options */
	syslog(LOG_INFO, "UVCCamDevice: Scanning %u alternate settings for bandwidth\n",
		(unsigned)streaming->CountAlternates());

	/* XHCI HIGH-BANDWIDTH BUG WORKAROUND:
	 *
	 * Haiku's XHCI driver has a bug in bandwidth allocation for high-bandwidth
	 * isochronous endpoints (mult > 1). The driver fails with:
	 *   "unsuccessful command 12, error Bandwidth (8)"
	 *   "transfer error on slot X endpoint Y: Endpoint not enabled"
	 *
	 * High-bandwidth endpoints use mult=2 (2 transactions/microframe) or
	 * mult=3 (3 transactions/microframe), allowing up to 3072 bytes/microframe.
	 * wMaxPacketSize encodes this: bits 10:0 = base size, bits 12:11 = additional
	 * transactions (0=1 trans, 1=2 trans, 2=3 trans).
	 *
	 * STRATEGY: Two-pass selection
	 * Pass 1: Only consider endpoints with mult=1 (single transaction, max 1024 bytes)
	 * Pass 2: If no suitable endpoint found and user explicitly enables high-bandwidth,
	 *         allow mult>1 endpoints as fallback
	 *
	 * This ensures the driver works reliably on Haiku while still allowing
	 * advanced users to enable high-bandwidth if their hardware supports it.
	 */

	bool allowHighBandwidth = _ShouldUseHighBandwidth();

	// Select LARGEST bandwidth alternate with mult=1 for best quality
	uint32 bestBandwidth = 0;
	uint32 alternateIndex = 0;
	uint32 endpointIndex = 0;
	bool selectedHighBandwidth = false;

	// PASS 1: Only consider single-transaction endpoints (mult=1)
	WEBCAM_VERBOSE("UVCCamDevice: Pass 1 - scanning for single-transaction endpoints (mult=1)\n");

	for (uint32 i = 0; i < streaming->CountAlternates(); i++) {
		const BUSBInterface* alternate = streaming->AlternateAt(i);

		for (uint32 j = 0; j < alternate->CountEndpoints(); j++) {
			const BUSBEndpoint* endpoint = alternate->EndpointAt(j);

			if (!endpoint->IsIsochronous() || !endpoint->IsInput())
				continue;

			// Decode wMaxPacketSize for USB 2.0 high-bandwidth endpoints
			uint32 rawMaxPacketSize = endpoint->MaxPacketSize();
			uint32 basePacketSize = rawMaxPacketSize & 0x7FF;
			uint32 transactions = ((rawMaxPacketSize >> 11) & 0x3) + 1;
			uint32 totalBandwidth = basePacketSize * transactions;

			WEBCAM_VERBOSE("UVCCamDevice: Alt %u EP %u: raw=0x%04x base=%u trans=%u total=%u bytes\n",
				i, j, rawMaxPacketSize, basePacketSize, transactions, totalBandwidth);

			// Pass 1: Skip high-bandwidth endpoints (mult > 1)
			if (transactions > 1) {
				WEBCAM_VERBOSE("UVCCamDevice: Pass 1: Skipping high-bandwidth endpoint (mult=%u)\n",
					transactions);
				continue;
			}

			// Use base packet size for single-transaction endpoints
			if (basePacketSize > bestBandwidth) {
				bestBandwidth = basePacketSize;
				endpointIndex = j;
				alternateIndex = i;
			}
		}
	}

	// Check if we found a suitable single-transaction endpoint
	if (bestBandwidth > 0) {
		syslog(LOG_INFO, "UVCCamDevice: Pass 1: Found single-transaction endpoint with %u bytes/uframe\n",
			bestBandwidth);
	}

	// PASS 1.5: Pass 1 found a single-transaction alternate but the probe says
	// we need more bandwidth than it provides. Promote to a high-bandwidth
	// (mult>1) alternate that meets the requirement. Required for high-end
	// cameras like Logitech BRIO 4K or 1080p MJPEG/YUY2 streams whose
	// dwMaxPayloadTransferSize exceeds 1024 bytes/microframe.
	//
	// Gating: only runs when _ShouldUseHighBandwidth() returns true. By
	// default that is false on Haiku because EHCI/XHCI have known mult>1
	// bugs (EHCI drops payload after ~2 min; XHCI fails bandwidth alloc).
	// Users on a patched kernel opt in via WEBCAM_FORCE_HIGH_BANDWIDTH=1;
	// if transfers then fail repeatedly, _OnHighBandwidthFailure() flips the
	// flag and subsequent stream restarts skip this pass.
	if (_ShouldUseHighBandwidth() && fMaxPayloadTransferSize > bestBandwidth
			&& bestBandwidth > 0) {
		syslog(LOG_INFO, "UVCCamDevice: Pass 1.5 - need %u bytes/uframe but "
			"single-transaction max is %u, trying high-bandwidth\n",
			fMaxPayloadTransferSize, bestBandwidth);

		for (uint32 i = 0; i < streaming->CountAlternates(); i++) {
			const BUSBInterface* alternate = streaming->AlternateAt(i);
			for (uint32 j = 0; j < alternate->CountEndpoints(); j++) {
				const BUSBEndpoint* endpoint = alternate->EndpointAt(j);
				if (!endpoint->IsIsochronous() || !endpoint->IsInput())
					continue;
				uint32 rawMaxPacketSize = endpoint->MaxPacketSize();
				uint32 basePacketSize = rawMaxPacketSize & 0x7FF;
				uint32 transactions = ((rawMaxPacketSize >> 11) & 0x3) + 1;
				uint32 totalBandwidth = basePacketSize * transactions;

				if (transactions > 1
						&& totalBandwidth >= fMaxPayloadTransferSize
						&& totalBandwidth > bestBandwidth) {
					bestBandwidth = totalBandwidth;
					endpointIndex = j;
					alternateIndex = i;
					selectedHighBandwidth = true;
					syslog(LOG_INFO, "UVCCamDevice: Pass 1.5 promoted to alt %u, "
						"%u bytes/uframe (mult=%u)\n",
						i, totalBandwidth, transactions);
				}
			}
		}

		if (!selectedHighBandwidth) {
			syslog(LOG_INFO, "UVCCamDevice: Pass 1.5 - no high-bandwidth "
				"alternate meets %u bytes/uframe; staying with mult=1 (%u)\n",
				fMaxPayloadTransferSize, bestBandwidth);
		}
	}

	// PASS 2: If no single-transaction endpoint found OR if user forces high-bandwidth
	if (bestBandwidth == 0 && allowHighBandwidth) {
		syslog(LOG_WARNING, "UVCCamDevice: Pass 2 - no single-transaction endpoint found, "
			"trying high-bandwidth (may fail on Haiku XHCI)\n");

		for (uint32 i = 0; i < streaming->CountAlternates(); i++) {
			const BUSBInterface* alternate = streaming->AlternateAt(i);

			for (uint32 j = 0; j < alternate->CountEndpoints(); j++) {
				const BUSBEndpoint* endpoint = alternate->EndpointAt(j);

				if (!endpoint->IsIsochronous() || !endpoint->IsInput())
					continue;

				uint32 rawMaxPacketSize = endpoint->MaxPacketSize();
				uint32 basePacketSize = rawMaxPacketSize & 0x7FF;
				uint32 transactions = ((rawMaxPacketSize >> 11) & 0x3) + 1;
				uint32 totalBandwidth = basePacketSize * transactions;

				if (totalBandwidth > bestBandwidth) {
					bestBandwidth = totalBandwidth;
					endpointIndex = j;
					alternateIndex = i;
					selectedHighBandwidth = (transactions > 1);
				}
			}
		}

		if (selectedHighBandwidth) {
			syslog(LOG_WARNING, "UVCCamDevice: Pass 2: Using high-bandwidth endpoint (%u bytes/uframe)\n",
				bestBandwidth);
			syslog(LOG_WARNING, "UVCCamDevice: WARNING: This may cause 'Bandwidth error' on Haiku XHCI!\n");
			syslog(LOG_WARNING, "UVCCamDevice: If streaming fails, set WEBCAM_DISABLE_HIGH_BANDWIDTH=1\n");
		}
	} else if (bestBandwidth == 0) {
		syslog(LOG_ERR, "UVCCamDevice: No suitable isochronous endpoint found\n");
		syslog(LOG_ERR, "UVCCamDevice: Try setting WEBCAM_FORCE_HIGH_BANDWIDTH=1 to enable high-bandwidth\n");
		return B_ERROR;
	}

	/* Bandwidth selection result */
	if (bestBandwidth == 0)
		return B_ERROR;

	/* Calculate expected frame rate based on bandwidth */
	if (fMaxVideoFrameSize > 0 && bestBandwidth > 0) {
		/* USB 2.0 high-speed: 8000 microframes/second */
		uint32 bytesPerSecond = bestBandwidth * 8000;

		if (fIsMJPEG) {
			/* MJPEG: bandwidth estimate based on uncompressed size is meaningless.
			 * MJPEG typically compresses 10-50x, so real throughput is much higher
			 * than the uncompressed calculation suggests. */
			syslog(LOG_INFO, "UVCCamDevice: Selected bandwidth %u bytes (~%.1f MB/s) for MJPEG stream\n",
				bestBandwidth, bytesPerSecond / 1048576.0f);
		} else {
			float maxFps = (float)bytesPerSecond / fMaxVideoFrameSize;
			syslog(LOG_INFO, "UVCCamDevice: Selected bandwidth %u bytes (~%.1f MB/s, max %.1f fps for frame size %u)\n",
				bestBandwidth, bytesPerSecond / 1048576.0f, maxFps, fMaxVideoFrameSize);

			if (maxFps < 5.0f) {
				syslog(LOG_WARNING, "UVCCamDevice: Bandwidth may be insufficient (max %.1f fps)\n",
					maxFps);
			}
		}
	}

	syslog(LOG_INFO, "UVCCamDevice: Using alternate %u with endpoint %u (bandwidth %u bytes)\n",
		alternateIndex, endpointIndex, bestBandwidth);

	// WARNING: Haiku's BUSBInterface::SetAlternate() has a bug that causes
	// double-free/memory corruption when switching between alternates with
	// different endpoint counts. See patches/0001-USBKit-Fix-double-free-in-SetAlternate.patch
	//
	// The bug is in _UpdateDescriptorAndEndpoints(): it uses the NEW descriptor's
	// num_endpoints to delete the OLD endpoint array. When going from 0 endpoints
	// (alt 0) to N endpoints (alt N), it tries to delete N elements from an empty array.
	//
	// WORKAROUND: We must use SetAlternate() because ControlTransfer doesn't work
	// for SET_INTERFACE in Haiku (the kernel manages interfaces internally).
	// To minimize crash risk, we:
	// 1. Only call SetAlternate when absolutely necessary
	// 2. Immediately re-fetch all interface/endpoint references after the call
	// 3. Never access the old BUSBInterface object after SetAlternate
	if (fCurrentVideoAlternate != alternateIndex) {
		syslog(LOG_INFO, "UVCCamDevice: Changing alternate from %u to %u\n",
			fCurrentVideoAlternate, alternateIndex);

		const BUSBInterface* oldAlt = streaming->AlternateAt(fCurrentVideoAlternate);
		const BUSBInterface* newAlt = streaming->AlternateAt(alternateIndex);
		uint32 oldEndpoints = oldAlt ? oldAlt->CountEndpoints() : 0;
		uint32 newEndpoints = newAlt ? newAlt->CountEndpoints() : 0;

		// WORKAROUND: Haiku's SetAlternate() has a double-free bug when switching
		// between alternates with different endpoint counts (e.g., 0→1).
		// To reduce crash risk, if going from 0 to N endpoints, first switch to
		// an intermediate alternate that has the same endpoint count as the target.
		// This avoids the 0→N transition that triggers the bug.
		if (oldEndpoints == 0 && newEndpoints > 0) {
			// Find an intermediate alternate with >0 endpoints and lower bandwidth
			for (uint32 mid = 1; mid < streaming->CountAlternates(); mid++) {
				if (mid == alternateIndex)
					continue;
				const BUSBInterface* midAlt = streaming->AlternateAt(mid);
				if (midAlt && midAlt->CountEndpoints() > 0) {
					syslog(LOG_INFO, "UVCCamDevice: Using intermediate alternate %u "
						"to avoid 0->%u endpoint transition\n", mid, newEndpoints);
					((BUSBInterface*)streaming)->SetAlternate(mid);
					streaming = config->InterfaceAt(fStreamingIndex);
					if (streaming == NULL) {
						syslog(LOG_ERR, "UVCCamDevice: Interface lost after "
							"intermediate SetAlternate\n");
						return B_BAD_INDEX;
					}
					break;
				}
			}
		}

		status_t setAltResult = ((BUSBInterface*)streaming)->SetAlternate(alternateIndex);
		if (setAltResult != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: SetAlternate(%u) failed: %s\n",
				alternateIndex, strerror(setAltResult));
			return setAltResult;
		}

		syslog(LOG_INFO, "UVCCamDevice: SetAlternate(%u) successful\n", alternateIndex);
		fCurrentVideoAlternate = alternateIndex;

		// Re-fetch references - SetAlternate may invalidate old pointers
		streaming = config->InterfaceAt(fStreamingIndex);
		if (streaming == NULL) {
			syslog(LOG_ERR, "UVCCamDevice: Interface lost after SetAlternate\n");
			return B_BAD_INDEX;
		}
	}

	// Get endpoint from the correct alternate interface
	const BUSBInterface* activeAlt = streaming->AlternateAt(alternateIndex);
	if (activeAlt == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: Alternate %u not found\n", alternateIndex);
		return B_BAD_INDEX;
	}
	streaming = activeAlt;

	fIsoIn = streaming->EndpointAt(endpointIndex);
	fIsoMaxPacketSize = bestBandwidth;

	// Buffer size must be exactly packetSize * numPackets for EHCI alignment.
	//
	// P25 mitigation: IsochronousTransfer is synchronous on Haiku — every
	// completion is followed by a user-space round-trip before the next
	// transfer can start, and microframes that arrive in that gap are lost.
	// Larger batches mean fewer gaps. 64 packets/batch matches the new
	// kMaxPacketDescriptors limit in CamDevice::DataPumpThread, giving the
	// pump ~8 ms of headroom per batch at USB 2.0 high speed (well under
	// the 33 ms frame interval at 30 fps).
	const uint32 kInitialPackets = 64;
	uint32 requiredBufferSize = fIsoMaxPacketSize * kInitialPackets;

	if (requiredBufferSize != fBufferLen || fBuffer == NULL) {
		// FIX: allocate on a temporary so OOM keeps the old buffer valid
		// instead of leaving fBuffer NULL with a stale fBufferLen for the
		// pump to dereference.
		uint8* newBuffer = (uint8*)malloc(requiredBufferSize);
		if (newBuffer == NULL)
			return B_NO_MEMORY;
		free(fBuffer);
		fBuffer = newBuffer;
		fBufferLen = requiredBufferSize;
	}

	// Track if we're using high-bandwidth for auto-detection
	// Check the selected endpoint to see if it's high-bandwidth
	if (fIsoIn != NULL) {
		uint32 rawMaxPacketSize = fIsoIn->MaxPacketSize();
		uint32 transactions = ((rawMaxPacketSize >> 11) & 0x3) + 1;
		fUsingHighBandwidth = (transactions > 1);
		if (fUsingHighBandwidth) {
			syslog(LOG_INFO, "UVCCamDevice: High-bandwidth mode active (mult=%u)\n", transactions);
		}
	}

	return B_OK;
}


status_t
UVCCamDevice::_SelectIdleAlternate()
{
	// Switch to alternate 0 (zero-bandwidth) to turn off LED and stop streaming.
	//
	// IMPORTANT: Must use SetAlternate() to match _SelectBestAlternate().
	// Using ControlTransfer() would desync Haiku's internal alternate state,
	// causing subsequent SetAlternate() calls to fail.
	//
	// The webcam LED is controlled by the USB streaming state:
	// - Alternate 0 = zero-bandwidth (LED off)
	// - Alternate N = active streaming (LED on)

	syslog(LOG_INFO, "UVCCamDevice: _SelectIdleAlternate - switching to alternate 0 (LED off)\n");

	if (fDevice != NULL && fCurrentVideoAlternate != 0) {
		const BUSBConfiguration* config = fDevice->ActiveConfiguration();
		if (config == NULL) {
			syslog(LOG_WARNING, "UVCCamDevice: No active configuration for idle alternate\n");
			fIsoIn = NULL;
			fIsoMaxPacketSize = 0;
			return B_OK;
		}

		const BUSBInterface* streaming = config->InterfaceAt(fStreamingIndex);
		if (streaming == NULL) {
			syslog(LOG_WARNING, "UVCCamDevice: Streaming interface not found for idle alternate\n");
			fIsoIn = NULL;
			fIsoMaxPacketSize = 0;
			return B_OK;
		}

		// Log endpoint counts - transition from N->0 endpoints is safe
		const BUSBInterface* oldAlt = streaming->AlternateAt(fCurrentVideoAlternate);
		const BUSBInterface* newAlt = streaming->AlternateAt(0);
		uint32 oldEndpoints = oldAlt ? oldAlt->CountEndpoints() : 0;
		uint32 newEndpoints = newAlt ? newAlt->CountEndpoints() : 0;
		syslog(LOG_INFO, "UVCCamDevice: Idle transition endpoint count: old=%u new=%u\n",
			oldEndpoints, newEndpoints);

		// SetAlternate(0) - N->0 endpoint transition is generally safe
		status_t result = ((BUSBInterface*)streaming)->SetAlternate(0);
		if (result == B_OK) {
			syslog(LOG_INFO, "UVCCamDevice: SetAlternate(0) successful - LED should be off\n");
			fCurrentVideoAlternate = 0;
		} else {
			syslog(LOG_WARNING, "UVCCamDevice: SetAlternate(0) failed: %s (LED may stay on)\n",
				strerror(result));
		}
	}

	// Invalidate endpoint references - the endpoint is no longer valid for transfers
	fIsoIn = NULL;
	fIsoMaxPacketSize = 0;

	return B_OK;
}


// =============================================================================
// Feature 3: Resolution Fallback Methods
// =============================================================================


void
UVCCamDevice::_InitializeFallbackConfig()
{
	fFallbackConfig.error_threshold_percent = 10.0f;	// 10% packet loss triggers fallback
	fFallbackConfig.evaluation_interval = 5000000;		// 5 seconds
	fFallbackConfig.min_packets_for_eval = 100;			// Need at least 100 packets
	fFallbackConfig.auto_recovery_enabled = true;
	fFallbackConfig.recovery_delay = 30000000;			// 30 seconds of stability before recovery
}


void
UVCCamDevice::_EvaluatePacketLoss()
{
	bigtime_t now = system_time();

	// Grace period after stream start: skip evaluation for the first 5 seconds
	// to avoid false alarms from empty packets during USB endpoint initialization.
	// This prevents the "packet loss 100% → fallback → restart → 100% again"
	// death spiral that kills the stream after resolution changes.
	if (now - fTransferStartTime < 5000000)
		return;

	// Start new evaluation window if needed
	if (fEvalWindowStartTime == 0) {
		fEvalWindowStartTime = now;
		fEvalWindowPackets = 0;
		fEvalWindowErrors = 0;
		return;
	}

	// Check if evaluation window has elapsed
	if ((now - fEvalWindowStartTime) < fFallbackConfig.evaluation_interval) {
		return;
	}

	// Need minimum packets for valid evaluation
	if (fEvalWindowPackets < fFallbackConfig.min_packets_for_eval) {
		fEvalWindowStartTime = now;
		fEvalWindowPackets = 0;
		fEvalWindowErrors = 0;
		return;
	}

	// Calculate loss percentage
	float lossPercent = (float)fEvalWindowErrors * 100.0f / (float)fEvalWindowPackets;

	if (lossPercent > fFallbackConfig.error_threshold_percent) {
		// High packet loss detected - trigger resolution fallback via worker thread
		//
		// NOTE: We use RequestResolutionChange() which queues the change to be
		// processed by the ReconfigThread. This is safe because:
		// 1. The request is asynchronous (just sets a flag and signals semaphore)
		// 2. The actual SetAlternate() happens in ReconfigThread, not here
		// 3. ReconfigThread stops the data pump first, then changes resolution
		//
		// This prevents kernel panic "USB object did not become idle!"

		int32 maxLevel = _GetMaxResolutionLevel();

		if (fCurrentResolutionLevel >= maxLevel) {
			// Already at minimum resolution
			if (!fFallbackWarningShown) {
				syslog(LOG_WARNING, "UVCCamDevice: Packet loss %.1f%% exceeds threshold, "
					"but already at minimum resolution\n", lossPercent);
				fFallbackWarningShown = true;
			}
		} else if (!HasPendingReconfigRequest()) {
			// Calculate fallback resolution
			int32 targetLevel = fCurrentResolutionLevel + 1;
			uint32 newWidth, newHeight;
			_GetResolutionAtLevel(targetLevel, &newWidth, &newHeight);

			syslog(LOG_WARNING, "UVCCamDevice: Packet loss %.1f%% exceeds threshold %.1f%%, "
				"requesting fallback to %ux%u via worker thread\n",
				lossPercent, fFallbackConfig.error_threshold_percent,
				newWidth, newHeight);

			// Request the resolution change via worker thread (safe, non-blocking)
			RequestResolutionChange(newWidth, newHeight);

			fCurrentResolutionLevel = targetLevel;
			fFallbackActive = true;
			fLastFallbackTime = now;
			fFallbackWarningShown = false;
		}

		fStableStartTime = 0;  // Reset stability timer
	} else {
		// Good connection - could attempt recovery if stable for long enough
		// Recovery also uses the safe RequestResolutionChange() mechanism
		if (fStableStartTime == 0) {
			fStableStartTime = now;
		} else if (fFallbackConfig.auto_recovery_enabled &&
			fFallbackActive &&
			fCurrentResolutionLevel > 0 &&
			(now - fStableStartTime) > fFallbackConfig.recovery_delay &&
			!HasPendingReconfigRequest()) {

			// Calculate recovery resolution
			int32 targetLevel = fCurrentResolutionLevel - 1;
			uint32 newWidth, newHeight;
			_GetResolutionAtLevel(targetLevel, &newWidth, &newHeight);

			syslog(LOG_INFO, "UVCCamDevice: Connection stable, "
				"requesting recovery to %ux%u via worker thread\n",
				newWidth, newHeight);

			RequestResolutionChange(newWidth, newHeight);

			fCurrentResolutionLevel = targetLevel;
			if (fCurrentResolutionLevel == 0) {
				fFallbackActive = false;
			}
			fStableStartTime = 0;
		}
	}

	// Reset window
	fEvalWindowStartTime = now;
	fEvalWindowPackets = 0;
	fEvalWindowErrors = 0;
}


status_t
UVCCamDevice::_TriggerResolutionFallback()
{
	int32 maxLevel = _GetMaxResolutionLevel();

	if (fCurrentResolutionLevel >= maxLevel) {
		if (!fFallbackWarningShown) {
			syslog(LOG_WARNING, "UVCCamDevice: Already at minimum resolution, cannot fall back further\n");
			fFallbackWarningShown = true;
		}
		return B_ERROR;
	}

	fTargetResolutionLevel = fCurrentResolutionLevel + 1;

	uint32 newWidth, newHeight;
	_GetResolutionAtLevel(fTargetResolutionLevel, &newWidth, &newHeight);

	syslog(LOG_INFO, "UVCCamDevice: Falling back to resolution level %d (%ux%u)\n",
		(int)fTargetResolutionLevel, newWidth, newHeight);

	// FIX: Actually apply the resolution change by restarting the transfer
	// Stop current transfer
	if (TransferEnabled()) {
		StopTransfer();
		snooze(50000);  // 50ms for camera to process
	}

	// Apply the new resolution
	status_t result = AcceptVideoFrame(newWidth, newHeight);
	if (result != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice: Failed to set fallback resolution: %s\n",
			strerror(result));
		return result;
	}

	// Update current level to match target
	fCurrentResolutionLevel = fTargetResolutionLevel;

	// Restart transfer with new resolution
	result = StartTransfer();
	if (result != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice: Failed to restart transfer after fallback: %s\n",
			strerror(result));
		return result;
	}

	fFallbackActive = true;
	fLastFallbackTime = system_time();
	fFallbackWarningShown = false;

	syslog(LOG_INFO, "UVCCamDevice: Resolution fallback applied successfully\n");
	return B_OK;
}


status_t
UVCCamDevice::_AttemptResolutionRecovery()
{
	if (fCurrentResolutionLevel <= 0) {
		return B_OK;  // Already at best resolution
	}

	fTargetResolutionLevel = fCurrentResolutionLevel - 1;

	uint32 newWidth, newHeight;
	_GetResolutionAtLevel(fTargetResolutionLevel, &newWidth, &newHeight);

	syslog(LOG_INFO, "UVCCamDevice: Connection stable, attempting recovery to level %d (%ux%u)\n",
		(int)fTargetResolutionLevel, newWidth, newHeight);

	// FIX: Actually apply the resolution change
	if (TransferEnabled()) {
		StopTransfer();
		snooze(50000);
	}

	status_t result = AcceptVideoFrame(newWidth, newHeight);
	if (result != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice: Failed to set recovery resolution: %s\n",
			strerror(result));
		fStableStartTime = 0;
		return result;
	}

	fCurrentResolutionLevel = fTargetResolutionLevel;

	result = StartTransfer();
	if (result != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice: Failed to restart transfer after recovery: %s\n",
			strerror(result));
		fStableStartTime = 0;
		return result;
	}

	// Mark that we're no longer in fallback if we're back to original resolution
	if (fCurrentResolutionLevel == 0) {
		fFallbackActive = false;
	}

	fStableStartTime = 0;  // Reset for next recovery attempt
	syslog(LOG_INFO, "UVCCamDevice: Resolution recovery applied successfully\n");

	return B_OK;
}


void
UVCCamDevice::_GetResolutionAtLevel(int32 level, uint32* width, uint32* height)
{
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	int32* sortedIndices = fIsMJPEG ? fSortedMJPEGIndices : fSortedUncompressedIndices;
	int32 sortedCount = fIsMJPEG ? fSortedMJPEGCount : fSortedUncompressedCount;

	// Level 0 = highest resolution (first in sorted list)
	// Higher levels = lower resolutions (later in sorted list)
	int32 sortedLevel = level;

	if (sortedLevel < 0) {
		sortedLevel = 0;
	}

	// Use sorted indices if available, otherwise fall back to raw list order
	if (sortedCount > 0) {
		if (sortedLevel >= sortedCount) {
			sortedLevel = sortedCount - 1;
		}

		int32 frameIndex = sortedIndices[sortedLevel];
		if (frameIndex >= 0 && frameIndex < frameList->CountItems()) {
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)frameList->ItemAt(frameIndex);
			if (desc) {
				*width = desc->width;
				*height = desc->height;
				return;
			}
		}
	} else {
		// Fallback: use raw list order if sorted list not built yet
		int32 index = sortedLevel;
		if (index >= frameList->CountItems()) {
			index = frameList->CountItems() - 1;
		}
		if (index >= 0 && index < frameList->CountItems()) {
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)frameList->ItemAt(index);
			if (desc) {
				*width = desc->width;
				*height = desc->height;
				return;
			}
		}
	}

	// Fallback to safe defaults
	*width = 320;
	*height = 240;
}


// =============================================================================
// Safe Resolution Change Implementation (Worker Thread Handler)
// =============================================================================
// This method is called from the ReconfigThread (not from the data pump thread)
// which makes it safe to call StopTransfer() and SetAlternate().
//
// The sequence is:
// 1. Stop the data pump thread (wait for it to exit)
// 2. Change the USB alternate interface
// 3. Apply the new resolution
// 4. Restart the data pump thread

status_t
UVCCamDevice::_HandleResolutionChange(uint32 width, uint32 height)
{
	syslog(LOG_INFO, "UVCCamDevice: _HandleResolutionChange(%u, %u) starting\n",
		width, height);

	status_t result = B_OK;
	bool wasTransferring = TransferEnabled();

	// Step 1: Stop transfer if running
	// Use full StopTransfer (including idle alternate) to cleanly reset
	// the USB endpoint state before reconfiguring at a new resolution
	if (wasTransferring) {
		syslog(LOG_INFO, "UVCCamDevice: Stopping transfer for resolution change\n");
		result = StopTransfer();
		if (result != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Failed to stop transfer: %s\n",
				strerror(result));
			return result;
		}

		snooze(100000);  // 100ms for USB endpoint to fully reset
	}

	// Step 2: Apply the new resolution
	// AcceptVideoFrame will call _ProbeCommitFormat internally which
	// handles the USB alternate interface selection
	uint32 newWidth = width;
	uint32 newHeight = height;

	syslog(LOG_INFO, "UVCCamDevice: Applying resolution %ux%u\n",
		newWidth, newHeight);

	result = AcceptVideoFrame(newWidth, newHeight);
	if (result != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice: Failed to apply resolution %ux%u: %s\n",
			width, height, strerror(result));

		// Try to restart transfer even if resolution change failed
		if (wasTransferring) {
			syslog(LOG_WARNING, "UVCCamDevice: Attempting to restart with original resolution\n");
			StartTransfer();
		}
		return result;
	}

	// Step 3: Reset packet statistics for the new resolution
	ResetPacketStatistics();

	// Reset MJPEG frame size tracking for new resolution
	fMJPEGFrameSizeSum = 0;
	fMJPEGFrameSizeCount = 0;
	fExpectedMJPEGMinSize = 0;  // Will be recalculated

	// Reset fallback warning flag
	fFallbackWarningShown = false;

	// Step 4: Flush deframer to discard stale frames
	if (fDeframer) {
		fDeframer->Flush();
	}

	// Reset bad frame counter so we don't immediately trigger another downgrade
	fConsecutiveBadFrames = 0;

	// Step 5: Restart transfer if it was running
	if (wasTransferring) {
		syslog(LOG_INFO, "UVCCamDevice: Restarting transfer with new resolution\n");
		result = StartTransfer();
		if (result != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Failed to restart transfer: %s\n",
				strerror(result));
			return result;
		}
	}

	syslog(LOG_INFO, "UVCCamDevice: Resolution change to %ux%u completed successfully\n",
		newWidth, newHeight);

	return B_OK;
}


int32
UVCCamDevice::_GetMaxResolutionLevel()
{
	// Use sorted count, not raw frame list count
	int32 count = fIsMJPEG ? fSortedMJPEGCount : fSortedUncompressedCount;
	if (count == 0) {
		// Fallback to raw list if sorted not yet built
		BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
		count = frameList->CountItems();
	}
	return (count > 0) ? count - 1 : 0;
}


int32
UVCCamDevice::_FindResolutionLevel(uint32 width, uint32 height)
{
	// Find the level (position in sorted list) for a given resolution
	// Returns -1 if not found
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	int32* sortedIndices = fIsMJPEG ? fSortedMJPEGIndices : fSortedUncompressedIndices;
	int32 sortedCount = fIsMJPEG ? fSortedMJPEGCount : fSortedUncompressedCount;

	// Search in sorted list if available
	if (sortedCount > 0) {
		for (int32 level = 0; level < sortedCount; level++) {
			int32 frameIndex = sortedIndices[level];
			if (frameIndex >= 0 && frameIndex < frameList->CountItems()) {
				usb_video_frame_descriptor* desc =
					(usb_video_frame_descriptor*)frameList->ItemAt(frameIndex);
				if (desc && desc->width == width && desc->height == height) {
					return level;
				}
			}
		}
	} else {
		// Fallback to raw list order
		for (int32 i = 0; i < frameList->CountItems(); i++) {
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc && desc->width == width && desc->height == height) {
				return i;
			}
		}
	}

	return -1;  // Not found
}


void
UVCCamDevice::_BuildSortedResolutionList()
{
	// Build sorted index lists for both MJPEG and Uncompressed frames
	// Sorted by pixel count in descending order (largest first)
	// This ensures level 0 = highest resolution, level N = lowest

	// Helper structure for sorting
	struct ResolutionEntry {
		int32 index;
		uint64 pixels;  // width * height
	};

	// Sort MJPEG frames
	int32 mjpegCount = fMJPEGFrames.CountItems();
	if (mjpegCount > 32) mjpegCount = 32;  // Cap at array size

	if (mjpegCount > 0) {
		ResolutionEntry entries[32];
		for (int32 i = 0; i < mjpegCount; i++) {
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)fMJPEGFrames.ItemAt(i);
			entries[i].index = i;
			entries[i].pixels = desc ? (uint64)desc->width * desc->height : 0;
		}

		// Simple bubble sort (small list, done once at init)
		for (int32 i = 0; i < mjpegCount - 1; i++) {
			for (int32 j = 0; j < mjpegCount - i - 1; j++) {
				if (entries[j].pixels < entries[j + 1].pixels) {
					ResolutionEntry temp = entries[j];
					entries[j] = entries[j + 1];
					entries[j + 1] = temp;
				}
			}
		}

		// Copy sorted indices
		for (int32 i = 0; i < mjpegCount; i++) {
			fSortedMJPEGIndices[i] = entries[i].index;
		}
		fSortedMJPEGCount = mjpegCount;

		// Log the sorted order
		WEBCAM_VERBOSE("UVC: MJPEG resolutions sorted by size (count=%d):\n", mjpegCount);
		for (int32 i = 0; i < mjpegCount; i++) {
			int32 sortedIdx = fSortedMJPEGIndices[i];
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)fMJPEGFrames.ItemAt(sortedIdx);
			if (desc) {
				WEBCAM_VERBOSE("UVCCamDevice:   [%d] %ux%u frame_index=%u\n",
					i, desc->width, desc->height, desc->frame_index);
			} else {
				syslog(LOG_WARNING, "UVCCamDevice:   [%d] NULL descriptor at sorted index %d\n",
					i, sortedIdx);
			}
		}
	}

	// Sort Uncompressed frames
	int32 uncompCount = fUncompressedFrames.CountItems();
	if (uncompCount > 32) uncompCount = 32;

	if (uncompCount > 0) {
		ResolutionEntry entries[32];
		for (int32 i = 0; i < uncompCount; i++) {
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)fUncompressedFrames.ItemAt(i);
			entries[i].index = i;
			entries[i].pixels = desc ? (uint64)desc->width * desc->height : 0;
		}

		// Simple bubble sort
		for (int32 i = 0; i < uncompCount - 1; i++) {
			for (int32 j = 0; j < uncompCount - i - 1; j++) {
				if (entries[j].pixels < entries[j + 1].pixels) {
					ResolutionEntry temp = entries[j];
					entries[j] = entries[j + 1];
					entries[j + 1] = temp;
				}
			}
		}

		// Copy sorted indices
		for (int32 i = 0; i < uncompCount; i++) {
			fSortedUncompressedIndices[i] = entries[i].index;
		}
		fSortedUncompressedCount = uncompCount;

		// Log the sorted order
		WEBCAM_VERBOSE("UVC: Uncompressed resolutions sorted by size (count=%d):\n", uncompCount);
		for (int32 i = 0; i < uncompCount; i++) {
			int32 sortedIdx = fSortedUncompressedIndices[i];
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)fUncompressedFrames.ItemAt(sortedIdx);
			if (desc) {
				WEBCAM_VERBOSE("UVCCamDevice:   [%d] %ux%u frame_index=%u\n",
					i, desc->width, desc->height, desc->frame_index);
			} else {
				syslog(LOG_WARNING, "UVCCamDevice:   [%d] NULL descriptor at sorted index %d\n",
					i, sortedIdx);
			}
		}
	}
}


// High-bandwidth auto-detection implementation
// These functions manage automatic fallback from high-bandwidth to low-bandwidth
// endpoints when EHCI controller bugs are detected.


void
UVCCamDevice::OnConsecutiveTransferFailures(uint32 count)
{
	// Call parent implementation for logging
	CamDevice::OnConsecutiveTransferFailures(count);

	// Track failures for high-bandwidth auto-detection
	_OnHighBandwidthFailure();

	// At 300 consecutive failures, suspect EHCI host system error
	// (controller may have entered error state). Try to recover by
	// cycling the streaming alternate: drop to alt 0 (idle) then back
	// to the streaming alternate. This re-initializes the isochronous
	// endpoint without requiring a full controller reset.
	if (count == 300 && !fEHCIRecoveryInProgress.load()) {
		fEHCIRecoveryInProgress.store(true);
		syslog(LOG_ERR, "UVCCamDevice: 300+ consecutive failures - "
			"attempting EHCI recovery via alternate cycle\n");

		// Snapshot under lock, cycle outside so the USB
		// wait never blocks reader threads.
		uint8 streamAlt = 0;
		BUSBDevice* device = NULL;
		uint32 streamingIndex = 0;
		{
			BAutolock lock(Locker());
			if (lock.IsLocked() && fDevice != NULL) {
				streamAlt = (uint8)fCurrentVideoAlternate;
				device = fDevice;
				streamingIndex = fStreamingIndex;
			}
		}
		if (streamAlt > 0 && device != NULL) {
			const BUSBConfiguration* cfg = device->ActiveConfiguration();
			if (cfg != NULL) {
				BUSBInterface* iface = const_cast<BUSBInterface*>(
					cfg->InterfaceAt(streamingIndex));
				if (iface != NULL)
					iface->SetAlternate(0);
			}
			snooze(100000);
			// Re-fetch after SetAlternate, old pointers dangle.
			cfg = device->ActiveConfiguration();
			if (cfg != NULL) {
				BUSBInterface* iface = const_cast<BUSBInterface*>(
					cfg->InterfaceAt(streamingIndex));
				if (iface != NULL) {
					iface->SetAlternate(streamAlt);
					syslog(LOG_INFO, "UVCCamDevice: EHCI recovery alt cycle "
						"complete (alt %u -> 0 -> %u)\n", streamAlt, streamAlt);
				}
			}
			// Refresh endpoint, SetAlternate recreates it.
			BAutolock relock(Locker());
			if (relock.IsLocked() && fDevice != NULL) {
				const BUSBConfiguration* freshCfg
					= fDevice->ActiveConfiguration();
				if (freshCfg != NULL) {
					const BUSBInterface* freshIface
						= freshCfg->InterfaceAt(streamingIndex);
					const BUSBInterface* activeAlt = freshIface != NULL
						? freshIface->AlternateAt(streamAlt) : NULL;
					if (activeAlt != NULL) {
						for (uint32 i = 0;
							i < activeAlt->CountEndpoints(); i++) {
							const BUSBEndpoint* endpoint
								= activeAlt->EndpointAt(i);
							if (endpoint != NULL
								&& endpoint->IsIsochronous()
								&& endpoint->IsInput()) {
								fIsoIn = endpoint;
								fIsoMaxPacketSize
									= endpoint->MaxPacketSize() & 0x7FF;
								break;
							}
						}
					}
				}
			}
		}
		fEHCIRecoveryInProgress.store(false);
	}
}


void
UVCCamDevice::OnTransferSuccess()
{
	// Reset failure counter and confirm high-bandwidth works
	_ResetHighBandwidthState();
}


bool
UVCCamDevice::_ShouldUseHighBandwidth()
{
	/* XHCI HIGH-BANDWIDTH BUG WORKAROUND:
	 *
	 * Haiku's XHCI driver has a known bug that fails to properly allocate
	 * bandwidth for high-bandwidth isochronous endpoints (mult > 1).
	 * The error manifests as:
	 *   "unsuccessful command 12, error Bandwidth (8)"
	 *   "Endpoint not enabled"
	 *
	 * Therefore, we DEFAULT TO DISABLED on Haiku.
	 * Users can explicitly enable high-bandwidth via environment variable
	 * if they want to test or if they have a patched kernel.
	 */

	// Check environment variable override first
	const char* disableHighBW = getenv("WEBCAM_DISABLE_HIGH_BANDWIDTH");
	if (disableHighBW != NULL && (strcmp(disableHighBW, "1") == 0 || strcmp(disableHighBW, "yes") == 0)) {
		return false;
	}

	// Only enable high-bandwidth if EXPLICITLY requested by user
	const char* forceHighBW = getenv("WEBCAM_FORCE_HIGH_BANDWIDTH");
	if (forceHighBW != NULL && (strcmp(forceHighBW, "1") == 0 || strcmp(forceHighBW, "yes") == 0)) {
		syslog(LOG_WARNING, "UVCCamDevice: High-bandwidth FORCED via WEBCAM_FORCE_HIGH_BANDWIDTH\n");
		syslog(LOG_WARNING, "UVCCamDevice: This may cause 'Bandwidth error' on Haiku XHCI!\n");
		return true;
	}

	// If we've already tested and it worked, continue using it
	if (fHighBandwidthTested && fHighBandwidthWorks) {
		return true;
	}

	// If we've already tested and it failed, don't try again
	if (fHighBandwidthTested && !fHighBandwidthWorks) {
		return false;
	}

	/* DEFAULT: Disabled on Haiku due to XHCI bug
	 *
	 * The kernel bug is in xhci.cpp bandwidth allocation. Until this is
	 * fixed upstream, high-bandwidth endpoints will fail. Users on systems
	 * with working XHCI (or with patched kernel) can enable via:
	 *   export WEBCAM_FORCE_HIGH_BANDWIDTH=1
	 */
	return false;
}


void
UVCCamDevice::_OnHighBandwidthFailure()
{
	fHighBandwidthFailures++;

	// After 5 consecutive failures, mark high-bandwidth as broken
	// Reduced from 50 because XHCI bandwidth errors cause immediate timeouts
	// and waiting for 50 failures wastes too much time
	const uint32 kFailureThreshold = 5;

	if (fHighBandwidthFailures >= kFailureThreshold && fUsingHighBandwidth) {
		syslog(LOG_WARNING, "UVCCamDevice: %u consecutive transfer failures detected\n",
			fHighBandwidthFailures);
		syslog(LOG_WARNING, "UVCCamDevice: Disabling high-bandwidth mode (EHCI limitation detected)\n");

		fHighBandwidthTested = true;
		fHighBandwidthWorks = false;

		// Trigger a stream restart at the next lower resolution. Without this
		// the producer keeps timing out at the current resolution because
		// _SelectBestAlternate() now refuses mult>1, but the chosen resolution
		// still requires more bandwidth than mult=1 can carry. Dropping a
		// resolution level frees up bandwidth so the stream actually recovers.
		int32 maxLevel = _GetMaxResolutionLevel();
		if (fCurrentResolutionLevel < maxLevel && !HasPendingReconfigRequest()) {
			int32 targetLevel = fCurrentResolutionLevel + 1;
			uint32 newWidth = 0, newHeight = 0;
			_GetResolutionAtLevel(targetLevel, &newWidth, &newHeight);
			if (newWidth > 0 && newHeight > 0) {
				syslog(LOG_INFO, "UVCCamDevice: High-bandwidth failed - "
					"falling back to %ux%u via worker thread\n",
					newWidth, newHeight);
				RequestResolutionChange(newWidth, newHeight);
				fCurrentResolutionLevel = targetLevel;
				fFallbackActive = true;
				fLastFallbackTime = system_time();
			}
		} else {
			syslog(LOG_INFO, "UVCCamDevice: High-bandwidth failed but already "
				"at minimum resolution or reconfig pending - waiting\n");
		}
	}
}


void
UVCCamDevice::_ResetHighBandwidthState()
{
	// Called when stream starts successfully - reset failure counter
	fHighBandwidthFailures = 0;

	// If we're using high-bandwidth and getting data, mark it as working
	if (fUsingHighBandwidth && !fHighBandwidthTested) {
		fHighBandwidthTested = true;
		fHighBandwidthWorks = true;
		syslog(LOG_INFO, "UVCCamDevice: High-bandwidth mode confirmed working (XHCI detected)\n");
	}
}


// =============================================================================
// USB Controller and Speed Detection (XHCI Optimization Support)
// =============================================================================
// These methods detect the USB host controller type and device speed to enable
// XHCI-specific optimizations such as:
// - High-bandwidth isochronous endpoints (mult>1)
// - Dynamic interrupt moderation (low latency mode)
// - TBC/TLBPC for reduced packet loss
// - USB 3.0+ SuperSpeed bandwidth utilization

void
UVCCamDevice::_DetectControllerType()
{
	if (fControllerDetected)
		return;

	// Detect USB device speed first
	usb_device_speed speed = _GetUSBSpeed();
	fControllerInfo.device_speed = speed;

	/* XHCI HIGH-BANDWIDTH BUG:
	 *
	 * Haiku's XHCI driver has a bug in bandwidth allocation for high-bandwidth
	 * isochronous endpoints (mult > 1). Until this is fixed upstream, we must
	 * mark ALL controller types as NOT high-bandwidth safe by default.
	 *
	 * The bug is in src/add-ons/kernel/busses/usb/xhci.cpp and manifests as:
	 *   "unsuccessful command 12, error Bandwidth (8)"
	 *   "transfer error on slot X endpoint Y: Endpoint not enabled"
	 *
	 * Users can override this via WEBCAM_FORCE_HIGH_BANDWIDTH=1 env var.
	 */

	// Infer controller type from device speed and behavior
	// USB 3.0+ speeds can only be achieved with XHCI
	if (speed >= USB_SPEED_SUPER) {
		fControllerInfo.type = USB_HC_XHCI;
		fControllerInfo.type_name = "XHCI";
		// Remove USB_CAP_HIGH_BANDWIDTH due to Haiku bug
		fControllerInfo.capabilities = USB_CAP_DYNAMIC_IMOD
			| USB_CAP_TBC_TLBPC
			| USB_CAP_LPM
			| USB_CAP_STREAMS;
		fControllerInfo.expected_imod = XHCI_IMOD_LOW_LATENCY;
		fControllerInfo.high_bandwidth_safe = false;  // Disabled due to Haiku XHCI bug

		// Do NOT pre-confirm high-bandwidth - let user explicitly enable
		fHighBandwidthTested = false;
		fHighBandwidthWorks = false;

		syslog(LOG_INFO, "UVCCamDevice: XHCI detected (USB 3.0+), high-bandwidth DISABLED by default\n");
		syslog(LOG_INFO, "UVCCamDevice: Set WEBCAM_FORCE_HIGH_BANDWIDTH=1 to enable high-bandwidth\n");
	} else if (speed == USB_SPEED_HIGH) {
		// USB 2.0 High-Speed - could be EHCI or XHCI in compatibility mode
		// Both have issues with high-bandwidth on Haiku
		fControllerInfo.type = USB_HC_XHCI;  // Assume XHCI (most common)
		fControllerInfo.type_name = "XHCI (USB 2.0 mode)";
		// Remove USB_CAP_HIGH_BANDWIDTH due to Haiku bug
		fControllerInfo.capabilities = USB_CAP_DYNAMIC_IMOD
			| USB_CAP_TBC_TLBPC;
		fControllerInfo.expected_imod = XHCI_IMOD_LOW_LATENCY;
		fControllerInfo.high_bandwidth_safe = false;  // Disabled due to Haiku XHCI bug

		// Do NOT pre-confirm high-bandwidth
		fHighBandwidthTested = false;
		fHighBandwidthWorks = false;

		syslog(LOG_INFO, "UVCCamDevice: USB 2.0 High-Speed device, high-bandwidth DISABLED by default\n");
	} else if (speed == USB_SPEED_FULL) {
		// USB 1.1 Full-Speed - could be OHCI, UHCI, or USB 2.0/3.0 hub
		fControllerInfo.type = USB_HC_EHCI;  // Most likely behind EHCI companion
		fControllerInfo.type_name = "EHCI (full-speed)";
		fControllerInfo.capabilities = USB_CAP_NONE;
		fControllerInfo.expected_imod = XHCI_IMOD_DEFAULT;
		fControllerInfo.high_bandwidth_safe = false;

		fHighBandwidthTested = true;
		fHighBandwidthWorks = false;  // Full-speed doesn't support high-bandwidth
	} else {
		// Low-speed or unknown
		fControllerInfo.type = USB_HC_UNKNOWN;
		fControllerInfo.type_name = "unknown";
		fControllerInfo.capabilities = USB_CAP_NONE;
		fControllerInfo.expected_imod = XHCI_IMOD_DEFAULT;
		fControllerInfo.high_bandwidth_safe = false;

		fHighBandwidthTested = true;
		fHighBandwidthWorks = false;
	}

	fControllerDetected = true;
}


usb_device_speed
UVCCamDevice::_GetUSBSpeed()
{
	// Try to determine device speed from available information
	// Haiku's BUSBDevice doesn't directly expose speed, but we can infer it
	// from endpoint characteristics and device descriptor

	if (fDevice == NULL)
		return USB_SPEED_UNKNOWN;

	// Check bcdUSB field in device descriptor for USB version support
	// This tells us the maximum speed the device supports
	const usb_device_descriptor* desc = fDevice->Descriptor();
	if (desc == NULL)
		return USB_SPEED_UNKNOWN;

	uint16 bcdUSB = desc->usb_version;

	// USB 3.1+ devices
	if (bcdUSB >= 0x0310) {
		syslog(LOG_INFO, "UVCCamDevice: Device supports USB 3.1+ (bcdUSB=0x%04x)\n", bcdUSB);
		return USB_SPEED_SUPER_PLUS;
	}
	// USB 3.0 devices
	if (bcdUSB >= 0x0300) {
		syslog(LOG_INFO, "UVCCamDevice: Device supports USB 3.0 (bcdUSB=0x%04x)\n", bcdUSB);
		return USB_SPEED_SUPER;
	}
	// USB 2.0 devices - check endpoint max packet size for actual speed
	if (bcdUSB >= 0x0200) {
		// Check if any isochronous endpoint has high-speed characteristics
		// High-speed isoch endpoints can have maxPacketSize > 64 bytes
		// and use mult bits (bits 12:11 of wMaxPacketSize)
		if (fIsoIn != NULL) {
			uint16 maxPacket = fIsoIn->MaxPacketSize();
			uint16 baseSize = maxPacket & 0x7FF;
			uint8 mult = ((maxPacket >> 11) & 0x3) + 1;

			if (baseSize > 64 || mult > 1) {
				syslog(LOG_INFO, "UVCCamDevice: High-speed detected (maxPacket=%u, base=%u, mult=%u)\n",
					maxPacket, baseSize, mult);
				return USB_SPEED_HIGH;
			}
		}

		// Control endpoint 0 maxPacketSize can also indicate speed
		// USB 2.0 high-speed: 64 bytes, Full-speed: 8/16/32/64 bytes
		if (desc->max_packet_size_0 == 64) {
			syslog(LOG_INFO, "UVCCamDevice: Likely high-speed (EP0 maxPacket=64)\n");
			return USB_SPEED_HIGH;
		}

		syslog(LOG_INFO, "UVCCamDevice: USB 2.0 device, assuming high-speed\n");
		return USB_SPEED_HIGH;
	}

	// USB 1.x devices
	syslog(LOG_INFO, "UVCCamDevice: USB 1.x device (bcdUSB=0x%04x)\n", bcdUSB);
	return USB_SPEED_FULL;
}


void
UVCCamDevice::_LogControllerCapabilities()
{
	if (!fControllerDetected)
		return;

	const char* speedName;
	switch (fControllerInfo.device_speed) {
		case USB_SPEED_LOW:			speedName = "Low (1.5 Mbps)"; break;
		case USB_SPEED_FULL:		speedName = "Full (12 Mbps)"; break;
		case USB_SPEED_HIGH:		speedName = "High (480 Mbps)"; break;
		case USB_SPEED_SUPER:		speedName = "Super (5 Gbps)"; break;
		case USB_SPEED_SUPER_PLUS:	speedName = "Super+ (10+ Gbps)"; break;
		default:					speedName = "Unknown"; break;
	}

	syslog(LOG_INFO, "UVCCamDevice: USB Controller Detection Results:\n");
	syslog(LOG_INFO, "  Controller type: %s\n", fControllerInfo.type_name);
	syslog(LOG_INFO, "  Device speed: %s\n", speedName);
	syslog(LOG_INFO, "  High-bandwidth safe: %s\n",
		fControllerInfo.high_bandwidth_safe ? "yes" : "no");

	// Log capabilities
	if (fControllerInfo.capabilities != USB_CAP_NONE) {
		syslog(LOG_INFO, "  Capabilities:\n");
		if (fControllerInfo.capabilities & USB_CAP_HIGH_BANDWIDTH)
			syslog(LOG_INFO, "    - High-bandwidth isochronous (mult>1)\n");
		if (fControllerInfo.capabilities & USB_CAP_DYNAMIC_IMOD)
			syslog(LOG_INFO, "    - Dynamic interrupt moderation\n");
		if (fControllerInfo.capabilities & USB_CAP_TBC_TLBPC)
			syslog(LOG_INFO, "    - TBC/TLBPC isochronous TRBs\n");
		if (fControllerInfo.capabilities & USB_CAP_LPM)
			syslog(LOG_INFO, "    - Link Power Management\n");
		if (fControllerInfo.capabilities & USB_CAP_STREAMS)
			syslog(LOG_INFO, "    - Bulk streams\n");
	}

	// Log expected IMOD mode for isochronous streaming
	const char* imodName;
	switch (fControllerInfo.expected_imod) {
		case XHCI_IMOD_LOW_LATENCY:	imodName = "Low latency (16000 IRQ/s)"; break;
		case XHCI_IMOD_MEDIUM:		imodName = "Medium (8000 IRQ/s)"; break;
		case XHCI_IMOD_DEFAULT:		imodName = "Default (4000 IRQ/s)"; break;
		case XHCI_IMOD_POWER_SAVE:	imodName = "Power save (2000 IRQ/s)"; break;
		default:					imodName = "Unknown"; break;
	}
	syslog(LOG_INFO, "  Expected IMOD: %s\n", imodName);

	// Recommendation for 1080p streaming
	if (fControllerInfo.high_bandwidth_safe) {
		syslog(LOG_INFO, "UVCCamDevice: 1080p@30fps streaming is SUPPORTED\n");
	} else {
		syslog(LOG_WARNING, "UVCCamDevice: 1080p may require fallback - limited bandwidth\n");
	}
}


bigtime_t
UVCCamDevice::_GetOptimalPollInterval()
{
	// Return optimal buffer poll interval based on detected IMOD mode
	// This helps the driver synchronize with XHCI's interrupt rate

	if (!fControllerDetected) {
		// Not detected yet, use safe default
		return CamConfig::kPollIntervalDefault;
	}

	switch (fControllerInfo.expected_imod) {
		case XHCI_IMOD_LOW_LATENCY:
			// 16000 IRQ/s - poll frequently for isochronous
			return CamConfig::kPollIntervalLowLatency;

		case XHCI_IMOD_MEDIUM:
			// 8000 IRQ/s - moderate polling
			return CamConfig::kPollIntervalMedium;

		case XHCI_IMOD_POWER_SAVE:
			// 2000 IRQ/s - slower polling to save CPU
			return CamConfig::kPollIntervalPowerSave;

		case XHCI_IMOD_DEFAULT:
		default:
			// 4000 IRQ/s or unknown - safe default
			return CamConfig::kPollIntervalDefault;
	}
}


uint32
UVCCamDevice::_GetExpectedIRQsPerFrame()
{
	// Return expected number of IRQs per video frame based on IMOD mode
	// Useful for predicting buffer accumulation behavior

	if (!fControllerDetected) {
		return CamConfig::kIRQsPerFrameDefault;
	}

	switch (fControllerInfo.expected_imod) {
		case XHCI_IMOD_LOW_LATENCY:
			return CamConfig::kIRQsPerFrameLowLatency;

		case XHCI_IMOD_MEDIUM:
			return CamConfig::kIRQsPerFrameMedium;

		case XHCI_IMOD_DEFAULT:
		case XHCI_IMOD_POWER_SAVE:
		default:
			return CamConfig::kIRQsPerFrameDefault;
	}
}


size_t
UVCCamDevice::_GetOptimalBufferSize()
{
	// Return optimal USB transfer buffer size based on device speed
	// USB 3.0 devices can efficiently handle larger transfers

	if (!fControllerDetected) {
		return CamConfig::kUSB2OptimalTransfer;
	}

	switch (fControllerInfo.device_speed) {
		case USB_SPEED_SUPER:
		case USB_SPEED_SUPER_PLUS:
			// USB 3.0+ can handle larger buffers efficiently
			return CamConfig::kUSB3OptimalTransfer;

		case USB_SPEED_HIGH:
		case USB_SPEED_FULL:
		case USB_SPEED_LOW:
		default:
			return CamConfig::kUSB2OptimalTransfer;
	}
}


uint64
UVCCamDevice::_GetMaxBandwidth()
{
	// Return maximum theoretical bandwidth based on USB speed
	// Used for calculating achievable frame rates

	if (!fControllerDetected) {
		return CamConfig::kUSB2HighSpeedBandwidth;
	}

	switch (fControllerInfo.device_speed) {
		case USB_SPEED_SUPER_PLUS:
			return CamConfig::kUSB3SuperSpeedPlusBW;

		case USB_SPEED_SUPER:
			return CamConfig::kUSB3SuperSpeedBandwidth;

		case USB_SPEED_HIGH:
			return CamConfig::kUSB2HighSpeedBandwidth;

		case USB_SPEED_FULL:
			return 1500000;  // ~1.5 MB/s (12 Mbps)

		case USB_SPEED_LOW:
			return 187500;   // ~187 KB/s (1.5 Mbps)

		default:
			return CamConfig::kUSB2HighSpeedBandwidth;
	}
}


float
UVCCamDevice::_GetExpectedPacketCompletionRate()
{
	// Return expected packet completion rate based on controller capabilities
	// XHCI with TBC/TLBPC has better packet delivery than EHCI

	if (_HasTBCTLBPCSupport()) {
		// XHCI with TBC/TLBPC: expect 99.9% packet completion
		return CamConfig::kXHCIPacketCompletionRate;
	}

	// EHCI or unknown: expect 99.5% packet completion
	return CamConfig::kEHCIPacketCompletionRate;
}


bool
UVCCamDevice::_HasTBCTLBPCSupport()
{
	// Check if the controller supports TBC/TLBPC isochronous optimization
	// This is an XHCI-specific feature

	if (!fControllerDetected) {
		return false;
	}

	// TBC/TLBPC is available on XHCI controllers
	return (fControllerInfo.capabilities & USB_CAP_TBC_TLBPC) != 0;
}


UVCCamDeviceAddon::UVCCamDeviceAddon(WebCamMediaAddOn* webcam)
	: CamDeviceAddon(webcam)
{
	syslog(LOG_INFO, "UVCCamDeviceAddon::UVCCamDeviceAddon(WebCamMediaAddOn* webcam)\n");
	SetSupportedDevices(kSupportedDevices);
}


UVCCamDeviceAddon::~UVCCamDeviceAddon()
{
}


const char *
UVCCamDeviceAddon::BrandName()
{
	syslog(LOG_INFO, "UVCCamDeviceAddon::BrandName()\n");
	return "USB Video Class";
}


UVCCamDevice *
UVCCamDeviceAddon::Instantiate(CamRoster& roster, BUSBDevice* from)
{
	syslog(LOG_INFO, "UVCCamDeviceAddon::Instantiate()\n");
	return new UVCCamDevice(*this, from);
}


extern "C" status_t
B_WEBCAM_MKINTFUNC(uvccam)
(WebCamMediaAddOn* webcam, CamDeviceAddon **addon)
{
	*addon = new UVCCamDeviceAddon(webcam);
	return B_OK;
}
