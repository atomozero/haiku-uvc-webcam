/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * Bounds-safe validation of raw UVC class descriptors. USB descriptors carry
 * device-controlled length/count fields; trusting them is how a hostile or
 * buggy camera walks the parser off the end of a buffer. These helpers read
 * only within an explicit `avail` bound and never trust an internal bLength
 * beyond it. Being pure functions over raw bytes they are unit-tested AND
 * fuzzed (tests/test_descriptors.cpp, tests/fuzz_descriptors.cpp), and the
 * production parser calls them so the fuzzed code IS the shipped code.
 */
#ifndef _UVC_DESCRIPTORS_H
#define _UVC_DESCRIPTORS_H


#include <SupportDefs.h>
#include <stddef.h>


// Result of validating a VS_FRAME_UNCOMPRESSED / VS_FRAME_MJPEG descriptor.
struct UVCFrameDescCheck {
	bool	valid;				// safe and sane to use
	uint16	width;
	uint16	height;
	uint32	maxVideoFrameSize;
	uint8	frameIntervalType;	// number of discrete intervals that are safe
								// to read from this buffer (0 == continuous);
								// guaranteed: 26 + 4*frameIntervalType <= avail
};

// Fixed portion of a frame descriptor: bytes 0..25, i.e. everything up to and
// including bFrameIntervalType. Discrete intervals (4 bytes each) follow.
static const size_t kUVCFrameDescFixedLen = 26;

// Validate a raw VS_FRAME_* descriptor. `avail` is the number of bytes that are
// actually readable at `bytes`; the function never reads past it, no matter
// what the descriptor's own bLength claims. Returns valid=false for anything
// too short, inconsistent, or out of sane range.
UVCFrameDescCheck UVCCheckFrameDescriptor(const uint8* bytes, size_t avail);


// Result of validating a VS_FORMAT_UNCOMPRESSED descriptor.
struct UVCUncompressedFormatCheck {
	bool	valid;
	uint8	formatIndex;			// 1-based; 0 is rejected
	uint8	numFrameDescriptors;
	uint8	defaultFrameIndex;
	uint8	bitsPerPixel;
	uint8	guid[16];				// safely copied 16-byte format GUID
};

// Fixed length of a VS_FORMAT_UNCOMPRESSED descriptor (UVC spec: 27 bytes).
static const size_t kUVCUncFormatFixedLen = 27;

// Validate a raw VS_FORMAT_UNCOMPRESSED descriptor, reading only within
// `avail`. The 16-byte GUID is copied out safely (zeroed if unavailable) so the
// caller never identifies a format from stale memory.
UVCUncompressedFormatCheck UVCCheckUncompressedFormatDescriptor(
	const uint8* bytes, size_t avail);


// Result of validating a VC_EXTENSION_UNIT descriptor. This descriptor is the
// riskiest to parse: it has two device-controlled variable-length arrays
// (source_id[bNrInPins] and bmControls[bControlSize]), and the offset of
// bControlSize and iExtension is computed from those untrusted counts.
struct UVCExtensionUnitCheck {
	bool	valid;
	uint8	unitID;
	uint8	numControls;
	uint8	numInputPins;
	uint8	guid[16];			// safely copied 16-byte extension GUID
	uint8	controlSize;		// bControlSize, or 0 if not within bounds
	uint8	iExtension;			// string index, or 0 if not within bounds
	uint8	sourceIds[8];		// first up to 8 source ids
	uint8	sourceIdCount;		// number of valid entries in sourceIds[]
};

// Fixed prefix of a VC_EXTENSION_UNIT descriptor: bytes 0..21 (through
// bNrInPins). The variable arrays follow.
static const size_t kUVCXUFixedLen = 22;

// Validate a raw VC_EXTENSION_UNIT descriptor, reading only within `avail`. The
// GUID/unit id/control count are extracted when the fixed prefix is present;
// bControlSize and iExtension are read only when their device-computed offsets
// fall inside the descriptor (0 otherwise), so the notorious Extension()
// offset math can never run off the end.
UVCExtensionUnitCheck UVCCheckExtensionUnitDescriptor(
	const uint8* bytes, size_t avail);


// Fixed prefix lengths of the VS input/output header descriptors, i.e. the
// offset at which the bmaControls[bNumFormats][bControlSize] array begins.
static const size_t kUVCVSInputHeaderFixedLen = 13;
static const size_t kUVCVSOutputHeaderFixedLen = 9;

// Number of bmaControls entries that fit fully inside a VS input/output header
// of length `bLength`, whose array starts at `arrayOffset` and whose entries
// are `controlSize` bytes each. Never exceeds `numFormats`, and returns 0 when
// controlSize is 0 (a zero-stride array would otherwise loop forever). Use it
// to bound the bmaControls iteration so a large bNumFormats/bControlSize can't
// walk the parser past the descriptor.
uint8 UVCVSHeaderSafeFormatCount(uint8 numFormats, uint8 controlSize,
	size_t arrayOffset, uint8 bLength);


// --- Bounds-checked field readers -------------------------------------------
// Read a little-endian field from a descriptor of `len` readable bytes. If the
// field is not fully within [0, len) they return 0 instead of reading past the
// end — so parsing code can never be walked off a descriptor by a bad offset.
uint8  UVCDescByte(const uint8* desc, size_t len, size_t off);
uint16 UVCDescLE16(const uint8* desc, size_t len, size_t off);
uint32 UVCDescLE32(const uint8* desc, size_t len, size_t off);


// --- Safe descriptor walker -------------------------------------------------
// Iterates a raw buffer of consecutive USB descriptors ([bLength][bType]...)
// with full bounds checking. It never reads past the buffer and stops cleanly
// at the end or at the first length that is malformed (bLength < 2) or would
// run past the buffer — turning "walk off the end" into "stop iterating".
class UVCDescriptorCursor {
public:
						UVCDescriptorCursor(const uint8* buf, size_t len);

	// Yield the next descriptor: set *outDesc/*outLen to its bytes and length
	// (always >= 2 and fully within the buffer), advance, and return true.
	// Return false at the end of the buffer or on a malformed length.
	bool				Next(const uint8** outDesc, size_t* outLen);

	void				Reset();

private:
	const uint8*		fBuf;
	size_t				fLen;
	size_t				fPos;
};


#endif	// _UVC_DESCRIPTORS_H
