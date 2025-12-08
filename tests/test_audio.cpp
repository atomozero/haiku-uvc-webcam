/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for audio optimizations (Group 8)
 *
 * Build:
 *   g++ -O2 -o test_audio test_audio.cpp -lbe
 *
 * Run:
 *   ./test_audio
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <OS.h>
#include <stdint.h>


// =============================================================================
// Audio Statistics (matches driver implementation)
// =============================================================================

struct audio_timing_stats {
	// Buffer timing
	bigtime_t	last_buffer_time;
	bigtime_t	buffer_interval_sum;
	bigtime_t	buffer_interval_min;
	bigtime_t	buffer_interval_max;
	uint32		buffer_count;

	// Buffer delivery
	uint32		buffers_sent;
	uint32		buffers_dropped;
	uint32		underruns;
	uint32		overruns;

	// Audio levels (RMS)
	float		peak_level;
	float		rms_level;
	double		rms_sum;
	uint64		samples_processed;

	void Reset() {
		last_buffer_time = 0;
		buffer_interval_sum = 0;
		buffer_interval_min = INT64_MAX;
		buffer_interval_max = 0;
		buffer_count = 0;
		buffers_sent = 0;
		buffers_dropped = 0;
		underruns = 0;
		overruns = 0;
		peak_level = 0.0f;
		rms_level = 0.0f;
		rms_sum = 0.0;
		samples_processed = 0;
	}

	void RecordBuffer(bigtime_t now) {
		if (last_buffer_time > 0) {
			bigtime_t interval = now - last_buffer_time;
			buffer_interval_sum += interval;
			if (interval < buffer_interval_min)
				buffer_interval_min = interval;
			if (interval > buffer_interval_max)
				buffer_interval_max = interval;
		}
		last_buffer_time = now;
		buffer_count++;
	}

	void RecordSamples(const int16* samples, size_t count) {
		for (size_t i = 0; i < count; i++) {
			float normalized = samples[i] / 32768.0f;
			float absVal = normalized < 0 ? -normalized : normalized;

			if (absVal > peak_level)
				peak_level = absVal;

			rms_sum += (double)(normalized * normalized);
		}
		samples_processed += count;

		// Update RMS periodically
		if (samples_processed > 0 && samples_processed % 1000 == 0) {
			rms_level = (float)sqrt(rms_sum / samples_processed);
		}
	}

	float GetAverageBufferRate() const {
		if (buffer_count < 2 || buffer_interval_sum == 0)
			return 0.0f;
		bigtime_t avgInterval = buffer_interval_sum / (buffer_count - 1);
		return 1000000.0f / avgInterval;
	}

	float GetDropRate() const {
		uint32 total = buffers_sent + buffers_dropped;
		if (total == 0)
			return 0.0f;
		return 100.0f * buffers_dropped / total;
	}
};


// =============================================================================
// Test 1: Buffer Rate Calculation
// =============================================================================

static bool
test_buffer_rate()
{
	printf("Test: Buffer rate calculation... ");

	audio_timing_stats stats;
	stats.Reset();

	// Simulate 100 buffers at ~93.75 buffers/second (10.67ms interval)
	bigtime_t start = system_time();
	for (int i = 0; i < 100; i++) {
		stats.RecordBuffer(start + i * 10667);  // ~93.75 Hz
	}

	float rate = stats.GetAverageBufferRate();
	// Expected: ~93.75 buffers/second
	if (rate < 90.0f || rate > 97.0f) {
		printf("FAIL (rate: %.2f, expected ~93.75)\\n", rate);
		return false;
	}

	printf("OK (rate: %.2f buf/s)\\n", rate);
	return true;
}


// =============================================================================
// Test 2: Drop Rate Calculation
// =============================================================================

