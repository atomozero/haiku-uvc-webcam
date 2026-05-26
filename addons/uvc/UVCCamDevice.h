/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2009, Ithamar Adema, <ithamar.adema@team-embedded.nl>.
 * Distributed under the terms of the MIT License.
 */
#ifndef _UVC_CAM_DEVICE_H
#define _UVC_CAM_DEVICE_H


#include "CamDevice.h"
#include "USB_video.h"
#include "USB_audio.h"
#include <usb/USB_video.h>
#include <turbojpeg.h>


// Frame validation constants
const size_t kMinMJPEGFrameSize = 1024;			// Frame < 1KB is corrupted
const size_t kMinYUY2FramePercent = 90;			// Frame YUY2 < 90% = incomplete
const uint32 kMaxConsecutiveBadFrames = 10;		// Report after N bad frames
const uint32 kFrameValidationReportInterval = 30;	// Seconds between stats reports


// =============================================================================
// USB Host Controller and Speed Detection (XHCI Optimization Support)
// =============================================================================

// USB host controller type - affects high-bandwidth and isochronous behavior
enum usb_host_controller_type {
	USB_HC_UNKNOWN = 0,		// Not yet detected
	USB_HC_OHCI,			// USB 1.1 - no high-bandwidth support
	USB_HC_UHCI,			// USB 1.1 - no high-bandwidth support
	USB_HC_EHCI,			// USB 2.0 - high-bandwidth with limitations
	USB_HC_XHCI				// USB 3.0+ - full high-bandwidth support
};

// USB device speed - affects bandwidth calculations
enum usb_device_speed {
	USB_SPEED_UNKNOWN = 0,
	USB_SPEED_LOW = 1,		// 1.5 Mbps (USB 1.0)
	USB_SPEED_FULL = 2,		// 12 Mbps (USB 1.1)
	USB_SPEED_HIGH = 3,		// 480 Mbps (USB 2.0)
	USB_SPEED_SUPER = 4,	// 5 Gbps (USB 3.0)
	USB_SPEED_SUPER_PLUS = 5	// 10/20 Gbps (USB 3.1/3.2)
};

// Controller capability flags
enum usb_controller_caps {
	USB_CAP_NONE = 0,
	USB_CAP_HIGH_BANDWIDTH = (1 << 0),		// Supports mult>1 isochronous
	USB_CAP_DYNAMIC_IMOD = (1 << 1),		// Dynamic interrupt moderation
	USB_CAP_TBC_TLBPC = (1 << 2),			// TBC/TLBPC for isochronous TRBs
	USB_CAP_LPM = (1 << 3),					// Link Power Management
	USB_CAP_STREAMS = (1 << 4)				// Bulk streams (USB 3.0+)
};

// XHCI interrupt moderation modes (matches kernel driver)
enum xhci_imod_mode {
	XHCI_IMOD_LOW_LATENCY = 0,	// 16000 IRQ/s - for isochronous streaming
	XHCI_IMOD_MEDIUM = 1,		// 8000 IRQ/s - for interrupt endpoints
	XHCI_IMOD_DEFAULT = 2,		// 4000 IRQ/s - bulk/control
	XHCI_IMOD_POWER_SAVE = 3	// 2000 IRQ/s - idle mode
};

// Controller detection result
struct usb_controller_info {
	usb_host_controller_type	type;
	usb_device_speed			device_speed;
	uint32						capabilities;
	xhci_imod_mode				expected_imod;
	bool						high_bandwidth_safe;
	const char*					type_name;
};


// =============================================================================
// YUV to RGB Lookup Tables for Optimized Color Conversion
// =============================================================================
// Pre-computed tables eliminate per-pixel multiplications and clipping.
// Total memory: ~3KB for all tables combined.

struct yuv_rgb_lookup_tables {
	// Y contribution to R,G,B (same value for all three)
	// y_table[i] = 298 * (i - 16) [unshifted, combined with U/V before shift]
	// Note: Uses int32 because max value (298*219=65262) exceeds int16 range
	int32	y_table[256];

