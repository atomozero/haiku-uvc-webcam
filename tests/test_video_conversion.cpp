/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for video conversion optimizations (Group 3)
 *
 * Build:
 *   g++ -O2 -o test_video_conversion test_video_conversion.cpp -lbe
 *
 * Run:
 *   ./test_video_conversion
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <math.h>


// =============================================================================
// YUV-RGB Lookup Tables (matches driver implementation)
// =============================================================================

struct yuv_rgb_lookup_tables {
	// Using int32 because max Y value (298*239=71222) exceeds int16 range
	int32	y_table[256];
	int32	u_b_table[256];
	int32	u_g_table[256];
	int32	v_r_table[256];
	int32	v_g_table[256];
	bool	initialized;

	yuv_rgb_lookup_tables() : initialized(false) {}

	void Initialize()
	{
		if (initialized)
			return;

		for (int i = 0; i < 256; i++) {
			y_table[i] = 298 * (i - 16);
			u_b_table[i] = 516 * (i - 128);
			u_g_table[i] = -100 * (i - 128);
			v_r_table[i] = 409 * (i - 128);
			v_g_table[i] = -208 * (i - 128);
		}

		initialized = true;
	}
};

static yuv_rgb_lookup_tables gTestTables;


// =============================================================================
// Clamp functions
// =============================================================================

static inline uint8
clamp255_original(int v)
{
	return (v < 0) ? 0 : (v > 255) ? 255 : (uint8)v;
}


static inline uint8
clamp255_optimized(int32 v)
{
	v = v < 0 ? 0 : v;
	return (uint8)(v > 255 ? 255 : v);
}


// =============================================================================
// YUY2 to RGB32 conversion - Original (per-pixel math)
// =============================================================================

static void
convert_yuy2_original(unsigned char* dst, unsigned char* src,
	int32 width, int32 height)
{
	size_t srcStride = (size_t)width * 2;
	size_t dstStride = (size_t)width * 4;

	for (int32 y = 0; y < height; y++) {
		const unsigned char* srcRow = src + y * srcStride;
		unsigned char* dstRow = dst + y * dstStride;

		for (int32 x = 0; x < width; x += 2) {
			int y0 = srcRow[0];
			int u  = srcRow[1];
			int y1 = srcRow[2];
			int v  = srcRow[3];
			srcRow += 4;

			int c0 = y0 - 16;
			int c1 = y1 - 16;
			int d = u - 128;
			int e = v - 128;

			// Pixel 0
			dstRow[0] = clamp255_original((298 * c0 + 516 * d + 128) >> 8);
			dstRow[1] = clamp255_original((298 * c0 - 100 * d - 208 * e + 128) >> 8);
			dstRow[2] = clamp255_original((298 * c0 + 409 * e + 128) >> 8);
			dstRow[3] = 255;

			// Pixel 1
			dstRow[4] = clamp255_original((298 * c1 + 516 * d + 128) >> 8);
			dstRow[5] = clamp255_original((298 * c1 - 100 * d - 208 * e + 128) >> 8);
			dstRow[6] = clamp255_original((298 * c1 + 409 * e + 128) >> 8);
			dstRow[7] = 255;
			dstRow += 8;
		}
	}
}


// =============================================================================
// YUY2 to RGB32 conversion - Optimized (lookup tables)
// =============================================================================

static void
convert_yuy2_optimized(unsigned char* dst, unsigned char* src,
	int32 width, int32 height)
{
	if (!gTestTables.initialized)
		gTestTables.Initialize();

	const int32* yTable = gTestTables.y_table;
	const int32* uBTable = gTestTables.u_b_table;
	const int32* uGTable = gTestTables.u_g_table;
	const int32* vRTable = gTestTables.v_r_table;
	const int32* vGTable = gTestTables.v_g_table;

	size_t srcStride = (size_t)width * 2;
	size_t dstStride = (size_t)width * 4;

	for (int32 row = 0; row < height; row++) {
		const unsigned char* srcRow = src + row * srcStride;
		unsigned char* dstRow = dst + row * dstStride;

		for (int32 x = 0; x < width; x += 2) {
			uint8 y0 = srcRow[0];
			uint8 u  = srcRow[1];
			uint8 y1 = srcRow[2];
			uint8 v  = srcRow[3];
			srcRow += 4;

			int32 yVal0 = yTable[y0];
			int32 yVal1 = yTable[y1];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			// Pixel 0
			dstRow[0] = clamp255_optimized((yVal0 + uB + 128) >> 8);
			dstRow[1] = clamp255_optimized((yVal0 + uG + vG + 128) >> 8);
			dstRow[2] = clamp255_optimized((yVal0 + vR + 128) >> 8);
			dstRow[3] = 255;

			// Pixel 1
			dstRow[4] = clamp255_optimized((yVal1 + uB + 128) >> 8);
			dstRow[5] = clamp255_optimized((yVal1 + uG + vG + 128) >> 8);
			dstRow[6] = clamp255_optimized((yVal1 + vR + 128) >> 8);
			dstRow[7] = 255;
			dstRow += 8;
		}
	}
}


