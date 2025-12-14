/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Central configuration constants for the webcam driver.
 * Group 7: Architecture Optimizations
 */
#ifndef _CAM_CONFIG_H
#define _CAM_CONFIG_H


#include <OS.h>


// =============================================================================
// USB Transfer Configuration
// =============================================================================

namespace CamConfig {

// USB transfer buffer sizes
static const size_t kUSBBufferSize			= 131072;	// 128KB receive buffer
static const size_t kUSBMaxPacketSize		= 1024;		// Max isochronous packet
static const size_t kUSBMaxTransfers		= 16;		// Concurrent transfers

// =============================================================================
// USB 3.0 SuperSpeed Configuration
// =============================================================================
// USB 3.0+ devices can achieve much higher bandwidth than USB 2.0.
// These constants enable optimal transfer sizes for SuperSpeed.

// USB speed bandwidth limits (bytes per second)
static const uint64 kUSB2HighSpeedBandwidth		= 60000000;		// ~60 MB/s (480 Mbps)
static const uint64 kUSB3SuperSpeedBandwidth	= 625000000;	// ~625 MB/s (5 Gbps)
static const uint64 kUSB3SuperSpeedPlusBW		= 1250000000;	// ~1.25 GB/s (10 Gbps)

// Maximum packet sizes by speed
static const uint16 kUSB2MaxIsochPacket		= 1024;		// USB 2.0 HS max
static const uint16 kUSB3MaxIsochPacket		= 1024;		// USB 3.0 SS base (up to 48KB burst)
static const uint32 kUSB3MaxBurstPayload	= 49152;	// 48KB (1024 * 3 * 16 max burst)

// Buffer size multipliers for SuperSpeed
// USB 3.0 can handle larger transfers more efficiently
static const uint32 kUSB2BufferMultiplier	= 1;		// Standard buffers
static const uint32 kUSB3BufferMultiplier	= 2;		// 2x buffers for SuperSpeed

// Optimal transfer sizes by speed
static const size_t kUSB2OptimalTransfer	= 131072;	// 128KB for USB 2.0
static const size_t kUSB3OptimalTransfer	= 262144;	// 256KB for USB 3.0

// Retry configuration
static const uint32 kUSBMaxRetries			= 3;
static const bigtime_t kUSBInitialDelay		= 100000;	// 100ms
static const bigtime_t kUSBMaxDelay			= 1000000;	// 1s
static const float kUSBBackoffMultiplier	= 2.0f;


// =============================================================================
// Frame Buffer Configuration
// =============================================================================

// Frame pool settings
static const uint32 kFramePoolCapacity		= 12;		// Max pooled frames
static const uint32 kMaxQueuedFrames		= 8;		// Max frames in queue
static const size_t kMaxFrameSize			= 4147200;	// 1080p YUY2

// Default frame dimensions
static const uint32 kDefaultWidth			= 640;
static const uint32 kDefaultHeight			= 480;


// =============================================================================
// Timing Configuration
// =============================================================================

// Frame timing
static const bigtime_t kDefaultFrameInterval	= 33333;	// 30fps
static const bigtime_t kMinFrameTimeout			= 10000;	// 10ms
static const bigtime_t kMaxFrameTimeout			= 500000;	// 500ms
static const bigtime_t kDefaultFrameTimeout		= 100000;	// 100ms

// Statistics reporting intervals
static const bigtime_t kStatsReportInterval		= 30000000;	// 30s
static const bigtime_t kErrorStatsWindow		= 5000000;	// 5s


// =============================================================================
// Error Handling Configuration
// =============================================================================

// Packet loss thresholds
static const float kPacketLossWarning		= 0.05f;	// 5%
static const float kPacketLossAction		= 0.10f;	// 10%
static const uint32 kMinPacketsForStats		= 100;

// Consecutive error thresholds
static const uint32 kMaxConsecutiveErrors	= 20;
static const uint32 kMaxRecoveryAttempts	= 5;


// =============================================================================
// Debug Configuration
// =============================================================================

// Logging throttle
static const int32 kLogThrottleInterval		= 1000;		// Log every N transfers
static const bigtime_t kLogTimeInterval		= 5000000;	// Or every 5s
static const int32 kMaxInitialLogs			= 5;		// Initial log count


// =============================================================================
// Video Format Configuration
// =============================================================================

// Bytes per pixel
static const uint32 kBytesPerPixelYUY2		= 2;
static const uint32 kBytesPerPixelRGB32		= 4;

// Common resolutions (width, height)
struct Resolution {
	uint32 width;
	uint32 height;

