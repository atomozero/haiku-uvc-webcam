/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for EOF/FID-based deframer fix
 * Verifies that the deframer correctly uses EOF and FID toggle
 * (not byte count) for frame boundary detection.
 *
 * Build:
 *   g++ -O2 -o test_deframer_fix test_deframer_fix.cpp -lbe
 *
 * Run:
 *   ./test_deframer_fix
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define GREEN "\033[32m"
#define RED   "\033[31m"
#define RESET "\033[0m"

static int sTestsPassed = 0;
static int sTestsFailed = 0;


// =============================================================================
// Minimal deframer simulation (mirrors UVCDeframer::Write logic)
// =============================================================================

struct SimDeframer {
	uint8_t*		buffer;
	size_t		bufferSize;
	size_t		bufferPos;
	size_t		expectedFrameSize;
	int			lastFID;
	int			framesCompleted;
	int			framesIncomplete;

	// Completed frame data (last frame)
	uint8_t*		completedFrame;
	size_t		completedFrameSize;

	void Init(size_t expectedSize)
	{
		bufferSize = 4 * 1024 * 1024;
		buffer = (uint8_t*)calloc(1, bufferSize);
		bufferPos = 0;
		expectedFrameSize = expectedSize;
		lastFID = -1;
		framesCompleted = 0;
		framesIncomplete = 0;
		completedFrame = (uint8_t*)calloc(1, expectedSize > 0 ? expectedSize : 65536);
		completedFrameSize = 0;
	}

	void Destroy()
	{
		free(buffer);
		free(completedFrame);
	}

	void _CompleteFrame()
	{
		if (bufferPos == 0)
			return;
		// Skip frames < 50% of expected (partial startup frames)
		if (expectedFrameSize > 0 && bufferPos < expectedFrameSize / 2) {
			framesIncomplete++;
			return;
		}
		// Pad YUY2 frames to expected size
		if (expectedFrameSize > 0 && bufferPos < expectedFrameSize) {
			uint8_t pad[4] = {0x00, 0x80, 0x00, 0x80};
			while (bufferPos < expectedFrameSize) {
				size_t rem = expectedFrameSize - bufferPos;
				size_t n = (rem < 4) ? rem : 4;
				memcpy(buffer + bufferPos, pad, n);
				bufferPos += n;
			}
		}
		memcpy(completedFrame, buffer, bufferPos);
		completedFrameSize = bufferPos;
		framesCompleted++;
	}

	// Process one UVC packet (header + payload)
	void Write(const uint8_t* pkt, size_t pktSize)
	{
		if (pktSize < 2 || pkt[0] < 2 || pkt[0] > pktSize)
			return;
		int hdrLen = pkt[0];
		int flags = pkt[1];
		int payloadSize = pktSize - hdrLen;
		bool eof = (flags & 0x02) != 0;
		int fid = flags & 0x01;
		bool fidChanged = (lastFID >= 0 && fid != lastFID);

		// FID toggle → complete previous frame, start new
		if (fidChanged) {
			_CompleteFrame();
			bufferPos = 0;
		}
		lastFID = fid;

		// Write payload (overflow protection only)
		if (payloadSize > 0) {
			size_t toWrite = payloadSize;
			if (bufferPos + toWrite > bufferSize)
				toWrite = bufferSize - bufferPos;
			memcpy(buffer + bufferPos, pkt + hdrLen, toWrite);
			bufferPos += toWrite;
		}

		// EOF → complete frame
		if (eof) {
			_CompleteFrame();
			bufferPos = 0;
		}
	}
};


// Helper: build a UVC packet with header + payload
static void
build_uvc_packet(uint8_t* out, size_t* outSize, int fid, bool eof,
	const uint8_t* payload, size_t payloadLen)
{
	int hdrLen = 12;
	out[0] = hdrLen;
	out[1] = 0x8C | (fid & 1) | (eof ? 0x02 : 0x00);
	// PTS (4 bytes) + SCR (6 bytes) = fill with zeros
	memset(out + 2, 0, 10);
	memcpy(out + hdrLen, payload, payloadLen);
	*outSize = hdrLen + payloadLen;
}