	// U contribution to B: u_b_table[i] = 516 * (i - 128)
	int32	u_b_table[256];

	// U contribution to G: u_g_table[i] = -100 * (i - 128)
	int32	u_g_table[256];

	// V contribution to R: v_r_table[i] = 409 * (i - 128)
	int32	v_r_table[256];

	// V contribution to G: v_g_table[i] = -208 * (i - 128)
	int32	v_g_table[256];

	bool	initialized;

	yuv_rgb_lookup_tables() : initialized(false) {}

	void Initialize();
};

// Global lookup tables (shared across all instances)
extern yuv_rgb_lookup_tables gYuvRgbTables;


// Frame validation result codes
enum frame_validation_result {
	FRAME_VALID = 0,
	FRAME_INCOMPLETE,
	FRAME_CORRUPTED_NO_SOI,
	FRAME_CORRUPTED_NO_EOI,
	FRAME_CORRUPTED_TRUNCATED,
	FRAME_CORRUPTED_INVALID_HEADER
};


// Frame validation statistics
struct frame_validation_stats {
	uint32		frames_validated;
	uint32		frames_valid;
	uint32		frames_incomplete;
	uint32		frames_no_soi;
	uint32		frames_no_eoi;
	uint32		frames_repeated;
	bigtime_t	last_valid_frame_time;
	bigtime_t	last_stats_report_time;
};


// Resolution fallback configuration
struct resolution_fallback_config {
	float		error_threshold_percent;	// Error threshold (default 10%)
	bigtime_t	evaluation_interval;		// Evaluation window (default 5s)
	uint32		min_packets_for_eval;		// Minimum packets before deciding
	bool		auto_recovery_enabled;		// Attempt to recover if stable
	bigtime_t	recovery_delay;				// Wait time before recovery attempt
};


// Camera control info for Processing Unit
struct camera_control_info {
	uint16		selector;			// UVC selector (PU_BRIGHTNESS_CONTROL, etc.)
	int16		min_value;
	int16		max_value;
	int16		default_value;
	int16		current_value;
	uint16		resolution;			// Step size
	uint8		info_caps;			// GET/SET capabilities
	int32		parameter_id;		// BParameter ID assigned
	bool		has_auto;			// Has auto mode
	int32		auto_parameter_id;	// BParameter ID for auto checkbox
	const char*	name;				// Control name for UI
};


// =============================================================================
// Extension Unit Support (Vendor-Specific Features)
// =============================================================================

// Known vendor Extension Unit GUIDs
// Format: {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}

// Microsoft: UVC 1.5 encoding extension
static const uint8 kMicrosoftH264XUGUID[16] = {
	0xa9, 0x4c, 0x5d, 0x1f,  // GUID Data1
	0xf5, 0x2b,              // GUID Data2
	0x46, 0x4e,              // GUID Data3
	0xb2, 0xe8, 0xd6, 0x5e, 0x3a, 0xb0, 0x95, 0x3c  // GUID Data4
};

// Sonix Technology (VID 0x0C45) - LED, face detection
static const uint8 kSonixXUGUID[16] = {
	0x18, 0x6e, 0xce, 0xbb,
	0x49, 0x82,
	0x48, 0x6c,
	0x8c, 0x50, 0x81, 0x27, 0xb1, 0x43, 0x00, 0x56
};

// Sonix System HW Control (SN9C291A/292) - ASIC R/W, H.264, MJPEG
static const uint8 kSonixSysHWGUID[16] = {
	0x70, 0x33, 0xf0, 0x28,
	0x11, 0x63,
	0x2e, 0x4a,
	0xba, 0x2c, 0x68, 0x90, 0xeb, 0x33, 0x40, 0x16
};

