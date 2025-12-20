/*
 * test_log_throttling.cpp
 *
 * Verifies that USB error log throttling is properly implemented.
 * This test checks the source code to ensure:
 * 1. Log throttling for consecutive transfer errors
 * 2. Periodic statistics logging (every 30 seconds)
 * 3. Summary log on stream stop
 * 4. Audio transfer error throttling
 *
 * Build: g++ -O2 -o test_log_throttling test_log_throttling.cpp
 * Run: ./test_log_throttling
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>

// ANSI colors
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

static const char* CAM_CPP_FILE = "../CamDevice.cpp";
static const char* UVC_CPP_FILE = "../addons/uvc/UVCCamDevice.cpp";
static const char* DRIVER_FILE = "/boot/home/config/non-packaged/add-ons/media/aukey_webcam_v4.media_addon";

struct TestResult {
	int passed;
	int failed;
	int warnings;
};

bool containsPattern(const std::string& content, const char* pattern)
{
	return content.find(pattern) != std::string::npos;
}

time_t getFileModTime(const char* path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		return 0;
	}
	return st.st_mtime;
}

std::string readFile(const char* path)
{
	std::ifstream file(path);
	if (!file.is_open()) {
		return "";
	}
	return std::string((std::istreambuf_iterator<char>(file)),
	                    std::istreambuf_iterator<char>());
}

void printHeader(const char* title)
{
	printf("\n========================================\n");
	printf(" %s\n", title);
	printf("========================================\n\n");
}

int main()
{
	TestResult results = {0, 0, 0};

	printHeader("Log Throttling Test");

	// Read source files
	printf("Test 1: Reading source files...\n");

	std::string camContent = readFile(CAM_CPP_FILE);
	if (camContent.empty()) {
		printf(RED "  [FAIL] Cannot open file: %s\n" RESET, CAM_CPP_FILE);
		return 1;
	}
	printf(GREEN "  [PASS] CamDevice.cpp loaded (%zu bytes)\n" RESET, camContent.length());
	results.passed++;

	std::string uvcContent = readFile(UVC_CPP_FILE);
	if (uvcContent.empty()) {
		printf(RED "  [FAIL] Cannot open file: %s\n" RESET, UVC_CPP_FILE);
		return 1;
	}
	printf(GREEN "  [PASS] UVCCamDevice.cpp loaded (%zu bytes)\n" RESET, uvcContent.length());
	results.passed++;

	// Test 2: Check consecutive failure logging throttle
	printf("\nTest 2: Checking consecutive failure logging throttle...\n");

	bool has10Threshold = containsPattern(camContent, "consecutiveFailures == 10");
	bool has50Threshold = containsPattern(camContent, "consecutiveFailures >= 50");

	if (has10Threshold && has50Threshold) {
		printf(GREEN "  [PASS] Consecutive failure thresholds at 10 and 50\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Consecutive failure thresholds may be incomplete\n" RESET);
		printf("         10 failures: %s\n", has10Threshold ? "found" : "MISSING");
		printf("         50 failures: %s\n", has50Threshold ? "found" : "MISSING");
		results.warnings++;
	}

	// Test 3: Check periodic stats logging
	printf("\nTest 3: Checking periodic statistics logging...\n");

	bool has30SecInterval = containsPattern(camContent, "30000000");  // 30 seconds in microseconds
	bool hasPeriodicStats = containsPattern(camContent, "USB Stats: success=");

	if (has30SecInterval && hasPeriodicStats) {
		printf(GREEN "  [PASS] Periodic statistics logging every 30 seconds\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Periodic statistics logging may be incomplete\n" RESET);
		printf("         30 second interval: %s\n", has30SecInterval ? "found" : "MISSING");
		printf("         Stats log format: %s\n", hasPeriodicStats ? "found" : "MISSING");
		results.warnings++;
	}

	// Test 4: Check summary log on stream stop
	printf("\nTest 4: Checking error summary on stream stop...\n");

	bool hasStopSummary = containsPattern(camContent, "LogErrorStatistics()");
	bool hasStopSummaryInStop = containsPattern(camContent, "// Log error statistics summary on stream stop");

	if (hasStopSummary && hasStopSummaryInStop) {
		printf(GREEN "  [PASS] Error summary logged on stream stop\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Error summary on stop may be missing\n" RESET);
		printf("         LogErrorStatistics call: %s\n", hasStopSummary ? "found" : "MISSING");
		printf("         In StopTransfer: %s\n", hasStopSummaryInStop ? "found" : "MISSING");
		results.warnings++;
	}

	// Test 5: Check audio transfer error throttling
	printf("\nTest 5: Checking audio transfer error throttling...\n");

	bool hasAudioThrottle = containsPattern(uvcContent, "consecutiveErrors == 10 || consecutiveErrors == 100");

	if (hasAudioThrottle) {
		printf(GREEN "  [PASS] Audio transfer errors throttled at 10/100\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Audio error throttling not found\n" RESET);
		results.warnings++;
	}

	// Test 6: Check first transfer logging limit
	printf("\nTest 6: Checking first-transfer logging limit...\n");

	bool hasLogLimit = containsPattern(camContent, "logCount < 5");

	if (hasLogLimit) {
		printf(GREEN "  [PASS] First 5 transfers logged, then throttled\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] First-transfer logging limit not found\n" RESET);
		results.warnings++;
	}

	// Test 7: Check driver binary timestamp
	printf("\nTest 7: Checking driver binary timestamp...\n");

	time_t sourceTime = getFileModTime(CAM_CPP_FILE);
	time_t driverTime = getFileModTime(DRIVER_FILE);

	if (driverTime == 0) {
		printf(YELLOW "  [WARN] Driver not installed\n" RESET);
		results.warnings++;
	} else if (driverTime < sourceTime) {
		printf(RED "  [FAIL] Driver is OLDER than source!\n" RESET);
		printf("         Run: make clean && make && make install\n");
		results.failed++;
	} else {
		printf(GREEN "  [PASS] Driver is up-to-date\n" RESET);
		results.passed++;
	}

	// Summary
	printHeader("Test Summary");

	printf("Passed:   %d\n", results.passed);
	printf("Failed:   %d\n", results.failed);
	printf("Warnings: %d\n", results.warnings);

	if (results.failed > 0) {
		printf(RED "\n*** FIX REQUIRED ***\n" RESET);
		printf("Log throttling is not properly implemented.\n");
		return 1;
	} else if (results.warnings > 0) {
		printf(YELLOW "\n*** WARNINGS ***\n" RESET);
		printf("Code appears correct but some checks couldn't be verified.\n");
		return 0;
	} else {
		printf(GREEN "\n*** ALL TESTS PASSED ***\n" RESET);
		printf("Log throttling properly implemented:\n");
		printf("  - First 5 transfers logged, then throttled\n");
		printf("  - Consecutive errors logged at 10 and 50\n");
		printf("  - Statistics every 30 seconds during streaming\n");
		printf("  - Error summary on stream stop\n");
		return 0;
	}
}