// =============================================================================
// Test 1: EOF-based frame completion
// =============================================================================

static bool
test_eof_completion()
{
	printf("Test 1: EOF-based frame completion... ");

	SimDeframer d;
	d.Init(640); // Expected frame size = 640 bytes

	// Send one complete frame in 2 packets + EOF
	uint8_t pkt[1024];
	size_t pktSize;
	uint8_t payload[320];
	memset(payload, 0xAA, sizeof(payload));

	// Packet 1: 320 bytes, FID=0, no EOF
	build_uvc_packet(pkt, &pktSize, 0, false, payload, 320);
	d.Write(pkt, pktSize);

	// Packet 2: 320 bytes, FID=0, EOF=true
	memset(payload, 0xBB, sizeof(payload));
	build_uvc_packet(pkt, &pktSize, 0, true, payload, 320);
	d.Write(pkt, pktSize);

	bool ok = (d.framesCompleted == 1 && d.completedFrameSize == 640
		&& d.completedFrame[0] == 0xAA && d.completedFrame[320] == 0xBB);

	d.Destroy();
	return ok;
}


// =============================================================================
// Test 2: FID toggle completes previous frame
// =============================================================================

static bool
test_fid_toggle()
{
	printf("Test 2: FID toggle completes previous frame... ");

	SimDeframer d;
	d.Init(640);

	uint8_t pkt[1024];
	size_t pktSize;
	uint8_t payload[640];

	// Frame 1: FID=0, full frame, no EOF
	memset(payload, 0x11, 640);
	build_uvc_packet(pkt, &pktSize, 0, false, payload, 640);
	d.Write(pkt, pktSize);

	// Frame 2 starts: FID=1 (toggle!) → should complete frame 1
	memset(payload, 0x22, 640);
	build_uvc_packet(pkt, &pktSize, 1, false, payload, 640);
	d.Write(pkt, pktSize);

	bool ok = (d.framesCompleted == 1 && d.completedFrameSize == 640
		&& d.completedFrame[0] == 0x11);

	d.Destroy();
	return ok;
}


// =============================================================================
// Test 3: Mid-stream start (first frame is partial, should be skipped)
// =============================================================================

static bool
test_midstream_start()
{
	printf("Test 3: Mid-stream start (partial first frame skipped)... ");

	SimDeframer d;
	d.Init(1000);

	uint8_t pkt[1024];
	size_t pktSize;
	uint8_t payload[800];

	// Partial data from end of unknown frame (FID=0, no EOF)
	// Only 200 bytes — less than 50% of expected 1000
	memset(payload, 0xDD, 200);
	build_uvc_packet(pkt, &pktSize, 0, false, payload, 200);
	d.Write(pkt, pktSize);

	// EOF arrives for partial frame
	build_uvc_packet(pkt, &pktSize, 0, true, payload, 100);
	d.Write(pkt, pktSize);

	// Frame 1 was incomplete (300 bytes < 500 = 50% of 1000) → skipped
	bool partialSkipped = (d.framesCompleted == 0 && d.framesIncomplete == 1);

	// Now FID toggles → new full frame
	memset(payload, 0xEE, 800);
	build_uvc_packet(pkt, &pktSize, 1, false, payload, 500);
	d.Write(pkt, pktSize);

	memset(payload, 0xFF, 800);
	build_uvc_packet(pkt, &pktSize, 1, true, payload, 500);
	d.Write(pkt, pktSize);

	// Frame 2: 1000 bytes, should be complete and correct
	bool fullFrame = (d.framesCompleted == 1 && d.completedFrameSize == 1000
		&& d.completedFrame[0] == 0xEE && d.completedFrame[500] == 0xFF);

	d.Destroy();
	return partialSkipped && fullFrame;
}


// =============================================================================
// Test 4: No SIZE-based completion (data exceeds expected without EOF)
// =============================================================================