// Sonix User HW Control (SN9C292, alternate) - frame info, multi-stream
static const uint8 kSonixUsrHWGUID[16] = {
	0x3f, 0xae, 0x12, 0x28,
	0xd7, 0xbc,
	0x11, 0x4e,
	0xa3, 0x57, 0x6f, 0x1e, 0xde, 0xf7, 0xd6, 0x1d
};

// Logitech - PTZ, LED, H.264 encoding
static const uint8 kLogitechXUGUID[16] = {
	0x82, 0x06, 0x61, 0x63,
	0x70, 0x50,
	0xab, 0x49,
	0xb8, 0xcc, 0xb3, 0x85, 0x5e, 0x8d, 0x22, 0x50
};

// Realtek (VID 0x0BDA) - HDR, noise reduction
static const uint8 kRealtekXUGUID[16] = {
	0x3a, 0x17, 0x15, 0x28,
	0xab, 0x46,
	0x4a, 0x47,
	0xa5, 0x9c, 0x02, 0xec, 0x3e, 0x79, 0x8d, 0x2a
};

// Extension Unit vendor identification
enum extension_unit_vendor {
	XU_VENDOR_UNKNOWN = 0,
	XU_VENDOR_MICROSOFT,
	XU_VENDOR_SONIX,
	XU_VENDOR_LOGITECH,
	XU_VENDOR_REALTEK
};

// Extension Unit capability flags
enum extension_unit_caps {
	XU_CAP_NONE = 0,
	XU_CAP_LED_CONTROL = (1 << 0),		// LED on/off
	XU_CAP_FACE_DETECTION = (1 << 1),	// Face detection
	XU_CAP_HDR = (1 << 2),				// HDR mode
	XU_CAP_NOISE_REDUCTION = (1 << 3),	// Digital noise reduction
	XU_CAP_H264_ENCODING = (1 << 4),	// Hardware H.264 encoding
	XU_CAP_PTZ_CONTROL = (1 << 5)		// Enhanced PTZ
};

// Extension Unit information
struct extension_unit_info {
	uint8					unit_id;
	uint8					guid[16];
	extension_unit_vendor	vendor;
	uint32					capabilities;	// XU_CAP_* flags
	uint8					num_controls;
	uint8					num_input_pins;
	uint8					source_ids[8];	// Up to 8 source pins
	char					description[64];
	const char*				vendor_name;
};


// =============================================================================
// Still Image Capture Support
// =============================================================================

// UVC Still Capture Methods (from VS_INPUT_HEADER)
enum still_capture_method {
	STILL_CAPTURE_NONE = 0,			// Not supported
	STILL_CAPTURE_METHOD_1 = 1,		// Dedicated button (interrupt endpoint)
	STILL_CAPTURE_METHOD_2 = 2,		// Host software-triggered
	STILL_CAPTURE_METHOD_3 = 3		// Dedicated still pipe with button
};

// Still image size pattern from VS_STILL_IMAGE_FRAME
struct still_image_size {
	uint16		width;
	uint16		height;
};

// Still image frame information
struct still_image_info {
	uint8					endpoint_address;
	uint8					num_sizes;
	still_image_size		sizes[16];		// Up to 16 still resolutions
	uint8					num_compressions;
	uint8					compressions[8];	// Compression values
};


class UVCCamDevice : public CamDevice {
public:
								UVCCamDevice(CamDeviceAddon &_addon,
									BUSBDevice* _device);
	virtual						~UVCCamDevice();

	virtual bool				SupportsIsochronous();
	virtual status_t			StartTransfer();
	virtual status_t			StopTransfer();
	virtual status_t			SuggestVideoFrame(uint32 &width,
									uint32 &height);
	virtual status_t			AcceptVideoFrame(uint32 &width,
									uint32 &height);
	virtual void				AddParameters(BParameterGroup *group,
									int32 &index);
	virtual status_t			GetParameterValue(int32 id,
									bigtime_t *last_change, void *value,
									size_t *size);
	virtual status_t			SetParameterValue(int32 id, bigtime_t when,
									const void *value, size_t size);
	virtual status_t			FillFrameBuffer(BBuffer *buffer,
									bigtime_t *stamp = NULL);


