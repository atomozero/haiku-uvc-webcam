/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2009, Ithamar Adema, <ithamar.adema@team-embedded.nl>.
 * Distributed under the terms of the MIT License.
 */


#include "UVCCamDevice.h"
#include "UVCDeframer.h"
#include "CamDebug.h"
#include "CamConfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <ParameterWeb.h>
#include <media/Buffer.h>

#undef TRACE
#define TRACE(x...) do {} while(0)
//#define TRACE(x...) printf(x)


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
	{{ 0, 0, 0, 0x04f2, 0xb40a, }, "Chicony",       "HD UVC WebCam",                   "??" },

	// Alcor Micro
	{{ 0, 0, 0, 0x058f, 0x3820, }, "Alcor Micro",   "AU3820 PC USB Webcam",            "??" },

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

	// Generic class-based matching (fallback for unknown devices)
	{{ USB_VIDEO_DEVICE_CLASS, USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS, 0, 0, 0 }, "Generic UVC", "Video Class", "??" },
	{{ 0xEF, 0x02, 0, 0, 0 }, "Miscellaneous device", "Interface association", "??" },
	{{ 0, 0, 0, 0, 0}, NULL, NULL, NULL }
};

/* Table 2-1 Compression Formats of USB Video Payload Uncompressed */
usbvc_guid kYUY2Guid = {0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
usbvc_guid kNV12Guid = {0x4e, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00, 0x80,
	0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};


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
	if (!memcmp(guid, kYUY2Guid, sizeof(usbvc_guid)))
		printf("YUY2");
	else if (!memcmp(guid, kNV12Guid, sizeof(usbvc_guid)))
		printf("NV12");
	else {
		printf("%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:"
			"%02x:%02x:%02x:%02x", guid[0], guid[1], guid[2], guid[3], guid[4],
			guid[5], guid[6], guid[7], guid[8], guid[9], guid[10], guid[11],
			guid[12], guid[13], guid[14], guid[15]);
	}
}


UVCCamDevice::UVCCamDevice(CamDeviceAddon& _addon, BUSBDevice* _device)
	: CamDevice(_addon, _device),
	fHeaderDescriptor(NULL),
	fInterruptIn(NULL),
	fCurrentVideoAlternate(0),
	fUncompressedFormatIndex(1),
	fUncompressedFrameIndex(1),
	fMJPEGFormatIndex(1),
	fMJPEGFrameIndex(1),		// Initialize to 1, will be updated by AcceptVideoFrame
	fMaxVideoFrameSize(0),
	fMaxPayloadTransferSize(0),
	fProbeCommitSize(34),		// Default UVC 1.1+ size, will be auto-detected
	fJpegDecompressor(NULL),
	fIsMJPEG(false),
	fIsNV12(false),
	fMicrodiaQuirk(false),
	// FIX BUG 6: Inizializza contatori diagnostici per istanza
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
	fNumFrameIntervals(0),
	fSelectedFrameInterval(333333),  // Default 30fps (10000000/30)
	fAudioRingSem(-1),
	// Frame validation state (Feature 1)
	fLastValidFrame(NULL),
	fLastValidFrameSize(0),
	fLastValidWidth(0),
	fLastValidHeight(0),
	fConsecutiveBadFrames(0),
	fFrameRepeatEnabled(true),
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

	for (uint32 i = 0; i < _device->CountConfigurations(); i++) {
		config = _device->ConfigurationAt(i);
		if (config == NULL)
			continue;
		_device->SetConfiguration(config);
		for (uint32 j = 0; j < config->CountInterfaces(); j++) {
			interface = config->InterfaceAt(j);
			if (interface == NULL)
				continue;

			if (interface->Class() == USB_VIDEO_DEVICE_CLASS && interface->Subclass()
				== USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS) {
				printf("UVCCamDevice: (%" B_PRIu32 ",%" B_PRIu32 "): Found Video Control "
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
					const BUSBEndpoint* e = interface->EndpointAt(k);  // FIX BUG 1: era 'i', corretto in 'k'
					if (e && e->IsInterrupt() && e->IsInput()) {
						fInterruptIn = e;
						break;
					}
				}
				// FIX BUG 3: fInitStatus spostato dopo parsing completo (vedi fine costruttore)
			} else if (interface->Class() == USB_VIDEO_DEVICE_CLASS && interface->Subclass()
				== USB_VIDEO_INTERFACE_VIDEOSTREAMING_SUBCLASS) {
				printf("UVCCamDevice: (%" B_PRIu32 ",%" B_PRIu32 "): Found Video Streaming "
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

				printf("UVCCamDevice: Total frames found: uncompressed=%d, mjpeg=%d\n",
					(int)fUncompressedFrames.CountItems(), (int)fMJPEGFrames.CountItems());

				for (uint32 k = 0; k < interface->CountEndpoints(); k++) {
					const BUSBEndpoint* e = interface->EndpointAt(k);  // FIX BUG 1: era 'i', corretto in 'k'
					if (e && e->IsIsochronous() && e->IsInput()) {
						fIsoIn = e;
						break;
					}
				}
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
				usb_video_frame_descriptor* desc = new usb_video_frame_descriptor;
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
				fMJPEGFrames.AddItem(desc);
				syslog(LOG_INFO, "UVCCamDevice: Added MJPEG %ux%u\n", desc->width, desc->height);
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
				usb_video_frame_descriptor* desc = new usb_video_frame_descriptor;
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
				fUncompressedFrames.AddItem(desc);
				syslog(LOG_INFO, "UVCCamDevice: Added YUY2 %ux%u\n", desc->width, desc->height);
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
				usb_video_frame_descriptor* desc = new usb_video_frame_descriptor;
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
				usb_video_frame_descriptor* desc = new usb_video_frame_descriptor;
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
				fUncompressedFrames.AddItem(desc);
				syslog(LOG_INFO, "UVCCamDevice: Added YUY2 %ux%u\n", desc->width, desc->height);
			}

			syslog(LOG_INFO, "UVCCamDevice: Hardcoded %d MJPEG + %d YUY2 frames for Microdia 0x6720\n",
				(int)fMJPEGFrames.CountItems(), (int)fUncompressedFrames.CountItems());
		}
	}

	// Choose a sensible default resolution:
	// MJPEG: 640x480 (compressed, bandwidth is not an issue)
	// YUY2: 320x240 (uncompressed, limited by USB 2.0 bandwidth)
	{
		BList* defaultList = (fMJPEGFrames.CountItems() > 0)
			? &fMJPEGFrames : &fUncompressedFrames;
		uint32 targetPixels = (fMJPEGFrames.CountItems() > 0)
			? (640 * 480) : (320 * 240);
		int32 bestIndex = 0;
		uint32 bestDiff = UINT32_MAX;
		for (int32 i = 0; i < defaultList->CountItems(); i++) {
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)defaultList->ItemAt(i);
			if (desc == NULL) continue;
			uint32 pixels = (uint32)desc->width * desc->height;
			uint32 diff = (pixels > targetPixels)
				? (pixels - targetPixels) : (targetPixels - pixels);
			if (diff < bestDiff) {
				bestDiff = diff;
				bestIndex = i;
			}
		}
		fSelectedResolutionIndex = bestIndex;
		usb_video_frame_descriptor* desc =
			(usb_video_frame_descriptor*)defaultList->ItemAt(bestIndex);
		if (desc) {
			syslog(LOG_INFO, "UVCCamDevice: Default resolution set to %ux%u (index %d)\n",
				desc->width, desc->height, (int)bestIndex);
		}
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

	// FIX BUG 3: Impostare fInitStatus solo dopo parsing completo
	// Requisito minimo: avere almeno un formato video disponibile
	// (interfacce possono avere indice 0, quindi non controlliamo > 0)
	if (fUncompressedFrames.CountItems() > 0 || fMJPEGFrames.CountItems() > 0) {
		fInitStatus = B_OK;

		// FIX: Initialize fIsMJPEG based on available formats
		// Prefer MJPEG for better bandwidth usage (compressed vs raw YUY2)
		if (fMJPEGFrames.CountItems() > 0 && fJpegDecompressor != NULL)
			fIsMJPEG = true;
		else
			fIsMJPEG = false;

		// QUIRK: Microdia 0c45:6409 uses 352-pixel internal buffer width
		// This causes row stride issues for resolutions < 352 pixels wide
		if (fDevice->VendorID() == 0x0c45 && fDevice->ProductID() == 0x6409) {
			fMicrodiaQuirk = true;
			syslog(LOG_INFO, "UVCCamDevice: Microdia 0c45:6409 quirk enabled (352-pixel stride)\n");
		}

		syslog(LOG_INFO, "UVCCamDevice: Init OK - ctrl=%u stream=%u frames=%d+%d format=%s\n",
			fControlIndex, fStreamingIndex,
			(int)fUncompressedFrames.CountItems(), (int)fMJPEGFrames.CountItems(),
			fIsMJPEG ? "MJPEG" : "YUY2");
		syslog(LOG_INFO, "UVCCamDevice: Format indices: MJPEG=%d, Uncompressed=%d\n",
			fMJPEGFormatIndex, fUncompressedFormatIndex);

		// Build sorted resolution list for proper fallback ordering
		// (level 0 = highest, level N = lowest)
		_BuildSortedResolutionList();

		// Log frame indices for current format (raw USB order, for debugging)
		BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
		syslog(LOG_DEBUG, "UVCCamDevice: Raw %s frame list (%d entries):\n",
			fIsMJPEG ? "MJPEG" : "YUY2", (int)frameList->CountItems());
		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc)
				syslog(LOG_DEBUG, "UVCCamDevice:   raw[%d] %ux%u frame_index=%u\n",
					(int)i, desc->width, desc->height, desc->frame_index);
		}
	} else {
		syslog(LOG_ERR, "UVCCamDevice: Init FAILED - no video frames available\n");
	}
}


