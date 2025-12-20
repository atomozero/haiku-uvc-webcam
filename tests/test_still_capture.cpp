/*
 * test_still_capture.cpp
 *
 * Verifies Still Image Capture detection and data structures.
 *
 * Build: g++ -O2 -o test_still_capture test_still_capture.cpp
 * Run: ./test_still_capture
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

static const char* UVC_CPP = "../addons/uvc/UVCCamDevice.cpp";
static const char* UVC_H = "../addons/uvc/UVCCamDevice.h";

struct TestResult {
	int passed;
	int failed;
};

bool containsPattern(const std::string& content, const char* pattern)
{
	return content.find(pattern) != std::string::npos;
}

std::string readFile(const char* path)
{
	std::ifstream file(path);
	if (!file.is_open())
		return "";
	return std::string((std::istreambuf_iterator<char>(file)),
	                    std::istreambuf_iterator<char>());
}

int main()
{
	TestResult results = {0, 0};

	printf("\n========================================\n");
	printf(" Still Image Capture Test\n");
	printf("========================================\n\n");

	std::string cppContent = readFile(UVC_CPP);
	std::string hContent = readFile(UVC_H);

	if (cppContent.empty() || hContent.empty()) {
		printf(RED "[FAIL] Cannot read source files\n" RESET);
		return 1;
	}

	printf("--- Still Image Data Structures ---\n\n");

	printf("Test: still_capture_method enum...\n");
	if (containsPattern(hContent, "enum still_capture_method")) {
		printf(GREEN "  [PASS] still_capture_method defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: still_image_size struct...\n");
	if (containsPattern(hContent, "struct still_image_size")) {
		printf(GREEN "  [PASS] still_image_size defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: still_image_info struct...\n");
	if (containsPattern(hContent, "struct still_image_info")) {
		printf(GREEN "  [PASS] still_image_info defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Still Capture Method Constants ---\n\n");

	printf("Test: STILL_CAPTURE_NONE...\n");
	if (containsPattern(hContent, "STILL_CAPTURE_NONE")) {
		printf(GREEN "  [PASS] STILL_CAPTURE_NONE defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: STILL_CAPTURE_METHOD_1...\n");
	if (containsPattern(hContent, "STILL_CAPTURE_METHOD_1")) {
		printf(GREEN "  [PASS] Method 1 (button) defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: STILL_CAPTURE_METHOD_2...\n");
	if (containsPattern(hContent, "STILL_CAPTURE_METHOD_2")) {
		printf(GREEN "  [PASS] Method 2 (software) defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: STILL_CAPTURE_METHOD_3...\n");
	if (containsPattern(hContent, "STILL_CAPTURE_METHOD_3")) {
		printf(GREEN "  [PASS] Method 3 (pipe+button) defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Member Variables ---\n\n");

	printf("Test: fStillCaptureMethod...\n");
	if (containsPattern(hContent, "fStillCaptureMethod")) {
		printf(GREEN "  [PASS] fStillCaptureMethod member\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: fStillImageInfo...\n");
	if (containsPattern(hContent, "fStillImageInfo")) {
		printf(GREEN "  [PASS] fStillImageInfo member\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: fHasStillCapture...\n");
	if (containsPattern(hContent, "fHasStillCapture")) {
		printf(GREEN "  [PASS] fHasStillCapture flag\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: fTriggerSupport...\n");
	if (containsPattern(hContent, "fTriggerSupport")) {
		printf(GREEN "  [PASS] fTriggerSupport flag\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Still Image Methods ---\n\n");

	printf("Test: _ParseStillImageFrame...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_ParseStillImageFrame")) {
		printf(GREEN "  [PASS] _ParseStillImageFrame implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _LogStillImageCapabilities...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_LogStillImageCapabilities")) {
		printf(GREEN "  [PASS] _LogStillImageCapabilities implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _GetStillCaptureMethodName...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_GetStillCaptureMethodName")) {
		printf(GREEN "  [PASS] _GetStillCaptureMethodName implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Initialization ---\n\n");

	printf("Test: Constructor init...\n");
	if (containsPattern(cppContent, "fStillCaptureMethod(STILL_CAPTURE_NONE)")) {
		printf(GREEN "  [PASS] Constructor initializes still capture method\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: memset fStillImageInfo...\n");
	if (containsPattern(cppContent, "memset(&fStillImageInfo")) {
		printf(GREEN "  [PASS] fStillImageInfo initialized\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	// Summary
	printf("\n========================================\n");
	printf(" Test Summary\n");
	printf("========================================\n\n");
	printf("Passed: %d\n", results.passed);
	printf("Failed: %d\n", results.failed);

	if (results.failed == 0) {
		printf(GREEN "\n*** ALL TESTS PASSED ***\n" RESET);
		printf("Still Image Capture Detection:\n");
		printf("  - Data structures defined\n");
		printf("  - Capture method enum (None, Method 1-3)\n");
		printf("  - Still image resolution tracking\n");
		printf("  - Hardware trigger support detection\n");
		printf("  - Logging and diagnostics\n");
		return 0;
	} else {
		printf(RED "\n*** SOME TESTS FAILED ***\n" RESET);
		return 1;
	}
}
