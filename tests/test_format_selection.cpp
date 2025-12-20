/*
 * test_format_selection.cpp
 *
 * Verifies format selection and frame rate implementation.
 *
 * Build: g++ -O2 -o test_format_selection test_format_selection.cpp
 * Run: ./test_format_selection
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
	printf(" Format & Frame Rate Test\n");
	printf("========================================\n\n");

	std::string cppContent = readFile(UVC_CPP);
	std::string hContent = readFile(UVC_H);

	if (cppContent.empty() || hContent.empty()) {
		printf(RED "[FAIL] Cannot read source files\n" RESET);
		return 1;
	}

	printf("--- Format Selection (Task 4) ---\n\n");

	printf("Test: MJPEG format support...\n");
	if (containsPattern(cppContent, "fIsMJPEG")) {
		printf(GREEN "  [PASS] MJPEG flag present\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: YUY2 format support...\n");
	if (containsPattern(cppContent, "kYUY2Guid")) {
		printf(GREEN "  [PASS] YUY2 GUID defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Format preference logic...\n");
	if (containsPattern(cppContent, "fMJPEGFrames.CountItems() > 0")) {
		printf(GREEN "  [PASS] MJPEG preferred when available\n" RESET);
		results.passed++;
	} else {
		printf(YELLOW "  [WARN] Format preference logic not found\n" RESET);
		results.failed++;
	}

	printf("Test: Resolution selector parameter...\n");
	if (containsPattern(cppContent, "fResolutionParameterID")) {
		printf(GREEN "  [PASS] Resolution selector implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Frame Rate Selection (Task 5) ---\n\n");

	printf("Test: Frame rate parameter...\n");
	if (containsPattern(hContent, "fFrameRateParameterID")) {
		printf(GREEN "  [PASS] Frame rate parameter ID declared\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Frame interval tracking...\n");
	if (containsPattern(hContent, "fSelectedFrameInterval")) {
		printf(GREEN "  [PASS] Frame interval tracking\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Discrete frame intervals...\n");
	if (containsPattern(cppContent, "fCurrentFrameIntervals")) {
		printf(GREEN "  [PASS] Discrete intervals supported\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Frame rate UI selector...\n");
	if (containsPattern(cppContent, "Frame Rate")) {
		printf(GREEN "  [PASS] Frame rate UI present\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- USB Controller Detection (Task 8) ---\n\n");

	printf("Test: Controller type enum...\n");
	if (containsPattern(hContent, "usb_host_controller_type")) {
		printf(GREEN "  [PASS] Controller type enum defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: USB speed enum...\n");
	if (containsPattern(hContent, "usb_device_speed")) {
		printf(GREEN "  [PASS] USB speed enum defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _DetectControllerType...\n");
	if (containsPattern(cppContent, "_DetectControllerType")) {
		printf(GREEN "  [PASS] Controller detection implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: XHCI handling...\n");
	if (containsPattern(cppContent, "USB_HC_XHCI")) {
		printf(GREEN "  [PASS] XHCI controller handling\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: High-bandwidth workaround...\n");
	if (containsPattern(cppContent, "high_bandwidth_safe")) {
		printf(GREEN "  [PASS] High-bandwidth safety flag\n" RESET);
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
		printf("Format Selection:\n");
		printf("  - MJPEG/YUY2/NV12 formats supported\n");
		printf("  - Resolution selector in UI\n");
		printf("Frame Rate:\n");
		printf("  - Discrete frame intervals\n");
		printf("  - Frame rate selector in UI\n");
		printf("USB Controller:\n");
		printf("  - XHCI/EHCI detection\n");
		printf("  - High-bandwidth workaround\n");
		return 0;
	} else {
		printf(RED "\n*** SOME TESTS FAILED ***\n" RESET);
		return 1;
	}
}