UVCCamDevice::~UVCCamDevice()
{
	printf("UVCCamDevice::~UVCCamDevice() - Destroying device\n");

	// Stop audio transfer if running
	if (fAudioTransferRunning) {
		StopAudioTransfer();
	}

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
		fprintf(stderr, "UVCCamDevice: TurboJPEG decompressor destroyed\n");
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

	// Cleanup frame validation cache (Feature 1)
	if (fLastValidFrame) {
		delete[] fLastValidFrame;
		fLastValidFrame = NULL;
	}

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
	switch (_descriptor->descriptorSubtype) {
		case USB_VIDEO_VS_INPUT_HEADER:
		{
			const usb_video_class_specific_vs_interface_input_header_descriptor* descriptor
				= (const usb_video_class_specific_vs_interface_input_header_descriptor*)_descriptor;
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
			const struct usb_video_class_specific_vs_interface_input_header_descriptor::ma_controls*
				controls = descriptor->_ma_controls;
			for (uint8 i = 0; i < descriptor->num_formats; i++,
				controls =
				(const struct usb_video_class_specific_vs_interface_input_header_descriptor
					::ma_controls*)((const char*)controls + descriptor->control_size)) {
				printf("\tfmt%d: %s %s %s %s - %s %s\n", i,
					(controls->key_frame_rate) ? "wKeyFrameRate" : "",
					(controls->p_frame_rate) ? "wPFrameRate" : "",
					(controls->comp_quality) ? "wCompQuality" : "",
					(controls->comp_window_size) ? "wCompWindowSize" : "",
					(controls->generate_key_frame) ? "<Generate Key Frame>" : "",
					(controls->update_frame_segment) ? "<Update Frame Segment>" : "");
			}
			break;
		}
		case USB_VIDEO_VS_FORMAT_UNCOMPRESSED:
		{
			const usbvc_format_descriptor* descriptor
				= (const usbvc_format_descriptor*)_descriptor;
			fUncompressedFormatIndex = descriptor->formatIndex;
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

			// Detect NV12 format (YUV 4:2:0 planar)
			if (!memcmp(descriptor->uncompressed.format, kNV12Guid, sizeof(usbvc_guid))) {
				fIsNV12 = true;
				printf("\tDetected NV12 (YUV 4:2:0) format\n");
			}
			break;
		}
		case USB_VIDEO_VS_FRAME_MJPEG:
		case USB_VIDEO_VS_FRAME_UNCOMPRESSED:
		{
			const usb_video_frame_descriptor* descriptor
				= (const usb_video_frame_descriptor*)_descriptor;
			if (_descriptor->descriptorSubtype == USB_VIDEO_VS_FRAME_UNCOMPRESSED) {
				printf("VS_FRAME_UNCOMPRESSED:");
				fUncompressedFrames.AddItem(
					new usb_video_frame_descriptor(*descriptor));
			} else {
				printf("VS_FRAME_MJPEG:");
				fMJPEGFrames.AddItem(new usb_video_frame_descriptor(*descriptor));
			}
			printf("\tbFrameIdx=%d,stillsupported=%s,"
				"fixedframerate=%s\n", descriptor->frame_index,
				(descriptor->capabilities & 1) ? "yes" : "no",
				(descriptor->capabilities & 2) ? "yes" : "no");
			printf("\twidth=%u,height=%u,min/max bitrate=%" B_PRIu32 "/%" B_PRIu32 ", maxbuf=%" B_PRIu32 "\n",
				descriptor->width, descriptor->height,
				descriptor->min_bit_rate, descriptor->max_bit_rate,
				descriptor->max_video_frame_buffer_size);

			// Validate frame buffer size to prevent excessive memory allocation
			const uint32 MAX_REASONABLE_FRAME_SIZE = 50 * 1024 * 1024; // 50MB
			if (descriptor->max_video_frame_buffer_size > MAX_REASONABLE_FRAME_SIZE) {
				fprintf(stderr, "WARNING: Frame buffer size (%" B_PRIu32 " bytes) exceeds reasonable limit (%u bytes)\n",
					descriptor->max_video_frame_buffer_size, MAX_REASONABLE_FRAME_SIZE);
				fprintf(stderr, "         This may indicate corrupted USB descriptors.\n");
			}

			printf("\tdefault frame interval: %" B_PRIu32 ", #intervals(0=cont): %d\n",
				descriptor->default_frame_interval, descriptor->frame_interval_type);
			if (descriptor->frame_interval_type == 0) {
				printf("min/max frame interval=%" B_PRIu32 "/%" B_PRIu32 ", step=%" B_PRIu32 "\n",
					descriptor->continuous.min_frame_interval,
					descriptor->continuous.max_frame_interval,
					descriptor->continuous.frame_interval_step);
			} else for (uint8 i = 0; i < descriptor->frame_interval_type; i++) {
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
			printf("VS_OUTPUT_HEADER:\t#fmts=%d,ept=0x%x (%s)\n",
				descriptor->num_formats, descriptor->_endpoint_address.endpoint_number,
				descriptor->_endpoint_address.direction ? "IN" : "OUT");
			printf("\toutput terminal id=%d\n", descriptor->terminal_link);
			const struct usb_video_class_specific_vs_interface_output_header_descriptor::ma_controls*
				controls = descriptor->_ma_controls;
			for (uint8 i = 0; i < descriptor->num_formats; i++,
				controls
					= (const struct usb_video_class_specific_vs_interface_output_header_descriptor
					::ma_controls*)((const char*)controls + descriptor->control_size)) {
				printf("\tfmt%d: %s %s %s %s\n", i,
					(controls->key_frame_rate) ? "wKeyFrameRate" : "",
					(controls->p_frame_rate) ? "wPFrameRate" : "",
					(controls->comp_quality) ? "wCompQuality" : "",
					(controls->comp_window_size) ? "wCompWindowSize" : "");
			}
			break;
		}
		case USB_VIDEO_VS_STILL_IMAGE_FRAME:
		{
			const usb_video_still_image_frame_descriptor* descriptor
				= (const usb_video_still_image_frame_descriptor*)_descriptor;
			_ParseStillImageFrame(descriptor);
			break;
		}
		case USB_VIDEO_VS_FORMAT_MJPEG:
		{
			const usbvc_format_descriptor* descriptor
				= (const usbvc_format_descriptor*)_descriptor;
			fMJPEGFormatIndex = descriptor->formatIndex;
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
			printf("VS_FORMAT_FRAME_BASED:\t\n");
			break;
		case USB_VIDEO_VS_FRAME_FRAME_BASED:
			printf("VS_FRAME_FRAME_BASED:\t\n");
			break;
		case USB_VIDEO_VS_FORMAT_STREAM_BASED:
			printf("VS_FORMAT_STREAM_BASED:\t\n");
			break;
		default:
			printf("INVALID STREAM UNIT TYPE=%d!\n",
				_descriptor->descriptorSubtype);
	}
}


void
UVCCamDevice::_ParseVideoControl(const usbvc_class_descriptor* _descriptor,
	size_t len)
{
	switch (_descriptor->descriptorSubtype) {
		case USB_VIDEO_VC_HEADER:
		{
			if (fHeaderDescriptor != NULL) {
				printf("ERROR: multiple VC_HEADER! Skipping...\n");
				break;
			}
			fHeaderDescriptor = (usbvc_interface_header_descriptor*)malloc(len);
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
			const usbvc_input_terminal_descriptor* descriptor
				= (const usbvc_input_terminal_descriptor*)_descriptor;
			printf("VC_INPUT_TERMINAL:\tid=%d,type=%04x,associated terminal="
				"%d\n", descriptor->terminalID, descriptor->terminalType,
				descriptor->associatedTerminal);
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->terminal));
			if (descriptor->terminalType == USB_VIDEO_CAMERA_IN) {
				const usb_video_camera_terminal_descriptor* desc
					= (const usb_video_camera_terminal_descriptor*)descriptor;
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
			const usb_video_selector_unit_descriptor* descriptor
				= (const usb_video_selector_unit_descriptor*)_descriptor;
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
			const usb_video_processing_unit_descriptor* descriptor
				= (const usb_video_processing_unit_descriptor*)_descriptor;
			fControlRequestIndex = fControlIndex + (descriptor->unit_id << 8);
			fProcessingUnitID = descriptor->unit_id;
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
	switch (_descriptor->descriptorSubtype) {
		case USB_AUDIO_AC_HEADER:
			break;

		case USB_AUDIO_AC_INPUT_TERMINAL:
		{
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
	switch (_descriptor->descriptorSubtype) {
		case USB_AUDIO_AS_GENERAL:
			break;

		case USB_AUDIO_AS_FORMAT_TYPE:
		{
			const usb_audio_format_type_i_descriptor* descriptor
				= (const usb_audio_format_type_i_descriptor*)_descriptor;
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


status_t
UVCCamDevice::StartTransfer()
{
	status_t err = _ProbeCommitFormat();
	if (err != B_OK)
		return err;

	err = _SelectBestAlternate();
	if (err != B_OK)
		return err;

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
	printf("UVCCamDevice::SuggestVideoFrame(%" B_PRIu32 ", %" B_PRIu32 ")\n", width, height);

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
				uint32 pixels = desc->width * desc->height;
				if (pixels < smallestPixels) {
					smallestPixels = pixels;
					bestIndex = i;
				}
			}
			fSelectedResolutionIndex = bestIndex;
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(bestIndex);
			width = desc->width;
			height = desc->height;
			syslog(LOG_INFO, "UVCCamDevice: SAFE MODE - using lowest resolution %ux%u\n",
				width, height);
			AcceptVideoFrame(width, height);
			return B_OK;
		}
	}

	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

	// First check if fIsMJPEG needs to be initialized
	if (fMJPEGFrames.CountItems() > 0)
		fIsMJPEG = true;
	else if (fUncompressedFrames.CountItems() > 0)
		fIsMJPEG = false;

	// Re-select the frame list after determining format
	frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

	// Suggest a resolution that fits the available bandwidth.
	// For MJPEG: prefer 640x480 (good quality, low bandwidth due to compression).
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
			uint32 diff = abs((int)(desc->width * desc->height)
				- (int)(targetW * targetH));
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
	printf("UVCCamDevice::SuggestVideoFrame: No frames available, using fallback 320x240\n");
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
	// Prefer MJPEG (better bandwidth usage) over uncompressed
	if (mjpegCount > 0)
		fIsMJPEG = true;
	else if (uncompressedCount > 0)
		fIsMJPEG = false;
	else {
		// FALLBACK: If USB descriptor parsing failed (common on Haiku),
		// accept any format with hardcoded 320x240 resolution.
		// This allows video to work even when OtherDescriptorAt() doesn't
		// return UVC class-specific descriptors.
		printf("UVCCamDevice::AcceptVideoFrame: No frames parsed, using fallback 320x240\n");
		if (width == 0 || height == 0) {
			width = 320;
			height = 240;
		}
		// Try MJPEG first (less bandwidth), then fall back to uncompressed
		fIsMJPEG = true;
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
				printf("UVCCamDevice::AcceptVideoFrame: Using selected resolution %ux%u (index %d)\n",
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

			/* FIX BUG 11: Usare descriptor->frame_index invece di i+1.
			 * Il frame_index nel descrittore USB è il valore che va usato
			 * nel Probe/Commit, NON la posizione nella lista.
			 * Se la lista non è ordinata per frame_index, i+1 è sbagliato.
			 */
			if (fIsMJPEG) {
				fMJPEGFrameIndex = descriptor->frame_index;
			} else {
				fUncompressedFrameIndex = descriptor->frame_index;
				// Set expected frame size for YUY2
				if (fDeframer) {
					((UVCDeframer*)fDeframer)->SetExpectedFrameSize(width * height * 2);
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
	request._hint.frame_interval = 1;

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
			frameInterval = frameDesc->default_frame_interval;
			syslog(LOG_INFO, "UVCCamDevice: Using device default frame interval %u (%.1f fps)\n",
				frameInterval, 10000000.0f / frameInterval);
		}
	}

	/* For YUY2 (uncompressed), check bandwidth and adapt if needed */
	if (!fIsMJPEG && frameIndex > 0 && frameIndex <= (uint32)frameList->CountItems()) {
		const usb_video_frame_descriptor* frameDesc =
			(const usb_video_frame_descriptor*)frameList->ItemAt(frameIndex - 1);
		if (frameDesc != NULL) {
			uint32 maxBandwidth = _GetMaxAvailableBandwidth();
			if (maxBandwidth > 0) {
				uint32 frameSize = frameDesc->width * frameDesc->height * 2;
				// USB 2.0 high-speed: 8000 microframes/second
				uint32 bytesPerSecond = maxBandwidth * 8000;
				float maxFps = (float)bytesPerSecond / frameSize;

				syslog(LOG_INFO, "UVCCamDevice: YUY2 bandwidth check: max=%u bytes/uframe (%.1f MB/s), frameSize=%u, maxFps=%.1f\n",
					maxBandwidth, bytesPerSecond / 1048576.0f, frameSize, maxFps);

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
						uint32 requiredBandwidth = (uint32)(frameSize * fps);

						syslog(LOG_INFO, "UVCCamDevice: Checking interval %u (%.1f fps): requires %u bytes/sec, available %u\n",
							interval, fps, requiredBandwidth, bytesPerSecond);

						// Track slowest valid interval for fallback
						if (interval > slowestValidInterval)
							slowestValidInterval = interval;

						// Select this interval if it fits within bandwidth
						// Use 90% safety margin
						if (requiredBandwidth <= (uint32)(bytesPerSecond * 0.9f)) {
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

	// Determine initial probe/commit size based on UVC version
	// UVC 1.0: 26 bytes, UVC 1.1+: 34 bytes, UVC 1.5: 48 bytes
	// Some cameras don't follow the spec, so we try multiple sizes if needed
	size_t length = fHeaderDescriptor->version > 0x100 ? 34 : 26;

	// Log what we're requesting
	syslog(LOG_INFO, "UVC Probe request: format=%d frame=%d interval=%u (fIsMJPEG=%d)\n",
		request.format_index, request.frame_index, request.frame_interval, fIsMJPEG);
	syslog(LOG_INFO, "UVC Format indices: MJPEG=%d, Uncompressed=%d\n",
		fMJPEGFormatIndex, fUncompressedFormatIndex);

	// Try SET_CUR Probe with retry logic and fallback to different sizes
	// Some cameras need multiple attempts before responding to control transfers.
	// EHCI controller errors (0x00080248) often indicate timing issues that
	// can be resolved with retries and increased delays.
	static const size_t kProbeSizes[] = { 34, 26, 48, 22 };
	static const int kNumProbeSizes = sizeof(kProbeSizes) / sizeof(kProbeSizes[0]);
	static const int kMaxRetries = 5;  // Increased retries per size for EHCI stability
	static const bigtime_t kRetryDelays[] = { 100000, 200000, 300000, 400000, 500000 };  // 100-500ms delays

	size_t actualLength = 0;
	bool probeSuccess = false;

	// First try the expected size based on UVC version with retries
	syslog(LOG_INFO, "UVC Probe: trying size %zu (UVC version 0x%04x)\n",
		length, fHeaderDescriptor->version);

	for (int retry = 0; retry < kMaxRetries && !probeSuccess; retry++) {
		if (retry > 0) {
			syslog(LOG_INFO, "UVC Probe: retry %d with delay %lldms\n",
				retry, kRetryDelays[retry - 1] / 1000);
			snooze(kRetryDelays[retry - 1]);
		}

		actualLength = fDevice->ControlTransfer(
			USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
			USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &request);

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

			syslog(LOG_INFO, "UVC Probe: trying alternative size %zu\n", trySize);

			for (int retry = 0; retry < kMaxRetries && !probeSuccess; retry++) {
				// Delay before each attempt (including first)
				snooze(kRetryDelays[retry < kMaxRetries - 1 ? retry : kMaxRetries - 2]);

				actualLength = fDevice->ControlTransfer(
					USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
					USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, trySize, &request);

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

	if (!probeSuccess) {
		syslog(LOG_ERR, "UVC Probe SET_CUR failed with all sizes (26, 34, 48, 22)\n");
		syslog(LOG_ERR, "  Last attempt returned: %zd (expected: %zu)\n",
			(ssize_t)actualLength, length);
		return B_ERROR;
	}

	// Store the working probe size for future use
	fProbeCommitSize = length;

	// GET_CUR Probe (get negotiated values)
	usb_video_probe_and_commit_controls response;
	memset(&response, 0, sizeof(response));
	actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN, USB_VIDEO_RC_GET_CUR,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &response);

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
		USB_VIDEO_VS_COMMIT_CONTROL << 8, fStreamingIndex, length, &response);
	if (actualLength != length) {
		syslog(LOG_ERR, "UVC Commit failed: expected %zu, got %zu\n", length, actualLength);
		return B_ERROR;
	}

	fMaxVideoFrameSize = response.max_video_frame_size;
	fMaxPayloadTransferSize = response.max_payload_transfer_size;

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

	uint32 bytesPerSecond = bandwidth * 8000;

	if (isMJPEG) {
		// MJPEG is compressed, typically 1/10 to 1/20 of raw YUY2 size
		// Use conservative estimate of 1/8 compression ratio
		uint32 estimatedFrameSize = (width * height * 2) / 8;
		return (float)bytesPerSecond / estimatedFrameSize;
	} else {
		// YUY2 uncompressed: 2 bytes per pixel
		uint32 frameSize = width * height * 2;
		return (float)bytesPerSecond / frameSize;
	}
}


bool
UVCCamDevice::_IsResolutionSupportable(uint32 width, uint32 height, bool isMJPEG)
{
	// Check if a resolution can achieve minimum acceptable FPS
	// We require at least 5 FPS for reasonable video playback

	const float kMinAcceptableFps = 5.0f;
	float maxFps = _EstimateMaxFps(width, height, isMJPEG);

	if (maxFps < kMinAcceptableFps) {
		syslog(LOG_WARNING, "UVCCamDevice: Resolution %ux%u (%s) limited to %.1f fps "
			"(requires high-bandwidth endpoints)\n",
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
	syslog(LOG_INFO, "UVCCamDevice: Pass 1 - scanning for single-transaction endpoints (mult=1)\n");

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

			syslog(LOG_INFO, "UVCCamDevice: Alt %u EP %u: raw=0x%04x base=%u trans=%u total=%u bytes\n",
				i, j, rawMaxPacketSize, basePacketSize, transactions, totalBandwidth);

			// Pass 1: Skip high-bandwidth endpoints (mult > 1)
			if (transactions > 1) {
				syslog(LOG_INFO, "UVCCamDevice: Pass 1: Skipping high-bandwidth endpoint (mult=%u)\n",
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

	// PASS 2: If no single-transaction endpoint found OR if user forces high-bandwidth
	// and we found a higher bandwidth option, use high-bandwidth endpoint
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
		float maxFps = (float)bytesPerSecond / fMaxVideoFrameSize;
		syslog(LOG_INFO, "UVCCamDevice: Selected bandwidth %u bytes (~%.1f MB/s, max %.1f fps for frame size %u)\n",
			bestBandwidth, bytesPerSecond / 1048576.0f, maxFps, fMaxVideoFrameSize);

		/* Warn if bandwidth is likely insufficient */
		if (maxFps < 5.0f) {
			syslog(LOG_WARNING, "UVCCamDevice: Bandwidth may be insufficient for this resolution (max %.1f fps)\n",
				maxFps);
			syslog(LOG_WARNING, "UVCCamDevice: Consider using a lower resolution or USB 3.0 port if available\n");
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

		// Log endpoint counts to diagnose potential crashes
		const BUSBInterface* oldAlt = streaming->AlternateAt(fCurrentVideoAlternate);
		const BUSBInterface* newAlt = streaming->AlternateAt(alternateIndex);
		uint32 oldEndpoints = oldAlt ? oldAlt->CountEndpoints() : 0;
		uint32 newEndpoints = newAlt ? newAlt->CountEndpoints() : 0;
		syslog(LOG_INFO, "UVCCamDevice: Endpoint count: old=%u new=%u\n",
			oldEndpoints, newEndpoints);

		// CRITICAL: If switching from 0 endpoints to N endpoints, the Haiku bug
		// will trigger. Log a warning so user knows to apply kernel patch.
		if (oldEndpoints == 0 && newEndpoints > 0) {
			syslog(LOG_WARNING, "UVCCamDevice: Risky transition (0->%u endpoints) - "
				"may crash on unpatched Haiku. Apply kernel patch if crash occurs.\n",
				newEndpoints);
		}

		// Call SetAlternate - may crash on unpatched Haiku due to double-free bug
		status_t setAltResult = ((BUSBInterface*)streaming)->SetAlternate(alternateIndex);
		if (setAltResult != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: SetAlternate(%u) failed: %s\n",
				alternateIndex, strerror(setAltResult));
			return setAltResult;
		}

		syslog(LOG_INFO, "UVCCamDevice: SetAlternate(%u) successful\n", alternateIndex);
		fCurrentVideoAlternate = alternateIndex;

		// CRITICAL: Re-fetch ALL references immediately to avoid using corrupted pointers
		// The SetAlternate call may have corrupted the old streaming pointer
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

	// Buffer size must be exactly packetSize * numPackets for EHCI alignment
	// OPTIMIZATION: Use 32 packets (max) to reduce transfer overhead and improve
	// timing - fewer transfers = less chance of missing USB bus frames
	const uint32 kInitialPackets = 32;
	uint32 requiredBufferSize = fIsoMaxPacketSize * kInitialPackets;

	if (requiredBufferSize != fBufferLen || fBuffer == NULL) {
		free(fBuffer);
		fBuffer = (uint8*)malloc(requiredBufferSize);
		if (fBuffer == NULL)
			return B_NO_MEMORY;
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


/* Audio Transfer Methods */

status_t
UVCCamDevice::StartAudioTransfer()
{
	if (!fHasAudio) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: No audio interface\n");
		return B_ERROR;
	}

	if (fAudioTransferRunning)
		return B_OK;

	// Apply fallback values for missing audio parameters
	// These defaults match common USB webcam microphone configurations
	if (fAudioSampleRate == 0) {
		fAudioSampleRate = 48000;  // Most common USB audio sample rate
		syslog(LOG_WARNING, "UVCCamDevice: Using fallback sample rate: %u Hz\n",
			(unsigned)fAudioSampleRate);
	}
	if (fAudioChannels == 0) {
		fAudioChannels = 2;  // Stereo is common for webcam mics
		syslog(LOG_WARNING, "UVCCamDevice: Using fallback channel count: %u\n",
			(unsigned)fAudioChannels);
	}
	if (fAudioBitResolution == 0) {
		fAudioBitResolution = 16;  // 16-bit PCM is standard
		syslog(LOG_WARNING, "UVCCamDevice: Using fallback bit resolution: %u\n",
			(unsigned)fAudioBitResolution);
	}
	if (fAudioSubFrameSize == 0) {
		fAudioSubFrameSize = fAudioBitResolution / 8;
	}

	// Select audio alternate with proper bandwidth
	status_t err = _SelectAudioAlternate();
	if (err != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to select "
			"alternate: %s\n", strerror(err));
		return err;
	}

	// Set sample rate on the endpoint (required for USB Audio Class 1.0)
	if (fAudioIsoIn != NULL) {
		uint32 sampleRate = fAudioSampleRate;
		uint8 rateData[3];
		rateData[0] = sampleRate & 0xFF;
		rateData[1] = (sampleRate >> 8) & 0xFF;
		rateData[2] = (sampleRate >> 16) & 0xFF;

		uint8 endpointAddr = fAudioIsoIn->Descriptor()->endpoint_address;

		// SET_CUR request to set sampling frequency on endpoint
		// bmRequestType: 0x22 = Host-to-device, Class, Endpoint
		// bRequest: 0x01 = SET_CUR
		// wValue: 0x0100 = SAMPLING_FREQ_CONTROL << 8
		// wIndex: endpoint address
		ssize_t transferred = fDevice->ControlTransfer(
			USB_REQTYPE_CLASS | USB_REQTYPE_ENDPOINT_OUT,  // 0x22
			0x01,  // SET_CUR
			0x0100,  // SAMPLING_FREQ_CONTROL << 8
			endpointAddr,
			3,
			rateData);

		syslog(LOG_INFO, "UVCCamDevice: Set USB sample rate to %u Hz (result=%zd)\n",
			(unsigned)sampleRate, transferred);

		// Verify with GET_CUR that the device accepted the sample rate
		if (transferred == 3) {
			uint8 verifyData[3] = {0};
			ssize_t got = fDevice->ControlTransfer(
				USB_REQTYPE_CLASS | USB_REQTYPE_ENDPOINT_IN,  // 0xA2
				0x81,  // GET_CUR
				0x0100,  // SAMPLING_FREQ_CONTROL << 8
				endpointAddr,
				3,
				verifyData);

			if (got == 3) {
				uint32 readBack = verifyData[0]
					| ((uint32)verifyData[1] << 8)
					| ((uint32)verifyData[2] << 16);
				if (readBack != sampleRate) {
					syslog(LOG_WARNING,
						"UVCCamDevice: Device reports sample rate %u Hz "
						"(requested %u Hz)\n",
						(unsigned)readBack, (unsigned)sampleRate);
					fAudioSampleRate = readBack;
				}
			}
		}
	}

	// Allocate ring buffer sized for ~2 seconds of audio.
	// Scale with actual sample rate and channel count to avoid
	// underruns at high rates or wasted memory at low rates.
	fAudioRingSize = fAudioSampleRate * fAudioChannels * 2 * 2;
	if (fAudioRingSize < 16384)
		fAudioRingSize = 16384;
	if (fAudioRingSize > 262144)
		fAudioRingSize = 262144;
	fAudioRingBuffer = (uint8*)malloc(fAudioRingSize);
	if (!fAudioRingBuffer) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to allocate ring buffer\n");
		_SelectAudioIdleAlternate();
		return B_NO_MEMORY;
	}
	fAudioRingHead = 0;
	fAudioRingTail = 0;

	// Create semaphore for ring buffer synchronization
	fAudioRingSem = create_sem(0, "audio ring buffer");
	if (fAudioRingSem < 0) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to create semaphore\n");
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
		_SelectAudioIdleAlternate();
		return B_ERROR;
	}

	// Mark as running before starting thread
	fAudioTransferRunning = true;

	// Start audio pump thread
	fAudioPumpThread = spawn_thread(_audio_pump_thread_, "audio pump",
		B_REAL_TIME_PRIORITY, this);
	if (fAudioPumpThread < 0) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to spawn thread\n");
		fAudioTransferRunning = false;
		delete_sem(fAudioRingSem);
		fAudioRingSem = -1;
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
		_SelectAudioIdleAlternate();
		return B_ERROR;
	}

	if (resume_thread(fAudioPumpThread) != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to resume thread\n");
		fAudioTransferRunning = false;
		kill_thread(fAudioPumpThread);
		delete_sem(fAudioRingSem);
		fAudioRingSem = -1;
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
		_SelectAudioIdleAlternate();
		return B_ERROR;
	}

	return B_OK;
}


status_t
UVCCamDevice::StopAudioTransfer()
{
	if (!fAudioTransferRunning)
		return B_OK;

	// Signal thread to stop
	fAudioTransferRunning = false;

	// Wake up thread if waiting on semaphore
	if (fAudioRingSem >= 0) {
		delete_sem(fAudioRingSem);
		fAudioRingSem = -1;
	}

	// Wait for thread to exit
	if (fAudioPumpThread >= 0) {
		status_t threadStatus;
		wait_for_thread_etc(fAudioPumpThread, B_RELATIVE_TIMEOUT, 5000000, &threadStatus);
		fAudioPumpThread = -1;
	}

	// Free ring buffer
	if (fAudioRingBuffer) {
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
	}

	// Set audio interface to idle
	_SelectAudioIdleAlternate();

	return B_OK;
}


size_t
UVCCamDevice::ReadAudioData(void* buffer, size_t size)
{
	if (fAudioRingBuffer == NULL || buffer == NULL || size == 0)
		return 0;

	// Wait for enough data with timeout.
	// Use 200ms to accommodate USB controllers with variable isochronous
	// latency (XHCI controllers can batch multiple microframes).
	int retries = 200;
	size_t available = 0;

	while (retries-- > 0) {
		int32 head = atomic_get(&fAudioRingHead);
		int32 tail = atomic_get(&fAudioRingTail);

		if (head >= tail)
			available = head - tail;
		else
			available = fAudioRingSize - tail + head;

		if (available >= size)
			break;

		snooze(1000);  // Wait 1ms for more data
	}

	if (available == 0)
		return 0;

	// Re-read both pointers atomically for the actual copy.
	// The pump thread may have advanced head since the wait loop.
	// Tail is only modified by us, so it's stable.
	int32 tail = atomic_get(&fAudioRingTail);
	int32 head = atomic_get(&fAudioRingHead);

	// Validate pointers
	if (tail < 0 || (size_t)tail >= fAudioRingSize) {
		atomic_set(&fAudioRingTail, 0);
		return 0;
	}

	// Recalculate available with fresh pointer values
	if (head >= tail)
		available = head - tail;
	else
		available = fAudioRingSize - tail + head;

	if (available == 0)
		return 0;

	size_t toRead = (available < size) ? available : size;
	size_t firstChunk = fAudioRingSize - tail;

	if (firstChunk >= toRead) {
		memcpy(buffer, fAudioRingBuffer + tail, toRead);
	} else {
		memcpy(buffer, fAudioRingBuffer + tail, firstChunk);
		memcpy((uint8*)buffer + firstChunk, fAudioRingBuffer, toRead - firstChunk);
	}

	atomic_set(&fAudioRingTail, (tail + toRead) % fAudioRingSize);

	return toRead;
}


status_t
UVCCamDevice::_SelectAudioAlternate()
{
	if (fDevice == NULL || fAudioStreamingIndex == 0) {
		syslog(LOG_ERR, "UVCCamDevice: _SelectAudioAlternate: No device or "
			"streaming index\n");
		return B_ERROR;
	}

	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	if (config == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: _SelectAudioAlternate: No active config\n");
		return B_ERROR;
	}

	const BUSBInterface* streaming = config->InterfaceAt(fAudioStreamingIndex);
	if (streaming == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: _SelectAudioAlternate: Interface %u "
			"not found\n", (unsigned)fAudioStreamingIndex);
		return B_BAD_INDEX;
	}

	// Find best alternate (highest bandwidth with valid isochronous endpoint)
	uint32 bestBandwidth = 0;
	uint32 alternateIndex = 0;
	uint32 endpointIndex = 0;
	uint32 alternatesChecked = 0;
	uint32 endpointsChecked = 0;

	syslog(LOG_INFO, "UVCCamDevice: Scanning %u audio alternates\n",
		(unsigned)streaming->CountAlternates());

	for (uint32 i = 1; i < streaming->CountAlternates(); i++) {
		const BUSBInterface* alternate = streaming->AlternateAt(i);
		if (alternate == NULL)
			continue;

		alternatesChecked++;

		for (uint32 j = 0; j < alternate->CountEndpoints(); j++) {
			const BUSBEndpoint* endpoint = alternate->EndpointAt(j);
			if (endpoint == NULL)
				continue;

			endpointsChecked++;

			// Must be isochronous input endpoint
			if (!endpoint->IsIsochronous() || !endpoint->IsInput())
				continue;

			// Validate endpoint descriptor
			const usb_endpoint_descriptor* desc = endpoint->Descriptor();
			if (desc == NULL) {
				syslog(LOG_WARNING, "UVCCamDevice: Audio endpoint %u.%u has "
					"no descriptor\n", (unsigned)i, (unsigned)j);
				continue;
			}

			uint32 maxPacketSize = desc->max_packet_size & 0x7FF;

			// Sanity check: packet size should be reasonable for audio
			// Minimum: 1 sample * 2 bytes * 1 channel = 2 bytes
			// Maximum: 48kHz * 2ch * 2bytes / 1000 * 2 = 384 bytes (with margin)
			if (maxPacketSize < 2 || maxPacketSize > 1024) {
				syslog(LOG_WARNING, "UVCCamDevice: Audio endpoint %u.%u has "
					"unusual packet size: %u\n",
					(unsigned)i, (unsigned)j, (unsigned)maxPacketSize);
			}

			if (maxPacketSize > bestBandwidth) {
				bestBandwidth = maxPacketSize;
				endpointIndex = j;
				alternateIndex = i;
			}
		}
	}

	syslog(LOG_INFO, "UVCCamDevice: Checked %u alternates, %u endpoints\n",
		(unsigned)alternatesChecked, (unsigned)endpointsChecked);

	if (bestBandwidth == 0 || alternateIndex == 0) {
		syslog(LOG_ERR, "UVCCamDevice: No suitable audio alternate found\n");
		return B_ERROR;
	}

	syslog(LOG_INFO, "UVCCamDevice: Selected audio alternate %u, endpoint %u, "
		"maxPacket %u\n",
		(unsigned)alternateIndex, (unsigned)endpointIndex,
		(unsigned)bestBandwidth);

	// Same Haiku bug workaround as video - must use SetAlternate() despite the bug
	// because ControlTransfer doesn't work for SET_INTERFACE in Haiku.
	if (fCurrentAudioAlternate != alternateIndex) {
		syslog(LOG_INFO, "UVCCamDevice: Audio changing alternate from %u to %u\n",
			(unsigned)fCurrentAudioAlternate, (unsigned)alternateIndex);

		// Call SetAlternate - may crash on unpatched Haiku
		status_t setAltResult = ((BUSBInterface*)streaming)->SetAlternate(alternateIndex);
		if (setAltResult != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Audio SetAlternate(%u) failed: %s\n",
				(unsigned)alternateIndex, strerror(setAltResult));
			return setAltResult;
		}

		syslog(LOG_INFO, "UVCCamDevice: Audio SetAlternate(%u) successful\n",
			(unsigned)alternateIndex);
		fCurrentAudioAlternate = alternateIndex;

		// Re-fetch interface reference to avoid corrupted pointers
		streaming = config->InterfaceAt(fAudioStreamingIndex);
		if (streaming == NULL) {
			syslog(LOG_ERR, "UVCCamDevice: Audio interface lost after SetAlternate\n");
			return B_BAD_INDEX;
		}
	}

	// Get endpoint from selected alternate
	const BUSBInterface* selectedAlt = streaming->AlternateAt(alternateIndex);
	if (selectedAlt == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: Selected alternate %u not found\n",
			(unsigned)alternateIndex);
		return B_ERROR;
	}

	fAudioIsoIn = selectedAlt->EndpointAt(endpointIndex);
	if (fAudioIsoIn == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: Audio endpoint %u not found in "
			"alternate %u\n", (unsigned)endpointIndex, (unsigned)alternateIndex);
		return B_ERROR;
	}

	fAudioMaxPacketSize = bestBandwidth;

	// Allocate audio buffer for isochronous transfers
	const uint32 kAudioPackets = 16;
	fAudioBufferLen = fAudioMaxPacketSize * kAudioPackets;
	fAudioBuffer = (uint8*)malloc(fAudioBufferLen);
	if (fAudioBuffer == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: Failed to allocate %u bytes for "
			"audio buffer\n", (unsigned)fAudioBufferLen);
		fAudioIsoIn = NULL;
		return B_NO_MEMORY;
	}

	syslog(LOG_INFO, "UVCCamDevice: Audio ready: %u Hz, %u ch, buffer %u bytes\n",
		(unsigned)fAudioSampleRate, (unsigned)fAudioChannels,
		(unsigned)fAudioBufferLen);

	return B_OK;
}


status_t
UVCCamDevice::_SelectAudioIdleAlternate()
{
	if (fDevice == NULL || fAudioStreamingIndex == 0)
		return B_ERROR;

	syslog(LOG_INFO, "UVCCamDevice: Audio switching to idle (alternate 0)\n");

	// Switch to alternate 0 for consistency with _SelectAudioAlternate()
	if (fCurrentAudioAlternate != 0) {
		const BUSBConfiguration* config = fDevice->ActiveConfiguration();
		if (config != NULL) {
			const BUSBInterface* streaming = config->InterfaceAt(fAudioStreamingIndex);
			if (streaming != NULL) {
				status_t result = ((BUSBInterface*)streaming)->SetAlternate(0);
				if (result == B_OK) {
					syslog(LOG_INFO, "UVCCamDevice: Audio SetAlternate(0) successful\n");
					fCurrentAudioAlternate = 0;
				} else {
					syslog(LOG_WARNING, "UVCCamDevice: Audio SetAlternate(0) failed: %s\n",
						strerror(result));
				}
			}
		}
	}

	// Invalidate endpoint references
	fAudioIsoIn = NULL;
	fAudioMaxPacketSize = 0;

	if (fAudioBuffer != NULL) {
		free(fAudioBuffer);
		fAudioBuffer = NULL;
		fAudioBufferLen = 0;
	}

	return B_OK;
}


int32
UVCCamDevice::_audio_pump_thread_(void* data)
{
	return ((UVCCamDevice*)data)->AudioPumpThread();
}


int32
UVCCamDevice::AudioPumpThread()
{
	if (fAudioIsoIn == NULL || fAudioBuffer == NULL || fAudioRingBuffer == NULL)
		return B_ERROR;

	// Use multiple packets like FreeBSD (but fewer due to EHCI issues)
	const uint32 kPacketsPerTransfer = 16;
	usb_iso_packet_descriptor packetDescs[kPacketsPerTransfer];

	// Use the endpoint's maxPacketSize for USB slot allocation.
	// The actual payload per packet varies (e.g., 128 bytes for 32kHz stereo)
	// but the kernel allocates fixed-size slots based on the transfer buffer.
	// Using maxPacketSize ensures no data gets truncated.
	uint32 bytesPerPacket = fAudioMaxPacketSize;
	if (bytesPerPacket == 0) {
		bytesPerPacket = (fAudioSampleRate * fAudioChannels * 2) / 1000;
		if (bytesPerPacket == 0)
			bytesPerPacket = 192;
	}
	syslog(LOG_INFO, "UVCCamDevice: Audio pump: maxPkt=%u, slotSize=%u, "
		"rate=%u, ch=%u\n",
		(unsigned)fAudioMaxPacketSize, (unsigned)bytesPerPacket,
		(unsigned)fAudioSampleRate, (unsigned)fAudioChannels);

	// Retry configuration (similar to video transfer retry logic)
	const uint32 kMaxRetries = 3;
	const bigtime_t kInitialBackoff = 1000;		// 1ms
	const bigtime_t kMaxBackoff = 10000;		// 10ms

	uint32 consecutiveErrors = 0;
	bigtime_t currentBackoff = kInitialBackoff;

	// Statistics for logging
	uint32 transferCount = 0;
	uint32 errorCount = 0;
	bigtime_t lastLogTime = system_time();

	while (fAudioTransferRunning) {
		// Verify endpoint is still valid (device may have been unplugged)
		if (fAudioIsoIn == NULL)
			break;

		// Initialize packet descriptors
		for (uint32 i = 0; i < kPacketsPerTransfer; i++) {
			packetDescs[i].request_length = bytesPerPacket;
			packetDescs[i].actual_length = 0;
			packetDescs[i].status = B_OK;
		}

		// Perform isochronous transfer with retry logic
		ssize_t transferred = -1;
		uint32 retryCount = 0;

		while (retryCount < kMaxRetries && fAudioTransferRunning) {
			if (fAudioIsoIn == NULL) {
				syslog(LOG_WARNING,
					"UVCCamDevice: Audio endpoint lost during transfer\n");
				fAudioTransferRunning = false;
				break;
			}

			transferred = fAudioIsoIn->IsochronousTransfer(fAudioBuffer,
				bytesPerPacket * kPacketsPerTransfer, packetDescs,
				kPacketsPerTransfer);

			if (transferred >= 0)
				break;

			// Transient error - retry with backoff
			retryCount++;
			if (retryCount < kMaxRetries) {
				snooze(currentBackoff);
				currentBackoff = min_c(currentBackoff * 2, kMaxBackoff);
			}
		}

		transferCount++;

		if (transferred < 0) {
			errorCount++;
			consecutiveErrors++;

			if (consecutiveErrors == 10) {
				syslog(LOG_WARNING,
					"UVCCamDevice: Audio transfer errors: %u consecutive\n",
					(unsigned)consecutiveErrors);
			}

			// After 50 consecutive failures, attempt endpoint recovery
			// by stopping and restarting the alternate setting
			if (consecutiveErrors == 50) {
				syslog(LOG_WARNING,
					"UVCCamDevice: Audio: 50 failures, attempting recovery\n");
				snooze(50000);
				consecutiveErrors = 0;
			}

			snooze(currentBackoff);
			if (currentBackoff < 10000)
				currentBackoff *= 2;
			continue;
		}

		// Success - reset error tracking
		if (consecutiveErrors > 0) {
			consecutiveErrors = 0;
			currentBackoff = kInitialBackoff;
		}

		// Periodic statistics logging (every 30 seconds)
		bigtime_t now = system_time();
		if (now - lastLogTime > 30000000) {
			if (errorCount > 0) {
				syslog(LOG_INFO,
					"UVCCamDevice: Audio stats: %u transfers, %u errors (%.1f%%)\n",
					(unsigned)transferCount, (unsigned)errorCount,
					100.0f * errorCount / transferCount);
			}
			lastLogTime = now;
			transferCount = 0;
			errorCount = 0;
		}

		// Copy received audio data to ring buffer using atomic operations
		for (uint32 i = 0; i < kPacketsPerTransfer; i++) {
			if (packetDescs[i].status != B_OK || packetDescs[i].actual_length == 0)
				continue;

			uint8* packetData = fAudioBuffer + (i * bytesPerPacket);
			size_t packetLen = packetDescs[i].actual_length;

			// Validate packet length against buffer size
			if (packetLen > bytesPerPacket)
				packetLen = bytesPerPacket;

			// Calculate space in ring buffer
			int32 head = atomic_get(&fAudioRingHead);
			int32 tail = atomic_get(&fAudioRingTail);

			// Validate pointers are within bounds
			if (head < 0 || (size_t)head >= fAudioRingSize
				|| tail < 0 || (size_t)tail >= fAudioRingSize) {
				atomic_set(&fAudioRingHead, 0);
				atomic_set(&fAudioRingTail, 0);
				continue;
			}

			ssize_t space;
			if (head >= tail)
				space = (ssize_t)fAudioRingSize - head + tail - 1;
			else
				space = tail - head - 1;

			if (space < 0)
				space = 0;

			if ((size_t)space < packetLen) {
				static int32 sOverflowLog = 0;
				if (++sOverflowLog <= 5 || (sOverflowLog % 100) == 0)
					syslog(LOG_WARNING,
						"UVCCamDevice: Audio ring buffer overflow #%d "
						"(need %zu, have %zu)\n",
						(int)sOverflowLog, packetLen, space);
				continue;
			}

			// Copy to ring buffer (handle wraparound)
			size_t firstChunk = fAudioRingSize - head;
			if (firstChunk >= packetLen) {
				memcpy(fAudioRingBuffer + head, packetData, packetLen);
			} else {
				memcpy(fAudioRingBuffer + head, packetData, firstChunk);
				memcpy(fAudioRingBuffer, packetData + firstChunk,
					packetLen - firstChunk);
			}

			atomic_set(&fAudioRingHead, (head + packetLen) % fAudioRingSize);
		}
	}

	return B_OK;
}


void
UVCCamDevice::_AddProcessingParameter(BParameterGroup* group,
	int32 index, const usb_video_processing_unit_descriptor* descriptor)
{
	BParameterGroup* subgroup;
	uint16 wValue = 0; // Control Selector
	float minValue = 0.0;
	float maxValue = 100.0;
	if (descriptor->control_size >= 1) {
		if (descriptor->controls[0] & 1) {
			// debug_printf("\tBRIGHTNESS\n");
			fBrightness = _AddParameter(group, &subgroup, index,
				USB_VIDEO_PU_BRIGHTNESS_CONTROL, "Brightness");
		}
		if (descriptor->controls[0] & 2) {
			// debug_printf("\tCONSTRAST\n");
			fContrast = _AddParameter(group, &subgroup, index + 1,
				USB_VIDEO_PU_CONTRAST_CONTROL, "Contrast");
		}
		if (descriptor->controls[0] & 4) {
			// debug_printf("\tHUE\n");
			fHue = _AddParameter(group, &subgroup, index + 2,
				USB_VIDEO_PU_HUE_CONTROL, "Hue");
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 8) {
					fHueAuto = _AddAutoParameter(subgroup, index + 3,
						USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL);
				}
			}
		}
		if (descriptor->controls[0] & 8) {
			// debug_printf("\tSATURATION\n");
			fSaturation = _AddParameter(group, &subgroup, index + 4,
				USB_VIDEO_PU_SATURATION_CONTROL, "Saturation");
		}
		if (descriptor->controls[0] & 16) {
			// debug_printf("\tSHARPNESS\n");
			fSharpness = _AddParameter(group, &subgroup, index + 5,
				USB_VIDEO_PU_SHARPNESS_CONTROL, "Sharpness");
		}
		if (descriptor->controls[0] & 32) {
			// debug_printf("\tGamma\n");
			fGamma = _AddParameter(group, &subgroup, index + 6,
				USB_VIDEO_PU_GAMMA_CONTROL, "Gamma");
		}
		if (descriptor->controls[0] & 64) {
			// debug_printf("\tWHITE BALANCE TEMPERATURE\n");
			fWBTemp = _AddParameter(group, &subgroup, index + 7,
				USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL, "WB Temperature");
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 16) {
					fWBTempAuto = _AddAutoParameter(subgroup, index + 8,
						USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL);
				}
			}
		}
		if (descriptor->controls[0] & 128) {
			// debug_printf("\tWhite Balance Component\n");
			fWBComponent = _AddParameter(group, &subgroup, index + 9,
				USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_CONTROL, "WB Component");
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 32) {
					fWBTempAuto = _AddAutoParameter(subgroup, index + 10,
						USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL);
				}
			}
		}
	}
	if (descriptor->control_size >= 2) {
		if (descriptor->controls[1] & 1) {
			// debug_printf("\tBACKLIGHT COMPENSATION\n");
			int16 data;
			wValue = USB_VIDEO_PU_BACKLIGHT_COMPENSATION_CONTROL << 8;
			fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_MAX, wValue, fControlRequestIndex, sizeof(data), &data);
			maxValue = (float)data;
			fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_MIN, wValue, fControlRequestIndex, sizeof(data), &data);
			minValue = (float)data;
			fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data);
			fBacklightCompensation = (float)data;
			subgroup = group->MakeGroup("Backlight Compensation");
			if (maxValue - minValue == 1) { // Binary Switch
				fBinaryBacklightCompensation = true;
				subgroup->MakeDiscreteParameter(index + 11,
					B_MEDIA_RAW_VIDEO, "Backlight Compensation",
					B_ENABLE);
			} else { // Range of values
				fBinaryBacklightCompensation = false;
				subgroup->MakeContinuousParameter(index + 11,
				B_MEDIA_RAW_VIDEO, "Backlight Compensation",
				B_GAIN, "", minValue, maxValue, 1.0 / (maxValue - minValue));
			}
		}
		if (descriptor->controls[1] & 2) {
			// debug_printf("\tGAIN\n");
			fGain = _AddParameter(group, &subgroup, index + 12, USB_VIDEO_PU_GAIN_CONTROL,
				"Gain");
		}
		if (descriptor->controls[1] & 4) {
			// debug_printf("\tPOWER LINE FREQUENCY\n");
			wValue = USB_VIDEO_PU_POWER_LINE_FREQUENCY_CONTROL << 8;
			int8 data;
			if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
					USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data)
				== sizeof(data)) {
				fPowerlineFrequency = data;
			}
			subgroup = group->MakeGroup("Power Line Frequency");
			/* FIX: Use discrete parameter instead of continuous slider */
			BDiscreteParameter* plf = subgroup->MakeDiscreteParameter(index + 13,
				B_MEDIA_RAW_VIDEO, "Anti-Flicker", B_GENERIC);
			plf->AddItem(0, "Disabled");
			plf->AddItem(1, "50 Hz");
			plf->AddItem(2, "60 Hz");
		}
		// TODO Determine whether controls apply to these
		/*
		if (descriptor->controls[1] & 64)
			debug_printf("\tDigital Multiplier\n");
		if (descriptor->controls[1] & 128)
			debug_printf("\tDigital Multiplier Limit\n");
		*/
	}
	// TODO Determine whether controls apply to these
	/*
	if (descriptor->controlSize >= 3) {
		if (descriptor->controls[2] & 1)
			debug_printf("\tAnalog Video Standard\n");
		if (descriptor->controls[2] & 2)
			debug_printf("\tAnalog Video Lock Status\n");
	}
	*/

}



