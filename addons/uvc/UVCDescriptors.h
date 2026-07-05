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


#endif	// _UVC_DESCRIPTORS_H
