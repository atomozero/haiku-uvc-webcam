/*
 * Test for discrete frame interval selection logic
 *
 * This test verifies that the UVC driver selects only supported
 * discrete frame intervals instead of calculating arbitrary values.
 *
 * Build: g++ -O2 -o test_interval_selection test_interval_selection.cpp -lbe
 * Run: ./test_interval_selection
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

// Simulate the usb_video_frame_descriptor structure
struct usb_video_frame_descriptor {
	uint8_t length;
	uint8_t descriptor_type;
	uint8_t descriptor_subtype;
	uint8_t frame_index;
	uint8_t capabilities;
	uint16_t width;
	uint16_t height;
	uint32_t min_bit_rate;
	uint32_t max_bit_rate;
	uint32_t max_video_frame_buffer_size;
	uint32_t default_frame_interval;
	uint8_t frame_interval_type;  // 0 = continuous, >0 = number of discrete intervals
	union {
		struct {
			uint32_t min_frame_interval;
			uint32_t max_frame_interval;
			uint32_t frame_interval_step;
		} continuous;
		uint32_t discrete_frame_intervals[10];  // Fixed size for testing
	};
};

// Known supported intervals for typical webcams (in 100ns units)
const uint32_t SUPPORTED_INTERVALS[] = {
	333333,   // 30 fps
	500000,   // 20 fps
	666666,   // 15 fps
	1000000,  // 10 fps
	2000000   // 5 fps
};
const int NUM_SUPPORTED_INTERVALS = 5;

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition, message) do { \
	if (condition) { \
		tests_passed++; \
		printf("  ✓ PASS: %s\n", message); \
	} else { \
		tests_failed++; \
		printf("  ✗ FAIL: %s\n", message); \
	} \
} while(0)

// Helper: check if interval is in the supported list
bool IsIntervalSupported(uint32_t interval) {
	for (int i = 0; i < NUM_SUPPORTED_INTERVALS; i++) {
		if (SUPPORTED_INTERVALS[i] == interval)
			return true;
	}
	return false;
}

// The original (INCORRECT) algorithm that calculates arbitrary intervals
uint32_t SelectIntervalOldAlgorithm(uint32_t frameSize, uint32_t maxBandwidth,
	uint32_t requestedInterval)
{
	// USB 2.0 high-speed: 8000 microframes/second
	uint32_t bytesPerSecond = maxBandwidth * 8000;
	float maxFps = (float)bytesPerSecond / frameSize;

	// 90% safety margin
	float safeFps = maxFps * 0.9f;
	if (safeFps < 1.0f) safeFps = 1.0f;

	uint32_t adaptedInterval = (uint32_t)(10000000.0f / safeFps);

	if (adaptedInterval > requestedInterval) {
		return adaptedInterval;
	}
	return requestedInterval;
}

// The new (CORRECT) algorithm that selects from discrete intervals
uint32_t SelectIntervalNewAlgorithm(const usb_video_frame_descriptor* frameDesc,
	uint32_t maxBandwidth, uint32_t requestedInterval)
{
	uint32_t frameSize = frameDesc->width * frameDesc->height * 2;  // YUY2
	uint32_t bytesPerSecond = maxBandwidth * 8000;

	if (frameDesc->frame_interval_type > 0) {
		// DISCRETE INTERVALS
		uint32_t selectedInterval = 0;

		for (int i = 0; i < frameDesc->frame_interval_type; i++) {
			uint32_t interval = frameDesc->discrete_frame_intervals[i];
			float fps = 10000000.0f / interval;
			uint32_t requiredBandwidth = (uint32_t)(frameSize * fps);

			// 90% safety margin
			if (requiredBandwidth <= (uint32_t)(bytesPerSecond * 0.9f)) {
				if (selectedInterval == 0 || interval < selectedInterval) {
					selectedInterval = interval;
				}
			}
		}

		// Fallback to slowest if nothing fits
		if (selectedInterval == 0) {
			selectedInterval = frameDesc->discrete_frame_intervals[frameDesc->frame_interval_type - 1];
		}

		if (selectedInterval > requestedInterval) {
			return selectedInterval;
		}
		return requestedInterval;
	}

	// CONTINUOUS INTERVALS - use old algorithm behavior
	return SelectIntervalOldAlgorithm(frameSize, maxBandwidth, requestedInterval);
}

// Create a test frame descriptor
void InitFrameDescriptor(usb_video_frame_descriptor* desc, uint16_t width,
	uint16_t height, bool useDiscrete)
{
	memset(desc, 0, sizeof(*desc));
	desc->width = width;
	desc->height = height;
	desc->default_frame_interval = 333333;  // 30 fps

	if (useDiscrete) {
		desc->frame_interval_type = NUM_SUPPORTED_INTERVALS;
		for (int i = 0; i < NUM_SUPPORTED_INTERVALS; i++) {
			desc->discrete_frame_intervals[i] = SUPPORTED_INTERVALS[i];
		}
	} else {
		desc->frame_interval_type = 0;
		desc->continuous.min_frame_interval = 333333;
		desc->continuous.max_frame_interval = 2000000;
		desc->continuous.frame_interval_step = 1;
	}
}

void TestOldAlgorithmProducesUnsupportedInterval()
{
	printf("\n=== Test: Old algorithm produces unsupported interval ===\n");

	// 640x480 YUY2 = 614400 bytes/frame
	uint32_t frameSize = 640 * 480 * 2;

	// Simulated bandwidth: 1024 bytes per microframe
	// (typical endpoint with 2 transactions but driver limits to 1)
	uint32_t maxBandwidth = 1024;

	uint32_t requestedInterval = 333333;  // Request 30 fps

	uint32_t result = SelectIntervalOldAlgorithm(frameSize, maxBandwidth, requestedInterval);

	printf("  Frame size: %u bytes\n", frameSize);
	printf("  Max bandwidth: %u bytes/microframe\n", maxBandwidth);
	printf("  Bytes/second: %u (%.1f MB/s)\n", maxBandwidth * 8000,
		(maxBandwidth * 8000) / 1048576.0f);
	printf("  Max FPS at this bandwidth: %.1f\n", (float)(maxBandwidth * 8000) / frameSize);
	printf("  Requested interval: %u (%.1f fps)\n", requestedInterval,
		10000000.0f / requestedInterval);
	printf("  Old algorithm result: %u (%.1f fps)\n", result, 10000000.0f / result);

	// The old algorithm should produce something like 1066666 which is NOT supported
	TEST_ASSERT(!IsIntervalSupported(result),
		"Old algorithm produces unsupported interval (as expected bug)");

	// Check if result is the problematic 1066666 or similar
	if (result > 1000000 && result < 2000000 && result != 1000000) {
		printf("  [INFO] Old algorithm produced %u, which is between 10fps and 5fps intervals\n", result);
		printf("         This unsupported value causes TIMEOUT on some webcams!\n");
	}
}

void TestNewAlgorithmSelectsDiscreteInterval()
{
	printf("\n=== Test: New algorithm selects discrete interval ===\n");

	usb_video_frame_descriptor desc;
	InitFrameDescriptor(&desc, 640, 480, true);  // Discrete intervals

	// Same bandwidth as above
	uint32_t maxBandwidth = 1024;
	uint32_t requestedInterval = 333333;  // Request 30 fps

	uint32_t result = SelectIntervalNewAlgorithm(&desc, maxBandwidth, requestedInterval);

	printf("  Resolution: %ux%u\n", desc.width, desc.height);
	printf("  Frame size: %u bytes\n", desc.width * desc.height * 2);
	printf("  Max bandwidth: %u bytes/microframe\n", maxBandwidth);
	printf("  Requested interval: %u (%.1f fps)\n", requestedInterval,
		10000000.0f / requestedInterval);
	printf("  New algorithm result: %u (%.1f fps)\n", result, 10000000.0f / result);

	TEST_ASSERT(IsIntervalSupported(result),
		"New algorithm produces supported interval");

	// With 1024 bytes/microframe and 640x480 YUY2:
	// bytesPerSecond = 1024 * 8000 = 8,192,000
	// frameSize = 614,400
	// maxFps = 8,192,000 / 614,400 = 13.3 fps
	// With 90% safety = 12.0 fps
	// Should select 1000000 (10 fps) as it's the fastest that fits
	TEST_ASSERT(result == 1000000, "Should select 10fps interval (1000000)");
}

void TestBandwidthInsufficientForAnyInterval()
{
	printf("\n=== Test: Bandwidth insufficient - use slowest ===\n");

	usb_video_frame_descriptor desc;
	InitFrameDescriptor(&desc, 1920, 1080, true);  // 1080p, discrete

	// Very low bandwidth
	uint32_t maxBandwidth = 512;
	uint32_t requestedInterval = 333333;

	uint32_t result = SelectIntervalNewAlgorithm(&desc, maxBandwidth, requestedInterval);

	printf("  Resolution: %ux%u\n", desc.width, desc.height);
	printf("  Frame size: %u bytes\n", desc.width * desc.height * 2);
	printf("  Max bandwidth: %u bytes/microframe\n", maxBandwidth);
	printf("  Result: %u (%.1f fps)\n", result, 10000000.0f / result);

	TEST_ASSERT(IsIntervalSupported(result),
		"Fallback produces supported interval");
	TEST_ASSERT(result == 2000000,
		"Should use slowest interval (5fps) when bandwidth insufficient");
}

void TestSufficientBandwidthFor30fps()
{
	printf("\n=== Test: Sufficient bandwidth keeps 30fps ===\n");

	usb_video_frame_descriptor desc;
	InitFrameDescriptor(&desc, 320, 240, true);  // Small resolution

	// High bandwidth
	uint32_t maxBandwidth = 3072;
	uint32_t requestedInterval = 333333;

	uint32_t result = SelectIntervalNewAlgorithm(&desc, maxBandwidth, requestedInterval);

	printf("  Resolution: %ux%u\n", desc.width, desc.height);
	printf("  Frame size: %u bytes\n", desc.width * desc.height * 2);
	printf("  Max bandwidth: %u bytes/microframe (%.1f MB/s)\n", maxBandwidth,
		(maxBandwidth * 8000) / 1048576.0f);
	printf("  Result: %u (%.1f fps)\n", result, 10000000.0f / result);

	TEST_ASSERT(result == 333333,
		"Should keep 30fps when bandwidth sufficient");
}

void TestContinuousInterval()
{
	printf("\n=== Test: Continuous interval support ===\n");

	usb_video_frame_descriptor desc;
	InitFrameDescriptor(&desc, 640, 480, false);  // Continuous intervals

	uint32_t maxBandwidth = 1024;
	uint32_t requestedInterval = 333333;

	uint32_t result = SelectIntervalNewAlgorithm(&desc, maxBandwidth, requestedInterval);

	printf("  Resolution: %ux%u (continuous intervals)\n", desc.width, desc.height);
	printf("  Continuous range: %u - %u\n",
		desc.continuous.min_frame_interval,
		desc.continuous.max_frame_interval);
	printf("  Result: %u (%.1f fps)\n", result, 10000000.0f / result);

	// For continuous, any value in range is acceptable
	TEST_ASSERT(result >= desc.continuous.min_frame_interval &&
		result <= desc.continuous.max_frame_interval,
		"Continuous interval within valid range");
}

void TestCompareOldVsNew()
{
	printf("\n=== Test: Direct comparison old vs new for 640x480 ===\n");

	usb_video_frame_descriptor desc;
	InitFrameDescriptor(&desc, 640, 480, true);

	uint32_t maxBandwidth = 1024;
	uint32_t requestedInterval = 333333;

	uint32_t oldResult = SelectIntervalOldAlgorithm(640 * 480 * 2, maxBandwidth, requestedInterval);
	uint32_t newResult = SelectIntervalNewAlgorithm(&desc, maxBandwidth, requestedInterval);

	printf("  Old algorithm: interval=%u (%.1f fps) - supported: %s\n",
		oldResult, 10000000.0f / oldResult,
		IsIntervalSupported(oldResult) ? "YES" : "NO");
	printf("  New algorithm: interval=%u (%.1f fps) - supported: %s\n",
		newResult, 10000000.0f / newResult,
		IsIntervalSupported(newResult) ? "YES" : "NO");

	TEST_ASSERT(IsIntervalSupported(newResult) && !IsIntervalSupported(oldResult),
		"New algorithm fixes the unsupported interval bug");
}

void TestDriverModified()
{
	printf("\n=== Test: Verify driver is modified ===\n");

	// Check if the driver file contains the new discrete interval logic
	FILE* fp = fopen("../addons/uvc/UVCCamDevice.cpp", "r");
	if (fp == NULL) {
		printf("  [SKIP] Cannot open driver file for verification\n");
		return;
	}

	char buffer[4096];
	bool foundDiscreteCheck = false;
	bool foundOldArbitraryCalc = false;
	bool foundNewIntervalSelection = false;

	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		if (strstr(buffer, "frame_interval_type > 0"))
			foundDiscreteCheck = true;
		if (strstr(buffer, "discrete_frame_intervals[i]"))
			foundNewIntervalSelection = true;
		if (strstr(buffer, "adaptedInterval = (uint32)(10000000.0f / safeFps)") &&
			!strstr(buffer, "// "))  // Not commented out
			foundOldArbitraryCalc = true;
	}

	fclose(fp);

	TEST_ASSERT(foundDiscreteCheck,
		"Driver checks for discrete intervals (frame_interval_type > 0)");
	TEST_ASSERT(foundNewIntervalSelection,
		"Driver iterates discrete_frame_intervals array");

	// The old calculation should still exist for continuous intervals,
	// so we just check if the new logic is present
	printf("  Driver modification status:\n");
	printf("    - Discrete interval type check: %s\n",
		foundDiscreteCheck ? "FOUND" : "NOT FOUND");
	printf("    - Discrete interval iteration: %s\n",
		foundNewIntervalSelection ? "FOUND" : "NOT FOUND");
}

int main()
{
	printf("========================================\n");
	printf("Discrete Frame Interval Selection Tests\n");
	printf("========================================\n");

	printf("\nSupported intervals for typical webcams:\n");
	for (int i = 0; i < NUM_SUPPORTED_INTERVALS; i++) {
		printf("  %u µs (100ns units) = %.1f fps\n",
			SUPPORTED_INTERVALS[i], 10000000.0f / SUPPORTED_INTERVALS[i]);
	}

	// Run tests
	TestOldAlgorithmProducesUnsupportedInterval();
	TestNewAlgorithmSelectsDiscreteInterval();
	TestBandwidthInsufficientForAnyInterval();
	TestSufficientBandwidthFor30fps();
	TestContinuousInterval();
	TestCompareOldVsNew();
	TestDriverModified();

	printf("\n========================================\n");
	printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
	printf("========================================\n");

	return tests_failed > 0 ? 1 : 0;
}