	size_t YUY2Size() const { return width * height * kBytesPerPixelYUY2; }
	size_t RGB32Size() const { return width * height * kBytesPerPixelRGB32; }
};

static const Resolution kResolution160x120	= { 160, 120 };
static const Resolution kResolution320x240	= { 320, 240 };
static const Resolution kResolution640x480	= { 640, 480 };
static const Resolution kResolution1280x720	= { 1280, 720 };
static const Resolution kResolution1920x1080= { 1920, 1080 };


// =============================================================================
// XHCI TBC/TLBPC Isochronous Transfer Configuration
// =============================================================================
// TBC (Transfer Burst Count) and TLBPC (Transfer Last Burst Packet Count)
// are XHCI-specific fields for isochronous TRBs that optimize packet delivery.
// The driver uses these values to predict and handle burst boundaries.

// Maximum TBC value (0-3, representing 1-4 bursts)
static const uint8 kMaxTBC					= 3;

// USB 2.0 high-speed: mult factor (additional transactions per microframe)
// Encoded in bits 12:11 of wMaxPacketSize: 0=1, 1=2, 2=3 transactions
static const uint8 kUSB2MaxMult				= 3;		// Max 3 transactions/microframe

// USB 3.0 SuperSpeed: max burst value (0-15, representing 1-16 packets)
static const uint8 kUSB3MaxBurst			= 15;		// Max 16 packets/burst

// Expected packet completion rates by controller type
static const float kXHCIPacketCompletionRate	= 0.999f;	// 99.9% with TBC/TLBPC
static const float kEHCIPacketCompletionRate	= 0.995f;	// 99.5% without TBC/TLBPC

// Burst boundary handling
static const uint32 kBurstBoundaryTolerance	= 2;		// Allow 2 missing packets before error
static const uint32 kMaxPartialBursts		= 5;		// Max partial bursts before fallback


// =============================================================================
// XHCI Interrupt Moderation Configuration
// =============================================================================
// These values match the kernel XHCI driver's dynamic IMOD settings.
// The driver uses these to optimize buffer polling and timing.

// IMOD values (in 250ns units) and resulting IRQ rates
static const uint32 kXHCI_IMOD_LowLatency	= 0x00FA;	// 16000 IRQ/s (62.5us)
static const uint32 kXHCI_IMOD_Medium		= 0x01F4;	// 8000 IRQ/s (125us)
static const uint32 kXHCI_IMOD_Default		= 0x03F8;	// 4000 IRQ/s (250us)
static const uint32 kXHCI_IMOD_PowerSave	= 0x07D0;	// 2000 IRQ/s (500us)

// Expected interrupt intervals in microseconds
static const bigtime_t kIRQIntervalLowLatency	= 63;	// ~62.5us
static const bigtime_t kIRQIntervalMedium		= 125;
static const bigtime_t kIRQIntervalDefault		= 250;
static const bigtime_t kIRQIntervalPowerSave	= 500;

// Optimal poll intervals based on IMOD mode (2x IRQ interval for safety)
static const bigtime_t kPollIntervalLowLatency	= 125;	// Fast polling for isoch
static const bigtime_t kPollIntervalMedium		= 250;
static const bigtime_t kPollIntervalDefault		= 500;
static const bigtime_t kPollIntervalPowerSave	= 1000;

// Buffer accumulation hints based on IMOD
// At low latency (16000 IRQ/s), expect data every 62.5us
// For 30fps video (33.3ms/frame), expect ~533 IRQs per frame
static const uint32 kIRQsPerFrameLowLatency		= 533;
static const uint32 kIRQsPerFrameMedium			= 267;
static const uint32 kIRQsPerFrameDefault		= 133;


// =============================================================================
// UVC Protocol Configuration
// =============================================================================

// UVC header flags
static const uint8 kUVCHeaderFlagFID		= 0x01;		// Frame ID
static const uint8 kUVCHeaderFlagEOF		= 0x02;		// End of Frame
static const uint8 kUVCHeaderFlagPTS		= 0x04;		// Has PTS
static const uint8 kUVCHeaderFlagSCR		= 0x08;		// Has SCR
static const uint8 kUVCHeaderFlagError		= 0x40;		// Error bit

// UVC header sizes
static const uint8 kUVCMinHeaderSize		= 2;
static const uint8 kUVCHeaderWithPTS		= 6;		// 2 + 4 (PTS)
static const uint8 kUVCHeaderWithSCR		= 8;		// 2 + 6 (SCR)
static const uint8 kUVCHeaderFull			= 12;		// 2 + 4 + 6


// =============================================================================
// Helper Functions
// =============================================================================

// Calculate YUY2 frame size from dimensions
inline size_t
CalculateYUY2Size(uint32 width, uint32 height)
{
	return width * height * kBytesPerPixelYUY2;
}

// Calculate RGB32 frame size from dimensions
inline size_t
CalculateRGB32Size(uint32 width, uint32 height)
{
	return width * height * kBytesPerPixelRGB32;
}

// Calculate expected frame interval from FPS
inline bigtime_t
FPSToInterval(float fps)
{
	if (fps <= 0.0f)
		return kDefaultFrameInterval;
	return (bigtime_t)(1000000.0f / fps);
}

// Calculate FPS from frame interval
inline float
IntervalToFPS(bigtime_t interval)
{
	if (interval <= 0)
		return 30.0f;  // Default
	return 1000000.0f / interval;
}

}  // namespace CamConfig


#endif /* _CAM_CONFIG_H */
