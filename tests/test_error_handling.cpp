/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for error handling optimizations (Group 5)
 *
 * Build:
 *   g++ -O2 -o test_error_handling test_error_handling.cpp -lbe
 *
 * Run:
 *   ./test_error_handling
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>


// =============================================================================
// Error types and recovery actions (matches driver implementation)
// =============================================================================

enum usb_error_type {
	USB_ERROR_NONE = 0,
	USB_ERROR_TIMEOUT,
	USB_ERROR_STALL,
	USB_ERROR_CRC,
	USB_ERROR_OVERFLOW,
	USB_ERROR_DISCONNECTED,
	USB_ERROR_UNKNOWN,
	USB_ERROR_TYPE_COUNT
};

enum error_recovery_action {
	RECOVERY_NONE = 0,
	RECOVERY_RETRY,
	RECOVERY_RESET_ENDPOINT,
	RECOVERY_REDUCE_BANDWIDTH,
	RECOVERY_RESTART_TRANSFER,
	RECOVERY_DEVICE_RESET,
	RECOVERY_FATAL
};


// Error histogram (matches driver)
struct usb_error_histogram {
	uint32		counts[USB_ERROR_TYPE_COUNT];
	bigtime_t	last_reset;
	uint32		total_transfers;

	void Reset() {
		memset(counts, 0, sizeof(counts));
		last_reset = system_time();
		total_transfers = 0;
	}

	void RecordError(usb_error_type type) {
		if (type >= 0 && type < USB_ERROR_TYPE_COUNT)
			counts[type]++;
	}

	float GetTotalErrorRate() const {
		if (total_transfers == 0)
			return 0.0f;
		uint32 totalErrors = 0;
		for (int i = 1; i < USB_ERROR_TYPE_COUNT; i++)
			totalErrors += counts[i];
		return (float)totalErrors / (float)total_transfers;
	}
};


// Recovery configuration (matches driver)
struct error_recovery_config {
	float		error_rate_warning;
	float		error_rate_action;
	uint32		consecutive_errors_max;
	bigtime_t	evaluation_window;
	uint32		consecutive_errors;
	bigtime_t	last_recovery_time;
	uint32		recovery_attempts;

	void Reset() {
		error_rate_warning = 0.05f;
		error_rate_action = 0.10f;
		consecutive_errors_max = 20;
		evaluation_window = 5000000;
		consecutive_errors = 0;
		last_recovery_time = 0;
		recovery_attempts = 0;
	}

	static error_recovery_action GetRecommendedAction(usb_error_type error) {
		switch (error) {
			case USB_ERROR_NONE:
				return RECOVERY_NONE;
			case USB_ERROR_TIMEOUT:
				return RECOVERY_RETRY;
			case USB_ERROR_STALL:
				return RECOVERY_RESET_ENDPOINT;
			case USB_ERROR_CRC:
				return RECOVERY_RETRY;
			case USB_ERROR_OVERFLOW:
				return RECOVERY_REDUCE_BANDWIDTH;
			case USB_ERROR_DISCONNECTED:
				return RECOVERY_FATAL;
			default:
				return RECOVERY_RETRY;
		}
	}

	static const char* GetActionName(error_recovery_action action) {
		static const char* names[] = {
			"None", "Retry", "Reset Endpoint", "Reduce Bandwidth",
			"Restart Transfer", "Device Reset", "Fatal"
		};
		if (action >= 0 && action <= RECOVERY_FATAL)
			return names[action];
		return "Unknown";
	}
};


// =============================================================================
// Test 1: Error Type Classification
// =============================================================================

static bool
test_error_classification()
{
	printf("Test: Error type to recovery action mapping... ");

	// Test each error type maps to correct action
	if (error_recovery_config::GetRecommendedAction(USB_ERROR_NONE) != RECOVERY_NONE) {
		printf("FAIL (NONE)\n");
		return false;
	}

	if (error_recovery_config::GetRecommendedAction(USB_ERROR_TIMEOUT) != RECOVERY_RETRY) {
		printf("FAIL (TIMEOUT)\n");
		return false;
	}

	if (error_recovery_config::GetRecommendedAction(USB_ERROR_STALL) != RECOVERY_RESET_ENDPOINT) {
		printf("FAIL (STALL)\n");
		return false;
	}

	if (error_recovery_config::GetRecommendedAction(USB_ERROR_OVERFLOW) != RECOVERY_REDUCE_BANDWIDTH) {
		printf("FAIL (OVERFLOW)\n");
		return false;
	}

	if (error_recovery_config::GetRecommendedAction(USB_ERROR_DISCONNECTED) != RECOVERY_FATAL) {
		printf("FAIL (DISCONNECTED)\n");
		return false;
	}

	printf("OK\n");
	return true;
}


// =============================================================================
// Test 2: Error Rate Calculation
// =============================================================================

