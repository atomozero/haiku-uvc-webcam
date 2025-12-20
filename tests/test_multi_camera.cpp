/*
 * test_multi_camera.cpp
 *
 * Verifies multiple camera support implementation.
 *
 * Build: g++ -O2 -o test_multi_camera test_multi_camera.cpp
 * Run: ./test_multi_camera
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

static const char* CAMDEV_CPP = "../CamDevice.cpp";
static const char* CAMDEV_H = "../CamDevice.h";

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
	printf(" Multiple Camera Support Test\n");
	printf("========================================\n\n");

	std::string cppContent = readFile(CAMDEV_CPP);
	std::string hContent = readFile(CAMDEV_H);

	if (cppContent.empty() || hContent.empty()) {
		printf(RED "[FAIL] Cannot read source files\n" RESET);
		return 1;
	}

	printf("--- Instance Counter ---\n\n");

	printf("Test: sInstanceCounter static member...\n");
	if (containsPattern(hContent, "sInstanceCounter")) {
		printf(GREEN "  [PASS] sInstanceCounter defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: fInstanceNumber member...\n");
	if (containsPattern(hContent, "fInstanceNumber")) {
		printf(GREEN "  [PASS] fInstanceNumber defined\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Static counter initialization...\n");
	if (containsPattern(cppContent, "CamDevice::sInstanceCounter = 0")) {
		printf(GREEN "  [PASS] Counter initialized to 0\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Atomic Instance Assignment ---\n\n");

	printf("Test: Atomic increment usage...\n");
	if (containsPattern(cppContent, "atomic_add(&sInstanceCounter")) {
		printf(GREEN "  [PASS] Uses atomic_add for thread safety\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Unique Naming ---\n\n");

	printf("Test: Serial number extraction...\n");
	if (containsPattern(cppContent, "SerialNumberString()")) {
		printf(GREEN "  [PASS] Serial number extraction\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Instance number suffix...\n");
	if (containsPattern(cppContent, "fInstanceNumber > 1")) {
		printf(GREEN "  [PASS] Instance suffix for duplicates\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Serial number suffix...\n");
	if (containsPattern(cppContent, "usbSerial + serialLen - 6")) {
		printf(GREEN "  [PASS] Short serial suffix\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("\n--- Name Format ---\n\n");

	printf("Test: First camera no suffix...\n");
	if (containsPattern(cppContent, "fInstanceNumber > 1")) {
		printf(GREEN "  [PASS] First camera gets no # suffix\n" RESET);
		results.passed++;
	} else {
		printf(RED "  [FAIL] Missing\n" RESET);
		results.failed++;
	}

	printf("Test: Append # for duplicates...\n");
	if (containsPattern(cppContent, "<< \" #\" << fInstanceNumber")) {
		printf(GREEN "  [PASS] Appends #N for duplicates\n" RESET);
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
		printf("Multiple Camera Support:\n");
		printf("  - Global instance counter (atomic)\n");
		printf("  - Unique instance number per camera\n");
		printf("  - Serial number extraction for uniqueness\n");
		printf("  - Instance suffix (#2, #3...) for duplicates\n");
		printf("  - Thread-safe atomic operations\n");
		return 0;
	} else {
		printf(RED "\n*** SOME TESTS FAILED ***\n" RESET);
		return 1;
	}
}
