/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * Portable overflow-safe helpers for the webcam driver. These functions have
 * no Haiku dependencies (only <stdint.h>) so they are unit-tested and fuzzed
 * on plain Linux CI, and the production driver calls them on Haiku.
 *
 * Coverage:
 *   FIX-C3  Producer Connect() heap overflow (w*h*4)
 *   FIX-M5  memset(dst,0,w*h*4) int32 overflow in YUV converters
 *   FIX-H8  audio ring/buffer size overflow from untrusted descriptors
 *   FIX-T10 field_rate float/div-by-zero in Producer frame pacing
 *   FIX-M8  RingBufferIndex modulo-zero (capacity guard)
 *   FIX-B4  CalculateBackoffDelay float overflow / invalid config
 */
#ifndef _UVC_SAFETY_H
#define _UVC_SAFETY_H


#ifdef __HAIKU__
#	include <SupportDefs.h>
#else
#	include <stdint.h>
#	include <stddef.h>
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef uint64_t uint64;
typedef int64_t int64;
#endif


// --- Overflow-checked size math -------------------------------------------
// All helpers return false on overflow / invalid input and leave *out
// untouched, so callers can fail closed before any allocation or memset.

bool UVCSafeMulSize(size_t a, size_t b, size_t* out);
bool UVCSafeMul3Size(size_t a, size_t b, size_t c, size_t* out);

// YUY2 = w*h*2, RGB32 = w*h*4. Dimensions come from device descriptors and
// must be bounded before use; the ceiling matches the descriptor validator
// (kMaxReasonableDim = 8192).
bool UVCSafeYUY2Size(uint32 width, uint32 height, size_t* out);
bool UVCSafeRGB32Size(uint32 width, uint32 height, size_t* out);

// Producer Connect() buffer: 4 * line_count * line_width with the same
// overflow discipline. Rejects zero dimensions (malloc(0) guard).
bool UVCSafeProducerBufferSize(uint32 lineWidth, uint32 lineCount,
	size_t* out);

// Audio ring: sampleRate * channels * 2 (bytes/sample) * 2 (double buffer).
// Computed in 64 bit with saturation, then clamped to [16KB, 256KB].
// Returns false when the inputs are unusable (rate/channels == 0).
bool UVCSafeAudioRingSize(uint32 sampleRate, uint8 channels, size_t* out);

// Audio iso buffer: maxPacketSize * packetCount. Rejects maxPacketSize == 0.
bool UVCSafeAudioBufferSize(uint32 maxPacketSize, uint32 packetCount,
	size_t* out);


// --- Frame pacing ----------------------------------------------------------
// Convert a float field_rate (fps) to a frame duration in microseconds.
// Returns false and leaves *out untouched when the rate is not a sane
// positive frequency; callers must fall back to 33333 (30 fps). This avoids
// the INF -> int64 cast UB of (bigtime_t)(1000000 / 0.0f).
bool UVCSafeFieldInterval(float fieldRate, int64* outUsec);


// --- Retry backoff ----------------------------------------------------------
// Validate a retry configuration triple before use.
bool UVCSafeRetryConfig(int64 initialDelay, int64 maxDelay, float multiplier);

// Pure exponential backoff in double precision, clamped before the cast so
// no INF/precision loss can reach the integer domain. Returns maxDelay when
// the config is invalid (fail closed with the longest wait).
int64 UVCSafeBackoffDelay(uint32 attempt, int64 initialDelay, int64 maxDelay,
	float multiplier);


// --- Ring index -------------------------------------------------------------
// Guard for RingBufferIndex: capacity 0 would make every % fCapacity a
// SIGFPE. Normalises 0 to 1 so the structure stays usable.
uint32 UVCGuardRingCapacity(uint32 capacity);


// --- Descriptor array bounds ------------------------------------------------
// Number of source_id entries of a selector-unit that fit in bLength.
// fixedLen covers everything up to and including num_input_pins.
uint8 UVCSafeSelectorSources(uint8 numPins, size_t fixedLen, uint8 bLength);

// Number of bmControls entries of a VS input/output header that fit.
// Mirrors UVCVSHeaderSafeFormatCount for the VC header / selector family.
uint8 UVCSafeArrayCount(uint8 count, uint8 entrySize, size_t arrayOffset,
	uint8 bLength);

// Still-image frame descriptor: 6-byte fixed prefix + 4 bytes per size
// pattern + 1 count byte + M compression bytes. Returns false when the
// (numSizes, numCompressions) pair cannot fit in bLength.
bool UVCSafeStillImageCounts(uint8 numSizes, uint8 numCompressions,
	uint8 bLength);

// Audio Format Type I: sampleFreqType == 0 needs 6 trailing bytes
// (min+max, 3 bytes each), else 3 bytes per discrete frequency.
bool UVCSafeAudioFormatICount(uint8 freqType, size_t avail);


// Minimum readable length for small terminal/control descriptors, so the
// parser can reject truncated blobs before dereferencing fields.
static const size_t kUVCMinVCHeaderLen = 12;
static const size_t kUVCMinInputTerminalLen = 8;
static const size_t kUVCMinCameraTerminalLen = 15;
static const size_t kUVCMinOutputTerminalLen = 9;
static const size_t kUVCMinSelectorLen = 6;
static const size_t kUVCMinProcessingLen = 8;
static const size_t kUVCMinAudioInputTerminalLen = 12;
static const size_t kUVCMinAudioFeatureUnitLen = 7;
static const size_t kUVCMinAudioFormatTypeILen = 8;
static const size_t kUVCMinStillImageLen = 6;

// Continuous frame descriptors carry min/max/step (12 bytes) after the
// 26-byte fixed header, hence 38 bytes minimum.
static const size_t kUVCFrameContinuousMinLen = 38;


#endif	// _UVC_SAFETY_H
