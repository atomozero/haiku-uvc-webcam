/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */
#ifndef _CAM_DEVICE_H
#define _CAM_DEVICE_H


#include <OS.h>
#include <image.h>
#include <string.h>
#include <SupportDefs.h> // For atomic operations
#ifdef __HAIKU__
//#if 1
#	include <USB3.h>
#	include <USBKit.h>
#	define SUPPORT_ISO
#else
#	include <USB.h>
#	include <usb/USBKit.h>
#endif
#include <Locker.h>
#include <MediaAddOn.h>
#include <String.h>
#include <Rect.h>

class BBitmap;
class BBuffer;
class BDataIO;
class BParameterGroup;
class CamRoster;
class CamDeviceAddon;
class CamSensor;
class CamDeframer;
class WebCamMediaAddOn;


// USB transfer retry configuration
struct usb_retry_config {
	uint32		max_retries;		// Maximum retry attempts (default: 3)
	bigtime_t	initial_delay;		// Initial delay in microseconds (default: 100ms)
	bigtime_t	max_delay;			// Maximum delay cap (default: 1s)
	float		backoff_multiplier;	// Exponential backoff factor (default: 2.0)
};

// Default retry configuration
const usb_retry_config kDefaultRetryConfig = {
	3,			// max_retries
	100000,		// initial_delay: 100ms
	1000000,	// max_delay: 1s
	2.0f		// backoff_multiplier
};

// USB error classification for recovery strategy
enum usb_error_type {
	USB_ERROR_NONE = 0,
	USB_ERROR_TIMEOUT,		// Transfer timed out
	USB_ERROR_STALL,		// Endpoint stalled
	USB_ERROR_CRC,			// CRC/data corruption
	USB_ERROR_OVERFLOW,		// Buffer overflow
	USB_ERROR_DISCONNECTED,	// Device disconnected
	USB_ERROR_UNKNOWN,		// Other errors
	USB_ERROR_TYPE_COUNT	// Number of error types (for histogram)
};


// =============================================================================
// Error Recovery Actions (Group 5: Error Handling Optimization)
// =============================================================================
// Defines recommended actions for each error type

enum error_recovery_action {
	RECOVERY_NONE = 0,			// No recovery needed
	RECOVERY_RETRY,				// Retry the operation
	RECOVERY_RESET_ENDPOINT,	// Reset the endpoint
	RECOVERY_REDUCE_BANDWIDTH,	// Reduce bandwidth (lower resolution/fps)
	RECOVERY_RESTART_TRANSFER,	// Stop and restart the transfer
	RECOVERY_DEVICE_RESET,		// Reset the entire device
	RECOVERY_FATAL				// Unrecoverable - stop operation
};


// Error recovery configuration and state
struct error_recovery_config {
	// Thresholds for triggering recovery actions
	float		error_rate_warning;		// Warn at this rate (default 5%)
	float		error_rate_action;		// Take action at this rate (default 10%)
	uint32		consecutive_errors_max;	// Max consecutive errors before action
	bigtime_t	evaluation_window;		// Window for calculating error rate

	// Current state
	uint32		consecutive_errors;
	bigtime_t	last_recovery_time;
	uint32		recovery_attempts;

	void Reset() {
		error_rate_warning = 0.05f;
		error_rate_action = 0.10f;
		consecutive_errors_max = 20;
		evaluation_window = 5000000;  // 5 seconds
		consecutive_errors = 0;
		last_recovery_time = 0;
		recovery_attempts = 0;
	}

	// Get recommended action based on error type
	static error_recovery_action GetRecommendedAction(usb_error_type error) {
		switch (error) {
			case USB_ERROR_NONE:
				return RECOVERY_NONE;
			case USB_ERROR_TIMEOUT:
				return RECOVERY_RETRY;
			case USB_ERROR_STALL:
				return RECOVERY_RESET_ENDPOINT;
			case USB_ERROR_CRC:
				return RECOVERY_RETRY;
			case USB_ERROR_OVERFLOW:
				return RECOVERY_REDUCE_BANDWIDTH;
			case USB_ERROR_DISCONNECTED:
				return RECOVERY_FATAL;
			default:
				return RECOVERY_RETRY;
		}
	}