static bool
test_drop_rate()
{
	printf("Test: Drop rate calculation... ");

	audio_timing_stats stats;
	stats.Reset();

	// 90 sent, 10 dropped = 10% drop rate
	stats.buffers_sent = 90;
	stats.buffers_dropped = 10;

	float rate = stats.GetDropRate();
	if (rate < 9.9f || rate > 10.1f) {
		printf("FAIL (rate: %.2f%%, expected 10%%)\\n", rate);
		return false;
	}

	// 100% success
	stats.buffers_sent = 100;
	stats.buffers_dropped = 0;
	rate = stats.GetDropRate();
	if (rate != 0.0f) {
		printf("FAIL (0%% case: %.2f)\\n", rate);
		return false;
	}

	// Empty stats
	stats.buffers_sent = 0;
	stats.buffers_dropped = 0;
	rate = stats.GetDropRate();
	if (rate != 0.0f) {
		printf("FAIL (empty case: %.2f)\\n", rate);
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 3: Peak Level Detection
// =============================================================================

static bool
test_peak_level()
{
	printf("Test: Peak level detection... ");

	audio_timing_stats stats;
	stats.Reset();

	// Generate samples with known peak
	int16 samples[100];
	for (int i = 0; i < 100; i++) {
		samples[i] = (int16)(sin(i * 0.1) * 16384);  // 50% amplitude
	}
	samples[50] = 32767;  // Max positive
	samples[51] = -32768; // Max negative

	stats.RecordSamples(samples, 100);

	// Peak should be close to 1.0
	if (stats.peak_level < 0.99f) {
		printf("FAIL (peak: %.3f, expected ~1.0)\\n", stats.peak_level);
		return false;
	}

	// Test silence
	stats.Reset();
	memset(samples, 0, sizeof(samples));
	stats.RecordSamples(samples, 100);

	if (stats.peak_level != 0.0f) {
		printf("FAIL (silence peak: %.3f)\\n", stats.peak_level);
		return false;
	}

	printf("OK (peak: %.3f)\\n", stats.peak_level);
	return true;
}


// =============================================================================
// Test 4: RMS Level Calculation
// =============================================================================

static bool
test_rms_level()
{
	printf("Test: RMS level calculation... ");

	audio_timing_stats stats;
	stats.Reset();

	// Generate 1kHz sine wave at 50% amplitude (1000 samples at 48kHz)
	const int kSamples = 2000;
	int16 samples[kSamples];
	for (int i = 0; i < kSamples; i++) {
		// ~1kHz at 48kHz sample rate = ~20.8 samples per cycle
		samples[i] = (int16)(sin(i * 2.0 * M_PI / 20.8) * 16384);
	}

	stats.RecordSamples(samples, kSamples);

	// RMS of 50% amplitude sine wave = 0.5 / sqrt(2) = ~0.354
	float expectedRMS = 0.5f / sqrtf(2.0f);
	if (stats.rms_level < expectedRMS * 0.8f || stats.rms_level > expectedRMS * 1.2f) {
		printf("FAIL (rms: %.3f, expected ~%.3f)\\n", stats.rms_level, expectedRMS);
		return false;
	}

	printf("OK (rms: %.3f)\\n", stats.rms_level);
	return true;
}


// =============================================================================
// Test 5: Buffer Timing Min/Max
// =============================================================================

static bool
test_buffer_timing()
{
	printf("Test: Buffer timing min/max... ");

	audio_timing_stats stats;
	stats.Reset();

	bigtime_t start = system_time();

	// Variable intervals: 5ms, 15ms, 10ms, 10ms
	stats.RecordBuffer(start);
	stats.RecordBuffer(start + 5000);
	stats.RecordBuffer(start + 20000);
	stats.RecordBuffer(start + 30000);
	stats.RecordBuffer(start + 40000);

	// Min should be ~5000, max should be ~15000
	if (stats.buffer_interval_min > 6000) {
		printf("FAIL (min too high: %lld)\\n", stats.buffer_interval_min);
		return false;
	}

	if (stats.buffer_interval_max < 14000) {
		printf("FAIL (max too low: %lld)\\n", stats.buffer_interval_max);
		return false;
	}

	printf("OK (min: %lld, max: %lld us)\\n",
		stats.buffer_interval_min, stats.buffer_interval_max);
	return true;
}


// =============================================================================
// Test 6: Sample Count Tracking
// =============================================================================

static bool
test_sample_count()
{
	printf("Test: Sample count tracking... ");

	audio_timing_stats stats;
	stats.Reset();

	int16 samples[512];
	memset(samples, 0, sizeof(samples));

	// Process multiple batches
	stats.RecordSamples(samples, 256);
	stats.RecordSamples(samples, 256);
	stats.RecordSamples(samples, 512);

	if (stats.samples_processed != 1024) {
		printf("FAIL (count: %llu, expected 1024)\\n",
			(unsigned long long)stats.samples_processed);
		return false;
	}

	printf("OK (samples: %llu)\\n", (unsigned long long)stats.samples_processed);
	return true;
}


// =============================================================================
// Test 7: Stats Reset
// =============================================================================

static bool
test_stats_reset()
{
	printf("Test: Stats reset... ");

	audio_timing_stats stats;

	// Set some values
	stats.buffers_sent = 100;
	stats.buffers_dropped = 10;
	stats.underruns = 5;
	stats.peak_level = 0.8f;
	stats.rms_level = 0.5f;
	stats.samples_processed = 10000;

	// Reset
	stats.Reset();

	// Verify all cleared
	if (stats.buffers_sent != 0 || stats.buffers_dropped != 0 ||
		stats.underruns != 0 || stats.peak_level != 0.0f ||
		stats.rms_level != 0.0f || stats.samples_processed != 0) {
		printf("FAIL (not properly reset)\\n");
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 8: Audio Buffer Size Calculations
// =============================================================================

static bool
test_buffer_sizes()
{
	printf("Test: Audio buffer size calculations... ");

	// Standard CD quality: 44100 Hz, 16-bit stereo
	uint32 sampleRate = 44100;
	uint32 channels = 2;
	uint32 bitsPerSample = 16;
	uint32 bytesPerFrame = channels * (bitsPerSample / 8);

	// 10ms buffer
	uint32 framesPerBuffer = sampleRate / 100;  // 441 frames
	size_t bufferSize = framesPerBuffer * bytesPerFrame;

	if (bufferSize != 1764) {  // 441 * 4
		printf("FAIL (CD buffer size: %zu, expected 1764)\\n", bufferSize);
		return false;
	}

	// USB audio common: 48000 Hz, 16-bit stereo
	sampleRate = 48000;
	framesPerBuffer = sampleRate / 100;  // 480 frames
	bufferSize = framesPerBuffer * bytesPerFrame;

	if (bufferSize != 1920) {  // 480 * 4
		printf("FAIL (USB buffer size: %zu, expected 1920)\\n", bufferSize);
		return false;
	}

	printf("OK\\n");
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
	printf("Audio Tests (Group 8)\\n");
	printf("===========================================\\n\\n");

	int passed = 0;
	int failed = 0;

	if (test_buffer_rate())
		passed++;
	else
		failed++;

	if (test_drop_rate())
		passed++;
	else
		failed++;

	if (test_peak_level())
		passed++;
	else
		failed++;

	if (test_rms_level())
		passed++;
	else
		failed++;

	if (test_buffer_timing())
		passed++;
	else
		failed++;

	if (test_sample_count())
		passed++;
	else
		failed++;

	if (test_stats_reset())
		passed++;
	else
		failed++;

	if (test_buffer_sizes())
		passed++;
	else
		failed++;

	printf("\\n");
	printf("===========================================\\n");
	printf("Results: %d passed, %d failed\\n", passed, failed);
	printf("===========================================\\n\\n");

	return (failed == 0) ? 0 : 1;
}