float
UVCCamDevice::_AddParameter(BParameterGroup* group,
	BParameterGroup** subgroup, int32 index, uint16 wValue, const char* name)
{
	float minValue = 0.0;
	float maxValue = 100.0;
	float currValue = 0.0;
	int16 data;

	wValue <<= 8;

	if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_MAX, wValue, fControlRequestIndex, sizeof(data), &data)
		== sizeof(data)) {
		maxValue = (float)data;
	}
	if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_MIN, wValue, fControlRequestIndex, sizeof(data), &data)
		== sizeof(data)) {
		minValue = (float)data;
	}
	if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data)
		== sizeof(data)) {
		currValue = (float)data;
	}

	*subgroup = group->MakeGroup(name);
	(*subgroup)->MakeContinuousParameter(index,
		B_MEDIA_RAW_VIDEO, name, B_GAIN, "", minValue, maxValue,
		1.0 / (maxValue - minValue));
	return currValue;
}


uint8
UVCCamDevice::_AddAutoParameter(BParameterGroup* subgroup, int32 index,
	uint16 wValue)
{
	uint8 data;
	wValue <<= 8;

	fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, 1, &data);
	subgroup->MakeDiscreteParameter(index, B_MEDIA_RAW_VIDEO, "Auto",
		B_ENABLE);

	return data;
}