	// Get action name for logging
	static const char* GetActionName(error_recovery_action action) {
		static const char* names[] = {
			"None", "Retry", "Reset Endpoint", "Reduce Bandwidth",
			"Restart Transfer", "Device Reset", "Fatal"
		};
		if (action >= 0 && action <= RECOVERY_FATAL)
			return names[action];
		return "Unknown";
	}
};


// Double buffer configuration for USB transfers
// Allows processing one buffer while the other receives data
struct usb_double_buffer {
	uint8*		buffers[2];			// Two alternating buffers
	size_t		bufferSize;			// Size of each buffer
	int32		activeBuffer;		// Currently receiving data (0 or 1)
	int32		readyBuffer;		// Ready for processing (-1 if none)
	sem_id		bufferReady;		// Signaled when a buffer is ready
	bool		initialized;		// Setup completed

	usb_double_buffer()
		:
		bufferSize(0),
		activeBuffer(0),
		readyBuffer(-1),
		bufferReady(-1),
		initialized(false)
	{
		buffers[0] = NULL;
		buffers[1] = NULL;
	}
};


// PHASE 8: Error histogram for tracking error distribution
struct usb_error_histogram {
	uint32		counts[USB_ERROR_TYPE_COUNT];
	bigtime_t	last_reset;
	bigtime_t	total_transfer_time;
	uint32		total_transfers;

	void Reset() {
		memset(counts, 0, sizeof(counts));
		last_reset = system_time();
		total_transfer_time = 0;
		total_transfers = 0;
	}

	void RecordError(usb_error_type type) {
		if (type >= 0 && type < USB_ERROR_TYPE_COUNT)
			counts[type]++;
	}

	float GetErrorRate(usb_error_type type) const {
		if (total_transfers == 0)
			return 0.0f;
		return (float)counts[type] / (float)total_transfers;
	}

	float GetTotalErrorRate() const {
		if (total_transfers == 0)
			return 0.0f;
		uint32 totalErrors = 0;
		for (int i = 1; i < USB_ERROR_TYPE_COUNT; i++) // Skip USB_ERROR_NONE
			totalErrors += counts[i];
		return (float)totalErrors / (float)total_transfers;
	}
};


// =============================================================================
// Frame Timing Statistics (Group 4: Timing and Latency Optimization)
// =============================================================================
// Tracks frame delivery timing for adaptive scheduling and diagnostics

struct frame_timing_stats {
	// Frame timing measurements
	bigtime_t	last_frame_time;		// Time of last frame delivery
	bigtime_t	frame_interval_sum;		// Sum of frame intervals
	bigtime_t	frame_interval_min;		// Minimum frame interval
	bigtime_t	frame_interval_max;		// Maximum frame interval
	uint32		frame_count;			// Number of frames measured

	// Processing latency
	bigtime_t	processing_time_sum;	// Sum of processing times
	bigtime_t	processing_time_max;	// Maximum processing time

	// Jitter tracking (variance in frame intervals)
	bigtime_t	expected_interval;		// Expected frame interval (1/fps)
	bigtime_t	jitter_sum;				// Sum of |actual - expected|

	// Adaptive timeout support
	bigtime_t	adaptive_timeout;		// Current adaptive timeout value

	void Reset() {
		last_frame_time = 0;
		frame_interval_sum = 0;
		frame_interval_min = INT64_MAX;
		frame_interval_max = 0;
		frame_count = 0;
		processing_time_sum = 0;
		processing_time_max = 0;
		expected_interval = 33333;  // Default 30fps
		jitter_sum = 0;
		adaptive_timeout = 100000;  // Default 100ms timeout
	}

