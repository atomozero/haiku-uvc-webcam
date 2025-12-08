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