void
UVCCamDevice::AddParameters(BParameterGroup* group, int32& index)
{
	fFirstParameterID = index;
	CamDevice::AddParameters(group, index);

	// ── Stream Configuration ─────────────────────────────────
	BString streamLabel;
	streamLabel.SetToFormat("Stream (%s)", fIsMJPEG ? "MJPEG" : "YUY2");
	BParameterGroup* streamGroup = group->MakeGroup(streamLabel.String());

	// Resolution selector
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	if (frameList->CountItems() > 0) {
		fResolutionParameterID = index + 14;

		BDiscreteParameter* resParam = streamGroup->MakeDiscreteParameter(
			fResolutionParameterID, B_MEDIA_RAW_VIDEO, "Resolution",
			B_RESOLUTION);

		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc == NULL)
				continue;
			BString label;
			label.SetToFormat("%ux%u", desc->width, desc->height);
			resParam->AddItem(i, label.String());
		}

		if (fSelectedResolutionIndex >= frameList->CountItems())
			fSelectedResolutionIndex = 0;

		// Frame rate selector
		const usb_video_frame_descriptor* curFrame =
			(const usb_video_frame_descriptor*)frameList->ItemAt(
				fSelectedResolutionIndex);
		if (curFrame != NULL && curFrame->frame_interval_type > 0) {
			fFrameRateParameterID = index + 16;
			fNumFrameIntervals = curFrame->frame_interval_type;
			if (fNumFrameIntervals > 8)
				fNumFrameIntervals = 8;

			for (uint8 k = 0; k < fNumFrameIntervals; k++)
				fCurrentFrameIntervals[k] =
					curFrame->discrete_frame_intervals[k];

			BDiscreteParameter* fpsParam =
				streamGroup->MakeDiscreteParameter(fFrameRateParameterID,
					B_MEDIA_RAW_VIDEO, "Frame Rate", B_GENERIC);

			for (uint8 k = 0; k < fNumFrameIntervals; k++) {
				if (fCurrentFrameIntervals[k] == 0)
					continue;
				float fps = 10000000.0f / fCurrentFrameIntervals[k];
				BString label;
				label.SetToFormat("%.0f fps", fps);
				fpsParam->AddItem(k, label.String());
			}

			for (uint8 k = 0; k < fNumFrameIntervals; k++) {
				if (fCurrentFrameIntervals[k]
					== curFrame->default_frame_interval) {
					fSelectedFrameIntervalIndex = k;
					break;
				}
			}
			fSelectedFrameInterval =
				fCurrentFrameIntervals[fSelectedFrameIntervalIndex];
		}
	}

	// ── Image Adjustments ────────────────────────────────────
	const BUSBConfiguration* config;
	const BUSBInterface* interface;
	uint8 buffer[1024];
	usb_descriptor* generic = (usb_descriptor*)buffer;

	for (uint32 i = 0; i < fDevice->CountConfigurations(); i++) {
		config = fDevice->ConfigurationAt(i);
		if (config == NULL)
			continue;
		fDevice->SetConfiguration(config);
		for (uint32 j = 0; j < config->CountInterfaces(); j++) {
			interface = config->InterfaceAt(j);
			if (interface == NULL)
				continue;
			if (interface->Class() != USB_VIDEO_DEVICE_CLASS
				|| interface->Subclass()
					!= USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS)
				continue;
			for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
				sizeof(buffer)) == B_OK; k++) {
				if (generic->generic.descriptor_type
					!= (USB_REQTYPE_CLASS | USB_DESCRIPTOR_INTERFACE))
					continue;
				if (((const usbvc_class_descriptor*)generic)
					->descriptorSubtype
					== USB_VIDEO_VC_PROCESSING_UNIT) {
					BParameterGroup* imageGroup =
						group->MakeGroup("Image");
					_AddProcessingParameter(imageGroup, index,
						(const usb_video_processing_unit_descriptor*)
							generic);
				}
			}
		}
	}

	// ── Camera Controls ──────────────────────────────────────
	_AddCameraTerminalControls(group, index);
}


status_t
UVCCamDevice::GetParameterValue(int32 id, bigtime_t* last_change, void* value,
	size_t* size)
{
	printf("UVCCAmDevice::GetParameterValue(%" B_PRId32 ")\n", id - fFirstParameterID);
	float* currValue;
	int* currValueInt;
	int16 data;
	uint16 wValue = 0;
	switch (id - fFirstParameterID) {
		case 0:
			// debug_printf("\tBrightness:\n");
			// debug_printf("\tValue = %f\n",fBrightness);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fBrightness;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 1:
			// debug_printf("\tContrast:\n");
			// debug_printf("\tValue = %f\n",fContrast);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fContrast;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 2:
			// debug_printf("\tHue:\n");
			// debug_printf("\tValue = %f\n",fHue);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fHue;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 4:
			// debug_printf("\tSaturation:\n");
			// debug_printf("\tValue = %f\n",fSaturation);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fSaturation;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 5:
			// debug_printf("\tSharpness:\n");
			// debug_printf("\tValue = %f\n",fSharpness);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fSharpness;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 6:
			// Gamma
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fGamma;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 7:
			// debug_printf("\tWB Temperature:\n");
			*size = sizeof(float);
			currValue = (float*)value;
			wValue = USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL << 8;
			if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data)
				== sizeof(data)) {
				fWBTemp = (float)data;
			}
			// debug_printf("\tValue = %f\n",fWBTemp);
			*currValue = fWBTemp;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 8:
			// debug_printf("\tWB Temperature Auto:\n");
			// debug_printf("\tValue = %d\n",fWBTempAuto);
			*size = sizeof(int);
			currValueInt = ((int*)value);
			*currValueInt = fWBTempAuto;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 11:
			if (!fBinaryBacklightCompensation) {
				// debug_printf("\tBacklight Compensation:\n");
				// debug_printf("\tValue = %f\n",fBacklightCompensation);
				*size = sizeof(float);
				currValue = (float*)value;
				*currValue = fBacklightCompensation;
				*last_change = fLastParameterChanges;
			} else {
				// debug_printf("\tBacklight Compensation:\n");
				// debug_printf("\tValue = %d\n",fBacklightCompensationBinary);
				currValueInt = (int*)value;
				*currValueInt = fBacklightCompensationBinary;
				*last_change = fLastParameterChanges;
			}
			return B_OK;
		case 12:
			// debug_printf("\tGain:\n");
			// debug_printf("\tValue = %f\n",fGain);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fGain;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 13:
			/* FIX: Return int for discrete parameter */
			*size = sizeof(int);
			currValueInt = (int*)value;
			*currValueInt = fPowerlineFrequency;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 14:
			/* Resolution selector (Task 2) */
			*size = sizeof(int);
			currValueInt = (int*)value;
			*currValueInt = fSelectedResolutionIndex;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 16:
			/* Frame rate selector (P2 Feature) */
			*size = sizeof(int);
			currValueInt = (int*)value;
			*currValueInt = fSelectedFrameIntervalIndex;
			*last_change = fLastParameterChanges;
			return B_OK;

	}

	/* Handle Camera Terminal controls by dynamic ID */
	if (id == fAutoExposureModeID && fAutoExposureModeID >= 0) {
		*size = sizeof(int);
		currValueInt = (int*)value;
		*currValueInt = (int)fAutoExposureMode;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fExposureTimeID && fExposureTimeID >= 0) {
		*size = sizeof(float);
		currValue = (float*)value;
		// Convert from 100μs units to milliseconds
		*currValue = fExposureTimeAbs / 10.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fAutoFocusID && fAutoFocusID >= 0) {
		*size = sizeof(int);
		currValueInt = (int*)value;
		*currValueInt = fAutoFocus ? 1 : 0;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fFocusAbsoluteID && fFocusAbsoluteID >= 0) {
		*size = sizeof(float);
		currValue = (float*)value;
		*currValue = (float)fFocusAbsolute;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fZoomAbsoluteID && fZoomAbsoluteID >= 0) {
		*size = sizeof(float);
		currValue = (float*)value;
		// Convert from internal units (100 = 1x) to display (1.0 = 1x)
		*currValue = fZoomAbsolute / 100.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fPanTiltID && fPanTiltID >= 0) {
		// Pan control - convert from arc-seconds to degrees
		*size = sizeof(float);
		currValue = (float*)value;
		*currValue = fPanAbsolute / 3600.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == (fPanTiltID + 1) && fPanTiltID >= 0) {
		// Tilt control - convert from arc-seconds to degrees
		*size = sizeof(float);
		currValue = (float*)value;
		*currValue = fTiltAbsolute / 3600.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}

	return B_BAD_VALUE;
}


