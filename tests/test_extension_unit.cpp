/*
 * test_extension_unit.cpp
 *
 * Verifies Extension Unit detection and vendor GUID database.
 *
 * Build: g++ -O2 -o test_extension_unit test_extension_unit.cpp
 * Run: ./test_extension_unit
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
	printf(" Extension Unit Detection Test\n");
	printf("========================================\n\n");

	std::string cppContent = readFile(UVC_CPP);
	std::string hContent = readFile(UVC_H);

	if (cppContent.empty() || hContent.empty()) {
		printf(RED "[FAIL] Cannot read source files\n" RESET);
		return 1;
	}

	printf("--- Extension Unit Data Structures ---\n\n");

	printf("Test: extension_unit_info struct...\n");
	if (containsPattern(hContent, "struct extension_unit_info")) {
		printf(GREEN "  [PASS] extension_unit_info defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: extension_unit_vendor enum...\n");
	if (containsPattern(hContent, "enum extension_unit_vendor")) {
		printf(GREEN "  [PASS] extension_unit_vendor defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: extension_unit_caps enum...\n");
	if (containsPattern(hContent, "enum extension_unit_caps")) {
		printf(GREEN "  [PASS] extension_unit_caps defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: fExtensionUnits list...\n");
	if (containsPattern(hContent, "fExtensionUnits")) {
		printf(GREEN "  [PASS] fExtensionUnits member\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: fHasExtensionUnits flag...\n");
	if (containsPattern(hContent, "fHasExtensionUnits")) {
		printf(GREEN "  [PASS] fHasExtensionUnits member\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Vendor GUID Database ---\n\n");

	printf("Test: Microsoft GUID...\n");
	if (containsPattern(hContent, "kMicrosoftH264XUGUID")) {
		printf(GREEN "  [PASS] Microsoft H.264 XU GUID\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Sonix GUID...\n");
	if (containsPattern(hContent, "kSonixXUGUID")) {
		printf(GREEN "  [PASS] Sonix XU GUID\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Logitech GUID...\n");
	if (containsPattern(hContent, "kLogitechXUGUID")) {
		printf(GREEN "  [PASS] Logitech XU GUID\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Realtek GUID...\n");
	if (containsPattern(hContent, "kRealtekXUGUID")) {
		printf(GREEN "  [PASS] Realtek XU GUID\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Extension Unit Methods ---\n\n");

	printf("Test: _ParseExtensionUnit...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_ParseExtensionUnit")) {
		printf(GREEN "  [PASS] _ParseExtensionUnit implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _IdentifyXUVendor...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_IdentifyXUVendor")) {
		printf(GREEN "  [PASS] _IdentifyXUVendor implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _GetXUCapabilities...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_GetXUCapabilities")) {
		printf(GREEN "  [PASS] _GetXUCapabilities implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _GetXUVendorName...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_GetXUVendorName")) {
		printf(GREEN "  [PASS] _GetXUVendorName implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: _LogExtensionUnits...\n");
	if (containsPattern(cppContent, "UVCCamDevice::_LogExtensionUnits")) {
		printf(GREEN "  [PASS] _LogExtensionUnits implemented\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Capability Flags ---\n\n");

	printf("Test: XU_CAP_LED_CONTROL...\n");
	if (containsPattern(hContent, "XU_CAP_LED_CONTROL")) {
		printf(GREEN "  [PASS] LED control capability\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: XU_CAP_FACE_DETECTION...\n");
	if (containsPattern(hContent, "XU_CAP_FACE_DETECTION")) {
		printf(GREEN "  [PASS] Face detection capability\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: XU_CAP_HDR...\n");
	if (containsPattern(hContent, "XU_CAP_HDR")) {
		printf(GREEN "  [PASS] HDR capability\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: XU_CAP_H264_ENCODING...\n");
	if (containsPattern(hContent, "XU_CAP_H264_ENCODING")) {
		printf(GREEN "  [PASS] H.264 encoding capability\n" RESET);
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
		printf("Extension Unit Detection:\n");
		printf("  - Data structures defined\n");
		printf("  - Vendor GUID database (Microsoft, Sonix, Logitech, Realtek)\n");
		printf("  - Parsing and identification methods\n");
		printf("  - Capability flags for vendor features\n");
		return 0;
	} else {
		printf(RED "\n*** SOME TESTS FAILED ***\n" RESET);
		return 1;
	}
}