	// PHASE 4: Override packet loss resolution fallback
	virtual status_t			ReduceResolution();

	// Safe resolution change via worker thread (prevents kernel panic)
	virtual status_t			_HandleResolutionChange(uint32 width,
									uint32 height);

	// High-bandwidth auto-detection overrides
	virtual void				OnConsecutiveTransferFailures(uint32 count);
	virtual void				OnTransferSuccess();
	// Audio support
			bool				HasAudio() const { return fHasAudio; }
			uint8				AudioChannels() const { return fAudioChannels; }
			uint8				AudioBitResolution() const { return fAudioBitResolution; }
			uint32				AudioSampleRate() const { return fAudioSampleRate; }

	// Audio transfer control
			status_t			StartAudioTransfer();
			status_t			StopAudioTransfer();
			size_t				ReadAudioData(void* buffer, size_t size);

private:
			status_t			_SelectAudioAlternate();
			status_t			_SelectAudioIdleAlternate();
			void				_ParseVideoControl(
									const usbvc_class_descriptor* descriptor,
									size_t len);
			void				_ParseVideoStreaming(
									const usbvc_class_descriptor* descriptor,
									size_t len);
			void				_ParseAudioControl(
									const usb_audio_class_descriptor* descriptor,
									size_t len);
			void				_ParseAudioStreaming(
									const usb_audio_class_descriptor* descriptor,
									size_t len);
			status_t			_ProbeCommitFormat();
			status_t			_SelectBestAlternate();
			status_t			_SelectIdleAlternate();
			void 				_ConvertYUY2toRGB32(unsigned char *dst,
									unsigned char *src, size_t srcSize,
									int32 width, int32 height);
			void				_ConvertNV12toRGB32(unsigned char* dst,
									const unsigned char* src, size_t srcSize,
									int32 width, int32 height);
			void				_DecompressMJPEGtoRGB32(unsigned char* dst,
									const unsigned char* src, size_t srcSize,
									int32 width, int32 height);

			void				_AddProcessingParameter(BParameterGroup* group,
									int32 index,
									const usb_video_processing_unit_descriptor*
										descriptor);
			float				_AddParameter(BParameterGroup* group,
									BParameterGroup** subgroup, int32 index,
									uint16 wValue, const char* name);
			uint8 				_AddAutoParameter(BParameterGroup* subgroup,
									int32 index, uint16 wValue);
			status_t			_SetParameterValue(uint16 wValue,
									int16 setValue);
			status_t			_SetParameterValue(uint16 wValue,
									int8 setValue);

	// Frame validation methods (Feature 1)
			frame_validation_result	_ValidateMJPEGFrame(const uint8* data,
									size_t size);
			frame_validation_result	_ValidateYUY2Frame(const uint8* data,
									size_t size, int32 width, int32 height);
			bool				_FindJpegMarker(const uint8* data, size_t size,
									uint8 marker, size_t* position);
			void				_CacheValidFrame(const uint8* data, size_t size,
									int32 width, int32 height);
			bool				_UseLastValidFrame(uint8* dst, size_t dstSize);
			void				_ReportValidationStats();

	// Camera control methods (Feature 2)
			status_t			_ProbeControlRange(uint16 selector,
									camera_control_info* info);
			status_t			_InitializeProcessingControls();
			void				_AddProcessingControls(BParameterGroup* group,
									int32& index);
			status_t			_GetControlValue(uint16 selector,
									int16* value);
			status_t			_SetControlValue(uint16 selector,
									int16 value);

	// Camera Terminal control methods (CT)
			status_t			_GetCTControlValue(uint16 selector,
									void* value, size_t size);
			status_t			_SetCTControlValue(uint16 selector,
									const void* value, size_t size);
			void				_AddCameraTerminalControls(BParameterGroup* group,
									int32& index);

