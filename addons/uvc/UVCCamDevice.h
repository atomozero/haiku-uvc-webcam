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

	// Resolution fallback methods (Feature 3)
			void				_EvaluatePacketLoss();
			status_t			_TriggerResolutionFallback();
			status_t			_AttemptResolutionRecovery();
			void				_GetResolutionAtLevel(int32 level,
									uint32* width, uint32* height);
			int32				_GetMaxResolutionLevel();
			void				_InitializeFallbackConfig();

	// Bandwidth calculation (YUY2 adaptive FPS support)
			uint32				_GetMaxAvailableBandwidth();

	// High-bandwidth auto-detection
			bool				_ShouldUseHighBandwidth();
			void				_OnHighBandwidthFailure();
			void				_ResetHighBandwidthState();


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

			// High-bandwidth auto-detection state
			bool				fHighBandwidthTested;		// Have we tried high-bandwidth?
			bool				fHighBandwidthWorks;		// Did it work?
			uint32				fHighBandwidthFailures;		// Consecutive failures
			bool				fUsingHighBandwidth;		// Currently using high-bandwidth?

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

