/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * Unit tests for the portable overflow-safe helpers (UVCSafety) and for the
 * continuous-frame regression fix (FIX-H1).
 *
 * Coverage:
 *   FIX-C3  producer buffer overflow
 *   FIX-M5  YUV/RGB32 size overflow
 *   FIX-H8  audio ring/buffer sizes
 *   FIX-T10 field_rate interval
 *   FIX-M8  ring capacity guard
 *   FIX-B4  backoff delay
 *   FIX-H1  continuous frame needs 38 bytes
 *   FIX-H2  still-image counts fit
 *   FIX-H5  audio Format Type I counts
 *   FIX-H6  selector/array bounds
 *
 * Build & run:
 *   g++ -O2 -o test_safety test_safety.cpp \
 *       ../addons/uvc/UVCSafety.cpp ../addons/uvc/UVCDescriptors.cpp \
 *       -I../addons/uvc && ./test_safety
 */
#include <cstdio>
#include <cstring>

#include "UVCSafety.h"
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


int
main()
{
	printf("=== UVCSafety test suite ===\n");

	// --- FIX-C3 / FIX-M5: frame size math ---
	{
		size_t out = 0;
		Expect("YUY2 640x480", UVCSafeYUY2Size(640, 480, &out)
			&& out == 640u * 480u * 2);
		Expect("RGB32 1920x1080", UVCSafeRGB32Size(1920, 1080, &out)
			&& out == 1920u * 1080u * 4);
		Expect("reject zero width", !UVCSafeYUY2Size(0, 480, &out));
		Expect("reject zero height", !UVCSafeRGB32Size(640, 0, &out));
		Expect("reject > 8192", !UVCSafeRGB32Size(9000, 480, &out));
		Expect("producer 640x480", UVCSafeProducerBufferSize(640, 480,
			&out) && out == 640u * 480u * 4);
		Expect("producer rejects 0", !UVCSafeProducerBufferSize(0, 480,
			&out));

		size_t a = 0, b = 0;
		Expect("mul overflow", !UVCSafeMulSize((size_t)-1, 2, &a));
		Expect("mul zero rejected", !UVCSafeMulSize(0, 100, &a));
		Expect("mul3 overflow", !UVCSafeMul3Size((size_t)-1,
			(size_t)-1, 4, &b));
		Expect("mul NULL out", !UVCSafeMulSize(2, 2, NULL));
	}

	// --- FIX-H8: audio sizes ---
	{
		size_t out = 0;
		Expect("audio ring 8k mono", UVCSafeAudioRingSize(8000, 1,
			&out) && out == 8000u * 1 * 2 * 2);
		Expect("audio ring 48k stereo clamps",
			UVCSafeAudioRingSize(48000, 2, &out) && out == 262144);
		Expect("audio ring rejects rate 0",
			!UVCSafeAudioRingSize(0, 2, &out));
		Expect("audio ring rejects ch 0",
			!UVCSafeAudioRingSize(48000, 0, &out));
		// Saturation: huge rate clamps to 256KB instead of wrapping.
		Expect("audio ring saturates", UVCSafeAudioRingSize(0xffffff, 8,
			&out) && out == 262144);
		Expect("audio buffer ok", UVCSafeAudioBufferSize(1024, 16, &out)
			&& out == 16384);
		Expect("audio buffer rejects 0 packet",
			!UVCSafeAudioBufferSize(0, 16, &out));
		Expect("audio buffer rejects 0 count",
			!UVCSafeAudioBufferSize(1024, 0, &out));
	}

	// --- FIX-T10: field rate ---
	{
		int64 usec = 0;
		Expect("30fps -> 33333", UVCSafeFieldInterval(30.0f, &usec)
			&& usec > 33000 && usec < 34000);
		Expect("reject 0", !UVCSafeFieldInterval(0.0f, &usec));
		Expect("reject negative", !UVCSafeFieldInterval(-30.0f, &usec));
		// NaN fails the range check (comparisons are false).
		Expect("reject NaN", !UVCSafeFieldInterval(0.0f / 0.0f, &usec));
		Expect("reject INF", !UVCSafeFieldInterval(1.0f / 0.0f, &usec));
		Expect("reject NULL", !UVCSafeFieldInterval(30.0f, NULL));
	}

	// --- FIX-B4: backoff ---
	{
		Expect("valid config", UVCSafeRetryConfig(100000, 1000000, 2.0f));
		Expect("reject 0 initial", !UVCSafeRetryConfig(0, 1000000, 2.0f));
		Expect("reject mult < 1", !UVCSafeRetryConfig(100000, 1000000,
			0.5f));
		Expect("reject initial > max",
			!UVCSafeRetryConfig(2000000, 1000000, 2.0f));
		Expect("backoff attempt 0", UVCSafeBackoffDelay(0, 100000,
			1000000, 2.0f) == 100000);
		Expect("backoff attempt 1", UVCSafeBackoffDelay(1, 100000,
			1000000, 2.0f) == 200000);
		Expect("backoff clamped", UVCSafeBackoffDelay(10, 100000,
			1000000, 2.0f) == 1000000);
		Expect("backoff bad config -> max",
			UVCSafeBackoffDelay(0, 0, 500000, 2.0f) == 500000);
	}

	// --- FIX-M8: ring capacity ---
	{
		Expect("capacity 0 -> 1", UVCGuardRingCapacity(0) == 1);
		Expect("capacity kept", UVCGuardRingCapacity(16) == 16);
	}

	// --- FIX-H6: selector / array bounds ---
	{
		Expect("selector 2 pins fit",
			UVCSafeSelectorSources(2, 6, 8) == 2);
		Expect("selector truncated",
			UVCSafeSelectorSources(200, 6, 8) == 2);
		Expect("selector bLength <= fixed -> 0",
			UVCSafeSelectorSources(2, 6, 6) == 0);
		Expect("array 2 fit",
			UVCSafeArrayCount(2, 1, 13, 15) == 2);
		Expect("array size 0 -> 0",
			UVCSafeArrayCount(5, 0, 13, 40) == 0);
	}

	// --- FIX-H2: still-image counts ---
	{
		Expect("still 2 sizes 1 comp fits",
			UVCSafeStillImageCounts(2, 1, (uint8)(6 + 8 + 1 + 1)));
		Expect("still hostile rejected",
			!UVCSafeStillImageCounts(16, 8, 10));
		Expect("still bLength < 6 rejected",
			!UVCSafeStillImageCounts(0, 0, 5));
	}

	// --- FIX-H5: audio Format Type I ---
	{
		Expect("format-I continuous needs 14",
			UVCSafeAudioFormatICount(0, 14));
		Expect("format-I continuous short rejected",
			!UVCSafeAudioFormatICount(0, 10));
		Expect("format-I 2 discrete needs 14",
			UVCSafeAudioFormatICount(2, 14));
		Expect("format-I hostile rejected",
			!UVCSafeAudioFormatICount(200, 48));
	}

	// --- FIX-H1: continuous frame regression ---
	// A 26-byte bLength with ftype == 0 must be rejected: the 12 continuous
	// bytes (min/max/step) are not present, and copying or printing them
	// would read out of bounds.
	{
		uint8 buf[64];
		memset(buf, 0, sizeof(buf));
		buf[0] = 26;		// bLength: fixed header only
		buf[1] = 0x24;
		buf[2] = 0x05;
		buf[3] = 1;
		buf[5] = 0x80; buf[6] = 0x02;	// 640
		buf[7] = 0xe0; buf[8] = 0x01;	// 480
		buf[17] = 0x00; buf[18] = 0x09; buf[19] = 0x09; buf[20] = 0x00;
		buf[25] = 0;		// continuous, but no room for min/max/step
		Expect("reject continuous bLength 26",
			!UVCCheckFrameDescriptor(buf, 26).valid);

		// Same header with the 12 continuous bytes present is accepted.
		memset(buf, 0, sizeof(buf));
		buf[0] = 38;
		buf[1] = 0x24;
		buf[2] = 0x05;
		buf[3] = 1;
		buf[5] = 0x80; buf[6] = 0x02;
		buf[7] = 0xe0; buf[8] = 0x01;
		buf[17] = 0x00; buf[18] = 0x09; buf[19] = 0x09; buf[20] = 0x00;
		buf[25] = 0;
		Expect("accept continuous bLength 38",
			UVCCheckFrameDescriptor(buf, 38).valid);
	}

	printf("\n%d passed, %d failed\n", sPass, sFail);
	return sFail == 0 ? 0 : 1;
}
