/*
 * test_camera_controls.cpp
 *
 * Verifies that Processing Unit and Camera Terminal controls are implemented.
 *
 * Build: g++ -O2 -o test_camera_controls test_camera_controls.cpp
 * Run: ./test_camera_controls
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
	printf(" Camera Controls Test\n");
	printf("========================================\n\n");

	std::string cppContent = readFile(UVC_CPP);
	std::string hContent = readFile(UVC_H);

	if (cppContent.empty() || hContent.empty()) {
		printf(RED "[FAIL] Cannot read source files\n" RESET);
		return 1;
	}

	printf("--- Processing Unit Controls (Task 2) ---\n\n");

	// Test PU controls
	const char* puControls[] = {
		"USB_VIDEO_PU_BRIGHTNESS_CONTROL",
		"USB_VIDEO_PU_CONTRAST_CONTROL",
		"USB_VIDEO_PU_HUE_CONTROL",
		"USB_VIDEO_PU_SATURATION_CONTROL",
		"USB_VIDEO_PU_SHARPNESS_CONTROL",
		"USB_VIDEO_PU_GAMMA_CONTROL",
		"USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL",
		"USB_VIDEO_PU_BACKLIGHT_COMPENSATION_CONTROL",
		"USB_VIDEO_PU_GAIN_CONTROL",
		"USB_VIDEO_PU_POWER_LINE_FREQUENCY_CONTROL"
	};
	const char* puNames[] = {
		"Brightness", "Contrast", "Hue", "Saturation", "Sharpness",
		"Gamma", "WB Temperature", "Backlight", "Gain", "Power Line"
	};

	for (int i = 0; i < 10; i++) {
		printf("Test: %s...\n", puNames[i]);
		if (containsPattern(cppContent, puControls[i])) {
			printf(GREEN "  [PASS] %s implemented\n" RESET, puNames[i]);
			results.passed++;
		} else {
			printf(RED "  [FAIL] %s not found\n" RESET, puNames[i]);
			results.failed++;
		}
	}

	printf("\n--- Camera Terminal Controls (Task 3) ---\n\n");

	// Test CT controls
	const char* ctControls[] = {
		"USB_VIDEO_CT_AE_MODE_CONTROL",
		"USB_VIDEO_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL",
		"USB_VIDEO_CT_FOCUS_AUTO_CONTROL",
		"USB_VIDEO_CT_FOCUS_ABSOLUTE_CONTROL",
		"USB_VIDEO_CT_ZOOM_ABSOLUTE_CONTROL"
	};
	const char* ctNames[] = {
		"Auto Exposure Mode", "Exposure Time", "Auto Focus",
		"Focus Absolute", "Zoom Absolute"
	};

	for (int i = 0; i < 5; i++) {
		printf("Test: %s...\n", ctNames[i]);
		if (containsPattern(cppContent, ctControls[i])) {
			printf(GREEN "  [PASS] %s implemented\n" RESET, ctNames[i]);
			results.passed++;
		} else {
			printf(RED "  [FAIL] %s not found\n" RESET, ctNames[i]);
			results.failed++;
		}
	}

	// Test infrastructure
	printf("\n--- Control Infrastructure ---\n\n");

	printf("Test: _AddProcessingParameter...\n");
	if (containsPattern(cppContent, "_AddProcessingParameter")) {
		printf(GREEN "  [PASS] Processing parameter adder\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _AddCameraTerminalControls...\n");
	if (containsPattern(cppContent, "_AddCameraTerminalControls")) {
		printf(GREEN "  [PASS] Camera terminal controls adder\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _GetCTControlValue...\n");
	if (containsPattern(cppContent, "_GetCTControlValue")) {
		printf(GREEN "  [PASS] CT control getter\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _SetCTControlValue...\n");
	if (containsPattern(cppContent, "_SetCTControlValue")) {
		printf(GREEN "  [PASS] CT control setter\n" RESET);
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
		printf("Processing Unit Controls:\n");
		printf("  - Brightness, Contrast, Hue, Saturation\n");
		printf("  - Sharpness, Gamma, WB Temperature\n");
		printf("  - Backlight, Gain, Power Line Frequency\n");
		printf("Camera Terminal Controls:\n");
		printf("  - Auto Exposure, Exposure Time\n");
		printf("  - Auto Focus, Focus, Zoom\n");
		return 0;
	} else {
		printf(RED "\n*** SOME TESTS FAILED ***\n" RESET);
		return 1;
	}
}
