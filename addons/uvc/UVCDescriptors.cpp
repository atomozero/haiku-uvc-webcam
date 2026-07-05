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


// --- Uncompressed format descriptor validation ------------------------------

// Field offsets within a VS_FORMAT_UNCOMPRESSED descriptor.
enum {
	kOffFmtIndex			= 3,	// bFormatIndex
	kOffNumFrameDesc		= 4,	// bNumFrameDescriptors
	kOffGuid				= 5,	// guidFormat[16]
	kOffBitsPerPixel		= 21,	// bBitsPerPixel
	kOffDefaultFrameIndex	= 22,	// bDefaultFrameIndex
};

UVCUncompressedFormatCheck
UVCCheckUncompressedFormatDescriptor(const uint8* bytes, size_t avail)
{
	UVCUncompressedFormatCheck r;
	r.valid = false;
	r.formatIndex = 0;
	r.numFrameDescriptors = 0;
	r.defaultFrameIndex = 0;
	r.bitsPerPixel = 0;
	for (int i = 0; i < 16; i++)
		r.guid[i] = 0;

	if (bytes == NULL || avail < kUVCUncFormatFixedLen)
		return r;

	const uint8 bLength = UVCDescByte(bytes, avail, kOffLength);
	if (bLength < kUVCUncFormatFixedLen || (size_t)bLength > avail)
		return r;

	const uint8 formatIndex = UVCDescByte(bytes, avail, kOffFmtIndex);
	if (formatIndex == 0)	// UVC indices are 1-based; 0 is unusable
		return r;

	r.formatIndex = formatIndex;
	r.numFrameDescriptors = UVCDescByte(bytes, avail, kOffNumFrameDesc);
	for (int i = 0; i < 16; i++)
		r.guid[i] = UVCDescByte(bytes, avail, kOffGuid + i);
	r.bitsPerPixel = UVCDescByte(bytes, avail, kOffBitsPerPixel);
	r.defaultFrameIndex = UVCDescByte(bytes, avail, kOffDefaultFrameIndex);
	r.valid = true;
	return r;
}


// --- Extension unit descriptor validation -----------------------------------

// Field offsets within a VC_EXTENSION_UNIT descriptor.
enum {
	kOffXUUnitID			= 3,	// bUnitID
	kOffXUGuid				= 4,	// guidExtensionCode[16]
	kOffXUNumControls		= 20,	// bNumControls
	kOffXUNumInputPins		= 21,	// bNrInPins
	kOffXUSourceIds			= 22,	// source_id[bNrInPins]
};

UVCExtensionUnitCheck
UVCCheckExtensionUnitDescriptor(const uint8* bytes, size_t avail)
{
	UVCExtensionUnitCheck r;
	r.valid = false;
	r.unitID = 0;
	r.numControls = 0;
	r.numInputPins = 0;
	r.controlSize = 0;
	r.iExtension = 0;
	r.sourceIdCount = 0;
	for (int i = 0; i < 16; i++)
		r.guid[i] = 0;
	for (int i = 0; i < 8; i++)
		r.sourceIds[i] = 0;

	// The fixed prefix (through bNrInPins) must be present; that also covers
	// the 16-byte GUID (offsets 4..19).
	if (bytes == NULL || avail < kUVCXUFixedLen)
		return r;

	const uint8 bLength = UVCDescByte(bytes, avail, kOffLength);
	if (bLength < kUVCXUFixedLen || (size_t)bLength > avail)
		return r;

	r.unitID = UVCDescByte(bytes, avail, kOffXUUnitID);
	for (int i = 0; i < 16; i++)
		r.guid[i] = UVCDescByte(bytes, avail, kOffXUGuid + i);
	r.numControls = UVCDescByte(bytes, avail, kOffXUNumControls);

	const uint8 pins = UVCDescByte(bytes, avail, kOffXUNumInputPins);
	r.numInputPins = pins;

	// Source IDs start at offset 22, one byte per input pin. Copy up to 8, and
	// only those that lie inside the descriptor's own bLength.
	size_t si = 0;
	for (uint8 i = 0; i < pins && si < 8; i++) {
		const size_t off = kOffXUSourceIds + (size_t)i;
		if (off >= (size_t)bLength)
			break;
		r.sourceIds[si++] = UVCDescByte(bytes, avail, off);
	}
	r.sourceIdCount = (uint8)si;

	// bControlSize sits right after the source_id array (offset 22 + pins), and
	// iExtension after the bmControls array (offset 22 + pins + 1 + ctrlSize).
	// Both offsets are device-computed, so read each only when it falls inside
	// the descriptor; otherwise leave it 0 rather than reading past the end.
	const size_t ctrlSizeOff = (size_t)kOffXUSourceIds + (size_t)pins;
	if (ctrlSizeOff < (size_t)bLength)
		r.controlSize = UVCDescByte(bytes, avail, ctrlSizeOff);

	const size_t iExtOff = ctrlSizeOff + 1 + (size_t)r.controlSize;
	if (iExtOff < (size_t)bLength)
		r.iExtension = UVCDescByte(bytes, avail, iExtOff);

	// The identifying part (unit id + GUID) is safely in hand; keep the unit so
	// a slightly-truncated descriptor doesn't cost a usable vendor feature.
	r.valid = true;
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
