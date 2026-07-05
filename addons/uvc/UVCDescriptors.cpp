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


// --- Bounds-checked field readers -------------------------------------------

uint8
UVCDescByte(const uint8* desc, size_t len, size_t off)
{
	if (desc == NULL || off >= len)
		return 0;
	return desc[off];
}


uint16
UVCDescLE16(const uint8* desc, size_t len, size_t off)
{
	if (desc == NULL || off + 2 > len)
		return 0;
	return (uint16)((uint32)desc[off] | ((uint32)desc[off + 1] << 8));
}


uint32
UVCDescLE32(const uint8* desc, size_t len, size_t off)
{
	if (desc == NULL || off + 4 > len)
		return 0;
	return (uint32)desc[off] | ((uint32)desc[off + 1] << 8)
		| ((uint32)desc[off + 2] << 16) | ((uint32)desc[off + 3] << 24);
}


// --- Frame descriptor validation --------------------------------------------

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
	const uint8 bLength = UVCDescByte(bytes, avail, kOffLength);
	if (bLength < kUVCFrameDescFixedLen || (size_t)bLength > avail)
		return r;

	// All field reads go through the bounds-checked readers (belt and braces:
	// avail >= fixed header is already guaranteed above).
	const uint16 width = UVCDescLE16(bytes, avail, kOffWidth);
	const uint16 height = UVCDescLE16(bytes, avail, kOffHeight);
	const uint32 maxBuf = UVCDescLE32(bytes, avail, kOffMaxVideoFrameSize);
	const uint8 ftype = UVCDescByte(bytes, avail, kOffFrameIntervalType);

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


// --- Safe descriptor walker -------------------------------------------------

UVCDescriptorCursor::UVCDescriptorCursor(const uint8* buf, size_t len)
	:
	fBuf(buf),
	fLen(buf != NULL ? len : 0),
	fPos(0)
{
}


void
UVCDescriptorCursor::Reset()
{
	fPos = 0;
}


bool
UVCDescriptorCursor::Next(const uint8** outDesc, size_t* outLen)
{
	// Need at least the 2-byte header (bLength, bDescriptorType).
	if (fPos + 2 > fLen)
		return false;

	const uint8 bLength = fBuf[fPos];

	// A length below the header, or one that would run past the buffer, ends
	// the walk instead of reading past the end.
	if (bLength < 2 || fPos + (size_t)bLength > fLen)
		return false;

	if (outDesc != NULL)
		*outDesc = fBuf + fPos;
	if (outLen != NULL)
		*outLen = bLength;

	fPos += bLength;
	return true;
}