	void RecordFrame(bigtime_t processingTime) {
		bigtime_t now = system_time();

		if (last_frame_time > 0) {
			bigtime_t interval = now - last_frame_time;

			// Update interval statistics
			frame_interval_sum += interval;
			if (interval < frame_interval_min)
				frame_interval_min = interval;
			if (interval > frame_interval_max)
				frame_interval_max = interval;

			// Update jitter (deviation from expected)
			bigtime_t jitter = interval > expected_interval
				? interval - expected_interval
				: expected_interval - interval;
			jitter_sum += jitter;

			// Update adaptive timeout (exponential moving average)
			// timeout = 2x max observed interval + processing time
			bigtime_t targetTimeout = frame_interval_max * 2 + processing_time_max;
			adaptive_timeout = (adaptive_timeout * 7 + targetTimeout) / 8;

			// Clamp to reasonable range (10ms - 500ms)
			if (adaptive_timeout < 10000)
				adaptive_timeout = 10000;
			if (adaptive_timeout > 500000)
				adaptive_timeout = 500000;
		}

		last_frame_time = now;
		frame_count++;

		// Update processing time stats
		processing_time_sum += processingTime;
		if (processingTime > processing_time_max)
			processing_time_max = processingTime;
	}

	float GetAverageFPS() const {
		if (frame_count < 2 || frame_interval_sum == 0)
			return 0.0f;
		bigtime_t avgInterval = frame_interval_sum / (frame_count - 1);
		return 1000000.0f / avgInterval;
	}

	bigtime_t GetAverageInterval() const {
		if (frame_count < 2)
			return expected_interval;
		return frame_interval_sum / (frame_count - 1);
	}

	bigtime_t GetAverageJitter() const {
		if (frame_count < 2)
			return 0;
		return jitter_sum / (frame_count - 1);
	}

	bigtime_t GetAverageProcessingTime() const {
		if (frame_count == 0)
			return 0;
		return processing_time_sum / frame_count;
	}
};


typedef struct {
	usb_support_descriptor desc;
	const char *vendor;
	const char *product;
	const char *sensors; // possible sensors this cam uses (comma separated)
} usb_webcam_support_descriptor;

// This class represents each webcam
class CamDevice {
	public: 
						CamDevice(CamDeviceAddon &_addon, BUSBDevice* _device);
	virtual				~CamDevice();

	virtual status_t	InitCheck();
		bool			Matches(BUSBDevice* _device);
		BUSBDevice*		GetDevice();
	virtual void		Unplugged(); // called before the BUSBDevice deletion
	virtual bool		IsPlugged(); // asserts on-line hardware
	
	virtual const char*	BrandName();
	virtual const char*	ModelName();
	const flavor_info*	FlavorInfo() const { return &fFlavorInfo; };
	virtual bool		SupportsBulk();
	virtual bool		SupportsIsochronous();
	virtual status_t	StartTransfer();
	virtual status_t	StopTransfer();
	virtual bool		TransferEnabled() const { return atomic_get((int32*)&fTransferEnabled) != 0; };

	virtual status_t	SuggestVideoFrame(uint32 &width, uint32 &height);
	virtual status_t	AcceptVideoFrame(uint32 &width, uint32 &height);
	virtual status_t	SetVideoFrame(BRect rect);
	virtual BRect		VideoFrame() const { return fVideoFrame; };
	virtual status_t	SetScale(float scale);
	virtual status_t	SetVideoParams(float brightness, float contrast, float hue, float red, float green, float blue);

	virtual void		AddParameters(BParameterGroup *group, int32 &index);
	virtual status_t	GetParameterValue(int32 id, bigtime_t *last_change, void *value, size_t *size);
	virtual status_t	SetParameterValue(int32 id, bigtime_t when, const void *value, size_t size);


	// for use by deframer
	virtual size_t		MinRawFrameSize();
	virtual size_t		MaxRawFrameSize();
	virtual bool		ValidateStartOfFrameTag(const uint8 *tag, size_t taglen);
	virtual bool		ValidateEndOfFrameTag(const uint8 *tag, size_t taglen, size_t datalen);

	// PHASE 4: Packet loss monitoring
	virtual void		RecordPacketSuccess();
	virtual void		RecordPacketError();
			float		GetPacketLossRate() const;
	virtual bool		ShouldReduceResolution();
	virtual status_t	ReduceResolution();
			void		ResetPacketStatistics();

