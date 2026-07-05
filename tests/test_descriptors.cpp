/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * Unit tests for the bounds-safe UVC frame-descriptor validator.
 *
 * Build & run:
 *   g++ -O2 -o test_descriptors test_descriptors.cpp \
 *       ../addons/uvc/UVCDescriptors.cpp -I../addons/uvc -lbe && ./test_descriptors
 */
#include <cstdio>
#include <cstring>

#include "UVCDescriptors.h"

static int sPass = 0;
static int sFail = 0;

static void
Expect(const char* name, bool cond)
{
	if (cond) {
		sPass++;
	} else {
		sFail++;
		printf("  FAIL %s\n", name);
	}
}


// Build a minimal well-formed frame descriptor into `buf`. `intervals` discrete
// intervals -> bLength = 26 + 4*intervals (ftype=intervals); intervals<0 means
// continuous (ftype=0, bLength=38). Returns bLength.
static uint8
MakeFrame(uint8* buf, uint16 w, uint16 h, uint32 maxBuf, int intervals)
{
	memset(buf, 0, 64);
	uint8 ftype = intervals < 0 ? 0 : (uint8)intervals;
	uint8 bLength = intervals < 0 ? 38 : (uint8)(26 + 4 * intervals);
	buf[0] = bLength;
	buf[1] = 0x24;			// CS_INTERFACE
	buf[2] = 0x05;			// VS_FRAME_UNCOMPRESSED
	buf[3] = 1;				// frame index
	buf[5] = (uint8)(w & 0xff); buf[6] = (uint8)(w >> 8);
	buf[7] = (uint8)(h & 0xff); buf[8] = (uint8)(h >> 8);
	buf[17] = (uint8)(maxBuf & 0xff);       buf[18] = (uint8)((maxBuf >> 8) & 0xff);
	buf[19] = (uint8)((maxBuf >> 16) & 0xff); buf[20] = (uint8)((maxBuf >> 24) & 0xff);
	buf[25] = ftype;
	return bLength;
}


int
main()
{
	printf("=== UVCDescriptors test suite ===\n");
	uint8 buf[64];

	// Well-formed, continuous.
	uint8 len = MakeFrame(buf, 640, 480, 640 * 480 * 2, -1);
	UVCFrameDescCheck c = UVCCheckFrameDescriptor(buf, len);
	Expect("valid continuous", c.valid && c.width == 640 && c.height == 480
		&& c.frameIntervalType == 0);

	// Well-formed, 3 discrete intervals.
	len = MakeFrame(buf, 1280, 720, 1280 * 720 * 2, 3);
	c = UVCCheckFrameDescriptor(buf, len);
	Expect("valid 3 discrete", c.valid && c.frameIntervalType == 3);

	// Too short to hold the fixed header.
	c = UVCCheckFrameDescriptor(buf, 20);
	Expect("reject short avail", !c.valid);

	// bLength claims more than avail (lying length).
	len = MakeFrame(buf, 640, 480, 100, 3);	// bLength = 38
	c = UVCCheckFrameDescriptor(buf, 30);	// but only 30 bytes available
	Expect("reject bLength > avail", !c.valid);

	// bLength smaller than the fixed header.
	MakeFrame(buf, 640, 480, 100, -1);
	buf[0] = 20;
	c = UVCCheckFrameDescriptor(buf, 38);
	Expect("reject bLength < fixed", !c.valid);

	// Zero / oversized dimensions.
	len = MakeFrame(buf, 0, 480, 100, -1);
	Expect("reject width 0", !UVCCheckFrameDescriptor(buf, len).valid);
	len = MakeFrame(buf, 640, 0, 100, -1);
	Expect("reject height 0", !UVCCheckFrameDescriptor(buf, len).valid);
	len = MakeFrame(buf, 9000, 480, 100, -1);
	Expect("reject width > 8192", !UVCCheckFrameDescriptor(buf, len).valid);

	// Oversized frame buffer.
	len = MakeFrame(buf, 640, 480, 60u * 1024 * 1024, -1);
	Expect("reject maxbuf > 50MB", !UVCCheckFrameDescriptor(buf, len).valid);

	// frame_interval_type exceeds what bLength can hold (bLength=26 -> 0 slots).
	MakeFrame(buf, 640, 480, 100, 0);	// bLength = 26, ftype set below
	buf[25] = 5;						// claim 5 discrete intervals
	c = UVCCheckFrameDescriptor(buf, 26);
	Expect("reject ftype > capacity", !c.valid);

	// frame_interval_type exactly at capacity.
	len = MakeFrame(buf, 640, 480, 100, 1);	// bLength=30, ftype=1
	c = UVCCheckFrameDescriptor(buf, len);
	Expect("accept ftype at capacity", c.valid && c.frameIntervalType == 1);

	// NULL / zero.
	Expect("reject NULL", !UVCCheckFrameDescriptor(NULL, 38).valid);
	Expect("reject avail 0", !UVCCheckFrameDescriptor(buf, 0).valid);

	// --- Bounds-checked field readers ---
	uint8 fld[4] = { 0x11, 0x22, 0x33, 0x44 };
	Expect("LE16 in range", UVCDescLE16(fld, 4, 0) == 0x2211);
	Expect("LE32 in range", UVCDescLE32(fld, 4, 0) == 0x44332211);
	Expect("LE16 out of range -> 0", UVCDescLE16(fld, 4, 3) == 0);
	Expect("LE32 out of range -> 0", UVCDescLE32(fld, 4, 1) == 0);
	Expect("Byte out of range -> 0", UVCDescByte(fld, 4, 4) == 0);

	// --- Safe descriptor walker ---
	// Two well-formed descriptors: lengths 4 and 6.
	uint8 seq[10] = { 4, 0x24, 0xAA, 0xBB,   6, 0x24, 1, 2, 3, 4 };
	{
		UVCDescriptorCursor cur(seq, sizeof(seq));
		const uint8* d; size_t l; int n = 0;
		bool ok = true;
		while (cur.Next(&d, &l)) {
			if (n == 0) ok = ok && (l == 4 && d == seq);
			if (n == 1) ok = ok && (l == 6 && d == seq + 4);
			n++;
			if (n > 8) { ok = false; break; }	// runaway guard
		}
		Expect("walk two descriptors", ok && n == 2);
	}
	// Truncated tail: second descriptor claims 6 bytes but only 3 remain.
	{
		uint8 trunc[7] = { 4, 0x24, 0xAA, 0xBB,   6, 0x24, 1 };
		UVCDescriptorCursor cur(trunc, sizeof(trunc));
		const uint8* d; size_t l; int n = 0;
		while (cur.Next(&d, &l)) { n++; if (n > 8) break; }
		Expect("stop at truncated tail", n == 1);
	}
	// Malformed length (bLength < 2) stops the walk.
	{
		uint8 bad[4] = { 1, 0x24, 0, 0 };
		UVCDescriptorCursor cur(bad, sizeof(bad));
		const uint8* d; size_t l;
		Expect("stop on bLength < 2", !cur.Next(&d, &l));
	}
	// Empty / NULL buffers yield nothing.
	{
		UVCDescriptorCursor cur(NULL, 10);
		const uint8* d; size_t l;
		Expect("NULL buffer -> no walk", !cur.Next(&d, &l));
	}

	printf("\n%d passed, %d failed\n", sPass, sFail);
	return sFail == 0 ? 0 : 1;
}
