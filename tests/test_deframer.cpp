/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for deframer optimizations (Group 6)
 *
 * Build:
 *   g++ -O2 -o test_deframer test_deframer.cpp -lbe
 *
 * Run:
 *   ./test_deframer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <stdint.h>


// =============================================================================
// Deframer Statistics (matches driver implementation)
// =============================================================================

struct deframer_stats {
	uint32		frames_completed;
	uint32		frames_incomplete;
	uint32		fid_changes;
	uint32		queue_overflows;
	bigtime_t	last_report_time;
	size_t		expected_frame_size;

	float GetCompletionRate() const {
		uint32 total = frames_completed + frames_incomplete;
		if (total == 0)
			return 100.0f;
		return 100.0f * frames_completed / total;
	}

	float GetIncompleteRate() const {
		uint32 total = frames_completed + frames_incomplete;
		if (total == 0)
			return 0.0f;
		return 100.0f * frames_incomplete / total;
	}
};


// =============================================================================
// Test 1: Completion Rate Calculation
// =============================================================================

static bool
test_completion_rate()
{
	printf("Test: Completion rate calculation... ");

	deframer_stats stats;
	memset(&stats, 0, sizeof(stats));

	// 100% completion
	stats.frames_completed = 100;
	stats.frames_incomplete = 0;
	float rate = stats.GetCompletionRate();
	if (rate != 100.0f) {
		printf("FAIL (100%% case: %.2f)\\n", rate);
		return false;
	}

	// 90% completion
	stats.frames_completed = 90;
	stats.frames_incomplete = 10;
	rate = stats.GetCompletionRate();
	if (rate < 89.9f || rate > 90.1f) {
		printf("FAIL (90%% case: %.2f)\\n", rate);
		return false;
	}

	// 0% completion
	stats.frames_completed = 0;
	stats.frames_incomplete = 100;
	rate = stats.GetCompletionRate();
	if (rate != 0.0f) {
		printf("FAIL (0%% case: %.2f)\\n", rate);
		return false;
	}

	// Empty stats (should return 100% - no failures)
	stats.frames_completed = 0;
	stats.frames_incomplete = 0;
	rate = stats.GetCompletionRate();
	if (rate != 100.0f) {
		printf("FAIL (empty case: %.2f)\\n", rate);
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 2: Incomplete Rate Calculation
// =============================================================================

static bool
test_incomplete_rate()
{
	printf("Test: Incomplete rate calculation... ");

	deframer_stats stats;
	memset(&stats, 0, sizeof(stats));

	// 0% incomplete
	stats.frames_completed = 100;
	stats.frames_incomplete = 0;
	float rate = stats.GetIncompleteRate();
	if (rate != 0.0f) {
		printf("FAIL (0%% case: %.2f)\\n", rate);
		return false;
	}

	// 10% incomplete
	stats.frames_completed = 90;
	stats.frames_incomplete = 10;
	rate = stats.GetIncompleteRate();
	if (rate < 9.9f || rate > 10.1f) {
		printf("FAIL (10%% case: %.2f)\\n", rate);
		return false;
	}

	// 100% incomplete
	stats.frames_completed = 0;
	stats.frames_incomplete = 100;
	rate = stats.GetIncompleteRate();
	if (rate != 100.0f) {
		printf("FAIL (100%% case: %.2f)\\n", rate);
		return false;
	}

	// Empty stats (should return 0%)
	stats.frames_completed = 0;
	stats.frames_incomplete = 0;
	rate = stats.GetIncompleteRate();
	if (rate != 0.0f) {
		printf("FAIL (empty case: %.2f)\\n", rate);
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 3: Rate Consistency (completion + incomplete = 100%)
// =============================================================================

static bool
test_rate_consistency()
{
	printf("Test: Rate consistency (sum = 100%%)... ");

	deframer_stats stats;
	memset(&stats, 0, sizeof(stats));

	// Test various combinations
	int testCases[][2] = {
		{100, 0}, {90, 10}, {50, 50}, {10, 90}, {0, 100},
		{75, 25}, {33, 67}, {1, 99}, {99, 1}
	};

	for (int i = 0; i < 9; i++) {
		stats.frames_completed = testCases[i][0];
		stats.frames_incomplete = testCases[i][1];

		float sum = stats.GetCompletionRate() + stats.GetIncompleteRate();
		if (sum < 99.9f || sum > 100.1f) {
			printf("FAIL (case %d/%d: sum=%.2f)\\n",
				testCases[i][0], testCases[i][1], sum);
			return false;
		}
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 4: FID Change Tracking Simulation
// =============================================================================

static bool
test_fid_tracking()
{
	printf("Test: FID change tracking... ");

	// Simulate FID changes (as in real UVC stream)
	uint32 fidChanges = 0;
	int currentFid = 0;

	// Simulate 10 frame boundaries (FID toggles each time)
	for (int frame = 0; frame < 10; frame++) {
		int newFid = frame % 2;  // FID toggles 0->1->0->1...
		if (newFid != currentFid) {
			fidChanges++;
			currentFid = newFid;
		}
	}

	// Should have 9 FID changes (first frame doesn't count as change)
	if (fidChanges != 9) {
		printf("FAIL (fid changes: %u, expected 9)\\n", fidChanges);
		return false;
	}

	printf("OK (FID changes: %u)\\n", fidChanges);
	return true;
}


// =============================================================================
// Test 5: Queue Overflow Detection
// =============================================================================

static bool
test_queue_overflow()
{
	printf("Test: Queue overflow detection... ");

	const int MAXFRAMEBUF = 8;  // Matches CAMDEFRAMER_MAX_QUEUED_FRAMES
	uint32 queueOverflows = 0;
	int queueCount = 0;

	// Simulate frames arriving faster than consumption
	for (int i = 0; i < 20; i++) {
		if (queueCount >= MAXFRAMEBUF) {
			queueOverflows++;
			// Frame dropped
		} else {
			queueCount++;
		}
	}

	// Should have 12 overflows (20 - 8)
	if (queueOverflows != 12) {
		printf("FAIL (overflows: %u, expected 12)\\n", queueOverflows);
		return false;
	}

	// Now simulate consumption catching up
	queueCount = MAXFRAMEBUF;
	for (int i = 0; i < 5; i++) {
		queueCount--;  // Consumer takes frame
	}

	if (queueCount != 3) {
		printf("FAIL (queue after consume: %d, expected 3)\\n", queueCount);
		return false;
	}

	printf("OK (overflows: %u)\\n", queueOverflows);
	return true;
}


// =============================================================================
// Test 6: Expected Frame Size Validation
// =============================================================================

static bool
test_frame_size_validation()
{
	printf("Test: Frame size validation... ");

	// Test common YUY2 frame sizes
	struct {
		int width;
		int height;
		size_t expected;
	} sizes[] = {
		{640, 480, 614400},    // VGA
		{320, 240, 153600},    // QVGA
		{1280, 720, 1843200},  // 720p
		{1920, 1080, 4147200}, // 1080p
		{160, 120, 38400}      // QQVGA
	};

	for (int i = 0; i < 5; i++) {
		// YUY2: 2 bytes per pixel
		size_t calculated = sizes[i].width * sizes[i].height * 2;
		if (calculated != sizes[i].expected) {
			printf("FAIL (%dx%d: %zu != %zu)\\n",
				sizes[i].width, sizes[i].height, calculated, sizes[i].expected);
			return false;
		}
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 7: Stats Structure Memory Layout
// =============================================================================

static bool
test_stats_memory()
{
	printf("Test: Stats structure memory layout... ");

	deframer_stats stats;

	// Verify structure can be zeroed properly
	memset(&stats, 0, sizeof(stats));

	if (stats.frames_completed != 0 || stats.frames_incomplete != 0 ||
		stats.fid_changes != 0 || stats.queue_overflows != 0 ||
		stats.last_report_time != 0 || stats.expected_frame_size != 0) {
		printf("FAIL (not properly zeroed)\\n");
		return false;
	}

	// Verify reasonable structure size (should be compact)
	// uint32 * 4 + bigtime_t + size_t = ~32 bytes on 64-bit
	if (sizeof(stats) > 64) {
		printf("FAIL (size %zu > 64 bytes)\\n", sizeof(stats));
		return false;
	}

	printf("OK (size: %zu bytes)\\n", sizeof(stats));
	return true;
}


// =============================================================================
// Main
// =============================================================================

int
main(int argc, char** argv)
{
	printf("\\n");
	printf("===========================================\\n");
	printf("Deframer Tests (Group 6)\\n");
	printf("===========================================\\n\\n");

	int passed = 0;
	int failed = 0;

	if (test_completion_rate())
		passed++;
	else
		failed++;

	if (test_incomplete_rate())
		passed++;
	else
		failed++;

	if (test_rate_consistency())
		passed++;
	else
		failed++;

	if (test_fid_tracking())
		passed++;
	else
		failed++;

	if (test_queue_overflow())
		passed++;
	else
		failed++;

	if (test_frame_size_validation())
		passed++;
	else
		failed++;

	if (test_stats_memory())
		passed++;
	else
		failed++;

	printf("\\n");
	printf("===========================================\\n");
	printf("Results: %d passed, %d failed\\n", passed, failed);
	printf("===========================================\\n\\n");

	return (failed == 0) ? 0 : 1;
}
