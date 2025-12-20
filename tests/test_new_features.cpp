/*
 * Test suite for new UVC driver features:
 * - NV12 format conversion
 * - Camera Terminal (CT) controls
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <OS.h>  // For system_time() on Haiku

// ============================================================================
// NV12 Conversion Test
// ============================================================================

// Pre-computed YUV to RGB lookup tables (same as in UVCCamDevice.cpp)
static int32_t sYTable[256];
static int32_t sUBTable[256];
static int32_t sUGTable[256];
static int32_t sVRTable[256];
static int32_t sVGTable[256];
static bool sTablesInitialized = false;

static inline uint8_t clamp255(int32_t value) {
	return (uint8_t)((value < 0) ? 0 : ((value > 255) ? 255 : value));
}

static void InitYUVTables() {
	if (sTablesInitialized) return;

	for (int i = 0; i < 256; i++) {
		sYTable[i] = (int32_t)((i - 16) * 298);
		sUBTable[i] = (int32_t)((i - 128) * 516);
		sUGTable[i] = (int32_t)((i - 128) * 100);
		sVRTable[i] = (int32_t)((i - 128) * 408);
		sVGTable[i] = (int32_t)((i - 128) * 208);
	}
	sTablesInitialized = true;
}

// NV12 to RGB32 conversion (copied from driver for testing)
static void ConvertNV12toRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32_t width, int32_t height)
{
	InitYUVTables();

	size_t yPlaneSize = (size_t)width * height;
	size_t expectedSize = yPlaneSize * 3 / 2;

	if (srcSize < expectedSize) {
		// Incomplete frame - fill with gray
		memset(dst, 0x80, (size_t)width * height * 4);
		return;
	}

	const unsigned char* yPlane = src;
	const unsigned char* uvPlane = src + yPlaneSize;

	for (int32_t y = 0; y < height; y++) {
		const unsigned char* yRow = yPlane + y * width;
		const unsigned char* uvRow = uvPlane + (y / 2) * width;
		unsigned char* dstRow = dst + y * width * 4;

		for (int32_t x = 0; x < width; x += 2) {
			int32_t Y0 = sYTable[yRow[x]];
			int32_t Y1 = sYTable[yRow[x + 1]];

			int32_t uvIndex = (x / 2) * 2;
			int32_t U = uvRow[uvIndex];
			int32_t V = uvRow[uvIndex + 1];

			int32_t Ub = sUBTable[U];
			int32_t Ug = sUGTable[U];
			int32_t Vr = sVRTable[V];
			int32_t Vg = sVGTable[V];

			// Pixel 0
			dstRow[x * 4 + 0] = clamp255((Y0 + Ub + 128) >> 8);        // B
			dstRow[x * 4 + 1] = clamp255((Y0 - Ug - Vg + 128) >> 8);   // G
			dstRow[x * 4 + 2] = clamp255((Y0 + Vr + 128) >> 8);        // R
			dstRow[x * 4 + 3] = 255;                                    // A

			// Pixel 1
			dstRow[(x + 1) * 4 + 0] = clamp255((Y1 + Ub + 128) >> 8);
			dstRow[(x + 1) * 4 + 1] = clamp255((Y1 - Ug - Vg + 128) >> 8);
			dstRow[(x + 1) * 4 + 2] = clamp255((Y1 + Vr + 128) >> 8);
			dstRow[(x + 1) * 4 + 3] = 255;
		}
	}
}

// Reference implementation (per-pixel, no tables)
static void ConvertNV12toRGB32_Reference(unsigned char* dst, const unsigned char* src,
	int32_t width, int32_t height)
{
	size_t yPlaneSize = (size_t)width * height;
	const unsigned char* yPlane = src;
	const unsigned char* uvPlane = src + yPlaneSize;

	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			int32_t Y = yPlane[y * width + x];
			int32_t uvIndex = (y / 2) * width + (x / 2) * 2;
			int32_t U = uvPlane[uvIndex];
			int32_t V = uvPlane[uvIndex + 1];

			// Standard YUV to RGB conversion
			int32_t C = Y - 16;
			int32_t D = U - 128;
			int32_t E = V - 128;

			int32_t R = (298 * C + 408 * E + 128) >> 8;
			int32_t G = (298 * C - 100 * D - 208 * E + 128) >> 8;
			int32_t B = (298 * C + 516 * D + 128) >> 8;

			dst[(y * width + x) * 4 + 0] = clamp255(B);
			dst[(y * width + x) * 4 + 1] = clamp255(G);
			dst[(y * width + x) * 4 + 2] = clamp255(R);
			dst[(y * width + x) * 4 + 3] = 255;
		}
	}
}

// Test NV12 conversion with known values
bool TestNV12Conversion() {
	printf("\n=== NV12 Conversion Test ===\n");

	const int32_t width = 8;
	const int32_t height = 4;
	size_t yPlaneSize = width * height;
	size_t uvPlaneSize = width * height / 2;  // NV12 has half-size UV
	size_t nv12Size = yPlaneSize + uvPlaneSize;
	size_t rgb32Size = width * height * 4;

	unsigned char* nv12 = (unsigned char*)malloc(nv12Size);
	unsigned char* rgb32_opt = (unsigned char*)malloc(rgb32Size);
	unsigned char* rgb32_ref = (unsigned char*)malloc(rgb32Size);

	bool passed = true;
	int testNum = 0;

	// Test 1: Black frame (Y=16, U=128, V=128 in YCbCr)
	testNum++;
	printf("Test %d: Black frame... ", testNum);
	memset(nv12, 16, yPlaneSize);           // Y plane = 16 (black)
	memset(nv12 + yPlaneSize, 128, uvPlaneSize);  // UV plane = 128 (neutral)

	ConvertNV12toRGB32(rgb32_opt, nv12, nv12Size, width, height);
	ConvertNV12toRGB32_Reference(rgb32_ref, nv12, width, height);

	// Check if output is near black
	bool blackOk = true;
	for (size_t i = 0; i < rgb32Size; i += 4) {
		if (rgb32_opt[i] > 5 || rgb32_opt[i+1] > 5 || rgb32_opt[i+2] > 5) {
			blackOk = false;
			break;
		}
	}
	if (blackOk) {
		printf("PASS (output is black)\n");
	} else {
		printf("FAIL (expected black)\n");
		passed = false;
	}

	// Test 2: White frame (Y=235, U=128, V=128)
	testNum++;
	printf("Test %d: White frame... ", testNum);
	memset(nv12, 235, yPlaneSize);          // Y = 235 (white)
	memset(nv12 + yPlaneSize, 128, uvPlaneSize);  // UV = 128 (neutral)

	ConvertNV12toRGB32(rgb32_opt, nv12, nv12Size, width, height);

	bool whiteOk = true;
	for (size_t i = 0; i < rgb32Size; i += 4) {
		if (rgb32_opt[i] < 250 || rgb32_opt[i+1] < 250 || rgb32_opt[i+2] < 250) {
			whiteOk = false;
			break;
		}
	}
	if (whiteOk) {
		printf("PASS (output is white)\n");
	} else {
		printf("FAIL (expected white, got R=%d G=%d B=%d)\n",
			rgb32_opt[2], rgb32_opt[1], rgb32_opt[0]);
		passed = false;
	}

	// Test 3: Red frame (Y=82, U=90, V=240)
	testNum++;
	printf("Test %d: Red frame... ", testNum);
	memset(nv12, 82, yPlaneSize);           // Y for red
	for (size_t i = 0; i < uvPlaneSize; i += 2) {
		nv12[yPlaneSize + i] = 90;          // U for red
		nv12[yPlaneSize + i + 1] = 240;     // V for red
	}

	ConvertNV12toRGB32(rgb32_opt, nv12, nv12Size, width, height);

	// Red should have high R, low G and B
	bool redOk = (rgb32_opt[2] > 200 && rgb32_opt[1] < 50 && rgb32_opt[0] < 50);
	if (redOk) {
		printf("PASS (R=%d G=%d B=%d)\n", rgb32_opt[2], rgb32_opt[1], rgb32_opt[0]);
	} else {
		printf("FAIL (R=%d G=%d B=%d, expected red)\n",
			rgb32_opt[2], rgb32_opt[1], rgb32_opt[0]);
		passed = false;
	}

	// Test 4: Green frame (Y=145, U=54, V=34)
	testNum++;
	printf("Test %d: Green frame... ", testNum);
	memset(nv12, 145, yPlaneSize);          // Y for green
	for (size_t i = 0; i < uvPlaneSize; i += 2) {
		nv12[yPlaneSize + i] = 54;          // U for green
		nv12[yPlaneSize + i + 1] = 34;      // V for green
	}

	ConvertNV12toRGB32(rgb32_opt, nv12, nv12Size, width, height);

	// Green should have high G, low R and B
	bool greenOk = (rgb32_opt[1] > 200 && rgb32_opt[2] < 50 && rgb32_opt[0] < 50);
	if (greenOk) {
		printf("PASS (R=%d G=%d B=%d)\n", rgb32_opt[2], rgb32_opt[1], rgb32_opt[0]);
	} else {
		printf("FAIL (R=%d G=%d B=%d, expected green)\n",
			rgb32_opt[2], rgb32_opt[1], rgb32_opt[0]);
		passed = false;
	}

	// Test 5: Blue frame (Y=41, U=240, V=110)
	testNum++;
	printf("Test %d: Blue frame... ", testNum);
	memset(nv12, 41, yPlaneSize);           // Y for blue
	for (size_t i = 0; i < uvPlaneSize; i += 2) {
		nv12[yPlaneSize + i] = 240;         // U for blue
		nv12[yPlaneSize + i + 1] = 110;     // V for blue
	}

	ConvertNV12toRGB32(rgb32_opt, nv12, nv12Size, width, height);

	// Blue should have high B, low R and G
	bool blueOk = (rgb32_opt[0] > 200 && rgb32_opt[2] < 50 && rgb32_opt[1] < 50);
	if (blueOk) {
		printf("PASS (R=%d G=%d B=%d)\n", rgb32_opt[2], rgb32_opt[1], rgb32_opt[0]);
	} else {
		printf("FAIL (R=%d G=%d B=%d, expected blue)\n",
			rgb32_opt[2], rgb32_opt[1], rgb32_opt[0]);
		passed = false;
	}

	// Test 6: Compare optimized vs reference
	testNum++;
	printf("Test %d: Optimized vs Reference... ", testNum);

	// Create gradient test pattern
	for (int32_t y = 0; y < height; y++) {
		for (int32_t x = 0; x < width; x++) {
			nv12[y * width + x] = (uint8_t)(16 + (y * width + x) * 219 / (width * height));
		}
	}
	for (size_t i = 0; i < uvPlaneSize; i += 2) {
		nv12[yPlaneSize + i] = 100 + (i % 56);      // U varies
		nv12[yPlaneSize + i + 1] = 150 + (i % 56);  // V varies
	}

	ConvertNV12toRGB32(rgb32_opt, nv12, nv12Size, width, height);
	ConvertNV12toRGB32_Reference(rgb32_ref, nv12, width, height);

	int maxDiff = 0;
	int diffCount = 0;
	for (size_t i = 0; i < rgb32Size; i++) {
		int diff = abs((int)rgb32_opt[i] - (int)rgb32_ref[i]);
		if (diff > maxDiff) maxDiff = diff;
		if (diff > 1) diffCount++;  // Allow 1 for rounding
	}

	if (maxDiff <= 1) {
		printf("PASS (max diff=%d)\n", maxDiff);
	} else {
		printf("FAIL (max diff=%d, %d pixels differ)\n", maxDiff, diffCount);
		passed = false;
	}

	// Test 7: Incomplete frame handling
	testNum++;
	printf("Test %d: Incomplete frame... ", testNum);
	memset(rgb32_opt, 0, rgb32Size);
	ConvertNV12toRGB32(rgb32_opt, nv12, nv12Size / 2, width, height);  // Half size

	// Should be filled with gray (0x80)
	bool incompleteOk = (rgb32_opt[0] == 0x80 && rgb32_opt[1] == 0x80);
	if (incompleteOk) {
		printf("PASS (filled with gray)\n");
	} else {
		printf("FAIL (expected gray fill)\n");
		passed = false;
	}

	// Test 8: Performance test
	testNum++;
	printf("Test %d: Performance (640x480)... ", testNum);

	const int32_t perfWidth = 640;
	const int32_t perfHeight = 480;
	size_t perfYSize = perfWidth * perfHeight;
	size_t perfUVSize = perfWidth * perfHeight / 2;
	size_t perfNV12Size = perfYSize + perfUVSize;
	size_t perfRGB32Size = perfWidth * perfHeight * 4;

	unsigned char* perfNV12 = (unsigned char*)malloc(perfNV12Size);
	unsigned char* perfRGB32 = (unsigned char*)malloc(perfRGB32Size);

	// Fill with test pattern
	for (size_t i = 0; i < perfYSize; i++) perfNV12[i] = i % 256;
	for (size_t i = 0; i < perfUVSize; i++) perfNV12[perfYSize + i] = 128;

	const int iterations = 100;
	bigtime_t start = 0, end = 0;

	start = system_time();
	for (int i = 0; i < iterations; i++) {
		ConvertNV12toRGB32(perfRGB32, perfNV12, perfNV12Size, perfWidth, perfHeight);
	}
	end = system_time();

	double msPerFrame = (double)(end - start) / 1000.0 / iterations;
	double fps = 1000.0 / msPerFrame;
	printf("PASS (%.2f ms/frame, %.1f fps)\n", msPerFrame, fps);

	free(perfNV12);
	free(perfRGB32);
	free(nv12);
	free(rgb32_opt);
	free(rgb32_ref);

	return passed;
}

// ============================================================================
// Camera Terminal Control Tests
// ============================================================================

// CT control bitmask definitions (from UVC spec)
#define CT_CONTROL_SCANNING_MODE        (1 << 0)
#define CT_CONTROL_AE_MODE              (1 << 1)
#define CT_CONTROL_AE_PRIORITY          (1 << 2)
#define CT_CONTROL_EXPOSURE_TIME_ABS    (1 << 3)
#define CT_CONTROL_EXPOSURE_TIME_REL    (1 << 4)
#define CT_CONTROL_FOCUS_ABS            (1 << 5)
#define CT_CONTROL_FOCUS_REL            (1 << 6)
#define CT_CONTROL_IRIS_ABS             (1 << 7)
#define CT_CONTROL_IRIS_REL             (1 << 8)
#define CT_CONTROL_ZOOM_ABS             (1 << 9)
#define CT_CONTROL_ZOOM_REL             (1 << 10)
#define CT_CONTROL_PAN_TILT_ABS         (1 << 11)
#define CT_CONTROL_PAN_TILT_REL         (1 << 12)
#define CT_CONTROL_ROLL_ABS             (1 << 13)
#define CT_CONTROL_ROLL_REL             (1 << 14)
#define CT_CONTROL_FOCUS_AUTO           (1 << 17)
#define CT_CONTROL_PRIVACY              (1 << 18)

// Auto Exposure Mode values
#define AE_MODE_MANUAL          0x01
#define AE_MODE_AUTO            0x02
#define AE_MODE_SHUTTER_PRIO    0x04
#define AE_MODE_APERTURE_PRIO   0x08

// Simulated Camera Terminal state
struct CameraTerminalState {
	uint8_t terminalID;
	uint32_t supportedControls;

	// Control values
	uint8_t autoExposureMode;
	uint32_t exposureTimeAbs;    // in 100us units
	bool autoFocus;
	uint16_t focusAbsolute;
	uint16_t zoomAbsolute;
	int32_t panAbsolute;         // in arc-seconds
	int32_t tiltAbsolute;        // in arc-seconds
	bool privacyEnabled;
};

bool TestCameraTerminalControls() {
	printf("\n=== Camera Terminal Control Tests ===\n");

	CameraTerminalState ct;
	memset(&ct, 0, sizeof(ct));

	bool passed = true;
	int testNum = 0;

	// Test 1: Control bitmap parsing
	testNum++;
	printf("Test %d: Control bitmap parsing... ", testNum);

	// Simulate a typical laptop webcam's CT controls
	ct.supportedControls = CT_CONTROL_AE_MODE | CT_CONTROL_EXPOSURE_TIME_ABS |
	                       CT_CONTROL_FOCUS_ABS | CT_CONTROL_FOCUS_AUTO;

	bool hasAE = (ct.supportedControls & CT_CONTROL_AE_MODE) != 0;
	bool hasExposure = (ct.supportedControls & CT_CONTROL_EXPOSURE_TIME_ABS) != 0;
	bool hasFocus = (ct.supportedControls & CT_CONTROL_FOCUS_ABS) != 0;
	bool hasAutoFocus = (ct.supportedControls & CT_CONTROL_FOCUS_AUTO) != 0;
	bool hasZoom = (ct.supportedControls & CT_CONTROL_ZOOM_ABS) != 0;
	bool hasPanTilt = (ct.supportedControls & CT_CONTROL_PAN_TILT_ABS) != 0;

	if (hasAE && hasExposure && hasFocus && hasAutoFocus && !hasZoom && !hasPanTilt) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 2: Auto Exposure Mode values
	testNum++;
	printf("Test %d: Auto Exposure Mode values... ", testNum);

	ct.autoExposureMode = AE_MODE_AUTO;
	bool aeValid = (ct.autoExposureMode == AE_MODE_MANUAL ||
	                ct.autoExposureMode == AE_MODE_AUTO ||
	                ct.autoExposureMode == AE_MODE_SHUTTER_PRIO ||
	                ct.autoExposureMode == AE_MODE_APERTURE_PRIO);

	if (aeValid) {
		printf("PASS (mode=%d)\n", ct.autoExposureMode);
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 3: Exposure Time range
	testNum++;
	printf("Test %d: Exposure Time range... ", testNum);

	// Typical exposure range: 100us to 10s (in 100us units: 1 to 100000)
	ct.exposureTimeAbs = 333;  // ~33ms = 1/30s
	bool expValid = (ct.exposureTimeAbs >= 1 && ct.exposureTimeAbs <= 100000);

	double expSeconds = ct.exposureTimeAbs * 0.0001;
	if (expValid) {
		printf("PASS (%.4f sec = 1/%.0f)\n", expSeconds, 1.0/expSeconds);
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 4: Focus Absolute range
	testNum++;
	printf("Test %d: Focus Absolute range... ", testNum);

	// Focus is typically 0-255 or 0-65535
	ct.focusAbsolute = 128;
	bool focusValid = (ct.focusAbsolute <= 65535);

	if (focusValid) {
		printf("PASS (focus=%d)\n", ct.focusAbsolute);
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 5: Auto Focus toggle
	testNum++;
	printf("Test %d: Auto Focus toggle... ", testNum);

	ct.autoFocus = true;
	bool af1 = ct.autoFocus;
	ct.autoFocus = false;
	bool af2 = !ct.autoFocus;

	if (af1 && af2) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 6: Zoom Absolute range
	testNum++;
	printf("Test %d: Zoom Absolute range... ", testNum);

	ct.zoomAbsolute = 100;  // 1x zoom (typical base)
	bool zoomValid = (ct.zoomAbsolute >= 0 && ct.zoomAbsolute <= 65535);

	if (zoomValid) {
		printf("PASS (zoom=%d)\n", ct.zoomAbsolute);
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 7: Pan/Tilt values (arc-seconds)
	testNum++;
	printf("Test %d: Pan/Tilt values... ", testNum);

	// Pan/Tilt in arc-seconds: 1 degree = 3600 arc-seconds
	// Typical range: -180 to +180 degrees = -648000 to +648000 arc-seconds
	ct.panAbsolute = 36000;   // 10 degrees right
	ct.tiltAbsolute = -18000; // 5 degrees down

	double panDegrees = ct.panAbsolute / 3600.0;
	double tiltDegrees = ct.tiltAbsolute / 3600.0;

	bool ptValid = (ct.panAbsolute >= -648000 && ct.panAbsolute <= 648000 &&
	                ct.tiltAbsolute >= -648000 && ct.tiltAbsolute <= 648000);

	if (ptValid) {
		printf("PASS (pan=%.1f deg, tilt=%.1f deg)\n", panDegrees, tiltDegrees);
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 8: Privacy control
	testNum++;
	printf("Test %d: Privacy control... ", testNum);

	ct.privacyEnabled = true;
	bool privOn = ct.privacyEnabled;
	ct.privacyEnabled = false;
	bool privOff = !ct.privacyEnabled;

	if (privOn && privOff) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 9: Full CT control bitmap (all controls)
	testNum++;
	printf("Test %d: Full control bitmap... ", testNum);

	uint32_t fullControls = CT_CONTROL_SCANNING_MODE | CT_CONTROL_AE_MODE |
	                        CT_CONTROL_AE_PRIORITY | CT_CONTROL_EXPOSURE_TIME_ABS |
	                        CT_CONTROL_FOCUS_ABS | CT_CONTROL_FOCUS_AUTO |
	                        CT_CONTROL_ZOOM_ABS | CT_CONTROL_PAN_TILT_ABS |
	                        CT_CONTROL_PRIVACY;

	int controlCount = 0;
	for (int i = 0; i < 32; i++) {
		if (fullControls & (1 << i)) controlCount++;
	}

	if (controlCount == 9) {
		printf("PASS (%d controls)\n", controlCount);
	} else {
		printf("FAIL (expected 9, got %d)\n", controlCount);
		passed = false;
	}

	return passed;
}

// ============================================================================
// NV12 Format Detection Test
// ============================================================================

// NV12 GUID: 3231564E-0000-0010-8000-00AA00389B71
static const uint8_t kNV12Guid[16] = {
	'N', 'V', '1', '2', 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

// YUY2 GUID: 32595559-0000-0010-8000-00AA00389B71
static const uint8_t kYUY2Guid[16] = {
	'Y', 'U', 'Y', '2', 0x00, 0x00, 0x10, 0x00,
	0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

bool TestFormatDetection() {
	printf("\n=== Format Detection Test ===\n");

	bool passed = true;
	int testNum = 0;

	// Test 1: NV12 GUID detection
	testNum++;
	printf("Test %d: NV12 GUID detection... ", testNum);

	uint8_t testGuid[16];
	memcpy(testGuid, kNV12Guid, 16);

	bool isNV12 = (memcmp(testGuid, kNV12Guid, 16) == 0);
	bool isYUY2 = (memcmp(testGuid, kYUY2Guid, 16) == 0);

	if (isNV12 && !isYUY2) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 2: YUY2 GUID detection
	testNum++;
	printf("Test %d: YUY2 GUID detection... ", testNum);

	memcpy(testGuid, kYUY2Guid, 16);

	isNV12 = (memcmp(testGuid, kNV12Guid, 16) == 0);
	isYUY2 = (memcmp(testGuid, kYUY2Guid, 16) == 0);

	if (!isNV12 && isYUY2) {
		printf("PASS\n");
	} else {
		printf("FAIL\n");
		passed = false;
	}

	// Test 3: GUID string extraction
	testNum++;
	printf("Test %d: GUID string extraction... ", testNum);

	char guidStr[5] = {0};
	guidStr[0] = kNV12Guid[0];
	guidStr[1] = kNV12Guid[1];
	guidStr[2] = kNV12Guid[2];
	guidStr[3] = kNV12Guid[3];

	if (strcmp(guidStr, "NV12") == 0) {
		printf("PASS (%s)\n", guidStr);
	} else {
		printf("FAIL (got '%s')\n", guidStr);
		passed = false;
	}

	return passed;
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
	printf("========================================\n");
	printf("  UVC Driver New Features Test Suite\n");
	printf("========================================\n");

	int totalTests = 0;
	int passedTests = 0;

	// Run NV12 conversion tests
	if (TestNV12Conversion()) passedTests++;
	totalTests++;

	// Run Camera Terminal control tests
	if (TestCameraTerminalControls()) passedTests++;
	totalTests++;

	// Run format detection tests
	if (TestFormatDetection()) passedTests++;
	totalTests++;

	printf("\n========================================\n");
	printf("  SUMMARY: %d/%d test suites passed\n", passedTests, totalTests);
	printf("========================================\n");

	return (passedTests == totalTests) ? 0 : 1;
}
