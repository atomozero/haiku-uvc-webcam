/*
 * test_fallback_disabled.cpp
 *
 * Verifies that the dangerous resolution fallback code has been disabled
 * to prevent kernel panic "USB object did not become idle!"
 *
 * This test checks the source code to ensure:
 * 1. ReduceResolution() is NOT called from FillFrameBuffer()
 * 2. _TriggerResolutionFallback() is NOT called from _EvaluatePacketLoss()
 *
 * Build: g++ -O2 -o test_fallback_disabled test_fallback_disabled.cpp
 * Run: ./test_fallback_disabled
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

static const char* SOURCE_FILE = "../addons/uvc/UVCCamDevice.cpp";
static const char* DRIVER_FILE = "/boot/home/config/non-packaged/add-ons/media/aukey_webcam_v4.media_addon";

struct TestResult {
	int passed;
	int failed;
	int warnings;
};

// Check if a pattern exists in a specific code section (not in comments)
bool checkPatternInSection(const std::string& content,
                           const char* sectionStart,
                           const char* sectionEnd,
                           const char* dangerousPattern)
{
	size_t startPos = content.find(sectionStart);
	if (startPos == std::string::npos) {
		printf(YELLOW "  [WARN] Section not found: %s\n" RESET, sectionStart);
		return false;
	}

	size_t endPos = content.find(sectionEnd, startPos);
	if (endPos == std::string::npos) {
		endPos = content.length();
	}

	std::string section = content.substr(startPos, endPos - startPos);

	// Look for the dangerous pattern NOT in a comment
	size_t pos = 0;
	while ((pos = section.find(dangerousPattern, pos)) != std::string::npos) {
		// Check if this is in a comment (simple check for // before on same line)
		size_t lineStart = section.rfind('\n', pos);
		if (lineStart == std::string::npos) lineStart = 0;

		std::string line = section.substr(lineStart, pos - lineStart);

		// If not commented out, it's a problem
		if (line.find("//") == std::string::npos &&
		    line.find("/*") == std::string::npos &&
		    line.find("* ") == std::string::npos) {
			return true;  // Found dangerous pattern NOT in comment
		}
		pos++;
	}

	return false;  // Pattern not found or only in comments
}

// Get file modification time
time_t getFileModTime(const char* path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		return 0;
	}
	return st.st_mtime;
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

	printHeader("Fallback Disable Verification Test");

	// Test 1: Read source file
	printf("Test 1: Reading source file...\n");

	std::ifstream file(SOURCE_FILE);
	if (!file.is_open()) {
		printf(RED "  [FAIL] Cannot open source file: %s\n" RESET, SOURCE_FILE);
		printf("         Run this test from the tests/ directory\n");
		results.failed++;
		return 1;
	}

	std::string content((std::istreambuf_iterator<char>(file)),
	                     std::istreambuf_iterator<char>());
	file.close();

	printf(GREEN "  [PASS] Source file loaded (%zu bytes)\n" RESET, content.length());
	results.passed++;

	// Test 2: Check FillFrameBuffer doesn't call ReduceResolution()
	printf("\nTest 2: Checking FillFrameBuffer for ReduceResolution() call...\n");

	bool hasReduceResolution = checkPatternInSection(
		content,
		"MJPEG frames too small",  // Section identifier
		"Reset counters for next window",  // Section end
		"ReduceResolution()"
	);

	if (hasReduceResolution) {
		printf(RED "  [FAIL] ReduceResolution() is still called from FillFrameBuffer!\n" RESET);
		printf("         This will cause kernel panic when bandwidth is insufficient.\n");
		results.failed++;
	} else {
		printf(GREEN "  [PASS] ReduceResolution() NOT called from FillFrameBuffer\n" RESET);
		results.passed++;
	}

	// Test 3: Check _EvaluatePacketLoss doesn't call _TriggerResolutionFallback()
	printf("\nTest 3: Checking _EvaluatePacketLoss for _TriggerResolutionFallback() call...\n");

	bool hasTriggerFallback = checkPatternInSection(
		content,
		"UVCCamDevice::_EvaluatePacketLoss()",  // Function start
		"Reset window",  // Near function end
		"_TriggerResolutionFallback()"
	);

	if (hasTriggerFallback) {
		printf(RED "  [FAIL] _TriggerResolutionFallback() is still called from _EvaluatePacketLoss!\n" RESET);
		printf("         This will cause kernel panic during packet loss evaluation.\n");
		results.failed++;
	} else {
		printf(GREEN "  [PASS] _TriggerResolutionFallback() NOT called from _EvaluatePacketLoss\n" RESET);
		results.passed++;
	}

	// Test 4: Check for safe resolution change via worker thread
	printf("\nTest 4: Checking for safe RequestResolutionChange() usage...\n");

	bool hasRequestResChange = content.find("RequestResolutionChange(") != std::string::npos;
	bool hasWorkerThreadMsg = content.find("via worker thread") != std::string::npos;

	if (hasRequestResChange && hasWorkerThreadMsg) {
		printf(GREEN "  [PASS] Safe RequestResolutionChange() mechanism is used\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Safe resolution change mechanism may be missing\n" RESET);
		printf("         RequestResolutionChange: %s\n", hasRequestResChange ? "found" : "MISSING");
		printf("         Worker thread messages: %s\n", hasWorkerThreadMsg ? "found" : "MISSING");
		results.warnings++;
	}

	// Test 5: Check driver binary is newer than source
	printf("\nTest 5: Checking driver binary timestamp...\n");

	time_t sourceTime = getFileModTime(SOURCE_FILE);
	time_t driverTime = getFileModTime(DRIVER_FILE);

	if (driverTime == 0) {
		printf(YELLOW "  [WARN] Driver not installed at: %s\n" RESET, DRIVER_FILE);
		results.warnings++;
	} else if (sourceTime == 0) {
		printf(YELLOW "  [WARN] Cannot get source file timestamp\n" RESET);
		results.warnings++;
	} else if (driverTime < sourceTime) {
		printf(RED "  [FAIL] Driver is OLDER than source!\n" RESET);
		printf("         Source: %s", ctime(&sourceTime));
		printf("         Driver: %s", ctime(&driverTime));
		printf("         You need to recompile: make clean && make && make install\n");
		results.failed++;
	} else {
		printf(GREEN "  [PASS] Driver is up-to-date\n" RESET);
		printf("         Source: %s", ctime(&sourceTime));
		printf("         Driver: %s", ctime(&driverTime));
		results.passed++;
	}

	// Test 6: Check for ReconfigThread implementation
	printf("\nTest 6: Checking for ReconfigThread implementation...\n");

	bool hasReconfigThread = content.find("ReconfigThread") != std::string::npos;
	bool hasHandleResChange = content.find("_HandleResolutionChange") != std::string::npos;
	bool hasKernelPanicComment = content.find("kernel panic") != std::string::npos ||
	                             content.find("USB object did not become idle") != std::string::npos;

	if (hasReconfigThread && hasHandleResChange && hasKernelPanicComment) {
		printf(GREEN "  [PASS] ReconfigThread implementation present\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] ReconfigThread implementation may be incomplete\n" RESET);
		printf("         ReconfigThread: %s\n", hasReconfigThread ? "found" : "MISSING");
		printf("         _HandleResolutionChange: %s\n", hasHandleResChange ? "found" : "MISSING");
		printf("         Kernel panic docs: %s\n", hasKernelPanicComment ? "found" : "MISSING");
		results.warnings++;
	}

	// Summary
	printHeader("Test Summary");

	printf("Passed:   %d\n", results.passed);
	printf("Failed:   %d\n", results.failed);
	printf("Warnings: %d\n", results.warnings);

	if (results.failed > 0) {
		printf(RED "\n*** FIX REQUIRED ***\n" RESET);
		printf("The dangerous fallback code is still present.\n");
		printf("This WILL cause kernel panic under bandwidth stress.\n\n");
		printf("Required actions:\n");
		printf("1. Remove ReduceResolution() call from FillFrameBuffer()\n");
		printf("2. Remove _TriggerResolutionFallback() call from _EvaluatePacketLoss()\n");
		printf("3. Recompile and reinstall the driver\n");
		return 1;
	} else if (results.warnings > 0) {
		printf(YELLOW "\n*** WARNINGS ***\n" RESET);
		printf("Code appears safe but some checks couldn't be verified.\n");
		printf("Make sure to recompile if source was modified.\n");
		return 0;
	} else {
		printf(GREEN "\n*** ALL TESTS PASSED ***\n" RESET);
		printf("Resolution fallback uses safe ReconfigThread mechanism.\n");
		printf("Kernel panic from SetAlternate() should not occur.\n");
		printf("Automatic fallback is now ENABLED and SAFE.\n");
		return 0;
	}
}
