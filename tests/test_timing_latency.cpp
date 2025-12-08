/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for timing and latency optimizations (Group 4)
 *
 * Build:
 *   g++ -O2 -o test_timing_latency test_timing_latency.cpp -lbe
 *
 * Run:
 *   ./test_timing_latency
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <stdint.h>


// =============================================================================
// Frame Timing Statistics (matches driver implementation)
// =============================================================================

struct frame_timing_stats {
	bigtime_t	last_frame_time;
	bigtime_t	frame_interval_sum;
	bigtime_t	frame_interval_min;
	bigtime_t	frame_interval_max;
	uint32		frame_count;
	bigtime_t	processing_time_sum;
	bigtime_t	processing_time_max;
	bigtime_t	expected_interval;
	bigtime_t	jitter_sum;
	bigtime_t	adaptive_timeout;

	void Reset()
	{
		last_frame_time = 0;
		frame_interval_sum = 0;
		frame_interval_min = INT64_MAX;
		frame_interval_max = 0;
		frame_count = 0;
		processing_time_sum = 0;
		processing_time_max = 0;
		expected_interval = 33333;  // 30fps default
		jitter_sum = 0;
		adaptive_timeout = 100000;
	}

	void RecordFrame(bigtime_t processingTime)
	{
		bigtime_t now = system_time();

		if (last_frame_time > 0) {
			bigtime_t interval = now - last_frame_time;

			frame_interval_sum += interval;
			if (interval < frame_interval_min)
				frame_interval_min = interval;
			if (interval > frame_interval_max)
				frame_interval_max = interval;

			bigtime_t jitter = interval > expected_interval
				? interval - expected_interval
				: expected_interval - interval;
			jitter_sum += jitter;

			bigtime_t targetTimeout = frame_interval_max * 2 + processing_time_max;
			adaptive_timeout = (adaptive_timeout * 7 + targetTimeout) / 8;

			if (adaptive_timeout < 10000)
				adaptive_timeout = 10000;
			if (adaptive_timeout > 500000)
				adaptive_timeout = 500000;
		}

		last_frame_time = now;
		frame_count++;

		processing_time_sum += processingTime;
		if (processingTime > processing_time_max)
			processing_time_max = processingTime;
	}

	float GetAverageFPS() const
	{
		if (frame_count < 2 || frame_interval_sum == 0)
			return 0.0f;
		bigtime_t avgInterval = frame_interval_sum / (frame_count - 1);
		return 1000000.0f / avgInterval;
	}

	bigtime_t GetAverageInterval() const
	{
		if (frame_count < 2)
			return expected_interval;
		return frame_interval_sum / (frame_count - 1);
	}

	bigtime_t GetAverageJitter() const
	{
		if (frame_count < 2)
			return 0;
		return jitter_sum / (frame_count - 1);
	}

	bigtime_t GetAverageProcessingTime() const
	{
		if (frame_count == 0)
			return 0;
		return processing_time_sum / frame_count;
	}
};


// =============================================================================
// Test 1: Basic Statistics Recording
// =============================================================================

static bool
test_basic_stats()
{
	printf("Test: Basic statistics recording... ");

	frame_timing_stats stats;
	stats.Reset();

	// Verify initial state
	if (stats.frame_count != 0 || stats.last_frame_time != 0) {
		printf("FAIL (initial state)\n");
		return false;
	}

	// Record some frames
	stats.RecordFrame(1000);  // 1ms processing
	snooze(33333);  // ~30fps interval
	stats.RecordFrame(1500);
	snooze(33333);
	stats.RecordFrame(1200);

	if (stats.frame_count != 3) {
		printf("FAIL (frame count: %u, expected 3)\n", stats.frame_count);
		return false;
	}

	if (stats.processing_time_max < 1500) {
		printf("FAIL (max processing: %lld, expected >= 1500)\n",
			stats.processing_time_max);
		return false;
	}

	printf("OK (frames: %u, avg proc: %lld us)\n",
		stats.frame_count, stats.GetAverageProcessingTime());
	return true;
}


// =============================================================================
// Test 2: FPS Calculation
// =============================================================================

static bool
test_fps_calculation()
{
	printf("Test: FPS calculation accuracy... ");

	frame_timing_stats stats;
	stats.Reset();

	// Simulate 30fps (33333us interval)
	const int kFrames = 10;
	const bigtime_t kInterval = 33333;  // ~30fps

	for (int i = 0; i < kFrames; i++) {
		stats.RecordFrame(1000);
		if (i < kFrames - 1)
			snooze(kInterval);
	}

	float fps = stats.GetAverageFPS();
	float expectedFps = 30.0f;

	// Allow 10% tolerance for timing jitter
	if (fps < expectedFps * 0.9f || fps > expectedFps * 1.1f) {
		printf("FAIL (fps: %.2f, expected ~%.2f)\n", fps, expectedFps);
		return false;
	}

	printf("OK (measured FPS: %.2f, expected: ~%.2f)\n", fps, expectedFps);
	return true;
}


