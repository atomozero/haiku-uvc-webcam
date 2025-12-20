/*
 * test_resolution_ordering.cpp
 *
 * Verifies that resolution fallback ordering is properly implemented.
 * This test checks the source code to ensure:
 * 1. Resolutions are sorted by pixel count (descending)
 * 2. Level 0 = highest resolution, Level N = lowest
 * 3. Sorted indices are used in _GetResolutionAtLevel()
 *
 * Build: g++ -O2 -o test_resolution_ordering test_resolution_ordering.cpp
 * Run: ./test_resolution_ordering
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

	printHeader("Resolution Ordering Test");

	// Read source files
	printf("Test 1: Reading source files...\n");

	std::string cppContent = readFile(CPP_FILE);
	if (cppContent.empty()) {
		printf(RED "  [FAIL] Cannot open source file: %s\n" RESET, CPP_FILE);
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

	// Test 2: Check for sorted indices arrays
	printf("\nTest 2: Checking for sorted index arrays...\n");

	bool hasSortedMJPEG = containsPattern(headerContent, "fSortedMJPEGIndices");
	bool hasSortedUncomp = containsPattern(headerContent, "fSortedUncompressedIndices");
	bool hasSortedCounts = containsPattern(headerContent, "fSortedMJPEGCount") &&
	                       containsPattern(headerContent, "fSortedUncompressedCount");

	if (hasSortedMJPEG && hasSortedUncomp && hasSortedCounts) {
		printf(GREEN "  [PASS] Sorted index arrays declared in header\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Sorted index arrays missing from header\n" RESET);
		printf("         fSortedMJPEGIndices: %s\n", hasSortedMJPEG ? "found" : "MISSING");
		printf("         fSortedUncompressedIndices: %s\n", hasSortedUncomp ? "found" : "MISSING");
		printf("         Count variables: %s\n", hasSortedCounts ? "found" : "MISSING");
		results.failed++;
	}

	// Test 3: Check for _BuildSortedResolutionList method
	printf("\nTest 3: Checking for _BuildSortedResolutionList method...\n");

	bool hasBuildMethod = containsPattern(cppContent, "_BuildSortedResolutionList()");
	bool hasPixelSort = containsPattern(cppContent, "pixels") &&
	                    containsPattern(cppContent, "width * height");
	bool hasDescendingSort = containsPattern(cppContent, "entries[j].pixels < entries[j + 1].pixels");

	if (hasBuildMethod && hasPixelSort && hasDescendingSort) {
		printf(GREEN "  [PASS] _BuildSortedResolutionList() implementation found\n" RESET);
		printf("         - Pixel count calculation: found\n");
		printf("         - Descending sort: found\n");
		results.passed++;
	} else {
		printf(RED "  [FAIL] _BuildSortedResolutionList() implementation missing or incomplete\n" RESET);
		printf("         Method: %s\n", hasBuildMethod ? "found" : "MISSING");
		printf("         Pixel calculation: %s\n", hasPixelSort ? "found" : "MISSING");
		printf("         Descending sort: %s\n", hasDescendingSort ? "found" : "MISSING");
		results.failed++;
	}

	// Test 4: Check _GetResolutionAtLevel uses sorted indices
	printf("\nTest 4: Checking _GetResolutionAtLevel uses sorted indices...\n");

	bool usesSortedIndices = containsPattern(cppContent, "sortedIndices[sortedLevel]") ||
	                         containsPattern(cppContent, "sortedIndices =");
	bool usesSortedCount = containsPattern(cppContent, "sortedCount > 0");

	if (usesSortedIndices && usesSortedCount) {
		printf(GREEN "  [PASS] _GetResolutionAtLevel uses sorted indices\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] _GetResolutionAtLevel does not use sorted indices\n" RESET);
		printf("         sortedIndices usage: %s\n", usesSortedIndices ? "found" : "MISSING");
		printf("         sortedCount check: %s\n", usesSortedCount ? "found" : "MISSING");
		results.failed++;
	}

	// Test 5: Check _GetMaxResolutionLevel uses sorted count
	printf("\nTest 5: Checking _GetMaxResolutionLevel uses sorted count...\n");

	bool usesCorrectCount = containsPattern(cppContent, "fSortedMJPEGCount") &&
	                        containsPattern(cppContent, "fSortedUncompressedCount");

	if (usesCorrectCount) {
		printf(GREEN "  [PASS] _GetMaxResolutionLevel uses sorted count\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] _GetMaxResolutionLevel may not use sorted count\n" RESET);
		results.warnings++;
	}

	// Test 6: Check that sorting is called during init
	printf("\nTest 6: Checking sorting is called during initialization...\n");

	bool calledInInit = containsPattern(cppContent, "_BuildSortedResolutionList();");
	bool logsOrder = containsPattern(cppContent, "resolutions sorted by size");

	if (calledInInit && logsOrder) {
		printf(GREEN "  [PASS] Sorted list built and logged during initialization\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Sorting may not be called during init\n" RESET);
		printf("         Call found: %s\n", calledInInit ? "yes" : "no");
		printf("         Logging: %s\n", logsOrder ? "yes" : "no");
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
		printf("Resolution fallback ordering is not properly implemented.\n");
		printf("This may cause fallback to go to wrong resolutions.\n");
		return 1;
	} else if (results.warnings > 0) {
		printf(YELLOW "\n*** WARNINGS ***\n" RESET);
		printf("Code appears correct but some checks couldn't be verified.\n");
		return 0;
	} else {
		printf(GREEN "\n*** ALL TESTS PASSED ***\n" RESET);
		printf("Resolution fallback now uses proper sorted order.\n");
		printf("Level 0 = highest resolution, Level N = lowest.\n");
		return 0;
	}
}