	// Extension Unit methods (XU) - Vendor-specific features
			void				_ParseExtensionUnit(
									const usb_video_extension_unit_descriptor* descriptor);
			extension_unit_vendor	_IdentifyXUVendor(const uint8* guid);
			uint32				_GetXUCapabilities(extension_unit_vendor vendor);
			const char*			_GetXUVendorName(extension_unit_vendor vendor);
			void				_LogExtensionUnits();

	// XU control transfer primitives
			status_t			_XUSetCur(uint8 unitId, uint8 selector,
									const uint8* data, uint16 length);
			status_t			_XUGetCur(uint8 unitId, uint8 selector,
									uint8* data, uint16 length);
			status_t			_XUGetMin(uint8 unitId, uint8 selector,
									uint8* data, uint16 length);
			status_t			_XUGetMax(uint8 unitId, uint8 selector,
									uint8* data, uint16 length);
			status_t			_XUGetInfo(uint8 unitId, uint8 selector,
									uint8* info);
			extension_unit_info*	_FindXU(extension_unit_vendor vendor);

	// Sonix-specific XU helpers
			status_t			_SonixAsicRead(uint16 addr, uint8* value);
			status_t			_SonixAsicWrite(uint16 addr, uint8 value);

	// Still image capture methods
			void				_ParseStillImageFrame(
									const usb_video_still_image_frame_descriptor* descriptor);
			void				_LogStillImageCapabilities();
			const char*			_GetStillCaptureMethodName(still_capture_method method);
	public:
			status_t			TriggerStillCapture(uint8* buffer, size_t bufferSize,
									size_t* bytesWritten, uint32 width = 0,
									uint32 height = 0);
			bool				HasStillCapture() const
									{ return fHasStillCapture
										&& fStillCaptureMethod != STILL_CAPTURE_NONE; }
	private:

	// Resolution fallback methods (Feature 3)
			void				_EvaluatePacketLoss();
			status_t			_TriggerResolutionFallback();
			status_t			_AttemptResolutionRecovery();
			void				_GetResolutionAtLevel(int32 level,
									uint32* width, uint32* height);
			int32				_GetMaxResolutionLevel();
			int32				_FindResolutionLevel(uint32 width, uint32 height);
			void				_InitializeFallbackConfig();
			void				_BuildSortedResolutionList();

	// Bandwidth calculation (YUY2 adaptive FPS support)
			uint32				_GetMaxAvailableBandwidth();
			float				_EstimateMaxFps(uint32 width, uint32 height, bool isMJPEG);
			bool				_IsResolutionSupportable(uint32 width, uint32 height, bool isMJPEG);

	// High-bandwidth auto-detection
			bool				_ShouldUseHighBandwidth();
			void				_OnHighBandwidthFailure();
			void				_ResetHighBandwidthState();

	// USB controller and speed detection (XHCI optimization)
			void				_DetectControllerType();
			usb_device_speed	_GetUSBSpeed();
			void				_LogControllerCapabilities();
			bigtime_t			_GetOptimalPollInterval();
			uint32				_GetExpectedIRQsPerFrame();
			size_t				_GetOptimalBufferSize();
			uint64				_GetMaxBandwidth();

	// TBC/TLBPC packet handling (XHCI isochronous optimization)
			float				_GetExpectedPacketCompletionRate();
			bool				_HasTBCTLBPCSupport();


			usbvc_interface_header_descriptor *fHeaderDescriptor;

			const BUSBEndpoint*	fInterruptIn;
			uint32				fControlIndex;
			uint16				fControlRequestIndex;
			uint32				fStreamingIndex;
			uint32				fCurrentVideoAlternate;  // Track current alternate to avoid re-setting
			uint32				fUncompressedFormatIndex;
			uint32				fUncompressedFrameIndex;
			uint32				fMJPEGFormatIndex;
			uint32				fMJPEGFrameIndex;
			uint32				fMaxVideoFrameSize;
			uint32				fMaxPayloadTransferSize;
			size_t				fProbeCommitSize;		// Working probe/commit size (26, 34, or 48 bytes)

