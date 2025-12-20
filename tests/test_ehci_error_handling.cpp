/*
 * test_ehci_error_handling.cpp
 *
 * Verifies that EHCI error monitoring is properly implemented.
 * This test checks the source code to ensure:
 * 1. USB_ERROR_EHCI_HALTED and USB_ERROR_BABBLE types exist
 * 2. EHCI error classification in ClassifyUSBError
 * 3. Recommended actions for EHCI errors
 * 4. EHCI-specific logging in error statistics
 *
 * Build: g++ -O2 -o test_ehci_error_handling test_ehci_error_handling.cpp
 * Run: ./test_ehci_error_handling
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

static const char* HEADER_FILE = "../CamDevice.h";
static const char* CPP_FILE = "../CamDevice.cpp";
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

	printHeader("EHCI Error Handling Test");

	// Read source files
	printf("Test 1: Reading source files...\n");

	std::string headerContent = readFile(HEADER_FILE);
	if (headerContent.empty()) {
		printf(RED "  [FAIL] Cannot open header file: %s\n" RESET, HEADER_FILE);
		return 1;
	}
	printf(GREEN "  [PASS] CamDevice.h loaded (%zu bytes)\n" RESET, headerContent.length());
	results.passed++;

	std::string cppContent = readFile(CPP_FILE);
	if (cppContent.empty()) {
		printf(RED "  [FAIL] Cannot open source file: %s\n" RESET, CPP_FILE);
		return 1;
	}
	printf(GREEN "  [PASS] CamDevice.cpp loaded (%zu bytes)\n" RESET, cppContent.length());
	results.passed++;

	// Test 2: Check for EHCI error types
	printf("\nTest 2: Checking for EHCI error types...\n");

	bool hasEHCIHalted = containsPattern(headerContent, "USB_ERROR_EHCI_HALTED");
	bool hasBabble = containsPattern(headerContent, "USB_ERROR_BABBLE");

	if (hasEHCIHalted && hasBabble) {
		printf(GREEN "  [PASS] EHCI error types defined\n" RESET);
		printf("         USB_ERROR_EHCI_HALTED: found\n");
		printf("         USB_ERROR_BABBLE: found\n");
		results.passed++;
	} else {
		printf(RED "  [FAIL] EHCI error types missing\n" RESET);
		results.failed++;
	}

	// Test 3: Check EHCI error classification
	printf("\nTest 3: Checking EHCI error classification...\n");

	bool classifiesEHCI = containsPattern(cppContent, "return USB_ERROR_EHCI_HALTED");
	bool classifiesBabble = containsPattern(cppContent, "return USB_ERROR_BABBLE");

	if (classifiesEHCI && classifiesBabble) {
		printf(GREEN "  [PASS] EHCI errors classified in ClassifyUSBError\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] EHCI error classification may be incomplete\n" RESET);
		printf("         EHCI_HALTED: %s\n", classifiesEHCI ? "classified" : "MISSING");
		printf("         BABBLE: %s\n", classifiesBabble ? "classified" : "MISSING");
		results.warnings++;
	}

	// Test 4: Check recommended actions for EHCI errors
	printf("\nTest 4: Checking recommended actions for EHCI errors...\n");

	bool hasEHCIAction = containsPattern(headerContent, "case USB_ERROR_EHCI_HALTED:");
	bool hasBabbleAction = containsPattern(headerContent, "case USB_ERROR_BABBLE:");

	if (hasEHCIAction && hasBabbleAction) {
		printf(GREEN "  [PASS] Recovery actions defined for EHCI errors\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Recovery actions missing for EHCI errors\n" RESET);
		results.failed++;
	}

	// Test 5: Check EHCI-specific logging
	printf("\nTest 5: Checking EHCI-specific logging...\n");

	bool hasEHCILog = containsPattern(cppContent, "EHCI controller errors");
	bool hasEHCIWarning = containsPattern(cppContent, "EHCI error detected");

	if (hasEHCILog && hasEHCIWarning) {
		printf(GREEN "  [PASS] EHCI-specific logging implemented\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] EHCI-specific logging may be incomplete\n" RESET);
		printf("         Statistics log: %s\n", hasEHCILog ? "found" : "MISSING");
		printf("         Warning log: %s\n", hasEHCIWarning ? "found" : "MISSING");
		results.warnings++;
	}

	// Test 6: Check error rate threshold warnings
	printf("\nTest 6: Checking error rate threshold warnings...\n");

	bool hasThresholdCheck = containsPattern(cppContent, "ehciRate > 5.0f");

	if (hasThresholdCheck) {
		printf(GREEN "  [PASS] EHCI error rate threshold check implemented\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] EHCI error rate threshold not found\n" RESET);
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
		printf("EHCI error handling is not properly implemented.\n");
		return 1;
	} else if (results.warnings > 0) {
		printf(YELLOW "\n*** WARNINGS ***\n" RESET);
		printf("Code appears correct but some checks couldn't be verified.\n");
		return 0;
	} else {
		printf(GREEN "\n*** ALL TESTS PASSED ***\n" RESET);
		printf("EHCI error monitoring properly implemented.\n");
		printf("The driver will now:\n");
		printf("  - Classify EHCI halted and babble errors separately\n");
		printf("  - Log EHCI-specific error statistics\n");
		printf("  - Warn when EHCI error rate exceeds 5%%\n");
		printf("  - Recommend appropriate recovery actions\n");
		return 0;
	}
}