status_t
UVCCamDevice::SetParameterValue(int32 id, bigtime_t when, const void* value,
	size_t size)
{
	printf("UVCCamDevice::SetParameterValue(%" B_PRId32 ")\n", id - fFirstParameterID);
	switch (id - fFirstParameterID) {
		case 0:
			// debug_printf("\tBrightness:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fBrightness = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_BRIGHTNESS_CONTROL, (int16)fBrightness);
		case 1:
			// debug_printf("\tContrast:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fContrast = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_CONTRAST_CONTROL, (int16)fContrast);
		case 2:
			// debug_printf("\tHue:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fHue = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_HUE_CONTROL, (int16)fHue);
		case 4:
			// debug_printf("\tSaturation:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fSaturation = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_SATURATION_CONTROL, (int16)fSaturation);
		case 5:
			// debug_printf("\tSharpness:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fSharpness = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_SHARPNESS_CONTROL, (int16)fSharpness);
		case 6:
			// Gamma
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fGamma = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_GAMMA_CONTROL, (int16)fGamma);
		case 7:
			if (fWBTempAuto)
				return B_OK;
			// debug_printf("\tWB Temperature:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fWBTemp = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL,
				(int16)fWBTemp);
		case 8:
			// debug_printf("\tWB Temperature Auto:\n");
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;
			fWBTempAuto = *((int*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(
				USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, (int8)fWBTempAuto);
		case 11:
			if (!fBinaryBacklightCompensation) {
				// debug_printf("\tBacklight Compensation:\n");
				if (!value || (size != sizeof(float)))
					return B_BAD_VALUE;
				fBacklightCompensation = *((float*)value);
			} else {
				// debug_printf("\tBacklight Compensation:\n");
				if (!value || (size != sizeof(int)))
					return B_BAD_VALUE;
				fBacklightCompensationBinary = *((int*)value);
			}
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_BACKLIGHT_COMPENSATION_CONTROL,
				(int16)fBacklightCompensationBinary);
		case 12:
			// debug_printf("\tGain:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fGain = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_GAIN_CONTROL, (int16)fGain);
		case 13:
			/* FIX: Accept int for discrete parameter */
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;
			fPowerlineFrequency = *((int*)value);
			/* Clamp to valid range 0-2 */
			if (fPowerlineFrequency < 0) fPowerlineFrequency = 0;
			if (fPowerlineFrequency > 2) fPowerlineFrequency = 2;
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_POWER_LINE_FREQUENCY_CONTROL,
				(int8)fPowerlineFrequency);
		case 14:
		{
			/* Resolution selector (Task 2 & 3) */
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;

			int32 newIndex = *((int*)value);
			BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

			/* Validate index */
			if (newIndex < 0 || newIndex >= frameList->CountItems()) {
				printf("UVCCamDevice: Invalid resolution index %d (max %d)\n",
					(int)newIndex, (int)frameList->CountItems() - 1);
				return B_BAD_VALUE;
			}

			/* Only change if different */
			if (newIndex != fSelectedResolutionIndex) {
				const usb_video_frame_descriptor* frameDesc =
					(const usb_video_frame_descriptor*)frameList->ItemAt(newIndex);
				if (frameDesc != NULL) {
					printf("UVCCamDevice: Resolution changed to %ux%u (index %d)\n",
						frameDesc->width, frameDesc->height, (int)newIndex);
					syslog(LOG_INFO, "UVCCamDevice: Resolution changed to %ux%u (index %d, frame_index %u)\n",
						frameDesc->width, frameDesc->height, (int)newIndex, frameDesc->frame_index);

					fSelectedResolutionIndex = newIndex;

					/* Task 3: Update frame indices for UVC format negotiation */
					if (fIsMJPEG) {
						fMJPEGFrameIndex = frameDesc->frame_index;
					} else {
						fUncompressedFrameIndex = frameDesc->frame_index;
						/* Set expected frame size for YUY2 deframer */
						if (fDeframer) {
							((UVCDeframer*)fDeframer)->SetExpectedFrameSize(
								frameDesc->width * frameDesc->height * 2);
						}
					}

					/* P2 Feature: Update available frame intervals for new resolution */
					if (frameDesc->frame_interval_type > 0) {
						fNumFrameIntervals = frameDesc->frame_interval_type;
						if (fNumFrameIntervals > 8)
							fNumFrameIntervals = 8;

						for (uint8 k = 0; k < fNumFrameIntervals; k++) {
							fCurrentFrameIntervals[k] = frameDesc->discrete_frame_intervals[k];
						}

						/* Reset to default frame interval for this resolution */
						fSelectedFrameIntervalIndex = 0;
						for (uint8 k = 0; k < fNumFrameIntervals; k++) {
							if (fCurrentFrameIntervals[k] == frameDesc->default_frame_interval) {
								fSelectedFrameIntervalIndex = k;
								break;
							}
						}
						fSelectedFrameInterval = fCurrentFrameIntervals[fSelectedFrameIntervalIndex];

						printf("UVCCamDevice: Frame intervals updated for new resolution: %d options, default=%.1f fps\n",
							(int)fNumFrameIntervals, 10000000.0f / fSelectedFrameInterval);
					} else {
						/* Continuous interval - use default */
						fNumFrameIntervals = 0;
						fSelectedFrameInterval = frameDesc->default_frame_interval;
					}

					/* FIX BUG 12: Non chiamare AcceptVideoFrame() perché cerca per
					 * risoluzione e potrebbe trovare un frame DIVERSO da quello
					 * selezionato (es. stessa risoluzione ma fps diversi).
					 * Questo sovrascriveva fMJPEGFrameIndex con il valore sbagliato.
					 * Invece, aggiorniamo direttamente fVideoFrame.
					 */
					uint32 newWidth = frameDesc->width;
					uint32 newHeight = frameDesc->height;
					SetVideoFrame(BRect(0, 0, newWidth - 1, newHeight - 1));
					syslog(LOG_INFO, "UVCCamDevice: VideoFrame updated to %ux%u (frame_index=%u)\n",
						newWidth, newHeight, frameDesc->frame_index);

					/* Always flush deframer to discard any frames from old resolution.
					 * This is important even when transfer is not running, as there may
					 * be stale frames in the queue from before the resolution change.
					 */
					if (fDeframer) {
						fDeframer->Flush();
						syslog(LOG_INFO, "UVCCamDevice: Deframer flushed for resolution change\n");
					}

					/* Always mark resolution transition start time. Frames with wrong
					 * dimensions will be skipped during the transition period.
					 * This handles the case where the transfer starts after resolution change.
					 */
					fResolutionTransitionStart = system_time();

					/* If transfer is running, we need to renegotiate */
					if (TransferEnabled()) {
						syslog(LOG_INFO, "UVCCamDevice: Transfer running, stopping to change resolution\n");
						StopTransfer();

						/* Small delay for camera to process format change */
						snooze(50000);  // 50ms

						status_t err = StartTransfer();
						if (err != B_OK) {
							syslog(LOG_ERR, "UVCCamDevice: Failed to restart transfer with new resolution: %s\n",
								strerror(err));
							return err;
						}
						syslog(LOG_INFO, "UVCCamDevice: Transfer restarted with new resolution\n");
					}

					fLastParameterChanges = when;
				}
			}
			return B_OK;
		}
		case 16:
		{
			/* Frame rate selector (P2 Feature) */
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;

			int32 newIndex = *((int*)value);

			/* Validate index */
			if (newIndex < 0 || newIndex >= fNumFrameIntervals) {
				printf("UVCCamDevice: Invalid frame rate index %d (max %d)\n",
					(int)newIndex, (int)fNumFrameIntervals - 1);
				return B_BAD_VALUE;
			}

			/* Only change if different */
			if (newIndex != fSelectedFrameIntervalIndex) {
				fSelectedFrameIntervalIndex = newIndex;
				fSelectedFrameInterval = fCurrentFrameIntervals[newIndex];

				float fps = 10000000.0f / fSelectedFrameInterval;
				printf("UVCCamDevice: Frame rate changed to %.1f fps (interval %u)\n",
					fps, fSelectedFrameInterval);
				syslog(LOG_INFO, "UVCCamDevice: Frame rate changed to %.1f fps (interval %u)\n",
					fps, fSelectedFrameInterval);

				/* If transfer is running, renegotiate format */
				if (TransferEnabled()) {
					syslog(LOG_INFO, "UVCCamDevice: Transfer running, stopping to change frame rate\n");
					StopTransfer();

					snooze(50000);  // 50ms delay

					status_t err = StartTransfer();
					if (err != B_OK) {
						syslog(LOG_ERR, "UVCCamDevice: Failed to restart transfer with new frame rate: %s\n",
							strerror(err));
						return err;
					}
					syslog(LOG_INFO, "UVCCamDevice: Transfer restarted with new frame rate\n");
				}

				fLastParameterChanges = when;
			}
			return B_OK;
		}

	}

	/* Handle Camera Terminal controls by dynamic ID */
	if (id == fAutoExposureModeID && fAutoExposureModeID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		uint8 mode = (uint8)*((int*)value);
		if (mode == 1 || mode == 2 || mode == 4 || mode == 8) {
			status_t err = _SetCTControlValue(USB_VIDEO_CT_AE_MODE_CONTROL, &mode, 1);
			if (err == B_OK) {
				fAutoExposureMode = mode;
				fLastParameterChanges = when;
				printf("UVCCamDevice: Auto Exposure Mode set to %d\n", mode);
			}
			return err;
		}
		return B_BAD_VALUE;
	}
	if (id == fExposureTimeID && fExposureTimeID >= 0) {
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		float msValue = *((float*)value);
		// Convert from milliseconds to 100μs units
		uint32 expTime = (uint32)(msValue * 10.0f);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL,
			&expTime, 4);
		if (err == B_OK) {
			fExposureTimeAbs = expTime;
			fLastParameterChanges = when;
			printf("UVCCamDevice: Exposure Time set to %.1f ms (%u units)\n",
				msValue, expTime);
		}
		return err;
	}
	if (id == fAutoFocusID && fAutoFocusID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		uint8 autoFocus = (*((int*)value) != 0) ? 1 : 0;
		status_t err = _SetCTControlValue(USB_VIDEO_CT_FOCUS_AUTO_CONTROL, &autoFocus, 1);
		if (err == B_OK) {
			fAutoFocus = (autoFocus != 0);
			fLastParameterChanges = when;
			printf("UVCCamDevice: Auto Focus set to %s\n", fAutoFocus ? "On" : "Off");
		}
		return err;
	}
	if (id == fFocusAbsoluteID && fFocusAbsoluteID >= 0) {
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		uint16 focusVal = (uint16)*((float*)value);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_FOCUS_ABSOLUTE_CONTROL, &focusVal, 2);
		if (err == B_OK) {
			fFocusAbsolute = focusVal;
			fLastParameterChanges = when;
			printf("UVCCamDevice: Focus set to %u\n", focusVal);
		}
		return err;
	}
	if (id == fZoomAbsoluteID && fZoomAbsoluteID >= 0) {
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		// Convert from display (1.0 = 1x) to internal units (100 = 1x)
		uint16 zoomVal = (uint16)(*((float*)value) * 100.0f);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_ZOOM_ABSOLUTE_CONTROL, &zoomVal, 2);
		if (err == B_OK) {
			fZoomAbsolute = zoomVal;
			fLastParameterChanges = when;
			printf("UVCCamDevice: Zoom set to %.1fx (%u)\n", zoomVal / 100.0f, zoomVal);
		}
		return err;
	}
	if (id == fPanTiltID && fPanTiltID >= 0) {
		// Pan control - convert from degrees to arc-seconds and write compound control
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		struct {
			int32 pan;
			int32 tilt;
		} panTilt;
		panTilt.pan = (int32)(*((float*)value) * 3600.0f);
		panTilt.tilt = fTiltAbsolute;  // Keep current tilt
		status_t err = _SetCTControlValue(USB_VIDEO_CT_PANTILT_ABSOLUTE_CONTROL, &panTilt, 8);
		if (err == B_OK) {
			fPanAbsolute = panTilt.pan;
			fLastParameterChanges = when;
			printf("UVCCamDevice: Pan set to %.1f°\n", panTilt.pan / 3600.0f);
		}
		return err;
	}
	if (id == (fPanTiltID + 1) && fPanTiltID >= 0) {
		// Tilt control - convert from degrees to arc-seconds and write compound control
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		struct {
			int32 pan;
			int32 tilt;
		} panTilt;
		panTilt.pan = fPanAbsolute;  // Keep current pan
		panTilt.tilt = (int32)(*((float*)value) * 3600.0f);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_PANTILT_ABSOLUTE_CONTROL, &panTilt, 8);
		if (err == B_OK) {
			fTiltAbsolute = panTilt.tilt;
			fLastParameterChanges = when;
			printf("UVCCamDevice: Tilt set to %.1f°\n", panTilt.tilt / 3600.0f);
		}
		return err;
	}

	return B_BAD_VALUE;
}


status_t
UVCCamDevice::_SetParameterValue(uint16 wValue, int16 setValue)
{
	return (fDevice->ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR, wValue << 8, fControlRequestIndex,
		sizeof(setValue), &setValue)) == sizeof(setValue);
}


status_t
UVCCamDevice::_SetParameterValue(uint16 wValue, int8 setValue)
{
	return (fDevice->ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR, wValue << 8, fControlRequestIndex,
		sizeof(setValue), &setValue)) == sizeof(setValue);
}


// FIX BUG 6: Contatori ora sono membri di istanza (vedi header)