			BList				fUncompressedFrames;
			BList				fMJPEGFrames;

			float				fBrightness;
			float				fContrast;
			float				fHue;
			float				fSaturation;
			float				fSharpness;
			float				fGamma;
			float				fWBTemp;
			float				fWBComponent;
			float				fBacklightCompensation;
			float				fGain;

			bool				fBinaryBacklightCompensation;

			int					fWBTempAuto;
			int					fWBCompAuto;
			int					fHueAuto;

			// MJPEG decompression support
			tjhandle			fJpegDecompressor;
			bool				fIsMJPEG;
			bool				fIsNV12;		// NV12 (YUV 4:2:0) format
			bool				fMicrodiaQuirk;	// Microdia 0c45:6409 stride quirk

			// FIX BUG 6: Contatori diagnostici per istanza (non statici)
			int32				fFillFrameCount;
			int32				fFillFrameSuccess;
			int32				fFillFrameTimeout;
			int32				fMjpegAttempts;
			int32				fMjpegSuccess;
			int32				fMjpegNoSOI;
			int32				fMjpegDecompressErrors;

			int					fBacklightCompensationBinary;
			int					fPowerlineFrequency;

			// Audio interface support (USB Audio Class 1.0)
			bool				fHasAudio;
			uint32				fAudioControlIndex;
			uint32				fAudioStreamingIndex;
			uint32				fCurrentAudioAlternate;  // Track current alternate to avoid USB crash
			const BUSBEndpoint*	fAudioIsoIn;
			uint32				fAudioMaxPacketSize;

			// Audio format info
			uint8				fAudioChannels;
			uint8				fAudioBitResolution;
			uint8				fAudioSubFrameSize;
			uint32				fAudioSampleRate;
			uint8				fAudioTerminalID;
			uint8				fAudioFeatureUnitID;

			// Audio alternate settings info
			BList				fAudioAlternates;

			// Audio transfer state
			bool				fAudioTransferRunning;
			thread_id			fAudioPumpThread;
			uint8*				fAudioBuffer;
			size_t				fAudioBufferLen;

			// Audio ring buffer for data exchange
			// FIX BUG 4: Usare int32 per operazioni atomiche
			uint8*				fAudioRingBuffer;
			size_t				fAudioRingSize;
			int32				fAudioRingHead;  // Atomico: scritto da producer, letto da consumer
			int32				fAudioRingTail;  // Atomico: letto da producer, scritto da consumer

			// Resolution selection (Task 2)
			int32				fSelectedResolutionIndex;  // Index into current frame list
			int32				fResolutionParameterID;    // Parameter ID for resolution selector
			bigtime_t			fResolutionTransitionStart; // Time when resolution change started

			// Frame rate selection (P2 Feature)
			int32				fSelectedFrameIntervalIndex;  // Index into frame's discrete_frame_intervals
			int32				fFrameRateParameterID;        // Parameter ID for fps selector
			uint32				fCurrentFrameIntervals[8];    // Copy of available intervals (max 8)
			uint8				fNumFrameIntervals;           // Number of intervals available
			uint32				fSelectedFrameInterval;       // Actual interval value in 100ns units

			sem_id				fAudioRingSem;

			// Frame validation state (Feature 1)
			frame_validation_stats	fValidationStats;
			uint8*				fLastValidFrame;
			size_t				fLastValidFrameSize;
			int32				fLastValidWidth;
			int32				fLastValidHeight;
			uint32				fConsecutiveBadFrames;
			bool				fFrameRepeatEnabled;

			// Processing Unit controls (Feature 2)
			BList				fProcessingControls;	// List of camera_control_info*
			uint8				fProcessingUnitID;
			bool				fControlsInitialized;

