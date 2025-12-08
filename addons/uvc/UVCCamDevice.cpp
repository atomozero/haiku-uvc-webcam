/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2009, Ithamar Adema, <ithamar.adema@team-embedded.nl>.
 * Distributed under the terms of the MIT License.
 */


#include "UVCCamDevice.h"
#include "UVCDeframer.h"
#include "CamDebug.h"

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
	{{ 0, 0, 0, 0x0c45, 0x6409, }, "Microdia",      "Motion Eye",                      "??" },
	{{ 0, 0, 0, 0x1bcf, 0x0001, }, "AUKEY",         "PC-LM1E",                         "??" },
	{{ 0, 0, 0, 0x045e, 0x00f8, }, "Microsoft",     "Lifecam NX-6000",                 "??" },
	{{ 0, 0, 0, 0x045e, 0x0723, }, "Microsoft",     "Lifecam VX-7000",                 "??" },
	{{ 0, 0, 0, 0x046d, 0x08c1, }, "Logitech",      "QuickCam Fusion",                 "??" },
	{{ 0, 0, 0, 0x046d, 0x08c2, }, "Logitech",      "QuickCam Orbit MP",               "??" },
	{{ 0, 0, 0, 0x046d, 0x08c3, }, "Logitech",      "QuickCam Pro for Notebook",       "??" },
	{{ 0, 0, 0, 0x046d, 0x08c5, }, "Logitech",      "QuickCam Pro 5000",               "??" },
	{{ 0, 0, 0, 0x046d, 0x08c6, }, "Logitech",      "QuickCam OEM Dell Notebook",      "??" },
	{{ 0, 0, 0, 0x046d, 0x08c7, }, "Logitech",      "QuickCam OEM Cisco VT Camera II", "??" },
	{{ 0, 0, 0, 0x046d, 0x0821, }, "Logitech",      "HD Pro Webcam C910",              "??" },
	{{ 0, 0, 0, 0x05ac, 0x8501, }, "Apple",         "Built-In iSight",                 "??" },
	{{ 0, 0, 0, 0x05e3, 0x0505, }, "Genesys Logic", "USB 2.0 PC Camera",               "??" },
	{{ 0, 0, 0, 0x0e8d, 0x0004, }, "N/A",           "MT6227",                          "??" },
	{{ 0, 0, 0, 0x174f, 0x5212, }, "Syntek",        "(HP Spartan)",                    "??" },
	{{ 0, 0, 0, 0x174f, 0x5931, }, "Syntek",        "(Samsung Q310)",                  "??" },
	{{ 0, 0, 0, 0x174f, 0x8a31, }, "Syntek",        "Asus F9SG",                       "??" },
	{{ 0, 0, 0, 0x174f, 0x8a33, }, "Syntek",        "Asus U3S",                        "??" },
	{{ 0, 0, 0, 0x17ef, 0x480b, }, "N/A",           "Lenovo Thinkpad SL500",           "??" },
	{{ 0, 0, 0, 0x18cd, 0xcafe, }, "Ecamm",         "Pico iMage",                      "??" },
	{{ 0, 0, 0, 0x19ab, 0x1000, }, "Bodelin",       "ProScopeHR",                      "??" },
	{{ 0, 0, 0, 0x1c4f, 0x3000, }, "SiGma Micro",   "USB Web Camera",                  "??" },
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
	fJpegDecompressor(NULL),
	fIsMJPEG(false),
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
	// High-bandwidth auto-detection state
	fHighBandwidthTested(false),
	fHighBandwidthWorks(true),		// Assume it works until proven otherwise
	fHighBandwidthFailures(0),
	fUsingHighBandwidth(false),
	// MJPEG frame size monitoring
	fMJPEGFrameSizeSum(0),
	fMJPEGFrameSizeCount(0),
	fExpectedMJPEGMinSize(0),
	fLastFrameSizeCheck(0)
{
	// Initialize frame validation stats
	memset(&fValidationStats, 0, sizeof(fValidationStats));

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

				// First try base interface (alternate 0)
				uint32 descCount = 0;
				for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
					sizeof(buffer)) == B_OK; k++) {
					descCount++;
					printf("UVCCamDevice: VS descriptor %u: type=0x%02x, len=%u\n",
						k, generic->generic.descriptor_type, generic->generic.length);
					if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
						| USB_DESCRIPTOR_INTERFACE))
						continue;
					_ParseVideoStreaming((const usbvc_class_descriptor*)generic,
						generic->generic.length);
				}
				printf("UVCCamDevice: Found %u descriptors in base interface\n", descCount);

				// If no frames found, try alternates (some devices put descriptors there)
				if (fUncompressedFrames.CountItems() == 0 && fMJPEGFrames.CountItems() == 0) {
					printf("UVCCamDevice: No frames in base, checking alternates...\n");
					for (uint32 alt = 0; alt < interface->CountAlternates(); alt++) {
						const BUSBInterface* alternate = interface->AlternateAt(alt);
						if (alternate == NULL)
							continue;
						printf("UVCCamDevice: Checking alternate %u\n", alt);
						for (uint32 k = 0; alternate->OtherDescriptorAt(k, generic,
							sizeof(buffer)) == B_OK; k++) {
							printf("UVCCamDevice: Alt %u desc %u: type=0x%02x, len=%u\n",
								alt, k, generic->generic.descriptor_type, generic->generic.length);
							if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
								| USB_DESCRIPTOR_INTERFACE))
								continue;
							_ParseVideoStreaming((const usbvc_class_descriptor*)generic,
								generic->generic.length);
						}
						if (fUncompressedFrames.CountItems() > 0 || fMJPEGFrames.CountItems() > 0)
							break;
					}
				}

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
	}

	// Default to highest resolution (index 0) - high-bandwidth EHCI is now supported
	// The Microdia 0c45:6409 workaround is no longer needed with modified EHCI driver
	if (fUncompressedFrames.CountItems() > 0) {
		fSelectedResolutionIndex = 0;  // First frame is typically highest resolution
		usb_video_frame_descriptor* desc =
			(usb_video_frame_descriptor*)fUncompressedFrames.ItemAt(0);
		if (desc) {
			syslog(LOG_INFO, "UVCCamDevice: Default resolution set to %ux%u (index 0)\n",
				desc->width, desc->height);
		}
	}

	// Initialize TurboJPEG decompressor
	fJpegDecompressor = tjInitDecompress();
	/* FIX: Check if TurboJPEG initialization failed */
	if (fJpegDecompressor == NULL) {
		syslog(LOG_WARNING, "UVCCamDevice: tjInitDecompress failed - MJPEG disabled\n");
		/* Continue anyway - YUY2 format will still work */
	}

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

		syslog(LOG_INFO, "UVCCamDevice: Init OK - ctrl=%u stream=%u frames=%d+%d format=%s\n",
			fControlIndex, fStreamingIndex,
			(int)fUncompressedFrames.CountItems(), (int)fMJPEGFrames.CountItems(),
			fIsMJPEG ? "MJPEG" : "YUY2");
		syslog(LOG_INFO, "UVCCamDevice: Format indices: MJPEG=%d, Uncompressed=%d\n",
			fMJPEGFormatIndex, fUncompressedFormatIndex);
		// Log frame indices for each resolution
		BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc)
				syslog(LOG_INFO, "UVCCamDevice:   [%d] %ux%u frame_index=%u\n",
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
			printf("\tstill capture method=%d\n", descriptor->still_capture_method);
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
			printf("VS_STILL_IMAGE_FRAME:\t#imageSizes=%d,compressions=%d,"
				"ept=0x%x\n", descriptor->num_image_size_patterns,
				descriptor->NumCompressionPatterns(),
				descriptor->endpoint_address);
			for (uint8 i = 0; i < descriptor->num_image_size_patterns; i++) {
				printf("imageSize%d: %dx%d\n", i,
					descriptor->_pattern_size[i].width,
					descriptor->_pattern_size[i].height);
			}
			for (uint8 i = 0; i < descriptor->NumCompressionPatterns(); i++) {
				printf("compression%d: %d\n", i,
					descriptor->CompressionPatterns()[i]);
			}
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
			if (descriptor->terminalType == 0x201) {
				const usb_video_camera_terminal_descriptor* desc
					= (const usb_video_camera_terminal_descriptor*)descriptor;
				printf("\tObjectiveFocalLength Min/Max %d/%d\n",
					desc->objective_focal_length_min,
					desc->objective_focal_length_max);
				printf("\tOcularFocalLength %d\n", desc->ocular_focal_length);
				printf("\tControlSize %d\n", desc->control_size);
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
			printf("VC_EXTENSION_UNIT:\tid=%d, guid=", descriptor->unit_id);
			print_guid(descriptor->guid_extension_code);
			printf("\n\t#ctrls=%d, #pins=%d\n", descriptor->num_controls,
				descriptor->num_input_pins);
			printf("\t");
			for (uint8 i = 0; i < descriptor->num_input_pins; i++)
				printf("%d ", descriptor->source_id[i]);
			printf("\n");
			printf("\tDesc: %s\n",
				fDevice->DecodeStringDescriptor(descriptor->Extension()));
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
	_SelectIdleAlternate();
	return CamDevice::StopTransfer();
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

	// Task 2: Use the selected resolution index
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

	// First check if fIsMJPEG needs to be initialized
	if (fMJPEGFrames.CountItems() > 0)
		fIsMJPEG = true;
	else if (fUncompressedFrames.CountItems() > 0)
		fIsMJPEG = false;

	// Re-select the frame list after determining format
	frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

	// Use the selected resolution if available
	if (frameList->CountItems() > 0) {
		int32 index = fSelectedResolutionIndex;
		if (index < 0 || index >= frameList->CountItems())
			index = 0;

		const usb_video_frame_descriptor* descriptor
			= (const usb_video_frame_descriptor*)frameList->ItemAt(index);
		if (descriptor != NULL) {
			width  = descriptor->width;
			height = descriptor->height;
			printf("UVCCamDevice::SuggestVideoFrame: Using resolution index %d: %ux%u\n",
				(int)index, (unsigned)width, (unsigned)height);
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

	usb_video_probe_and_commit_controls request;
	memset(&request, 0, sizeof(request));
	request._hint.frame_interval = 1;

	/* FIX: Use frame interval from device descriptor instead of hardcoded 30fps */
	uint32 frameInterval = 333333;  // Default 30 fps
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	uint32 frameIndex = fIsMJPEG ? fMJPEGFrameIndex : fUncompressedFrameIndex;

	if (frameIndex > 0 && frameIndex <= (uint32)frameList->CountItems()) {
		const usb_video_frame_descriptor* frameDesc =
			(const usb_video_frame_descriptor*)frameList->ItemAt(frameIndex - 1);
		if (frameDesc != NULL) {
			/* Use default_frame_interval from descriptor */
			frameInterval = frameDesc->default_frame_interval;
			syslog(LOG_INFO, "UVCCamDevice: Using device frame interval %u (%.1f fps)\n",
				frameInterval, 10000000.0f / frameInterval);

			/* For YUY2 (uncompressed), adapt FPS to available bandwidth.
			 * High-bandwidth endpoints are now supported with modified EHCI.
			 * Calculate max achievable FPS and request that if lower than default.
			 */
			if (!fIsMJPEG) {
				uint32 maxBandwidth = _GetMaxAvailableBandwidth();
				if (maxBandwidth > 0) {
					uint32 frameSize = frameDesc->width * frameDesc->height * 2;
					// USB 2.0 high-speed: 8000 microframes/second
					uint32 bytesPerSecond = maxBandwidth * 8000;
					float maxFps = (float)bytesPerSecond / frameSize;

					// Calculate frame_interval for max achievable FPS
					// frame_interval is in 100ns units: 10000000 / fps
					// Use slightly lower fps for safety margin (90%)
					float safeFps = maxFps * 0.9f;
					if (safeFps < 1.0f) safeFps = 1.0f;  // Minimum 1 fps

					uint32 adaptedInterval = (uint32)(10000000.0f / safeFps);

					// If adapted interval is larger (slower fps), use it
					if (adaptedInterval > frameInterval) {
						syslog(LOG_INFO, "UVCCamDevice: YUY2 bandwidth check: max=%u bytes/uframe, frameSize=%u\n",
							maxBandwidth, frameSize);
						syslog(LOG_INFO, "UVCCamDevice: Adapting FPS: %.1f -> %.1f (interval %u -> %u)\n",
							10000000.0f / frameInterval, safeFps, frameInterval, adaptedInterval);
						frameInterval = adaptedInterval;
					} else {
						syslog(LOG_INFO, "UVCCamDevice: YUY2 bandwidth OK: max=%u bytes/uframe (%.1f MB/s), requesting %.1f fps\n",
							maxBandwidth, bytesPerSecond / 1048576.0f, 10000000.0f / frameInterval);
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

	size_t length = fHeaderDescriptor->version > 0x100 ? 34 : 26;

	// Log what we're requesting
	syslog(LOG_INFO, "UVC Probe request: format=%d frame=%d interval=%u (fIsMJPEG=%d)\n",
		request.format_index, request.frame_index, request.frame_interval, fIsMJPEG);
	syslog(LOG_INFO, "UVC Format indices: MJPEG=%d, Uncompressed=%d\n",
		fMJPEGFormatIndex, fUncompressedFormatIndex);

	// SET_CUR Probe
	size_t actualLength = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR,
		USB_VIDEO_VS_PROBE_CONTROL << 8, fStreamingIndex, length, &request);
	if (actualLength != length) {
		syslog(LOG_ERR, "UVC Probe SET_CUR failed: expected %zu, got %zu\n", length, actualLength);
		return B_ERROR;
	}

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

	// Select LARGEST bandwidth alternate for best quality
	uint32 bestBandwidth = 0;
	uint32 alternateIndex = 0;
	uint32 endpointIndex = 0;

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
			uint32 maxPacketSize = basePacketSize * transactions;

			syslog(LOG_INFO, "UVCCamDevice: Alt %u EP %u: raw=0x%04x base=%u trans=%u total=%u bytes\n",
				i, j, rawMaxPacketSize, basePacketSize, transactions, maxPacketSize);

			/* HIGH-BANDWIDTH ENDPOINT HANDLING:
			 *
			 * USB 2.0 high-bandwidth isochronous endpoints (mult=2 or mult=3) allow
			 * up to 3072 bytes per microframe. Modern XHCI controllers handle these
			 * correctly, but older EHCI controllers have bugs.
			 *
			 * Strategy: Auto-detect via _ShouldUseHighBandwidth() which:
			 * 1. Checks environment variable overrides
			 * 2. Uses cached result from previous attempts
			 * 3. Defaults to trying high-bandwidth (most systems are XHCI)
			 *
			 * If high-bandwidth fails, _OnHighBandwidthFailure() will disable it
			 * and the stream will restart with low-bandwidth mode.
			 */
			bool allowHighBandwidth = _ShouldUseHighBandwidth();

			if (transactions > 1 && !allowHighBandwidth) {
				syslog(LOG_INFO, "UVCCamDevice: Skipping high-bandwidth endpoint (mult=%u) - %s\n",
					transactions,
					(fHighBandwidthTested && !fHighBandwidthWorks) ? "EHCI detected" : "disabled");
				continue;  // Skip this endpoint
			}

			if (transactions > 1) {
				syslog(LOG_INFO, "UVCCamDevice: Trying high-bandwidth endpoint (mult=%u, %u bytes/uframe)\n",
					transactions, maxPacketSize);
			}

			// Use maxPacketSize (includes mult factor) for bandwidth comparison
			// This ensures high-bandwidth endpoints are properly considered
			uint32 effectiveBandwidth = (transactions > 1 && allowHighBandwidth) ? maxPacketSize : basePacketSize;

			if (effectiveBandwidth > bestBandwidth) {
				bestBandwidth = effectiveBandwidth;
				endpointIndex = j;
				alternateIndex = i;
			}
		}
	}

	/* Log bandwidth selection result */
	/* FIX BUG 9: Rimosso messaggio obsoleto - ora usiamo high-bandwidth */

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

	// WORKAROUND for Haiku bug in BUSBInterface::SetAlternate()
	// The bug causes a double-free when the number of endpoints changes between alternates.
	// See USBInterface_fix.patch for the proper fix to submit to Haiku.
	//
	// Mitigation strategy:
	// 1. Only call SetAlternate if truly necessary (already implemented above)
	// 2. If we must change, try to minimize the impact by staying on similar alternates
	// 3. Log extensively to help diagnose crashes
	if (fCurrentVideoAlternate != alternateIndex) {
		syslog(LOG_INFO, "UVCCamDevice: Changing alternate from %u to %u (Haiku bug risk!)\n",
			fCurrentVideoAlternate, alternateIndex);

		// Log endpoint counts to help diagnose the Haiku bug
		const BUSBInterface* oldAlt = streaming->AlternateAt(fCurrentVideoAlternate);
		const BUSBInterface* newAlt = streaming->AlternateAt(alternateIndex);
		if (oldAlt && newAlt) {
			syslog(LOG_INFO, "UVCCamDevice: Endpoint count: old=%u new=%u\n",
				(unsigned)oldAlt->CountEndpoints(), (unsigned)newAlt->CountEndpoints());
		}

		status_t setAltResult = ((BUSBInterface*)streaming)->SetAlternate(alternateIndex);
		if (setAltResult != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: SetAlternate(%u) failed: %s\n",
				alternateIndex, strerror(setAltResult));
			return B_ERROR;
		}

		syslog(LOG_INFO, "UVCCamDevice: SetAlternate(%u) successful\n", alternateIndex);
		fCurrentVideoAlternate = alternateIndex;

		streaming = config->InterfaceAt(fStreamingIndex);
		if (streaming == NULL)
			return B_BAD_INDEX;
	}

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
	// FIX: Proper LED control via direct USB SET_INTERFACE command
	//
	// The webcam LED is controlled by the USB streaming state:
	// - Alternate 0 = zero-bandwidth (LED off)
	// - Alternate N = active streaming (LED on)
	//
	// We bypass BUSBInterface::SetAlternate() which has a double-free bug
	// in Haiku's _UpdateDescriptorAndEndpoints(). Instead, we send the
	// USB SET_INTERFACE command directly via ControlTransfer.
	//
	// USB SET_INTERFACE request:
	// - bmRequestType: 0x01 (Host-to-device, Standard, Interface)
	// - bRequest: 0x0B (SET_INTERFACE)
	// - wValue: alternate setting (0 for idle/LED off)
	// - wIndex: interface number

	syslog(LOG_INFO, "UVCCamDevice: _SelectIdleAlternate - switching to alternate 0 (LED off)\n");

	if (fDevice != NULL && fCurrentVideoAlternate != 0) {
		// Send SET_INTERFACE directly to avoid Haiku's buggy SetAlternate()
		ssize_t result = fDevice->ControlTransfer(
			USB_REQTYPE_STANDARD | USB_REQTYPE_INTERFACE_OUT,  // 0x01
			USB_REQUEST_SET_INTERFACE,                          // 0x0B
			0,                                                  // wValue: alternate 0
			fStreamingIndex,                                    // wIndex: interface number
			0,                                                  // length
			NULL);                                              // no data

		if (result >= B_OK) {
			syslog(LOG_INFO, "UVCCamDevice: SET_INTERFACE(0) successful - LED should be off\n");
			fCurrentVideoAlternate = 0;
		} else {
			syslog(LOG_WARNING, "UVCCamDevice: SET_INTERFACE(0) failed: %s (LED may stay on)\n",
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

		syslog(LOG_INFO, "UVCCamDevice: Set USB sample rate to %d Hz (result=%d)\n",
			(int)sampleRate, (int)transferred);
	}

	// Allocate ring buffer for audio data (64KB)
	fAudioRingSize = 65536;
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

	// FIX BUG 4: Usare operazioni atomiche per il ring buffer
	// Wait for enough data (with timeout)
	int retries = 50;  // 50 x 1ms = 50ms max wait
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

	// Read up to 'size' bytes (or whatever is available)
	size_t toRead = (available < size) ? available : size;
	int32 tail = atomic_get(&fAudioRingTail);
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

	// Same Haiku bug workaround as video - see _SelectBestAlternate()
	if (fCurrentAudioAlternate != alternateIndex) {
		syslog(LOG_INFO, "UVCCamDevice: Audio changing alternate from %u to %u\n",
			(unsigned)fCurrentAudioAlternate, (unsigned)alternateIndex);

		status_t setAltResult = ((BUSBInterface*)streaming)->SetAlternate(
			alternateIndex);
		if (setAltResult != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Audio SetAlternate(%u) failed: %s\n",
				(unsigned)alternateIndex, strerror(setAltResult));
			return setAltResult;
		}

		syslog(LOG_INFO, "UVCCamDevice: Audio SetAlternate(%u) successful\n",
			(unsigned)alternateIndex);
		fCurrentAudioAlternate = alternateIndex;

		// Re-fetch interface after SetAlternate
		streaming = config->InterfaceAt(fAudioStreamingIndex);
		if (streaming == NULL) {
			syslog(LOG_ERR, "UVCCamDevice: Interface lost after SetAlternate\n");
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

	// WORKAROUND for Haiku bug in BUSBInterface::_UpdateDescriptorAndEndpoints()
	// Same issue as video - don't call SetAlternate(0) to avoid double-free crash.
	// See _SelectIdleAlternate() for detailed explanation.

	// Simply invalidate our references without calling SetAlternate(0)
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

	// Calculate expected bytes per packet based on sample rate
	// 48kHz * 2ch * 2bytes / 1000 frames = 192 bytes per frame (1ms)
	// For USB full-speed: 1 packet per frame = 192 bytes max
	uint32 bytesPerPacket = (fAudioSampleRate * fAudioChannels * 2) / 1000;
	if (bytesPerPacket == 0)
		bytesPerPacket = 192;  // fallback for 48kHz stereo
	if (bytesPerPacket > fAudioMaxPacketSize)
		bytesPerPacket = fAudioMaxPacketSize;

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

			// Log persistent errors (but throttle logging)
			if (consecutiveErrors == 10 || consecutiveErrors == 100) {
				syslog(LOG_WARNING,
					"UVCCamDevice: Audio transfer errors: %u consecutive\n",
					(unsigned)consecutiveErrors);
			}

			snooze(currentBackoff);
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

			// Calculate space in ring buffer
			int32 head = atomic_get(&fAudioRingHead);
			int32 tail = atomic_get(&fAudioRingTail);
			size_t space;

			if (head >= tail)
				space = fAudioRingSize - head + tail - 1;
			else
				space = tail - head - 1;

			if (space < packetLen) {
				// Ring buffer overflow - data lost
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
	printf("UVCCamDevice::AddParameters()\n");
	fFirstParameterID = index;
//	debug_printf("fIndex = %d\n",fIndex);
	CamDevice::AddParameters(group, index);

	/* Add Video Format/Resolution info group */
	BParameterGroup* videoGroup = group->MakeGroup("Video Format");

	/* Add format info as text (read-only) */
	BString formatInfo;
	if (fIsMJPEG) {
		formatInfo = "Format: MJPEG (compressed)";
	} else {
		formatInfo = "Format: YUY2 (uncompressed)";
	}

	/* Add format type as text */
	videoGroup->MakeTextParameter(index + 15, B_MEDIA_RAW_VIDEO,
		formatInfo.String(), "Format", 64);

	/* Add resolution selector as discrete parameter (Task 2) */
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	if (frameList->CountItems() > 0) {
		fResolutionParameterID = index + 14;  // Store parameter ID for later

		BDiscreteParameter* resParam = videoGroup->MakeDiscreteParameter(
			fResolutionParameterID, B_MEDIA_RAW_VIDEO, "Resolution", B_RESOLUTION);

		/* Add each available resolution as an option */
		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* frameDesc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (frameDesc != NULL) {
				BString resName;
				resName << frameDesc->width << "x" << frameDesc->height;
				if (frameDesc->default_frame_interval > 0) {
					float fps = 10000000.0f / frameDesc->default_frame_interval;
					resName << " @ " << (int)fps << " fps";
				}
				resParam->AddItem(i, resName.String());
			}
		}

		/* Ensure selected index is valid */
		if (fSelectedResolutionIndex >= frameList->CountItems()) {
			fSelectedResolutionIndex = 0;
		}

		printf("UVCCamDevice: Added resolution selector with %d options, current=%d\n",
			(int)frameList->CountItems(), (int)fSelectedResolutionIndex);
	}

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
			if (interface->Class() != USB_VIDEO_DEVICE_CLASS || interface->Subclass()
				!= USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS)
				continue;
			for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
				sizeof(buffer)) == B_OK; k++) {
				if (generic->generic.descriptor_type != (USB_REQTYPE_CLASS
					| USB_DESCRIPTOR_INTERFACE))
					continue;

				if (((const usbvc_class_descriptor*)generic)->descriptorSubtype
					== USB_VIDEO_VC_PROCESSING_UNIT) {
					/* Add Image Settings group label */
					group->MakeGroup("Image Settings");
					_AddProcessingParameter(group, index,
						(const usb_video_processing_unit_descriptor*)generic);
				}
			}
		}
	}
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
		// Track frame sizes to detect if bandwidth is insufficient
		size_t frameSize = f->BufferLength();
		fMJPEGFrameSizeSum += frameSize;
		fMJPEGFrameSizeCount++;

		// Calculate expected minimum MJPEG size based on resolution
		// MJPEG typically compresses 10:1 to 20:1, so expect at least 5% of raw size
		if (fExpectedMJPEGMinSize == 0) {
			fExpectedMJPEGMinSize = (size_t)w * h * 2 / 20;  // 5% of YUY2 size
			syslog(LOG_INFO, "UVCCamDevice: MJPEG minimum expected size: %zu bytes for %dx%d\n",
				fExpectedMJPEGMinSize, (int)w, (int)h);
		}

		// Check every 30 frames if average size is too small
		bigtime_t now = system_time();
		if (fMJPEGFrameSizeCount >= 30 && (now - fLastFrameSizeCheck) > 5000000) {
			fLastFrameSizeCheck = now;
			size_t avgSize = fMJPEGFrameSizeSum / fMJPEGFrameSizeCount;

			// If average frame size is less than 30% of expected minimum,
			// bandwidth is severely insufficient - trigger fallback
			if (avgSize < fExpectedMJPEGMinSize * 30 / 100) {
				syslog(LOG_WARNING, "UVCCamDevice: MJPEG frames too small! avg=%zu, expected>%zu\n",
					avgSize, fExpectedMJPEGMinSize);
				syslog(LOG_WARNING, "UVCCamDevice: Bandwidth insufficient, triggering resolution fallback\n");

				// Reset counters and trigger fallback
				fMJPEGFrameSizeSum = 0;
				fMJPEGFrameSizeCount = 0;
				ReduceResolution();
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
		} else {
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

			// Pass actual size so conversion can calculate correct stride
			// (some webcams add padding to each row)
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

	size_t srcStride = (size_t)width * 2;  // YUY2: 2 bytes per pixel
	size_t dstStride = (size_t)width * 4;  // RGB32: 4 bytes per pixel

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
		// High packet loss - trigger fallback
		if (!fFallbackActive || fCurrentResolutionLevel < _GetMaxResolutionLevel()) {
			syslog(LOG_WARNING, "UVCCamDevice: Packet loss %.1f%% exceeds threshold %.1f%%, "
				"triggering resolution fallback\n",
				lossPercent, fFallbackConfig.error_threshold_percent);
			_TriggerResolutionFallback();
		}
		fStableStartTime = 0;  // Reset stability timer
	} else {
		// Good connection - check for recovery opportunity
		if (fStableStartTime == 0) {
			fStableStartTime = now;
		} else if (fFallbackConfig.auto_recovery_enabled &&
			fFallbackActive &&
			(now - fStableStartTime) > fFallbackConfig.recovery_delay) {
			_AttemptResolutionRecovery();
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

	// Level 0 = first (usually highest) resolution
	// Higher levels = lower resolutions (later in list)
	int32 index = level;

	if (index < 0) {
		index = 0;
	}
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

	// Fallback to safe defaults
	*width = 320;
	*height = 240;
}


int32
UVCCamDevice::_GetMaxResolutionLevel()
{
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	int32 count = frameList->CountItems();
	return (count > 0) ? count - 1 : 0;
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
	// Check environment variable override first
	const char* disableHighBW = getenv("WEBCAM_DISABLE_HIGH_BANDWIDTH");
	if (disableHighBW != NULL && (strcmp(disableHighBW, "1") == 0 || strcmp(disableHighBW, "yes") == 0)) {
		return false;
	}

	const char* forceHighBW = getenv("WEBCAM_FORCE_HIGH_BANDWIDTH");
	if (forceHighBW != NULL && (strcmp(forceHighBW, "1") == 0 || strcmp(forceHighBW, "yes") == 0)) {
		return true;
	}

	// If we've already tested and it failed, don't try again
	if (fHighBandwidthTested && !fHighBandwidthWorks) {
		return false;
	}

	// Default: try high-bandwidth (most systems have XHCI now)
	return true;
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