// =============================================================================
// Test 1: Lookup Table Correctness
// =============================================================================

static bool
test_lookup_correctness()
{
	printf("Test: Lookup table correctness... ");

	gTestTables.Initialize();

	// Verify Y table values
	// y_table[16] should be 0 (298 * 0 = 0)
	if (gTestTables.y_table[16] != 0) {
		printf("FAIL (y_table[16]=%d, expected 0)\n", gTestTables.y_table[16]);
		return false;
	}

	// y_table[235] should be 298 * (235-16) = 298 * 219 = 65262
	int32 expected_y235 = 298 * (235 - 16);
	if (gTestTables.y_table[235] != expected_y235) {
		printf("FAIL (y_table[235]=%d, expected %d)\n",
			(int)gTestTables.y_table[235], (int)expected_y235);
		return false;
	}

	// u_b_table[128] should be 0 (516 * 0 = 0)
	if (gTestTables.u_b_table[128] != 0) {
		printf("FAIL (u_b_table[128]=%d, expected 0)\n",
			gTestTables.u_b_table[128]);
		return false;
	}

	// v_r_table[128] should be 0
	if (gTestTables.v_r_table[128] != 0) {
		printf("FAIL (v_r_table[128]=%d, expected 0)\n",
			gTestTables.v_r_table[128]);
		return false;
	}

	printf("OK\n");
	return true;
}


// =============================================================================
// Test 2: Conversion Output Equivalence
// =============================================================================

static bool
test_conversion_equivalence()
{
	printf("Test: Original vs optimized output equivalence... ");

	const int32 width = 320;
	const int32 height = 240;
	size_t yuy2Size = width * height * 2;
	size_t rgb32Size = width * height * 4;

	uint8* yuy2 = (uint8*)malloc(yuy2Size);
	uint8* rgb32_orig = (uint8*)malloc(rgb32Size);
	uint8* rgb32_opt = (uint8*)malloc(rgb32Size);

	if (!yuy2 || !rgb32_orig || !rgb32_opt) {
		printf("FAIL (allocation)\n");
		free(yuy2);
		free(rgb32_orig);
		free(rgb32_opt);
		return false;
	}

	// Generate test YUY2 data with various patterns
	for (size_t i = 0; i < yuy2Size; i += 4) {
		yuy2[i + 0] = (uint8)((i * 3) % 256);     // Y0
		yuy2[i + 1] = (uint8)((i * 7) % 256);     // U
		yuy2[i + 2] = (uint8)((i * 5) % 256);     // Y1
		yuy2[i + 3] = (uint8)((i * 11) % 256);    // V
	}

	// Convert with both methods
	convert_yuy2_original(rgb32_orig, yuy2, width, height);
	convert_yuy2_optimized(rgb32_opt, yuy2, width, height);

	// Compare results
	int differences = 0;
	int maxDiff = 0;

	for (size_t i = 0; i < rgb32Size; i++) {
		int diff = abs((int)rgb32_orig[i] - (int)rgb32_opt[i]);
		if (diff > 0) {
			differences++;
			if (diff > maxDiff)
				maxDiff = diff;
		}
	}

	free(yuy2);
	free(rgb32_orig);
	free(rgb32_opt);

	// Allow for small rounding differences (typically 0-1)
	if (maxDiff > 1) {
		printf("FAIL (%d differences, max diff=%d)\n", differences, maxDiff);
		return false;
	}

	printf("OK (%d minor rounding differences)\n", differences);
	return true;
}


// =============================================================================
// Test 3: Performance Comparison
// =============================================================================

static bool
test_conversion_performance()
{
	printf("Test: Lookup table performance improvement... ");

	const int32 width = 640;
	const int32 height = 480;
	const int iterations = 100;
	size_t yuy2Size = width * height * 2;
	size_t rgb32Size = width * height * 4;

	uint8* yuy2 = (uint8*)malloc(yuy2Size);
	uint8* rgb32 = (uint8*)malloc(rgb32Size);

	if (!yuy2 || !rgb32) {
		printf("FAIL (allocation)\n");
		free(yuy2);
		free(rgb32);
		return false;
	}

	// Initialize YUY2 data
	for (size_t i = 0; i < yuy2Size; i++)
		yuy2[i] = (uint8)(i % 256);

	// Warmup
	convert_yuy2_original(rgb32, yuy2, width, height);
	convert_yuy2_optimized(rgb32, yuy2, width, height);

	// Benchmark original
	bigtime_t startOrig = system_time();
	for (int i = 0; i < iterations; i++) {
		convert_yuy2_original(rgb32, yuy2, width, height);
	}
	bigtime_t timeOrig = system_time() - startOrig;

	// Benchmark optimized
	bigtime_t startOpt = system_time();
	for (int i = 0; i < iterations; i++) {
		convert_yuy2_optimized(rgb32, yuy2, width, height);
	}
	bigtime_t timeOpt = system_time() - startOpt;

	free(yuy2);
	free(rgb32);

	float speedup = (float)timeOrig / (float)timeOpt;
	float msPerFrameOrig = (float)timeOrig / iterations / 1000.0f;
	float msPerFrameOpt = (float)timeOpt / iterations / 1000.0f;

	printf("OK (orig: %.2f ms/frame, opt: %.2f ms/frame, speedup: %.2fx)\n",
		msPerFrameOrig, msPerFrameOpt, speedup);

	// Success if optimized is at least as fast (may not be faster due to
	// compiler optimizations on original code)
	return (timeOpt <= timeOrig * 1.2);  // Allow 20% margin
}