			// Camera Terminal controls (CT) - Exposure, Focus, Zoom, Pan/Tilt
			uint8				fCameraTerminalID;
			uint32				fCameraTerminalControls;	// Bitmap of supported CT controls
			bool				fHasCameraTerminal;

			// CT control current values
			uint8				fAutoExposureMode;		// 1=Manual, 2=Auto, 4=Shutter, 8=Aperture
			uint32				fExposureTimeAbs;		// In 100μs units
			bool				fAutoFocus;
			uint16				fFocusAbsolute;
			uint16				fZoomAbsolute;
			int32				fPanAbsolute;			// Arc-seconds
			int32				fTiltAbsolute;			// Arc-seconds
			bool				fPrivacyEnabled;

			// CT control parameter IDs
			int32				fAutoExposureModeID;
			int32				fExposureTimeID;
			int32				fAutoFocusID;
			int32				fFocusAbsoluteID;
			int32				fZoomAbsoluteID;
			int32				fPanTiltID;

			// Extension Unit support (XU) - Vendor-specific features
			BList				fExtensionUnits;		// List of extension_unit_info*
			bool				fHasExtensionUnits;

			// Still image capture support
			still_capture_method	fStillCaptureMethod;
			still_image_info	fStillImageInfo;
			bool				fHasStillCapture;
			bool				fTriggerSupport;		// Hardware button available
			bool				fTriggerUsage;			// Button usage mode

			// Resolution fallback state (Feature 3)
			resolution_fallback_config	fFallbackConfig;
			int32				fCurrentResolutionLevel;	// 0=max, N=min
			int32				fTargetResolutionLevel;
			bigtime_t			fLastFallbackTime;
			bigtime_t			fStableStartTime;
			uint32				fEvalWindowPackets;
			uint32				fEvalWindowErrors;
			bigtime_t			fEvalWindowStartTime;
			bool				fFallbackActive;
			bool				fFallbackWarningShown;
			uint32				fLastPacketSuccessCount;	// For delta calculation
			uint32				fLastPacketErrorCount;

			// Sorted resolution indices (for proper fallback ordering)
			// Index 0 = highest resolution, higher indices = lower resolutions
			int32				fSortedMJPEGIndices[32];	// Max 32 resolutions
			int32				fSortedUncompressedIndices[32];
			int32				fSortedMJPEGCount;
			int32				fSortedUncompressedCount;

			// High-bandwidth auto-detection state
			bool				fHighBandwidthTested;		// Have we tried high-bandwidth?
			bool				fHighBandwidthWorks;		// Did it work?
			uint32				fHighBandwidthFailures;		// Consecutive failures
			bool				fUsingHighBandwidth;		// Currently using high-bandwidth?

			// Bandwidth estimation for resolution validation
			uint32				fMaxAvailableBandwidth;		// Max bytes/microframe (cached)
			bool				fBandwidthCalculated;		// True after first calculation

			// USB controller detection (XHCI optimization)
			usb_controller_info	fControllerInfo;			// Detected controller capabilities
			bool				fControllerDetected;		// True after detection complete

			// MJPEG frame size monitoring (for auto-fallback)
			size_t				fMJPEGFrameSizeSum;			// Sum of recent frame sizes
			uint32				fMJPEGFrameSizeCount;		// Count of frames measured
			size_t				fExpectedMJPEGMinSize;		// Minimum expected size based on resolution
			bigtime_t			fLastFrameSizeCheck;		// Last time we checked average size

static		int32				_audio_pump_thread_(void* data);
			int32				AudioPumpThread();
};


class UVCCamDeviceAddon : public CamDeviceAddon {
public:
								UVCCamDeviceAddon(WebCamMediaAddOn* webcam);
	virtual 					~UVCCamDeviceAddon();

	virtual const char*			BrandName();
	virtual UVCCamDevice*		Instantiate(CamRoster &roster,
									BUSBDevice *from);
};

#endif /* _UVC_CAM_DEVICE_H */

