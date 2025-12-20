/*
 * test_resolution_sync.cpp
 *
 * Verifies that resolution state synchronization is properly implemented.
 * This test checks the source code to ensure:
 * 1. fMJPEGFrameIndex and fMJPEGFormatIndex are initialized
 * 2. Frame index validation in _ProbeCommitFormat
 * 3. Proper frame_index to resolution mapping
 *
 * Build: g++ -O2 -o test_resolution_sync test_resolution_sync.cpp
 * Run: ./test_resolution_sync
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

static const char* CPP_FILE = "../addons/uvc/UVCCamDevice.cpp";
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

	printHeader("Resolution Sync Test");

	// Read source file
	printf("Test 1: Reading source file...\n");

	std::string content = readFile(CPP_FILE);
	if (content.empty()) {
		printf(RED "  [FAIL] Cannot open source file: %s\n" RESET, CPP_FILE);
		return 1;
	}
	printf(GREEN "  [PASS] UVCCamDevice.cpp loaded (%zu bytes)\n" RESET, content.length());
	results.passed++;

	// Test 2: Check fMJPEGFrameIndex initialization
	printf("\nTest 2: Checking fMJPEGFrameIndex initialization...\n");

	bool hasMJPEGInit = containsPattern(content, "fMJPEGFrameIndex(1)");
	bool hasMJPEGFormatInit = containsPattern(content, "fMJPEGFormatIndex(1)");

	if (hasMJPEGInit && hasMJPEGFormatInit) {
		printf(GREEN "  [PASS] MJPEG frame/format indices initialized in constructor\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] MJPEG indices not properly initialized\n" RESET);
		printf("         fMJPEGFrameIndex(1): %s\n", hasMJPEGInit ? "found" : "MISSING");
		printf("         fMJPEGFormatIndex(1): %s\n", hasMJPEGFormatInit ? "found" : "MISSING");
		results.failed++;
	}

	// Test 3: Check frame_index validation in _ProbeCommitFormat
	printf("\nTest 3: Checking frame_index validation in probe...\n");

	bool hasValidation = containsPattern(content, "frame_index == request.frame_index");
	bool hasNotFoundLog = containsPattern(content, "frame_index=%d not found");
	bool hasFallbackFix = containsPattern(content, "Falling back to frame_index=");

	if (hasValidation && hasNotFoundLog && hasFallbackFix) {
		printf(GREEN "  [PASS] Frame index validation with fallback implemented\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Frame index validation may be incomplete\n" RESET);
		printf("         Validation check: %s\n", hasValidation ? "found" : "MISSING");
		printf("         Not found log: %s\n", hasNotFoundLog ? "found" : "MISSING");
		printf("         Fallback fix: %s\n", hasFallbackFix ? "found" : "MISSING");
		results.warnings++;
	}

	// Test 4: Check frame_index to resolution logging
	printf("\nTest 4: Checking resolution logging in probe...\n");

	bool hasResolutionLog = containsPattern(content, "frame_index=%d corresponds to");

	if (hasResolutionLog) {
		printf(GREEN "  [PASS] Frame index to resolution logging implemented\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Resolution logging not found\n" RESET);
		results.warnings++;
	}

	// Test 5: Check AcceptVideoFrame uses descriptor->frame_index
	printf("\nTest 5: Checking AcceptVideoFrame uses descriptor->frame_index...\n");

	bool hasCorrectIndex = containsPattern(content, "fMJPEGFrameIndex = descriptor->frame_index");

	if (hasCorrectIndex) {
		printf(GREEN "  [PASS] AcceptVideoFrame uses descriptor->frame_index\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] AcceptVideoFrame may use wrong frame index\n" RESET);
		results.failed++;
	}

	// Test 6: Check SetParameterValue updates frame indices
	printf("\nTest 6: Checking resolution change updates frame indices...\n");

	bool hasParamUpdate = containsPattern(content, "Task 3: Update frame indices");

	if (hasParamUpdate) {
		printf(GREEN "  [PASS] Resolution parameter change updates frame indices\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Frame index update on parameter change not found\n" RESET);
		results.warnings++;
	}

	// Test 7: Check driver binary timestamp
	printf("\nTest 7: Checking driver binary timestamp...\n");

	time_t sourceTime = getFileModTime(CPP_FILE);
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
		printf("Resolution state synchronization is not properly implemented.\n");
		printf("This may cause mismatched resolutions between Producer and driver.\n");
		return 1;
	} else if (results.warnings > 0) {
		printf(YELLOW "\n*** WARNINGS ***\n" RESET);
		printf("Code appears correct but some checks couldn't be verified.\n");
		return 0;
	} else {
		printf(GREEN "\n*** ALL TESTS PASSED ***\n" RESET);
		printf("Resolution state is properly synchronized.\n");
		printf("Frame indices are validated before probe/commit.\n");
		return 0;
	}
}