status_t
UVCCamDevice::FillFrameBuffer(BBuffer* buffer, bigtime_t* stamp)
{
	fFillFrameCount++;

	// Debug: verify fDeframer
	static int32 sDeframerCheck = 0;
	if (++sDeframerCheck <= 3) {
		syslog(LOG_INFO, "FillFrameBuffer: fDeframer=%p this=%p\n",
			(void*)fDeframer, (void*)this);
	}

	if (fDeframer == NULL) {
		syslog(LOG_ERR, "FillFrameBuffer: fDeframer is NULL!\n");
		return B_ERROR;
	}

	status_t err = fDeframer->WaitFrame(2000000);
	if (err < B_OK) {
		fFillFrameTimeout++;
		// Report every 10 timeouts
		if (fFillFrameTimeout <= 5 || (fFillFrameTimeout % 10) == 0) {
			// Use syslog - safe in any thread
			syslog(LOG_WARNING, "UVCCamDevice::FillFrameBuffer: WaitFrame TIMEOUT #%d (err=%s)\n",
				(int)fFillFrameTimeout, strerror(err));
		}

		// Track timeouts for high-bandwidth auto-fallback
		// If using high-bandwidth and getting repeated timeouts, it likely means
		// the USB host controller doesn't support high-bandwidth isochronous
		if (fUsingHighBandwidth) {
			_OnHighBandwidthFailure();
		}

		return err;
	}

	CamFrame* f;
	err = fDeframer->GetFrame(&f, stamp);
	if (err < B_OK)
		return err;

	fFillFrameSuccess++;

	int32 w = (int32)(VideoFrame().right - VideoFrame().left + 1);
	int32 h = (int32)(VideoFrame().bottom - VideoFrame().top + 1);
	size_t bufferSize = (size_t)w * h * 4;

	// DEBUG: Log buffer size info to check for stride issues
	static int32 sBufSizeLog = 0;
	if (++sBufSizeLog <= 3) {
		size_t available = buffer->SizeAvailable();
		size_t expectedStride = (size_t)w * 4;
		size_t actualStride = (h > 1) ? (available / h) : expectedStride;
		syslog(LOG_INFO, "Buffer info: available=%zu needed=%zu w=%d h=%d expectedStride=%zu actualStride=%zu\n",
			available, bufferSize, (int)w, (int)h, expectedStride, actualStride);
		if (actualStride != expectedStride) {
			syslog(LOG_WARNING, "Buffer STRIDE MISMATCH! expected=%zu actual=%zu diff=%d\n",
				expectedStride, actualStride, (int)(actualStride - expectedStride));
		}
	}

	/* Task 6: Check if buffer is large enough for current resolution */
	if (buffer->SizeAvailable() < bufferSize) {
		static int32 sBufferTooSmall = 0;
		if (++sBufferTooSmall <= 5 || (sBufferTooSmall % 100) == 0) {
			syslog(LOG_WARNING, "FillFrameBuffer: Buffer too small #%d: need %zu, have %zu (%dx%d)\n",
				(int)sBufferTooSmall, bufferSize, buffer->SizeAvailable(), (int)w, (int)h);
			syslog(LOG_WARNING, "FillFrameBuffer: Resolution may have changed - restart stream for new buffers\n");
		}
		// Recycle frame back to pool instead of deleting
		if (fDeframer != NULL)
			fDeframer->RecycleFrame(f);
		else
			delete f;
		return B_ERROR;
	}

	// Feature 1: Frame Validation
	fValidationStats.frames_validated++;
	frame_validation_result validation;
	if (fIsMJPEG) {
		validation = _ValidateMJPEGFrame((const uint8*)f->Buffer(), f->BufferLength());

		// MJPEG frame size monitoring for auto-fallback
		size_t frameSize = f->BufferLength();
		fMJPEGFrameSizeSum += frameSize;
		fMJPEGFrameSizeCount++;

		// Minimum expected MJPEG size: 1% of raw YUY2 size.
		// MJPEG compression varies widely (10:1 to 50:1+), so use a low
		// threshold to avoid false positives on highly compressed streams.
		if (fExpectedMJPEGMinSize == 0) {
			fExpectedMJPEGMinSize = (size_t)w * h * 2 / 100;
			if (fExpectedMJPEGMinSize < 1024)
				fExpectedMJPEGMinSize = 1024;
		}

		// Check every 60 frames (skip first 60 to allow stream stabilization)
		bigtime_t now = system_time();
		if (fMJPEGFrameSizeCount >= 60 && (now - fLastFrameSizeCheck) > 10000000) {
			fLastFrameSizeCheck = now;
			size_t avgSize = fMJPEGFrameSizeSum / fMJPEGFrameSizeCount;

			if (avgSize < fExpectedMJPEGMinSize) {
				syslog(LOG_WARNING, "UVCCamDevice: MJPEG frames too small! avg=%zu, min=%zu\n",
					avgSize, fExpectedMJPEGMinSize);

				// Use RequestResolutionChange() for safe resolution change
				// The actual change happens in ReconfigThread, not here
				int32 maxLevel = _GetMaxResolutionLevel();
				if (fCurrentResolutionLevel < maxLevel && !HasPendingReconfigRequest()) {
					int32 targetLevel = fCurrentResolutionLevel + 1;
					uint32 newWidth, newHeight;
					_GetResolutionAtLevel(targetLevel, &newWidth, &newHeight);

					syslog(LOG_WARNING, "UVCCamDevice: Bandwidth insufficient, "
						"requesting fallback to %ux%u via worker thread\n",
						newWidth, newHeight);

					RequestResolutionChange(newWidth, newHeight);

					fCurrentResolutionLevel = targetLevel;
					fFallbackActive = true;
					fLastFallbackTime = system_time();
				} else if (fCurrentResolutionLevel >= maxLevel) {
					syslog(LOG_WARNING, "UVCCamDevice: Bandwidth insufficient, "
						"but already at minimum resolution\n");
				}

				// Reset counters
				fMJPEGFrameSizeSum = 0;
				fMJPEGFrameSizeCount = 0;
			} else {
				// Reset counters for next window
				fMJPEGFrameSizeSum = 0;
				fMJPEGFrameSizeCount = 0;
			}
		}
	} else {
		validation = _ValidateYUY2Frame((const uint8*)f->Buffer(), f->BufferLength(), w, h);
	}

	// Update validation statistics based on result
	switch (validation) {
		case FRAME_VALID:
			fValidationStats.frames_valid++;
			fConsecutiveBadFrames = 0;
			break;
		case FRAME_INCOMPLETE:
			fValidationStats.frames_incomplete++;
			fConsecutiveBadFrames++;
			break;
		case FRAME_CORRUPTED_NO_SOI:
			fValidationStats.frames_no_soi++;
			fConsecutiveBadFrames++;
			break;
		case FRAME_CORRUPTED_NO_EOI:
			fValidationStats.frames_no_eoi++;
			fConsecutiveBadFrames++;
			break;
		default:
			fConsecutiveBadFrames++;
			break;
	}

	// Report validation stats periodically
	_ReportValidationStats();

	// Feature 3: Update packet loss stats from base class and evaluate
	// Use delta from last check to update the evaluation window
	uint32 currentSuccess = fPacketSuccessCount;
	uint32 currentError = fPacketErrorCount;
	uint32 deltaSuccess = currentSuccess - fLastPacketSuccessCount;
	uint32 deltaError = currentError - fLastPacketErrorCount;
	fLastPacketSuccessCount = currentSuccess;
	fLastPacketErrorCount = currentError;
	fEvalWindowPackets += deltaSuccess + deltaError;
	fEvalWindowErrors += deltaError;
	_EvaluatePacketLoss();

	// Warn if too many consecutive bad frames
	if (fConsecutiveBadFrames == kMaxConsecutiveBadFrames) {
		syslog(LOG_WARNING, "UVCCamDevice: %u consecutive bad frames detected, "
			"consider lowering resolution\n", fConsecutiveBadFrames);
	}

	if (buffer->SizeAvailable() >= bufferSize) {
		unsigned char* dst = (unsigned char*)buffer->Data();

		// OPTIMIZATION: Only pre-fill buffer for incomplete/invalid frames
		// For valid frames, MJPEG decompression or YUY2 conversion will
		// overwrite the entire buffer, making pre-fill unnecessary.
		// This saves ~300KB of memory writes per frame at 320x240.
		bool needsPreFill = (validation != FRAME_VALID);

		if (needsPreFill) {
			// Use fast memset for pre-fill (dark blue pattern)
			// 0x40 gives a visible but not distracting background
			memset(dst, 0x40, bufferSize);
		}

		if (fIsMJPEG) {
			// For MJPEG, validation already happened above
			// If invalid and frame repeat enabled, we still try to decompress
			// as partial MJPEG might produce some valid data
			_DecompressMJPEGtoRGB32(dst,
				(const unsigned char*)f->Buffer(), f->BufferLength(), w, h);

			// Cache valid frames for potential frame repeat
			if (validation == FRAME_VALID) {
				_CacheValidFrame((const uint8*)f->Buffer(), f->BufferLength(), w, h);
			}
		} else if (fIsNV12) {
			// NV12 (YUV 4:2:0 planar) conversion
			size_t expectedNV12 = (size_t)w * h * 3 / 2;  // Y plane + UV plane (half size)
			size_t actualNV12 = f->BufferLength();

			if (actualNV12 < expectedNV12) {
				static int32 sIncompleteNV12 = 0;
				if (++sIncompleteNV12 <= 20 || (sIncompleteNV12 % 100) == 0)
					syslog(LOG_WARNING, "FillFrameBuffer: Incomplete NV12 #%d: %zu/%zu bytes (%.1f%%)\n",
						(int)sIncompleteNV12, actualNV12, expectedNV12,
						100.0f * actualNV12 / expectedNV12);
			}

			// DEBUG: Log first bytes of NV12 frame
			static int32 sNV12Debug = 0;
			if (++sNV12Debug <= 5) {
				const uint8* nv12Data = (const uint8*)f->Buffer();
				syslog(LOG_INFO, "NV12 frame #%d: size=%zu expected=%zu w=%d h=%d\n",
					(int)sNV12Debug, actualNV12, expectedNV12, (int)w, (int)h);
				syslog(LOG_INFO, "NV12 Y plane first 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x "
					"%02x %02x %02x %02x %02x %02x %02x %02x\n",
					nv12Data[0], nv12Data[1], nv12Data[2], nv12Data[3],
					nv12Data[4], nv12Data[5], nv12Data[6], nv12Data[7],
					nv12Data[8], nv12Data[9], nv12Data[10], nv12Data[11],
					nv12Data[12], nv12Data[13], nv12Data[14], nv12Data[15]);
				// Check UV plane start
				size_t uvOffset = (size_t)w * h;
				if (actualNV12 > uvOffset + 8) {
					syslog(LOG_INFO, "NV12 UV plane (offset %zu): %02x %02x %02x %02x %02x %02x %02x %02x\n",
						uvOffset, nv12Data[uvOffset], nv12Data[uvOffset+1],
						nv12Data[uvOffset+2], nv12Data[uvOffset+3],
						nv12Data[uvOffset+4], nv12Data[uvOffset+5],
						nv12Data[uvOffset+6], nv12Data[uvOffset+7]);
				}
			}

			_ConvertNV12toRGB32(dst,
				(unsigned char*)f->Buffer(), actualNV12, w, h);

			// Cache valid frames
			if (validation == FRAME_VALID) {
				_CacheValidFrame((const uint8*)f->Buffer(), f->BufferLength(), w, h);
			}
		} else {
			// YUY2 (YUV 4:2:2 packed) - default uncompressed format
			// Check for incomplete YUY2 data
			size_t expectedYUY2 = (size_t)w * h * 2;
			size_t actualYUY2 = f->BufferLength();

			if (actualYUY2 < expectedYUY2) {
				// Log incomplete frame
				static int32 sIncompleteYUY2 = 0;
				if (++sIncompleteYUY2 <= 20 || (sIncompleteYUY2 % 100) == 0)
					syslog(LOG_WARNING, "FillFrameBuffer: Incomplete YUY2 #%d: %zu/%zu bytes (%.1f%%)\n",
						(int)sIncompleteYUY2, actualYUY2, expectedYUY2,
						100.0f * actualYUY2 / expectedYUY2);
			}

			// DEBUG: Log first bytes of YUY2 frame to check format
			static int32 sYUY2Debug = 0;
			if (++sYUY2Debug <= 5) {
				const uint8* yuy2Data = (const uint8*)f->Buffer();
				syslog(LOG_INFO, "YUY2 frame #%d: size=%zu expected=%zu w=%d h=%d\n",
					(int)sYUY2Debug, actualYUY2, expectedYUY2, (int)w, (int)h);
				syslog(LOG_INFO, "YUY2 first 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x "
					"%02x %02x %02x %02x %02x %02x %02x %02x\n",
					yuy2Data[0], yuy2Data[1], yuy2Data[2], yuy2Data[3],
					yuy2Data[4], yuy2Data[5], yuy2Data[6], yuy2Data[7],
					yuy2Data[8], yuy2Data[9], yuy2Data[10], yuy2Data[11],
					yuy2Data[12], yuy2Data[13], yuy2Data[14], yuy2Data[15]);
				// Check row 2 start (offset = w*2 = 640 for 320 width)
				size_t row2Offset = (size_t)w * 2;
				if (actualYUY2 > row2Offset + 16) {
					syslog(LOG_INFO, "YUY2 row2 (offset %zu): %02x %02x %02x %02x %02x %02x %02x %02x\n",
						row2Offset, yuy2Data[row2Offset], yuy2Data[row2Offset+1],
						yuy2Data[row2Offset+2], yuy2Data[row2Offset+3],
						yuy2Data[row2Offset+4], yuy2Data[row2Offset+5],
						yuy2Data[row2Offset+6], yuy2Data[row2Offset+7]);
				}
			}

			_ConvertYUY2toRGB32(dst,
				(unsigned char*)f->Buffer(), actualYUY2, w, h);

			// Cache valid frames
			if (validation == FRAME_VALID) {
				_CacheValidFrame((const uint8*)f->Buffer(), f->BufferLength(), w, h);
			}
		}
	}

	// Recycle frame back to pool for reuse (reduces allocations)
	if (fDeframer != NULL)
		fDeframer->RecycleFrame(f);
	else
		delete f;

	return B_OK;
}


// Optimized inline clamp function using branchless technique
static inline uint8
clamp255(int32 v)
{
	// Branchless clamp: faster than conditional on most CPUs
	v = v < 0 ? 0 : v;
	return (uint8)(v > 255 ? 255 : v);
}


void
UVCCamDevice::_ConvertYUY2toRGB32(unsigned char* dst, unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// Optimized YUY2 to RGB32 conversion using pre-computed lookup tables.
	// Eliminates per-pixel multiplications - only table lookups and additions.
	// YUY2 format: Y0 U Y1 V (4 bytes = 2 pixels)

	if (!dst || !src || width <= 0 || height <= 0)
		return;

	// Ensure lookup tables are initialized
	if (!gYuvRgbTables.initialized) {
		gYuvRgbTables.Initialize();
	}

	// Cache table pointers for faster access in inner loop
	const int32* yTable = gYuvRgbTables.y_table;
	const int32* uBTable = gYuvRgbTables.u_b_table;
	const int32* uGTable = gYuvRgbTables.u_g_table;
	const int32* vRTable = gYuvRgbTables.v_r_table;
	const int32* vGTable = gYuvRgbTables.v_g_table;

	size_t expectedSize = (size_t)width * height * 2;
	size_t srcStride = (size_t)width * 2;  // YUY2: 2 bytes per pixel (default)
	size_t dstStride = (size_t)width * 4;  // RGB32: 4 bytes per pixel

	// STRIDE QUIRK: Microdia 0c45:6409 may use 352-pixel internal buffer width
	// Only apply when srcSize indicates actual padding (srcSize > expectedSize)
	if (fMicrodiaQuirk && width < 352 && srcSize > expectedSize) {
		size_t paddedStride = 352 * 2;
		if (srcSize >= paddedStride * height) {
			srcStride = paddedStride;
			static bool sQuirkApplied = false;
			if (!sQuirkApplied) {
				syslog(LOG_INFO, "YUY2: Applying Microdia stride quirk: %dx%d using stride %zu (srcSize=%zu)\n",
					(int)width, (int)height, srcStride, srcSize);
				sQuirkApplied = true;
			}
		}
	}
	// STRIDE FIX: Detect row padding (some cameras add padding)
	else if (srcSize > expectedSize && height > 1) {
		size_t actualStride = srcSize / height;
		// Only use if it's larger than expected and aligned reasonably
		if (actualStride > srcStride && actualStride <= srcStride + 256) {
			static bool sStrideWarned = false;
			if (!sStrideWarned) {
				syslog(LOG_WARNING, "YUY2: Detected row padding! expected stride=%zu, actual=%zu (padding=%zu bytes/row)\n",
					srcStride, actualStride, actualStride - srcStride);
				sStrideWarned = true;
			}
			srcStride = actualStride;
		}
	}

	// Enhanced YUY2 diagnostics to detect byte order issues
	static int32 sYUY2Diag = 0;
	if (++sYUY2Diag <= 5) {
		// Analyze first few pixels to detect YUYV vs UYVY
		// In YUYV: Y values vary widely (0-255), U/V are more stable (around 128 for gray)
		// In UYVY: positions are swapped
		uint8 b0 = src[0], b1 = src[1], b2 = src[2], b3 = src[3];
		uint8 b4 = src[4], b5 = src[5], b6 = src[6], b7 = src[7];

		// Calculate variance of even vs odd bytes across first 8 bytes
		int evenSum = b0 + b2 + b4 + b6;
		int oddSum = b1 + b3 + b5 + b7;
		int evenAvg = evenSum / 4;
		int oddAvg = oddSum / 4;

		// In YUYV: even bytes are Y (vary), odd bytes are U/V (near 128)
		// In UYVY: even bytes are U/V (near 128), odd bytes are Y (vary)
		bool probablyYUYV = (abs(oddAvg - 128) < abs(evenAvg - 128));

		syslog(LOG_INFO, "YUY2 diag #%d: bytes=[%02x %02x %02x %02x | %02x %02x %02x %02x]\n",
			(int)sYUY2Diag, b0, b1, b2, b3, b4, b5, b6, b7);
		syslog(LOG_INFO, "YUY2 diag #%d: evenAvg=%d oddAvg=%d -> likely %s\n",
			(int)sYUY2Diag, evenAvg, oddAvg, probablyYUYV ? "YUYV" : "UYVY");
		syslog(LOG_INFO, "YUY2 diag #%d: srcSize=%zu expected=%zu srcStride=%zu dstStride=%zu\n",
			(int)sYUY2Diag, srcSize, (size_t)width * height * 2, srcStride, dstStride);

		// Detect if camera pads rows to alignment boundaries
		// Check if srcSize is larger than expected and find actual stride
		if (srcSize > (size_t)width * height * 2) {
			size_t actualStride = srcSize / height;
			syslog(LOG_WARNING, "YUY2 diag #%d: Source has PADDING! srcSize=%zu > expected=%zu, actualStride=%zu (expected %zu)\n",
				(int)sYUY2Diag, srcSize, (size_t)width * height * 2, actualStride, srcStride);
		}

		// Also check if first row pattern repeats at unexpected offset
		// This detects row padding even when total size is correct (truncated)
		if (srcSize >= (size_t)width * 4) {  // Need at least 2 rows
			// Look for similar Y values at different offsets to find actual row start
			int expectedRowOffset = width * 2;
			// Check some common alignment values: 512, 1024, 2048
			int testOffsets[] = {512, 1024, 2048, expectedRowOffset};
			for (int i = 0; i < 4 && srcSize > (size_t)testOffsets[i] + 8; i++) {
				int off = testOffsets[i];
				// Compare first few pixels of row 0 with potential row 1
				int diff = abs((int)src[0] - (int)src[off]) + abs((int)src[2] - (int)src[off+2]) +
				           abs((int)src[4] - (int)src[off+4]) + abs((int)src[6] - (int)src[off+6]);
				syslog(LOG_INFO, "YUY2 stride test: offset=%d diff=%d (low=similar rows)\n", off, diff);
			}
		}
	}

	// TEST PATTERN MODE: Enable to debug alignment issues
	// Shows colored bars instead of camera data for first 100 frames
	// This verifies conversion and stride are correct
#if 0  // DISABLED - test pattern confirmed stride is correct
	static int32 sTestPattern = 0;
	if (++sTestPattern <= 100) {  // Show pattern for first 100 frames
		// Draw 8 colored vertical bars: R, G, B, W, C, M, Y, K
		// Each bar is width/8 pixels wide
		int barWidth = width / 8;
		for (int32 row = 0; row < height; row++) {
			unsigned char* dstRow = dst + row * dstStride;
			for (int32 x = 0; x < width; x++) {
				int bar = x / barWidth;
				switch (bar) {
					case 0: dstRow[0]=0;   dstRow[1]=0;   dstRow[2]=255; break; // Red (BGRA: B=0,G=0,R=255)
					case 1: dstRow[0]=0;   dstRow[1]=255; dstRow[2]=0;   break; // Green
					case 2: dstRow[0]=255; dstRow[1]=0;   dstRow[2]=0;   break; // Blue
					case 3: dstRow[0]=255; dstRow[1]=255; dstRow[2]=255; break; // White
					case 4: dstRow[0]=255; dstRow[1]=255; dstRow[2]=0;   break; // Cyan
					case 5: dstRow[0]=255; dstRow[1]=0;   dstRow[2]=255; break; // Magenta
					case 6: dstRow[0]=0;   dstRow[1]=255; dstRow[2]=255; break; // Yellow
					case 7: dstRow[0]=0;   dstRow[1]=0;   dstRow[2]=0;   break; // Black
				}
				dstRow[3] = 255;  // Alpha
				dstRow += 4;
			}
		}
		return;
	}
#endif

	// Row-by-row conversion for proper stride handling
	for (int32 row = 0; row < height; row++) {
		const unsigned char* srcRow = src + row * srcStride;
		unsigned char* dstRow = dst + row * dstStride;

		// Check source bounds for this row
		if ((size_t)(srcRow - src) + srcStride > srcSize)
			break;

		// Process this row (width pixels = width/2 YUY2 macro-pixels)
		for (int32 x = 0; x < width; x += 2) {
			// Read YUY2 macro-pixel (2 pixels)
			uint8 y0 = srcRow[0];
			uint8 u  = srcRow[1];
			uint8 y1 = srcRow[2];
			uint8 v  = srcRow[3];
			srcRow += 4;

			// Lookup pre-computed values (no multiplications!)
			int32 yVal0 = yTable[y0];
			int32 yVal1 = yTable[y1];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			// Pixel 0: BGRA (combine Y with U/V contributions, then shift)
			dstRow[0] = clamp255((yVal0 + uB + 128) >> 8);           // B
			dstRow[1] = clamp255((yVal0 + uG + vG + 128) >> 8);      // G
			dstRow[2] = clamp255((yVal0 + vR + 128) >> 8);           // R
			dstRow[3] = 255;                                          // A

			// Pixel 1: BGRA
			dstRow[4] = clamp255((yVal1 + uB + 128) >> 8);           // B
			dstRow[5] = clamp255((yVal1 + uG + vG + 128) >> 8);      // G
			dstRow[6] = clamp255((yVal1 + vR + 128) >> 8);           // R
			dstRow[7] = 255;                                          // A
			dstRow += 8;
		}
	}
}