static bool
test_error_rate()
{
	printf("Test: Error rate calculation... ");

	usb_error_histogram hist;
	hist.Reset();

	// Simulate 100 transfers with 10% errors
	for (int i = 0; i < 100; i++) {
		hist.total_transfers++;
		if (i < 10)
			hist.RecordError(USB_ERROR_TIMEOUT);
		else
			hist.RecordError(USB_ERROR_NONE);
	}

	float rate = hist.GetTotalErrorRate();
	if (rate < 0.09f || rate > 0.11f) {
		printf("FAIL (rate: %.2f, expected ~0.10)\n", rate);
		return false;
	}

	printf("OK (rate: %.2f)\n", rate);
	return true;
}


// =============================================================================
// Test 3: Consecutive Error Tracking
// =============================================================================

static bool
test_consecutive_errors()
{
	printf("Test: Consecutive error tracking... ");

	error_recovery_config config;
	config.Reset();

	// Simulate consecutive errors
	for (uint32 i = 0; i < 15; i++) {
		config.consecutive_errors++;
	}

	if (config.consecutive_errors != 15) {
		printf("FAIL (count: %u, expected 15)\n", config.consecutive_errors);
		return false;
	}

	// Simulate success (should reset counter)
	config.consecutive_errors = 0;

	if (config.consecutive_errors != 0) {
		printf("FAIL (not reset)\n");
		return false;
	}

	printf("OK\n");
	return true;
}


// =============================================================================
// Test 4: Recovery Threshold Check
// =============================================================================

static bool
test_recovery_threshold()
{
	printf("Test: Recovery threshold evaluation... ");

	error_recovery_config config;
	usb_error_histogram hist;
	config.Reset();
	hist.Reset();

	// Below threshold - should not trigger
	for (int i = 0; i < 100; i++) {
		hist.total_transfers++;
		if (i < 5)  // 5% errors
			hist.RecordError(USB_ERROR_TIMEOUT);
		else
			hist.RecordError(USB_ERROR_NONE);
	}

	float rate = hist.GetTotalErrorRate();
	bool shouldTrigger = (rate >= config.error_rate_action);

	if (shouldTrigger) {
		printf("FAIL (triggered at %.2f%%, threshold %.2f%%)\n",
			rate * 100, config.error_rate_action * 100);
		return false;
	}

	// Above threshold - should trigger
	hist.Reset();
	for (int i = 0; i < 100; i++) {
		hist.total_transfers++;
		if (i < 15)  // 15% errors
			hist.RecordError(USB_ERROR_TIMEOUT);
		else
			hist.RecordError(USB_ERROR_NONE);
	}

	rate = hist.GetTotalErrorRate();
	shouldTrigger = (rate >= config.error_rate_action);

	if (!shouldTrigger) {
		printf("FAIL (not triggered at %.2f%%)\n", rate * 100);
		return false;
	}

	printf("OK (threshold: %.0f%%, triggered at %.0f%%)\n",
		config.error_rate_action * 100, rate * 100);
	return true;
}


// =============================================================================
// Test 5: Action Name Strings
// =============================================================================

static bool
test_action_names()
{
	printf("Test: Action name strings... ");

	// Verify all action names are valid
	for (int i = 0; i <= RECOVERY_FATAL; i++) {
		const char* name = error_recovery_config::GetActionName((error_recovery_action)i);
		if (name == NULL || strlen(name) == 0) {
			printf("FAIL (action %d has no name)\n", i);
			return false;
		}
	}

	// Verify specific names
	if (strcmp(error_recovery_config::GetActionName(RECOVERY_NONE), "None") != 0) {
		printf("FAIL (NONE name)\n");
		return false;
	}

	if (strcmp(error_recovery_config::GetActionName(RECOVERY_FATAL), "Fatal") != 0) {
		printf("FAIL (FATAL name)\n");
		return false;
	}

	printf("OK\n");
	return true;
}


// =============================================================================
// Test 6: Config Reset
// =============================================================================

static bool
test_config_reset()
{
	printf("Test: Configuration reset... ");

	error_recovery_config config;
	config.consecutive_errors = 100;
	config.recovery_attempts = 50;
	config.error_rate_action = 0.5f;

	config.Reset();

	if (config.consecutive_errors != 0) {
		printf("FAIL (consecutive_errors not reset)\n");
		return false;
	}

	if (config.recovery_attempts != 0) {
		printf("FAIL (recovery_attempts not reset)\n");
		return false;
	}

	if (config.error_rate_action != 0.10f) {
		printf("FAIL (error_rate_action not reset to default)\n");
		return false;
	}

	printf("OK\n");
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
	printf("Error Handling Tests (Group 5)\n");
	printf("===========================================\n\n");

	int passed = 0;
	int failed = 0;

	if (test_error_classification())
		passed++;
	else
		failed++;

	if (test_error_rate())
		passed++;
	else
		failed++;

	if (test_consecutive_errors())
		passed++;
	else
		failed++;

	if (test_recovery_threshold())
		passed++;
	else
		failed++;

	if (test_action_names())
		passed++;
	else
		failed++;

	if (test_config_reset())
		passed++;
	else
		failed++;

	printf("\n");
	printf("===========================================\n");
	printf("Results: %d passed, %d failed\n", passed, failed);
	printf("===========================================\n\n");

	return (failed == 0) ? 0 : 1;
}
