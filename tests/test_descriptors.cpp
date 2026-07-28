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

	// --- Uncompressed format descriptor validator ---
	{
		uint8 fmtBuf[32];
		memset(fmtBuf, 0, sizeof(fmtBuf));
		fmtBuf[0] = 27;			// bLength (spec fixed size)
		fmtBuf[1] = 0x24;		// CS_INTERFACE
		fmtBuf[2] = 0x04;		// VS_FORMAT_UNCOMPRESSED
		fmtBuf[3] = 1;			// bFormatIndex
		fmtBuf[4] = 5;			// bNumFrameDescriptors
		// GUID at offset 5 (YUY2-ish sentinel).
		for (int i = 0; i < 16; i++) fmtBuf[5 + i] = (uint8)(0x40 + i);
		fmtBuf[21] = 16;		// bits per pixel
		fmtBuf[22] = 2;			// default frame index

		UVCUncompressedFormatCheck f = UVCCheckUncompressedFormatDescriptor(
			fmtBuf, 27);
		bool guidOk = true;
		for (int i = 0; i < 16; i++)
			guidOk = guidOk && (f.guid[i] == (uint8)(0x40 + i));
		Expect("valid uncompressed format", f.valid && f.formatIndex == 1
			&& f.numFrameDescriptors == 5 && f.defaultFrameIndex == 2
			&& f.bitsPerPixel == 16 && guidOk);

		// Too short to read the GUID/fields.
		Expect("reject format avail < 27",
			!UVCCheckUncompressedFormatDescriptor(fmtBuf, 20).valid);

		// bLength claims 27 but only 20 available.
		Expect("reject format bLength > avail",
			!UVCCheckUncompressedFormatDescriptor(fmtBuf, 20).valid);

		// bLength below spec fixed size.
		fmtBuf[0] = 20;
		Expect("reject format bLength < 27",
			!UVCCheckUncompressedFormatDescriptor(fmtBuf, 27).valid);
		fmtBuf[0] = 27;

		// formatIndex 0 is unusable.
		fmtBuf[3] = 0;
		Expect("reject format index 0",
			!UVCCheckUncompressedFormatDescriptor(fmtBuf, 27).valid);
	}

	// --- Extension unit descriptor validator ---
	{
		// Well-formed: 1 input pin, control size 1 -> bLength = 24+1+1 = 26.
		uint8 xu[32];
		memset(xu, 0, sizeof(xu));
		xu[0] = 26;			// bLength
		xu[1] = 0x24; xu[2] = 0x06;	// CS_INTERFACE, VC_EXTENSION_UNIT
		xu[3] = 4;			// bUnitID
		for (int i = 0; i < 16; i++) xu[4 + i] = (uint8)(0x70 + i);	// GUID
		xu[20] = 8;			// bNumControls
		xu[21] = 1;			// bNrInPins
		xu[22] = 3;			// source_id[0]
		xu[23] = 1;			// bControlSize
		xu[24] = 0x0F;		// bmControls[0]
		xu[25] = 7;			// iExtension

		UVCExtensionUnitCheck x = UVCCheckExtensionUnitDescriptor(xu, 26);
		bool guidOk = true;
		for (int i = 0; i < 16; i++)
			guidOk = guidOk && (x.guid[i] == (uint8)(0x70 + i));
		Expect("valid XU", x.valid && x.unitID == 4 && x.numControls == 8
			&& x.numInputPins == 1 && guidOk && x.controlSize == 1
			&& x.iExtension == 7 && x.sourceIdCount == 1 && x.sourceIds[0] == 3);

		// Too short for the fixed prefix.
		Expect("reject XU avail < 22",
			!UVCCheckExtensionUnitDescriptor(xu, 20).valid);

		// bLength lies (claims 26 but only 22 available).
		Expect("reject XU bLength > avail",
			!UVCCheckExtensionUnitDescriptor(xu, 22).valid);

		// Hostile counts: 200 input pins in a 26-byte descriptor. The fixed
		// prefix is still readable, so the unit is kept, but the computed
		// bControlSize / iExtension offsets fall outside -> 0, no over-read.
		xu[21] = 200;
		UVCExtensionUnitCheck h = UVCCheckExtensionUnitDescriptor(xu, 26);
		Expect("hostile pin count stays in bounds", h.valid
			&& h.controlSize == 0 && h.iExtension == 0
			&& h.sourceIdCount <= 8);
	}

	// --- VS input/output header bmaControls bound ---
	{
		// Input header (array at 13): 2 formats, control_size 1 -> needs 15.
		Expect("input: 2 fmts fit",
			UVCVSHeaderSafeFormatCount(2, 1, 13, 15) == 2);
		// Only 14 bytes: only 1 entry fits.
		Expect("input: truncated -> 1",
			UVCVSHeaderSafeFormatCount(2, 1, 13, 14) == 1);
		// Hostile num_formats, small descriptor: clamped to what fits.
		Expect("input: hostile 200 fmts clamped",
			UVCVSHeaderSafeFormatCount(200, 1, 13, 15) == 2);
		// control_size 0 -> 0 (no zero-stride walk).
		Expect("control_size 0 -> 0",
			UVCVSHeaderSafeFormatCount(5, 0, 13, 40) == 0);
		// bLength at/under the array offset -> 0.
		Expect("bLength <= arrayOffset -> 0",
			UVCVSHeaderSafeFormatCount(5, 2, 13, 13) == 0);
		// Wider control entries.
		Expect("input: control_size 4",
			UVCVSHeaderSafeFormatCount(10, 4, 13, 25) == 3);
		// Output header (array at 9).
		Expect("output: 3 fmts fit",
			UVCVSHeaderSafeFormatCount(3, 1, 9, 12) == 3);

		// Invariant sweep: the count never implies reading past bLength.
		bool inv = true;
		for (int nf = 0; nf < 256 && inv; nf++)
			for (int cs = 0; cs < 16 && inv; cs++)
				for (int bl = 0; bl < 64 && inv; bl++) {
					uint8 c = UVCVSHeaderSafeFormatCount(
						(uint8)nf, (uint8)cs, 13, (uint8)bl);
					if (c > nf) inv = false;
					if (c > 0 && (13 + (size_t)c * cs) > (size_t)bl) inv = false;
					if (cs == 0 && c != 0) inv = false;
				}
		Expect("VS header count invariant sweep", inv);
	}

	// --- Probe/commit size sanitisation ---
	{
		const uint32 raw = 320 * 240 * 4;	// 307200

		// Sane values pass through unchanged.
		UVCProbeSizes s = UVCSanitizeProbeSizes(153600, 3072, raw);
		Expect("probe: sane values kept", !s.clamped
			&& s.maxVideoFrameSize == 153600 && s.maxPayloadTransferSize == 3072);

		// Garbage frame size (the observed ~2 GB) falls back to the raw size.
		s = UVCSanitizeProbeSizes(2125179710u, 3072, raw);
		Expect("probe: garbage frame clamped to raw", s.clamped
			&& s.maxVideoFrameSize == raw && s.maxPayloadTransferSize == 3072);

		// Garbage payload (the observed ~1 GB) is clamped to the ceiling.
		s = UVCSanitizeProbeSizes(153600, 1124729481u, raw);
		Expect("probe: garbage payload clamped", s.clamped
			&& s.maxVideoFrameSize == 153600
			&& s.maxPayloadTransferSize == 3072u * 8);

		// Zero frame -> raw; zero payload stays zero (no constraint).
		s = UVCSanitizeProbeSizes(0, 0, raw);
		Expect("probe: zero frame -> raw, zero payload kept", s.clamped
			&& s.maxVideoFrameSize == raw && s.maxPayloadTransferSize == 0);

		// No known resolution (raw 0): ceiling is 50 MB, a sane value passes.
		s = UVCSanitizeProbeSizes(153600, 3072, 0);
		Expect("probe: raw 0 keeps sane frame", !s.clamped
			&& s.maxVideoFrameSize == 153600);

		// Frame above the 50 MB hard ceiling is clamped even if raw is larger.
		s = UVCSanitizeProbeSizes(80u * 1024 * 1024, 3072, 8000 * 8000 * 4);
		Expect("probe: frame above 50MB ceiling clamped", s.clamped
			&& s.maxVideoFrameSize == 50u * 1024 * 1024);
	}

	// --- Real-camera regression corpus ---
	// Descriptors shaped like the two cameras this driver was validated against
	// (parameters taken from their device logs; reconstructed, not raw-captured
	// — a raw-descriptor dump tool would let us store true captures later). If a
	// future parsing change rejects a real device shape, these fail.
	{
		uint8 b[64];

		// Microdia 0c45:6409 — YUY2, 5 resolutions, 3 discrete intervals each.
		const uint16 microdia[][2] = {
			{640,480},{352,288},{320,240},{176,144},{160,120}
		};
		bool ok = true;
		for (size_t i = 0; i < sizeof(microdia) / sizeof(microdia[0]); i++) {
			uint8 len = MakeFrame(b, microdia[i][0], microdia[i][1],
				(uint32)microdia[i][0] * microdia[i][1] * 2, 3);
			UVCFrameDescCheck c = UVCCheckFrameDescriptor(b, len);
			ok = ok && c.valid && c.width == microdia[i][0]
				&& c.height == microdia[i][1] && c.frameIntervalType == 3;
		}
		Expect("corpus: Microdia YUY2 frame descriptors", ok);

		// Microdia YUY2 uncompressed format descriptor (real YUY2 GUID).
		memset(b, 0, sizeof(b));
		b[0] = 27; b[1] = 0x24; b[2] = 0x04; b[3] = 1; b[4] = 5;
		const uint8 yuy2Guid[16] = { 0x59,0x55,0x59,0x32, 0x00,0x00, 0x10,0x00,
			0x80,0x00, 0x00,0xaa,0x00,0x38,0x9b,0x71 };
		for (int i = 0; i < 16; i++) b[5 + i] = yuy2Guid[i];
		b[21] = 16; b[22] = 1;
		UVCUncompressedFormatCheck f = UVCCheckUncompressedFormatDescriptor(b, 27);
		bool guidOk = true;
		for (int i = 0; i < 16; i++) guidOk = guidOk && (f.guid[i] == yuy2Guid[i]);
		Expect("corpus: Microdia YUY2 format descriptor",
			f.valid && f.formatIndex == 1 && guidOk);

		// AUKEY 1bcf:0001 — MJPEG, 6 resolutions (same frame-descriptor layout).
		const uint16 aukey[][2] = {
			{1920,1080},{1280,720},{1024,768},{800,600},{640,480},{320,240}
		};
		ok = true;
		for (size_t i = 0; i < sizeof(aukey) / sizeof(aukey[0]); i++) {
			uint8 len = MakeFrame(b, aukey[i][0], aukey[i][1],
				(uint32)aukey[i][0] * aukey[i][1] * 2, 1);
			UVCFrameDescCheck c = UVCCheckFrameDescriptor(b, len);
			ok = ok && c.valid && c.width == aukey[i][0]
				&& c.height == aukey[i][1];
		}
		Expect("corpus: AUKEY MJPEG frame descriptors", ok);
	}

	printf("\n%d passed, %d failed\n", sPass, sFail);
	return sFail == 0 ? 0 : 1;
}
