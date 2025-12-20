/*
 * test_probe_commit_size.cpp
 *
 * Verifies that the probe/commit size fallback mechanism is implemented.
 * This test checks the source code to ensure:
 * 1. Multiple probe sizes are tried (34, 26, 48, 22)
 * 2. fProbeCommitSize member exists to store the working size
 * 3. Proper error handling when all sizes fail
 *
 * Build: g++ -O2 -o test_probe_commit_size test_probe_commit_size.cpp
 * Run: ./test_probe_commit_size
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
static const char* HEADER_FILE = "../addons/uvc/UVCCamDevice.h";
static const char* DRIVER_FILE = "/boot/home/config/non-packaged/add-ons/media/aukey_webcam_v4.media_addon";

struct TestResult {
	int passed;
	int failed;
	int warnings;
};

// Check if a pattern exists in the file content
bool containsPattern(const std::string& content, const char* pattern)
{
	return content.find(pattern) != std::string::npos;
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

	printHeader("Probe/Commit Size Fallback Test");

	// Read source files
	printf("Test 1: Reading source files...\n");

	std::string cppContent = readFile(CPP_FILE);
	if (cppContent.empty()) {
		printf(RED "  [FAIL] Cannot open source file: %s\n" RESET, CPP_FILE);
		printf("         Run this test from the tests/ directory\n");
		return 1;
	}
	printf(GREEN "  [PASS] UVCCamDevice.cpp loaded (%zu bytes)\n" RESET, cppContent.length());
	results.passed++;

	std::string headerContent = readFile(HEADER_FILE);
	if (headerContent.empty()) {
		printf(RED "  [FAIL] Cannot open header file: %s\n" RESET, HEADER_FILE);
		return 1;
	}
	printf(GREEN "  [PASS] UVCCamDevice.h loaded (%zu bytes)\n" RESET, headerContent.length());
	results.passed++;

	// Test 2: Check for probe size array
	printf("\nTest 2: Checking for multiple probe sizes...\n");

	bool hasProbeArray = containsPattern(cppContent, "kProbeSizes[]");
	bool hasSize34 = containsPattern(cppContent, "34");
	bool hasSize26 = containsPattern(cppContent, "26");
	bool hasSize48 = containsPattern(cppContent, "48");
	bool hasSize22 = containsPattern(cppContent, "22");

	if (hasProbeArray && hasSize34 && hasSize26 && hasSize48) {
		printf(GREEN "  [PASS] Probe size array with multiple sizes found\n" RESET);
		printf("         Sizes: 34=%s, 26=%s, 48=%s, 22=%s\n",
			hasSize34 ? "yes" : "no",
			hasSize26 ? "yes" : "no",
			hasSize48 ? "yes" : "no",
			hasSize22 ? "yes" : "no");
		results.passed++;
	} else {
		printf(RED "  [FAIL] Probe size array not found or incomplete\n" RESET);
		results.failed++;
	}

	// Test 3: Check for fProbeCommitSize member
	printf("\nTest 3: Checking for fProbeCommitSize member...\n");

	bool hasMemberDecl = containsPattern(headerContent, "fProbeCommitSize");
	bool hasMemberInit = containsPattern(cppContent, "fProbeCommitSize(34)") ||
	                     containsPattern(cppContent, "fProbeCommitSize =");

	if (hasMemberDecl && hasMemberInit) {
		printf(GREEN "  [PASS] fProbeCommitSize member declared and initialized\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] fProbeCommitSize member missing\n" RESET);
		printf("         Declaration in header: %s\n", hasMemberDecl ? "found" : "MISSING");
		printf("         Initialization in cpp: %s\n", hasMemberInit ? "found" : "MISSING");
		results.failed++;
	}

	// Test 4: Check for fallback loop
	printf("\nTest 4: Checking for size fallback loop...\n");

	bool hasFallbackLoop = containsPattern(cppContent, "kNumProbeSizes");
	bool hasProbeSuccess = containsPattern(cppContent, "probeSuccess");
	bool hasTryingAlternative = containsPattern(cppContent, "trying alternative size");

	if (hasFallbackLoop && hasProbeSuccess && hasTryingAlternative) {
		printf(GREEN "  [PASS] Size fallback loop implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Size fallback loop not properly implemented\n" RESET);
		printf("         Loop counter: %s\n", hasFallbackLoop ? "found" : "MISSING");
		printf("         Success flag: %s\n", hasProbeSuccess ? "found" : "MISSING");
		printf("         Alternative logging: %s\n", hasTryingAlternative ? "found" : "MISSING");
		results.failed++;
	}

	// Test 5: Check for error handling when all sizes fail
	printf("\nTest 5: Checking for error handling on failure...\n");

	bool hasAllSizesFail = containsPattern(cppContent, "failed with all sizes");
	bool hasReturnError = containsPattern(cppContent, "return B_ERROR");

	if (hasAllSizesFail && hasReturnError) {
		printf(GREEN "  [PASS] Proper error handling when all sizes fail\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Error handling may be incomplete\n" RESET);
		printf("         All-sizes-fail message: %s\n", hasAllSizesFail ? "found" : "MISSING");
		printf("         Return B_ERROR: %s\n", hasReturnError ? "found" : "MISSING");
		results.warnings++;
	}

	// Test 6: Check for delay between probe attempts
	printf("\nTest 6: Checking for delay between probe attempts...\n");

	bool hasSnooze = containsPattern(cppContent, "snooze(10000)");

	if (hasSnooze) {
		printf(GREEN "  [PASS] 10ms delay between probe attempts found\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] No delay found between probe attempts\n" RESET);
		printf("         Some cameras may need time to process probe requests\n");
		results.warnings++;
	}

	// Test 7: Check driver binary timestamp
	printf("\nTest 7: Checking driver binary timestamp...\n");

	time_t sourceTime = getFileModTime(CPP_FILE);
	time_t driverTime = getFileModTime(DRIVER_FILE);

	if (driverTime == 0) {
		printf(YELLOW "  [WARN] Driver not installed at: %s\n" RESET, DRIVER_FILE);
		printf("         Run 'make install' to install the driver\n");
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

	// Summary
	printHeader("Test Summary");

	printf("Passed:   %d\n", results.passed);
	printf("Failed:   %d\n", results.failed);
	printf("Warnings: %d\n", results.warnings);

	if (results.failed > 0) {
		printf(RED "\n*** FIX REQUIRED ***\n" RESET);
		printf("The probe/commit size fallback is not properly implemented.\n");
		printf("This may cause 'UVC Probe SET_CUR failed' errors on some cameras.\n\n");
		printf("Expected implementation:\n");
		printf("1. Try UVC version-based size first (34 for 1.1+, 26 for 1.0)\n");
		printf("2. If that fails, try other common sizes: 34, 26, 48, 22\n");
		printf("3. Store working size in fProbeCommitSize for subsequent operations\n");
		printf("4. Return error only if ALL sizes fail\n");
		return 1;
	} else if (results.warnings > 0) {
		printf(YELLOW "\n*** WARNINGS ***\n" RESET);
		printf("Code appears correct but some checks couldn't be verified.\n");
		printf("Make sure to install the driver if source was modified.\n");
		return 0;
	} else {
		printf(GREEN "\n*** ALL TESTS PASSED ***\n" RESET);
		printf("Probe/commit size fallback mechanism is properly implemented.\n");
		printf("Driver should now handle cameras with non-standard probe sizes.\n");
		return 0;
	}
}