	// PHASE 8: Error histogram tracking
			void		RecordTransferResult(usb_error_type error);
			const usb_error_histogram&	GetErrorHistogram() const;
			void		ResetErrorHistogram();
			void		LogErrorStatistics();

	// Group 4: Frame timing statistics
			void		RecordFrameTiming(bigtime_t processingTime);
			const frame_timing_stats&	GetFrameTimingStats() const;
			void		ResetFrameTimingStats();
			void		SetExpectedFrameRate(float fps);
			bigtime_t	GetAdaptiveTimeout() const;
			void		LogFrameTimingStats();

	// Group 5: Error recovery
			error_recovery_action	EvaluateErrorRecovery(usb_error_type error);
			bool		ShouldTriggerRecovery() const;
			void		ResetErrorRecoveryState();
			const error_recovery_config&	GetErrorRecoveryConfig() const;
			void		LogRecoveryRecommendation(usb_error_type error);

	// High-bandwidth auto-detection callbacks (for UVC devices)
	virtual void		OnConsecutiveTransferFailures(uint32 count);
	virtual void		OnTransferSuccess();

	// several ways to get raw frames
	virtual status_t	WaitFrame(bigtime_t timeout);
	virtual status_t	GetFrameBitmap(BBitmap **bm, bigtime_t *stamp=NULL);
	virtual status_t	FillFrameBuffer(BBuffer *buffer, bigtime_t *stamp=NULL);

	// locking
	bool				Lock();
	void				Unlock();
	BLocker*			Locker() { return &fLocker; };

	// sensor chip handling
	CamSensor*			Sensor() const { return fSensor; };

	virtual status_t	PowerOnSensor(bool on);

	// generic register-like access
	virtual ssize_t		WriteReg(uint16 address, uint8 *data, size_t count=1);
	virtual ssize_t		WriteReg8(uint16 address, uint8 data);
	virtual ssize_t		WriteReg16(uint16 address, uint16 data);
	virtual ssize_t		ReadReg(uint16 address, uint8 *data, size_t count=1, bool cached=false);

	ssize_t				OrReg8(uint16 address, uint8 data, uint8 mask=0xff);
	ssize_t				AndReg8(uint16 address, uint8 data);
	
	// I2C-like access
	//virtual status_t	GetStatusIIC();
	//virtual status_t	WaitReadyIIC();
	virtual ssize_t		WriteIIC(uint8 address, uint8 *data, size_t count);
	virtual ssize_t		WriteIIC8(uint8 address, uint8 data);
	virtual ssize_t		WriteIIC16(uint8 address, uint16 data);
	virtual ssize_t		ReadIIC(uint8 address, uint8 *data);
	virtual ssize_t		ReadIIC8(uint8 address, uint8 *data);
	virtual ssize_t		ReadIIC16(uint8 address, uint16 *data);
	virtual status_t	SetIICBitsMode(size_t bits=8);
	
	
	void				SetDataInput(BDataIO *input);
	virtual status_t	DataPumpThread();
	static int32		_DataPumpThread(void *_this);
	
	virtual void		DumpRegs();
	
	protected:
	virtual status_t	SendCommand(uint8 dir, uint8 request, uint16 value,
									uint16 index, uint16 length, void* data);
	virtual status_t	ProbeSensor();

	// USB transfer with retry and exponential backoff
			ssize_t		ControlTransferWithRetry(uint8 requestType,
									uint8 request, uint16 value, uint16 index,
									uint16 length, void* data,
									const usb_retry_config& config
										= kDefaultRetryConfig);
			ssize_t		BulkTransferWithRetry(const BUSBEndpoint* endpoint,
									void* data, size_t length,
									const usb_retry_config& config
										= kDefaultRetryConfig);
	static	usb_error_type	ClassifyUSBError(ssize_t error);
	static	bigtime_t	CalculateBackoffDelay(uint32 attempt,
									const usb_retry_config& config);
	CamSensor			*CreateSensor(const char *name);
		status_t		fInitStatus;
		flavor_info		fFlavorInfo;
		media_format	fMediaFormat;
		BString			fFlavorInfoNameStr;
		BString			fFlavorInfoInfoStr;
		CamSensor*		fSensor;
		CamDeframer*	fDeframer;
		BDataIO*		fDataInput; // where data from usb goes, likely fDeframer
		const BUSBEndpoint*	fBulkIn;
		const BUSBEndpoint*	fIsoIn;
		uint32			fIsoMaxPacketSize;
		int32			fFirstParameterID;
		bigtime_t		fLastParameterChanges;