static bool
test_no_size_completion()
{
	printf("Test 4: No SIZE-based completion (overflow protection only)... ");

	SimDeframer d;
	d.Init(100);

	uint8_t pkt[1024];
	size_t pktSize;
	uint8_t payload[200];

	// Send 200 bytes (exceeds expected 100) WITHOUT EOF
	memset(payload, 0xCC, 200);
	build_uvc_packet(pkt, &pktSize, 0, false, payload, 200);
	d.Write(pkt, pktSize);

	// Frame should NOT be completed (no EOF, no FID change)
	bool notCompleted = (d.framesCompleted == 0);

	// Buffer should have data (overflow protected to bufferSize)
	bool hasData = (d.bufferPos == 200);

	// Now send EOF
	build_uvc_packet(pkt, &pktSize, 0, true, payload, 0);
	d.Write(pkt, pktSize);

	// NOW frame should be complete (via EOF)
	bool completed = (d.framesCompleted == 1);

	d.Destroy();
	return notCompleted && hasData && completed;
}


// =============================================================================
// Test 5: YUY2 padding on incomplete EOF frame
// =============================================================================

static bool
test_yuy2_padding()
{
	printf("Test 5: YUY2 padding on incomplete EOF frame... ");

	SimDeframer d;
	d.Init(8); // Expected 8 bytes (4 YUY2 pixels)

	uint8_t pkt[64];
	size_t pktSize;
	uint8_t payload[4] = {0x80, 0x80, 0x80, 0x80}; // 2 pixels

	// Send only 4 bytes then EOF (expected 8)
	build_uvc_packet(pkt, &pktSize, 0, true, payload, 4);
	d.Write(pkt, pktSize);

	// Frame should be padded to 8 bytes
	// First 4 bytes: original data (0x80)
	// Last 4 bytes: black padding (0x00 0x80 0x00 0x80)
	bool sizeOk = (d.completedFrameSize == 8);
	bool dataOk = (d.completedFrame[0] == 0x80 && d.completedFrame[1] == 0x80);
	bool padOk = (d.completedFrame[4] == 0x00 && d.completedFrame[5] == 0x80
		&& d.completedFrame[6] == 0x00 && d.completedFrame[7] == 0x80);

	d.Destroy();
	return sizeOk && dataOk && padOk;
}


// =============================================================================
// Test 6: Multiple frames in sequence with EOF
// =============================================================================

static bool
test_multiple_eof_frames()
{
	printf("Test 6: Multiple frames with EOF boundaries... ");

	SimDeframer d;
	d.Init(100);

	uint8_t pkt[256];
	size_t pktSize;
	uint8_t payload[100];

	// Frame 1: 100 bytes + EOF
	memset(payload, 0x11, 100);
	build_uvc_packet(pkt, &pktSize, 0, true, payload, 100);
	d.Write(pkt, pktSize);

	// Frame 2: 100 bytes + EOF (same FID — no toggle needed with EOF)
	memset(payload, 0x22, 100);
	build_uvc_packet(pkt, &pktSize, 0, true, payload, 100);
	d.Write(pkt, pktSize);

	// Frame 3: 100 bytes + EOF
	memset(payload, 0x33, 100);
	build_uvc_packet(pkt, &pktSize, 0, true, payload, 100);
	d.Write(pkt, pktSize);

	// Should have 3 completed frames, last one has 0x33 data
	bool ok = (d.framesCompleted == 3 && d.completedFrame[0] == 0x33);

	d.Destroy();
	return ok;
}


// =============================================================================
// Main
// =============================================================================

static void
run_test(bool (*testFunc)(), const char* name)
{
	if (testFunc()) {
		printf(GREEN "[PASS]" RESET "\n");
		sTestsPassed++;
	} else {
		printf(RED "[FAIL]" RESET "\n");
		sTestsFailed++;
	}
}


int
main()
{
	printf("=== UVC Deframer Fix Tests ===\n");
	printf("Verifying EOF/FID-only frame boundary detection\n\n");

	run_test(test_eof_completion, "EOF completion");
	run_test(test_fid_toggle, "FID toggle");
	run_test(test_midstream_start, "Mid-stream start");
	run_test(test_no_size_completion, "No SIZE completion");
	run_test(test_yuy2_padding, "YUY2 padding");
	run_test(test_multiple_eof_frames, "Multiple EOF frames");

	printf("\n=== Results: %d passed, %d failed ===\n",
		sTestsPassed, sTestsFailed);

	return sTestsFailed > 0 ? 1 : 0;
}