void
UVCCamDevice::_ConvertNV12toRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// NV12 to RGB32 conversion using pre-computed lookup tables.
	// NV12 format: Y plane (width*height bytes) followed by UV plane (width*height/2 bytes)
	// UV plane is interleaved: U0 V0 U1 V1 ... (subsampled 2x2)

	if (!dst || !src || width <= 0 || height <= 0)
		return;

	// Ensure lookup tables are initialized
	if (!gYuvRgbTables.initialized) {
		gYuvRgbTables.Initialize();
	}

	// Validate NV12 data size (Y plane + UV plane)
	size_t expectedSize = (size_t)width * height * 3 / 2;
	if (srcSize < expectedSize) {
		syslog(LOG_WARNING, "NV12 conversion: srcSize %zu < expected %zu\n",
			srcSize, expectedSize);
		// Fill with black and return
		memset(dst, 0, width * height * 4);
		return;
	}

	// Cache table pointers for faster access in inner loop
	const int32* yTable = gYuvRgbTables.y_table;
	const int32* uBTable = gYuvRgbTables.u_b_table;
	const int32* uGTable = gYuvRgbTables.u_g_table;
	const int32* vRTable = gYuvRgbTables.v_r_table;
	const int32* vGTable = gYuvRgbTables.v_g_table;

	const unsigned char* yPlane = src;
	const unsigned char* uvPlane = src + (width * height);
	size_t dstStride = (size_t)width * 4;

	// Process two rows at a time (they share the same UV values)
	for (int32 row = 0; row < height; row += 2) {
		unsigned char* dstRow0 = dst + row * dstStride;
		unsigned char* dstRow1 = (row + 1 < height) ? dst + (row + 1) * dstStride : dstRow0;
		const unsigned char* yRow0 = yPlane + row * width;
		const unsigned char* yRow1 = (row + 1 < height) ? yPlane + (row + 1) * width : yRow0;
		const unsigned char* uvRow = uvPlane + (row / 2) * width;

		for (int32 col = 0; col < width; col += 2) {
			// Get UV values (shared by 2x2 block)
			uint8 u = uvRow[col];
			uint8 v = uvRow[col + 1];

			// Pre-compute UV contributions
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			// Process 2x2 block
			for (int dy = 0; dy < 2 && (row + dy) < height; dy++) {
				unsigned char* dstPixel = (dy == 0) ? dstRow0 + col * 4 : dstRow1 + col * 4;
				const unsigned char* yPixel = (dy == 0) ? yRow0 + col : yRow1 + col;

				for (int dx = 0; dx < 2 && (col + dx) < width; dx++) {
					uint8 y = yPixel[dx];
					int32 yVal = yTable[y];

					// BGRA output
					dstPixel[dx * 4 + 0] = clamp255((yVal + uB + 128) >> 8);      // B
					dstPixel[dx * 4 + 1] = clamp255((yVal + uG + vG + 128) >> 8); // G
					dstPixel[dx * 4 + 2] = clamp255((yVal + vR + 128) >> 8);      // R
					dstPixel[dx * 4 + 3] = 255;                                    // A
				}
			}
		}
	}
}


// FIX BUG 6: Contatori MJPEG ora sono membri di istanza (vedi header)

void
UVCCamDevice::_DecompressMJPEGtoRGB32(unsigned char* dst,
                                       const unsigned char* src,
                                       size_t srcSize,
                                       int32 width, int32 height)
{
	fMjpegAttempts++;

	if (!fJpegDecompressor || !dst || !src || srcSize == 0 || width <= 0 || height <= 0)
		return;

	// Find JPEG SOI marker (0xFF 0xD8) - UVC may have header before JPEG data
	const unsigned char* jpegStart = src;
	size_t jpegSize = srcSize;
	size_t scanLimit = srcSize < 2048 ? srcSize : 2048;

	for (size_t i = 0; i < scanLimit - 1; i++) {
		if (src[i] == 0xFF && src[i + 1] == 0xD8) {
			jpegStart = src + i;
			jpegSize = srcSize - i;
			break;
		}
	}

	// Verify we found JPEG data
	if (jpegStart[0] != 0xFF || jpegStart[1] != 0xD8) {
		fMjpegNoSOI++;
		// Log first few failures and then periodically
		if (fMjpegNoSOI <= 5 || (fMjpegNoSOI % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: No SOI marker #%d, srcSize=%zu, first bytes=[%02x %02x %02x %02x]\n",
				(int)fMjpegNoSOI, srcSize,
				srcSize > 0 ? src[0] : 0, srcSize > 1 ? src[1] : 0,
				srcSize > 2 ? src[2] : 0, srcSize > 3 ? src[3] : 0);
		}
		return;
	}

	/* Task 5: Enhanced MJPEG decompression for various resolutions */
	/* First, get actual JPEG dimensions from header */
	int jpegWidth = 0, jpegHeight = 0, jpegSubsamp = 0, jpegColorspace = 0;
	int headerResult = tjDecompressHeader3(fJpegDecompressor, jpegStart, jpegSize,
		&jpegWidth, &jpegHeight, &jpegSubsamp, &jpegColorspace);

	if (headerResult != 0) {
		fMjpegDecompressErrors++;
		if (fMjpegDecompressErrors <= 5 || (fMjpegDecompressErrors % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: Header decode failed #%d: %s\n",
				(int)fMjpegDecompressErrors, tjGetErrorStr2(fJpegDecompressor));
		}
		return;
	}

	/* Warn if JPEG dimensions don't match expected output */
	if (jpegWidth != width || jpegHeight != height) {
		/* Check if we're in resolution transition grace period (500ms after change) */
		bigtime_t now = system_time();
		bool inTransition = (fResolutionTransitionStart > 0) &&
		                    (now - fResolutionTransitionStart < 500000);

		if (inTransition) {
			/* Silently skip frames during transition - camera is still switching */
			static int32 sTransitionSkipped = 0;
			if (++sTransitionSkipped <= 3) {
				syslog(LOG_INFO, "MJPEG: Skipping transition frame #%d (JPEG=%dx%d, expected=%dx%d)\n",
					(int)sTransitionSkipped, jpegWidth, jpegHeight, (int)width, (int)height);
			}
			return;
		}

		static int32 sDimensionMismatch = 0;
		if (++sDimensionMismatch <= 5 || (sDimensionMismatch % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: Dimension mismatch #%d: JPEG=%dx%d, expected=%dx%d\n",
				(int)sDimensionMismatch, jpegWidth, jpegHeight, (int)width, (int)height);
		}
		/* If JPEG is larger than buffer, we cannot decompress - skip frame */
		if (jpegWidth > width || jpegHeight > height) {
			syslog(LOG_ERR, "MJPEG: JPEG too large for buffer: JPEG=%dx%d, buffer=%dx%d, skipping\n",
				jpegWidth, jpegHeight, (int)width, (int)height);
			return;
		}
	}

	/* FIX: Use actual JPEG dimensions for decompression, not expected dimensions.
	 * When JPEG dimensions differ from expected, using expected width for pitch
	 * causes horizontal offset/stair-step artifacts because TurboJPEG writes
	 * jpegWidth pixels per row but the pitch assumes width pixels per row.
	 *
	 * Strategy: Decompress to actual JPEG dimensions with matching pitch.
	 * If JPEG is smaller, it will appear in top-left corner on the dark blue
	 * background (buffer was pre-filled in FillFrameBuffer).
	 */
	int decompressWidth = jpegWidth;
	int decompressHeight = jpegHeight;
	int decompressPitch = decompressWidth * 4;

	// If JPEG matches expected, use expected dimensions (normal case)
	if (jpegWidth == width && jpegHeight == height) {
		decompressWidth = width;
		decompressHeight = height;
		decompressPitch = width * 4;
	}

	// Decompress directly to BGRA (RGB32 on Haiku)
	int result = tjDecompress2(fJpegDecompressor, jpegStart, jpegSize, dst,
	              decompressWidth, decompressPitch, decompressHeight, TJPF_BGRA, TJFLAG_FASTDCT);

	if (result == 0) {
		fMjpegSuccess++;

		/* Clear resolution transition state on first successful frame */
		if (fResolutionTransitionStart > 0) {
			bigtime_t transitionDuration = system_time() - fResolutionTransitionStart;
			syslog(LOG_INFO, "MJPEG: Resolution transition complete after %lld ms, first valid %dx%d frame\n",
				transitionDuration / 1000, (int)width, (int)height);
			fResolutionTransitionStart = 0;
		}

		/* Log periodic success stats at high resolutions */
		if (width >= 1280 && (fMjpegSuccess % 300) == 0) {
			syslog(LOG_INFO, "MJPEG %dx%d: %d frames decoded (errors: %d, no SOI: %d)\n",
				(int)width, (int)height, (int)fMjpegSuccess,
				(int)fMjpegDecompressErrors, (int)fMjpegNoSOI);
		}
	} else {
		fMjpegDecompressErrors++;
		if (fMjpegDecompressErrors <= 5 || (fMjpegDecompressErrors % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: Decompress failed #%d at %dx%d: %s (src=%zu bytes)\n",
				(int)fMjpegDecompressErrors, (int)width, (int)height,
				tjGetErrorStr2(fJpegDecompressor), jpegSize);
		}
	}
}


// =============================================================================
// Feature 1: Frame Validation Methods
// =============================================================================


frame_validation_result
UVCCamDevice::_ValidateMJPEGFrame(const uint8* data, size_t size)
{
	// Check minimum size
	if (size < kMinMJPEGFrameSize) {
		return FRAME_CORRUPTED_TRUNCATED;
	}

	// Check for SOI marker (0xFF 0xD8) at start
	if (data[0] != 0xFF || data[1] != 0xD8) {
		return FRAME_CORRUPTED_NO_SOI;
	}

	// Check for EOI marker (0xFF 0xD9) near end
	// Search in last 32 bytes for robustness
	bool foundEOI = false;
	size_t searchStart = (size > 32) ? size - 32 : 0;
	for (size_t i = searchStart; i < size - 1; i++) {
		if (data[i] == 0xFF && data[i + 1] == 0xD9) {
			foundEOI = true;
			break;
		}
	}

	if (!foundEOI) {
		return FRAME_CORRUPTED_NO_EOI;
	}

	return FRAME_VALID;
}


frame_validation_result
UVCCamDevice::_ValidateYUY2Frame(const uint8* data, size_t size,
	int32 width, int32 height)
{
	(void)data;  // Unused for now, just size check
	size_t expectedSize = (size_t)width * height * 2;  // YUY2 is 2 bytes per pixel

	if (size < (expectedSize * kMinYUY2FramePercent / 100)) {
		return FRAME_INCOMPLETE;
	}

	return FRAME_VALID;
}


bool
UVCCamDevice::_FindJpegMarker(const uint8* data, size_t size,
	uint8 marker, size_t* position)
{
	for (size_t i = 0; i < size - 1; i++) {
		if (data[i] == 0xFF && data[i + 1] == marker) {
			if (position)
				*position = i;
			return true;
		}
	}
	return false;
}


void
UVCCamDevice::_CacheValidFrame(const uint8* data, size_t size,
	int32 width, int32 height)
{
	// Reallocate if size changed
	if (fLastValidFrame == NULL || fLastValidFrameSize < size) {
		delete[] fLastValidFrame;
		fLastValidFrame = new(std::nothrow) uint8[size];
		if (fLastValidFrame == NULL) {
			fLastValidFrameSize = 0;
			return;
		}
	}

	memcpy(fLastValidFrame, data, size);
	fLastValidFrameSize = size;
	fLastValidWidth = width;
	fLastValidHeight = height;
	fValidationStats.last_valid_frame_time = system_time();
}


bool
UVCCamDevice::_UseLastValidFrame(uint8* dst, size_t dstSize)
{
	if (fLastValidFrame == NULL || fLastValidFrameSize == 0) {
		return false;
	}

	// For RGB32 output, we need to decompress/convert the cached frame
	// This simplified version just copies if dst is large enough
	if (dstSize >= fLastValidFrameSize) {
		memcpy(dst, fLastValidFrame, fLastValidFrameSize);
		fValidationStats.frames_repeated++;
		return true;
	}

	return false;
}


void
UVCCamDevice::_ReportValidationStats()
{
	bigtime_t now = system_time();

	// Report every kFrameValidationReportInterval seconds if there are errors
	if ((now - fValidationStats.last_stats_report_time) <
		(bigtime_t)kFrameValidationReportInterval * 1000000) {
		return;
	}

	fValidationStats.last_stats_report_time = now;

	uint32 totalErrors = fValidationStats.frames_incomplete +
		fValidationStats.frames_no_soi + fValidationStats.frames_no_eoi;

	if (totalErrors > 0) {
		syslog(LOG_INFO, "UVCCamDevice: Frame validation - valid: %u, "
			"incomplete: %u, no_soi: %u, no_eoi: %u, repeated: %u\n",
			fValidationStats.frames_valid,
			fValidationStats.frames_incomplete,
			fValidationStats.frames_no_soi,
			fValidationStats.frames_no_eoi,
			fValidationStats.frames_repeated);
	}
}


// =============================================================================
// Feature 2: Camera Control Methods
// =============================================================================


status_t
UVCCamDevice::_ProbeControlRange(uint16 selector, camera_control_info* info)
{
	if (info == NULL || fProcessingUnitID == 0) {
		return B_BAD_VALUE;
	}

	ssize_t result;
	int16 value;

	// GET_MIN
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_MIN,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->min_value = value;
	} else {
		info->min_value = 0;
	}

	// GET_MAX
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_MAX,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->max_value = value;
	} else {
		info->max_value = 100;
	}

	// GET_DEF
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_DEF,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->default_value = value;
	} else {
		info->default_value = (info->min_value + info->max_value) / 2;
	}

	// GET_RES (resolution/step)
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_RES,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->resolution = (uint16)value;
	} else {
		info->resolution = 1;
	}

	// GET_CUR
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_CUR,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->current_value = value;
	} else {
		info->current_value = info->default_value;
	}

	return B_OK;
}


status_t
UVCCamDevice::_InitializeProcessingControls()
{
	if (fControlsInitialized) {
		return B_OK;
	}

	// Note: fProcessingUnitID should be set during _ParseVideoControl
	// For now we skip if it's not set
	if (fProcessingUnitID == 0) {
		syslog(LOG_INFO, "UVCCamDevice: No Processing Unit found, skipping controls init\n");
		fControlsInitialized = true;
		return B_OK;
	}

	syslog(LOG_INFO, "UVCCamDevice: Initializing processing controls for unit %u\n",
		fProcessingUnitID);

	fControlsInitialized = true;
	return B_OK;
}


void
UVCCamDevice::_AddProcessingControls(BParameterGroup* group, int32& index)
{
	(void)group;  // Will be used when adding parameters
	(void)index;

	if (!fControlsInitialized) {
		_InitializeProcessingControls();
	}

	// Controls are added via the existing _AddProcessingParameter mechanism
	// This method is a placeholder for future expansion
}


status_t
UVCCamDevice::_GetControlValue(uint16 selector, int16* value)
{
	if (value == NULL || fProcessingUnitID == 0) {
		return B_BAD_VALUE;
	}

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_CUR,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(*value), value);

	return (result >= 0) ? B_OK : (status_t)result;
}


status_t
UVCCamDevice::_SetControlValue(uint16 selector, int16 value)
{
	if (fProcessingUnitID == 0) {
		return B_BAD_VALUE;
	}

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_SET_CUR,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);

	return (result >= 0) ? B_OK : (status_t)result;
}


// =============================================================================
// Camera Terminal (CT) Control Methods
// =============================================================================

