/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * See UVCDescriptors.h for the design rationale.
 */
#include "UVCDescriptors.h"


// Field offsets within a UVC frame descriptor (little-endian on the wire).
enum {
	kOffLength				= 0,
	kOffWidth				= 5,	// uint16
	kOffHeight				= 7,	// uint16
	kOffMaxVideoFrameSize	= 17,	// uint32
	kOffFrameIntervalType	= 25,	// uint8
};

// Sanity ceilings (match the parser's historical descSane limits).
static const uint32 kMaxReasonableDim = 8192;
static const uint32 kMaxReasonableFrameSize = 50u * 1024 * 1024;	// 50 MB


static inline uint16
ReadLE16(const uint8* p)
{
	return (uint16)((uint32)p[0] | ((uint32)p[1] << 8));
}


static inline uint32
ReadLE32(const uint8* p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8)
		| ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}


UVCFrameDescCheck
UVCCheckFrameDescriptor(const uint8* bytes, size_t avail)
{
	UVCFrameDescCheck r = { false, 0, 0, 0, 0 };

	// Need the whole fixed header present to read the sanity fields safely.
	if (bytes == NULL || avail < kUVCFrameDescFixedLen)
		return r;

	// The descriptor's own length must be internally consistent: at least the
	// fixed header, and no larger than what is actually available. This is the
	// gate that stops a lying bLength from driving out-of-bounds reads below.
	const uint8 bLength = bytes[kOffLength];
	if (bLength < kUVCFrameDescFixedLen || (size_t)bLength > avail)
		return r;

	const uint16 width = ReadLE16(bytes + kOffWidth);
	const uint16 height = ReadLE16(bytes + kOffHeight);
	const uint32 maxBuf = ReadLE32(bytes + kOffMaxVideoFrameSize);
	const uint8 ftype = bytes[kOffFrameIntervalType];

	// Discrete intervals (4 bytes each) sit after the fixed header, bounded by
	// the descriptor's own (already validated) bLength.
	const uint32 maxIntervals =
		(uint32)(bLength - kUVCFrameDescFixedLen) / (uint32)sizeof(uint32);

	if (width == 0 || height == 0
		|| width > kMaxReasonableDim || height > kMaxReasonableDim
		|| maxBuf > kMaxReasonableFrameSize
		|| (ftype != 0 && (uint32)ftype > maxIntervals)) {
		return r;	// malformed / out of range
	}

	r.valid = true;
	r.width = width;
	r.height = height;
	r.maxVideoFrameSize = maxBuf;
	r.frameIntervalType = ftype;	// <= maxIntervals, so buffer-safe to iterate
	return r;
}