// =============================================================================
// Test 3: Adaptive Timeout Calculation
// =============================================================================

static bool
test_adaptive_timeout()
{
	printf("Test: Adaptive timeout calculation... ");

	frame_timing_stats stats;
	stats.Reset();

	// Initial timeout should be default (100ms)
	if (stats.adaptive_timeout != 100000) {
		printf("FAIL (initial timeout: %lld, expected 100000)\n",
			stats.adaptive_timeout);
		return false;
	}

	// Simulate regular 30fps frames
	for (int i = 0; i < 20; i++) {
		stats.RecordFrame(1000);
		snooze(33333);
	}

	// Adaptive timeout should have decreased towards 2x interval
	// Expected: ~2 * 33333 + processing = ~67000-70000
	if (stats.adaptive_timeout > 100000) {
		printf("FAIL (timeout didn't decrease: %lld)\n", stats.adaptive_timeout);
		return false;
	}

	printf("OK (adaptive timeout: %lld us)\n", stats.adaptive_timeout);
	return true;
}


// =============================================================================
// Test 4: Jitter Tracking
// =============================================================================

static bool
test_jitter_tracking()
{
	printf("Test: Jitter tracking... ");

	frame_timing_stats stats;
	stats.Reset();
	stats.expected_interval = 33333;  // 30fps

	// Simulate frames with varying intervals
	stats.RecordFrame(1000);
	snooze(33333);  // On time
	stats.RecordFrame(1000);
	snooze(40000);  // Late (+6667us)
	stats.RecordFrame(1000);
	snooze(26666);  // Early (-6667us)
	stats.RecordFrame(1000);
	snooze(33333);  // On time
	stats.RecordFrame(1000);

	bigtime_t avgJitter = stats.GetAverageJitter();

	// Should have detected jitter
	if (avgJitter < 1000) {
		printf("FAIL (jitter too low: %lld us)\n", avgJitter);
		return false;
	}

	printf("OK (avg jitter: %lld us)\n", avgJitter);
	return true;
}


// =============================================================================
// Test 5: Min/Max Interval Tracking
// =============================================================================

static bool
test_interval_minmax()
{
	printf("Test: Min/max interval tracking... ");

	frame_timing_stats stats;
	stats.Reset();

	// Record frames with varying intervals
	stats.RecordFrame(1000);
	snooze(20000);  // Fast
	stats.RecordFrame(1000);
	snooze(50000);  // Slow
	stats.RecordFrame(1000);
	snooze(33333);  // Normal
	stats.RecordFrame(1000);

	// Min should be around 20000, max around 50000
	// (Allow for timing variations)
	if (stats.frame_interval_min > 25000) {
		printf("FAIL (min too high: %lld)\n", stats.frame_interval_min);
		return false;
	}

	if (stats.frame_interval_max < 45000) {
		printf("FAIL (max too low: %lld)\n", stats.frame_interval_max);
		return false;
	}

	printf("OK (min: %lld us, max: %lld us)\n",
		stats.frame_interval_min, stats.frame_interval_max);
	return true;
}


// =============================================================================
// Test 6: Timeout Clamping
// =============================================================================

static bool
test_timeout_clamping()
{
	printf("Test: Timeout clamping... ");

	frame_timing_stats stats;
	stats.Reset();

	// Force very short intervals to test minimum clamp
	for (int i = 0; i < 50; i++) {
		stats.RecordFrame(100);
		snooze(1000);  // 1ms = 1000fps (unrealistic but tests clamp)
	}

	// Should be clamped to minimum (10ms)
	if (stats.adaptive_timeout < 10000) {
		printf("FAIL (below minimum: %lld)\n", stats.adaptive_timeout);
		return false;
	}

	printf("OK (clamped to minimum: %lld us)\n", stats.adaptive_timeout);
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
	printf("Timing and Latency Tests (Group 4)\n");
	printf("===========================================\n\n");

	int passed = 0;
	int failed = 0;

	if (test_basic_stats())
		passed++;
	else
		failed++;

	if (test_fps_calculation())
		passed++;
	else
		failed++;

	if (test_adaptive_timeout())
		passed++;
	else
		failed++;

	if (test_jitter_tracking())
		passed++;
	else
		failed++;

	if (test_interval_minmax())
		passed++;
	else
		failed++;

	if (test_timeout_clamping())
		passed++;
	else
		failed++;

	printf("\n");
	printf("===========================================\n");
	printf("Results: %d passed, %d failed\n", passed, failed);
	printf("===========================================\n\n");

	return (failed == 0) ? 0 : 1;
}