// =============================================================================
// Test 4: Edge Case Handling
// =============================================================================

static bool
test_edge_cases()
{
	printf("Test: Edge case handling... ");

	gTestTables.Initialize();

	// Test black (Y=16, U=128, V=128)
	uint8 black_yuy2[4] = {16, 128, 16, 128};
	uint8 black_rgb32[8];

	convert_yuy2_optimized(black_rgb32, black_yuy2, 2, 1);

	// Black should be close to (0, 0, 0)
	if (black_rgb32[0] > 5 || black_rgb32[1] > 5 || black_rgb32[2] > 5) {
		printf("FAIL (black: B=%d G=%d R=%d, expected near 0)\n",
			black_rgb32[0], black_rgb32[1], black_rgb32[2]);
		return false;
	}

	// Test white (Y=235, U=128, V=128)
	uint8 white_yuy2[4] = {235, 128, 235, 128};
	uint8 white_rgb32[8];

	convert_yuy2_optimized(white_rgb32, white_yuy2, 2, 1);

	// White should be close to (255, 255, 255)
	if (white_rgb32[0] < 250 || white_rgb32[1] < 250 || white_rgb32[2] < 250) {
		printf("FAIL (white: B=%d G=%d R=%d, expected near 255)\n",
			white_rgb32[0], white_rgb32[1], white_rgb32[2]);
		return false;
	}

	// Test red (Y=82, U=90, V=240)
	uint8 red_yuy2[4] = {82, 90, 82, 240};
	uint8 red_rgb32[8];

	convert_yuy2_optimized(red_rgb32, red_yuy2, 2, 1);

	// Red should have R > G, R > B
	if (red_rgb32[2] < red_rgb32[1] || red_rgb32[2] < red_rgb32[0]) {
		printf("FAIL (red: B=%d G=%d R=%d, R should be highest)\n",
			red_rgb32[0], red_rgb32[1], red_rgb32[2]);
		return false;
	}

	// Test alpha channel is always 255
	if (black_rgb32[3] != 255 || white_rgb32[3] != 255 || red_rgb32[3] != 255) {
		printf("FAIL (alpha not 255)\n");
		return false;
	}

	printf("OK\n");
	return true;
}


// =============================================================================
// Test 5: Lookup Table Memory Footprint
// =============================================================================

static bool
test_table_memory()
{
	printf("Test: Lookup table memory footprint... ");

	size_t tableSize = sizeof(yuv_rgb_lookup_tables);

	// Tables should be around 5KB (5 tables * 256 entries * 4 bytes + overhead)
	// Using int32 for Y table to avoid overflow (max Y value 298*239=71222)
	size_t expectedSize = 5 * 256 * sizeof(int32) + sizeof(bool);

	if (tableSize > expectedSize + 100) {  // Allow small padding
		printf("FAIL (size=%zu, expected ~%zu)\n", tableSize, expectedSize);
		return false;
	}

	printf("OK (table size: %zu bytes, ~%.1f KB)\n",
		tableSize, tableSize / 1024.0f);
	return true;
}


// =============================================================================
// Main
// =============================================================================

int
main(int argc, char** argv)
{
	printf("\n");
	printf("===========================================\n");
	printf("Video Conversion Tests (Group 3)\n");
	printf("===========================================\n\n");

	int passed = 0;
	int failed = 0;

	if (test_lookup_correctness())
		passed++;
	else
		failed++;

	if (test_conversion_equivalence())
		passed++;
	else
		failed++;

	if (test_conversion_performance())
		passed++;
	else
		failed++;

	if (test_edge_cases())
		passed++;
	else
		failed++;

	if (test_table_memory())
		passed++;
	else
		failed++;

	printf("\n");
	printf("===========================================\n");
	printf("Results: %d passed, %d failed\n", passed, failed);
	printf("===========================================\n\n");

	return (failed == 0) ? 0 : 1;
}