	protected:
		friend class CamDeviceAddon;
		CamDeviceAddon&	fCamDeviceAddon;
		BUSBDevice*		fDevice;
		int				fSupportedDeviceIndex;
		bool			fChipIsBigEndian;
		int32			fTransferEnabled; // Changed to int32 for atomic operations
		thread_id		fPumpThread;
		BLocker			fLocker;
		uint8			*fBuffer;
		size_t			fBufferLen;
		BRect			fVideoFrame;
		int fDumpFD;

		// PHASE 3/4: USB packet statistics for error tracking
		uint32			fPacketSuccessCount;
		uint32			fPacketErrorCount;
		bigtime_t		fLastStatsReport;
		bigtime_t		fTransferStartTime;

		// PHASE 4: Packet loss monitoring thresholds
		static const float	kPacketLossThreshold;	// Threshold for resolution fallback (5%)
		static const bigtime_t	kStatsWindowSize;	// Window for stats calculation (5s)
		static const uint32	kMinPacketsForStats;	// Min packets before calculating rate
		uint32			fConsecutiveHighLossEvents;	// Track sustained high loss

		// PHASE 8: Error histogram
		usb_error_histogram	fErrorHistogram;

		// Group 4: Frame timing statistics
		frame_timing_stats	fFrameTimingStats;

		// Group 5: Error recovery configuration
		error_recovery_config	fErrorRecoveryConfig;

		// Debug logging flags (converted from static to instance members)
		bool			fFirstTransferLogged;
		int				fDroppedFramesLogged;

		// Performance optimization: Double buffering for USB transfers
		usb_double_buffer	fDoubleBuffer;

		// Logging throttle control
		int32			fLogThrottleCounter;
		bigtime_t		fLastLogTime;
		static const int32	kLogThrottleInterval = 1000;	// Log every N transfers
		static const bigtime_t	kLogTimeInterval = 5000000;	// Or every 5 seconds

		// Helper methods for double buffering
		status_t		InitDoubleBuffering(size_t bufferSize);
		void			CleanupDoubleBuffering();
		uint8*			GetActiveBuffer();
		uint8*			GetReadyBuffer();
		void			SwapBuffers();
};

// the addon itself, that instanciate

class CamDeviceAddon {
	public:
						CamDeviceAddon(WebCamMediaAddOn* webcam);
	virtual 			~CamDeviceAddon();
						
	virtual const char*	BrandName();
	virtual status_t	Sniff(BUSBDevice *device);
	virtual CamDevice*	Instantiate(CamRoster &roster, BUSBDevice *from);

	void				SetSupportedDevices(const usb_webcam_support_descriptor *devs);
	const usb_webcam_support_descriptor*	SupportedDevices() const
		{ return fSupportedDevices; };
	WebCamMediaAddOn*	WebCamAddOn() const { return fWebCamAddOn; };

	private:
	WebCamMediaAddOn*	fWebCamAddOn;
	const usb_webcam_support_descriptor*	fSupportedDevices; // last is {{0,0,0,0,0}, NULL, NULL, NULL }
};

// internal modules
#define B_WEBCAM_MKINTFUNC(modname) \
get_webcam_addon_##modname

// external addons -- UNIMPLEMENTED
extern "C" status_t get_webcam_addon(WebCamMediaAddOn* webcam,
	CamDeviceAddon **addon);
#define B_WEBCAM_ADDON_INSTANTIATION_FUNC_NAME "get_webcam_addon"

#endif // _CAM_DEVICE_H
