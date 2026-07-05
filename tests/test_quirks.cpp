/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * Unit tests for the data-driven device quirk resolution (UVCQuirks).
 * These run without USB hardware: ResolveWebcamQuirks() is a pure function.
 *
 * Build & run:
 *   g++ -O2 -o test_quirks test_quirks.cpp ../addons/uvc/UVCQuirks.cpp \
 *       -I../addons/uvc -lbe && ./test_quirks
 */
#include <cstdio>

#include "UVCQuirks.h"

static int sPass = 0;
static int sFail = 0;

static void
Check(const char* name, uint32 got, uint32 want)
{
	if (got == want) {
		sPass++;
	} else {
		sFail++;
		printf("  FAIL %s: got 0x%x, want 0x%x\n", name, got, want);
	}
}


int
main()
{
	printf("=== UVCQuirks test suite ===\n");

	// Sonix bridge (Microdia/Sonix, VID 0x0c45): stride quirk vendor-wide,
	// for every product id, even with no per-device entry quirk.
	Check("Sonix 0c45:6409 -> stride",
		ResolveWebcamQuirks(0x0c45, 0x6409, UVC_QUIRK_NONE),
		UVC_QUIRK_SONIX_STRIDE);
	Check("Sonix 0c45:6720 -> stride (different pid)",
		ResolveWebcamQuirks(0x0c45, 0x6720, UVC_QUIRK_NONE),
		UVC_QUIRK_SONIX_STRIDE);

	// AUKEY (VID 0x1bcf), no quirks.
	Check("AUKEY 1bcf:0001 -> none",
		ResolveWebcamQuirks(0x1bcf, 0x0001, UVC_QUIRK_NONE),
		UVC_QUIRK_NONE);

	// Unknown vendor, matched only by the generic UVC fallback: no quirks.
	Check("unknown 1234:5678 -> none",
		ResolveWebcamQuirks(0x1234, 0x5678, UVC_QUIRK_NONE),
		UVC_QUIRK_NONE);

	// Per-device entry quirk is honoured even for a vendor with no vendor-wide
	// quirk (the mechanism that lets a single quirky PID be flagged in the
	// kSupportedDevices[] table).
	Check("per-device entry quirk honoured",
		ResolveWebcamQuirks(0x1bcf, 0x0001, UVC_QUIRK_SONIX_STRIDE),
		UVC_QUIRK_SONIX_STRIDE);

	// Vendor-wide and per-device quirks combine (OR), no double-count.
	Check("vendor + entry quirks OR together",
		ResolveWebcamQuirks(0x0c45, 0x6409, UVC_QUIRK_SONIX_STRIDE),
		UVC_QUIRK_SONIX_STRIDE);

	printf("\n%d passed, %d failed\n", sPass, sFail);
	return sFail == 0 ? 0 : 1;
}