status_t
UVCCamDevice::_GetCTControlValue(uint16 selector, void* value, size_t size)
{
	if (value == NULL || !fHasCameraTerminal || fCameraTerminalID == 0) {
		return B_BAD_VALUE;
	}

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_CUR,
		selector << 8,
		(fCameraTerminalID << 8) | fControlIndex,
		size, value);

	return (result >= 0) ? B_OK : (status_t)result;
}


status_t
UVCCamDevice::_SetCTControlValue(uint16 selector, const void* value, size_t size)
{
	if (value == NULL || !fHasCameraTerminal || fCameraTerminalID == 0) {
		return B_BAD_VALUE;
	}

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_SET_CUR,
		selector << 8,
		(fCameraTerminalID << 8) | fControlIndex,
		size, (void*)value);

	return (result >= 0) ? B_OK : (status_t)result;
}


void
UVCCamDevice::_AddCameraTerminalControls(BParameterGroup* group, int32& index)
{
	if (!fHasCameraTerminal || fCameraTerminalControls == 0)
		return;

	BParameterGroup* ctGroup = group->MakeGroup("Camera Controls");

	// Auto Exposure Mode (selector 0x02)
	if (fCameraTerminalControls & (1 << 1)) {
		BDiscreteParameter* aeMode = ctGroup->MakeDiscreteParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Auto Exposure",
			B_ENABLE);
		if (aeMode) {
			aeMode->AddItem(1, "Manual");
			aeMode->AddItem(2, "Auto");
			aeMode->AddItem(4, "Shutter Priority");
			aeMode->AddItem(8, "Aperture Priority");
			fAutoExposureModeID = index - 1;

			// Read current value from camera
			uint8 mode = 2;
			if (_GetCTControlValue(USB_VIDEO_CT_AE_MODE_CONTROL, &mode, 1) == B_OK) {
				fAutoExposureMode = mode;
			}
		}
	}

	// Exposure Time Absolute (selector 0x04) - value in 100μs units
	if (fCameraTerminalControls & (1 << 3)) {
		BContinuousParameter* exposure = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Exposure Time",
			B_GAIN,
			"ms",
			0.1f,      // min: 0.01ms (100μs)
			1000.0f,   // max: 100ms
			0.1f);     // step
		if (exposure) {
			fExposureTimeID = index - 1;

			// Read current value from camera (4 bytes, 100μs units)
			uint32 expTime = 333;
			if (_GetCTControlValue(USB_VIDEO_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL,
					&expTime, 4) == B_OK) {
				fExposureTimeAbs = expTime;
			}
		}
	}

	// Focus Auto (selector 0x08) - checkbox
	if (fCameraTerminalControls & (1 << 17)) {
		BDiscreteParameter* focusAuto = ctGroup->MakeDiscreteParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Auto Focus",
			B_ENABLE);
		if (focusAuto) {
			focusAuto->AddItem(0, "Off");
			focusAuto->AddItem(1, "On");
			fAutoFocusID = index - 1;

			// Read current value from camera
			uint8 autoFocus = 1;
			if (_GetCTControlValue(USB_VIDEO_CT_FOCUS_AUTO_CONTROL, &autoFocus, 1) == B_OK) {
				fAutoFocus = (autoFocus != 0);
			}
		}
	}

	// Focus Absolute (selector 0x06) - slider, 2 bytes
	if (fCameraTerminalControls & (1 << 5)) {
		BContinuousParameter* focus = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Focus",
			B_GAIN,
			"",
			0.0f,      // min
			255.0f,    // max (typical range, actual may vary)
			1.0f);     // step
		if (focus) {
			fFocusAbsoluteID = index - 1;

			// Read current value from camera (2 bytes)
			uint16 focusVal = 0;
			if (_GetCTControlValue(USB_VIDEO_CT_FOCUS_ABSOLUTE_CONTROL,
					&focusVal, 2) == B_OK) {
				fFocusAbsolute = focusVal;
			}
		}
	}

	// Zoom Absolute (selector 0x0B) - slider, 2 bytes
	if (fCameraTerminalControls & (1 << 9)) {
		BContinuousParameter* zoom = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Zoom",
			B_GAIN,
			"x",
			1.0f,      // min: 1x
			10.0f,     // max: 10x (typical range)
			0.1f);     // step
		if (zoom) {
			fZoomAbsoluteID = index - 1;

			// Read current value from camera (2 bytes)
			uint16 zoomVal = 100;
			if (_GetCTControlValue(USB_VIDEO_CT_ZOOM_ABSOLUTE_CONTROL,
					&zoomVal, 2) == B_OK) {
				fZoomAbsolute = zoomVal;
			}
		}
	}

	// Pan/Tilt Absolute (selector 0x0D) - compound control, 8 bytes (pan + tilt)
	// Pan and Tilt values are in arc-seconds (1/3600 of a degree)
	if (fCameraTerminalControls & (1 << 11)) {
		// Pan control (first 4 bytes)
		BContinuousParameter* pan = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Pan",
			B_GAIN,
			"°",
			-180.0f,   // min: -180 degrees
			180.0f,    // max: +180 degrees
			1.0f);     // step: 1 degree
		if (pan) {
			fPanTiltID = index - 1;  // Store first ID for the compound control
		}

		// Tilt control (last 4 bytes)
		BContinuousParameter* tilt = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Tilt",
			B_GAIN,
			"°",
			-180.0f,   // min: -180 degrees
			180.0f,    // max: +180 degrees
			1.0f);     // step: 1 degree

		// Read current values from camera (8 bytes total)
		if (pan || tilt) {
			struct {
				int32 pan;
				int32 tilt;
			} panTilt = { 0, 0 };
			if (_GetCTControlValue(USB_VIDEO_CT_PANTILT_ABSOLUTE_CONTROL,
					&panTilt, 8) == B_OK) {
				fPanAbsolute = panTilt.pan;
				fTiltAbsolute = panTilt.tilt;
			}
		}
	}

	printf("UVCCamDevice: Added Camera Terminal controls\n");
}


// =============================================================================
// Extension Unit Methods (XU) - Vendor-Specific Features
// =============================================================================


void
UVCCamDevice::_ParseExtensionUnit(
	const usb_video_extension_unit_descriptor* descriptor)
{
	// Create extension unit info structure
	extension_unit_info* xu = new extension_unit_info;
	memset(xu, 0, sizeof(extension_unit_info));

	// Copy basic info
	xu->unit_id = descriptor->unit_id;
	memcpy(xu->guid, descriptor->guid_extension_code, 16);
	xu->num_controls = descriptor->num_controls;
	xu->num_input_pins = descriptor->num_input_pins;

	// Copy source IDs (up to 8)
	uint8 pinCount = (descriptor->num_input_pins < 8)
		? descriptor->num_input_pins : 8;
	for (uint8 i = 0; i < pinCount; i++) {
		xu->source_ids[i] = descriptor->source_id[i];
	}

	// Get description string from device
	const char* desc = fDevice->DecodeStringDescriptor(descriptor->Extension());
	if (desc != NULL) {
		strncpy(xu->description, desc, sizeof(xu->description) - 1);
		xu->description[sizeof(xu->description) - 1] = '\0';
	}

	// Identify vendor from GUID
	xu->vendor = _IdentifyXUVendor(xu->guid);
	xu->vendor_name = _GetXUVendorName(xu->vendor);
	xu->capabilities = _GetXUCapabilities(xu->vendor);

	// Store the extension unit
	fExtensionUnits.AddItem(xu);
	fHasExtensionUnits = true;

	// Log the extension unit
	syslog(LOG_INFO, "UVCCamDevice: XU unit_id=%d vendor=%s controls=%d guid=%02x%02x%02x%02x\n",
		xu->unit_id, xu->vendor_name, xu->num_controls,
		xu->guid[0], xu->guid[1], xu->guid[2], xu->guid[3]);
	printf("VC_EXTENSION_UNIT:\tid=%d, vendor=%s\n", xu->unit_id, xu->vendor_name);
	printf("\tGUID: ");
	for (int i = 0; i < 16; i++) {
		printf("%02x", xu->guid[i]);
		if (i == 3 || i == 5 || i == 7 || i == 9)
			printf("-");
	}
	printf("\n\t#ctrls=%d, #pins=%d\n", xu->num_controls, xu->num_input_pins);
	if (xu->description[0] != '\0')
		printf("\tDesc: %s\n", xu->description);

	// Log capabilities if known vendor
	if (xu->capabilities != XU_CAP_NONE) {
		printf("\tCapabilities:");
		if (xu->capabilities & XU_CAP_LED_CONTROL)
			printf(" LED");
		if (xu->capabilities & XU_CAP_FACE_DETECTION)
			printf(" FaceDetect");
		if (xu->capabilities & XU_CAP_HDR)
			printf(" HDR");
		if (xu->capabilities & XU_CAP_NOISE_REDUCTION)
			printf(" NoiseReduction");
		if (xu->capabilities & XU_CAP_H264_ENCODING)
			printf(" H264");
		if (xu->capabilities & XU_CAP_PTZ_CONTROL)
			printf(" PTZ");
		printf("\n");
	}
}


extension_unit_vendor
UVCCamDevice::_IdentifyXUVendor(const uint8* guid)
{
	if (memcmp(guid, kMicrosoftH264XUGUID, 16) == 0)
		return XU_VENDOR_MICROSOFT;
	if (memcmp(guid, kSonixXUGUID, 16) == 0
		|| memcmp(guid, kSonixSysHWGUID, 16) == 0
		|| memcmp(guid, kSonixUsrHWGUID, 16) == 0)
		return XU_VENDOR_SONIX;
	if (memcmp(guid, kLogitechXUGUID, 16) == 0)
		return XU_VENDOR_LOGITECH;
	if (memcmp(guid, kRealtekXUGUID, 16) == 0)
		return XU_VENDOR_REALTEK;
	return XU_VENDOR_UNKNOWN;
}


uint32
UVCCamDevice::_GetXUCapabilities(extension_unit_vendor vendor)
{
	switch (vendor) {
		case XU_VENDOR_MICROSOFT:
			return XU_CAP_H264_ENCODING;
		case XU_VENDOR_SONIX:
			return XU_CAP_LED_CONTROL | XU_CAP_FACE_DETECTION;
		case XU_VENDOR_LOGITECH:
			return XU_CAP_LED_CONTROL | XU_CAP_PTZ_CONTROL | XU_CAP_H264_ENCODING;
		case XU_VENDOR_REALTEK:
			return XU_CAP_HDR | XU_CAP_NOISE_REDUCTION;
		default:
			return XU_CAP_NONE;
	}
}


const char*
UVCCamDevice::_GetXUVendorName(extension_unit_vendor vendor)
{
	switch (vendor) {
		case XU_VENDOR_MICROSOFT:
			return "Microsoft";
		case XU_VENDOR_SONIX:
			return "Sonix";
		case XU_VENDOR_LOGITECH:
			return "Logitech";
		case XU_VENDOR_REALTEK:
			return "Realtek";
		default:
			return "Unknown";
	}
}


void
UVCCamDevice::_LogExtensionUnits()
{
	if (!fHasExtensionUnits || fExtensionUnits.CountItems() == 0) {
		printf("UVCCamDevice: No Extension Units detected\n");
		return;
	}

	printf("UVCCamDevice: %d Extension Unit(s) detected:\n",
		fExtensionUnits.CountItems());

	for (int32 i = 0; i < fExtensionUnits.CountItems(); i++) {
		extension_unit_info* xu = (extension_unit_info*)fExtensionUnits.ItemAt(i);
		printf("  [%d] ID=%d Vendor=%s Controls=%d",
			i + 1, xu->unit_id, xu->vendor_name, xu->num_controls);
		if (xu->description[0] != '\0')
			printf(" (%s)", xu->description);
		printf("\n");
	}
}


// =============================================================================
// Still Image Capture Methods
// =============================================================================


void
UVCCamDevice::_ParseStillImageFrame(
	const usb_video_still_image_frame_descriptor* descriptor)
{
	// Store still image endpoint
	fStillImageInfo.endpoint_address = descriptor->endpoint_address;

	// Store still image sizes
	fStillImageInfo.num_sizes = (descriptor->num_image_size_patterns < 16)
		? descriptor->num_image_size_patterns : 16;
	for (uint8 i = 0; i < fStillImageInfo.num_sizes; i++) {
		fStillImageInfo.sizes[i].width = descriptor->_pattern_size[i].width;
		fStillImageInfo.sizes[i].height = descriptor->_pattern_size[i].height;
	}

	// Store compression patterns
	fStillImageInfo.num_compressions = (descriptor->NumCompressionPatterns() < 8)
		? descriptor->NumCompressionPatterns() : 8;
	for (uint8 i = 0; i < fStillImageInfo.num_compressions; i++) {
		fStillImageInfo.compressions[i] = descriptor->CompressionPatterns()[i];
	}

	// Mark still capture as available
	fHasStillCapture = true;

	// Log still image info
	printf("VS_STILL_IMAGE_FRAME:\t#imageSizes=%d, #compressions=%d, ept=0x%x\n",
		fStillImageInfo.num_sizes, fStillImageInfo.num_compressions,
		fStillImageInfo.endpoint_address);

	for (uint8 i = 0; i < fStillImageInfo.num_sizes; i++) {
		printf("\tstill size %d: %dx%d\n", i,
			fStillImageInfo.sizes[i].width, fStillImageInfo.sizes[i].height);
	}
}


void
UVCCamDevice::_LogStillImageCapabilities()
{
	if (!fHasStillCapture && fStillCaptureMethod == STILL_CAPTURE_NONE) {
		printf("UVCCamDevice: Still image capture not supported\n");
		return;
	}

	printf("UVCCamDevice: Still Image Capture Capabilities:\n");
	printf("  Capture Method: %s\n", _GetStillCaptureMethodName(fStillCaptureMethod));

	if (fTriggerSupport) {
		printf("  Hardware Trigger: Yes (%s)\n",
			fTriggerUsage ? "general purpose" : "fixed to still capture");
	}

	if (fHasStillCapture && fStillImageInfo.num_sizes > 0) {
		printf("  Endpoint: 0x%02x\n", fStillImageInfo.endpoint_address);
		printf("  Available Still Resolutions:\n");
		for (uint8 i = 0; i < fStillImageInfo.num_sizes; i++) {
			printf("    [%d] %dx%d\n", i,
				fStillImageInfo.sizes[i].width, fStillImageInfo.sizes[i].height);
		}
	}
}


const char*
UVCCamDevice::_GetStillCaptureMethodName(still_capture_method method)
{
	switch (method) {
		case STILL_CAPTURE_NONE:
			return "None";
		case STILL_CAPTURE_METHOD_1:
			return "Method 1 (Dedicated Button)";
		case STILL_CAPTURE_METHOD_2:
			return "Method 2 (Host Software Triggered)";
		case STILL_CAPTURE_METHOD_3:
			return "Method 3 (Dedicated Pipe + Button)";
		default:
			return "Unknown";
	}
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
	// This is safe because we're in the reconfig thread, not the data pump
	if (wasTransferring) {
		syslog(LOG_INFO, "UVCCamDevice: Stopping transfer for resolution change\n");
		result = StopTransfer();
		if (result != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Failed to stop transfer: %s\n",
				strerror(result));
			return result;
		}

		// Give the hardware a moment to settle
		snooze(50000);  // 50ms
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

	// Step 4: Restart transfer if it was running
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
		syslog(LOG_INFO, "UVC: MJPEG resolutions sorted by size (count=%d):\n", mjpegCount);
		for (int32 i = 0; i < mjpegCount; i++) {
			int32 sortedIdx = fSortedMJPEGIndices[i];
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)fMJPEGFrames.ItemAt(sortedIdx);
			if (desc) {
				syslog(LOG_INFO, "UVCCamDevice:   [%d] %ux%u frame_index=%u\n",
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
		syslog(LOG_INFO, "UVC: Uncompressed resolutions sorted by size (count=%d):\n", uncompCount);
		for (int32 i = 0; i < uncompCount; i++) {
			int32 sortedIdx = fSortedUncompressedIndices[i];
			usb_video_frame_descriptor* desc =
				(usb_video_frame_descriptor*)fUncompressedFrames.ItemAt(sortedIdx);
			if (desc) {
				syslog(LOG_INFO, "UVCCamDevice:   [%d] %ux%u frame_index=%u\n",
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

		// Request stream restart with low-bandwidth alternate
		// The next StartTransfer() will use _SelectBestAlternate() which will
		// now avoid high-bandwidth endpoints
		syslog(LOG_INFO, "UVCCamDevice: Will use low-bandwidth mode on next stream restart\n");
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
	printf("UVCCamDeviceAddon::UVCCamDeviceAddon(WebCamMediaAddOn* webcam)\n");
	SetSupportedDevices(kSupportedDevices);
}


UVCCamDeviceAddon::~UVCCamDeviceAddon()
{
}


const char *
UVCCamDeviceAddon::BrandName()
{
	printf("UVCCamDeviceAddon::BrandName()\n");
	return "USB Video Class";
}


UVCCamDevice *
UVCCamDeviceAddon::Instantiate(CamRoster& roster, BUSBDevice* from)
{
	printf("UVCCamDeviceAddon::Instantiate()\n");
	return new UVCCamDevice(*this, from);
}


extern "C" status_t
B_WEBCAM_MKINTFUNC(uvccam)
(WebCamMediaAddOn* webcam, CamDeviceAddon **addon)
{
	*addon = new UVCCamDeviceAddon(webcam);
	return B_OK;
}
