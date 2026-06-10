/*
 * Diagnose shifted/misaligned YUY2 frames
 * Analyzes raw YUY2 data to detect stride issues
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

// Analyze row correlation to find actual stride
void findActualStride(const uint8_t* data, size_t dataSize, int expectedWidth, int expectedHeight) {
	printf("\n=== Stride Analysis ===\n");
	printf("Expected: %dx%d, stride=%d bytes\n", expectedWidth, expectedHeight, expectedWidth * 2);
	printf("Data size: %zu bytes (expected %d)\n", dataSize, expectedWidth * expectedHeight * 2);

	if (dataSize < (size_t)expectedWidth * 4) {
		printf("ERROR: Not enough data for analysis\n");
		return;
	}

	// Test various stride values
	int testStrides[] = {
		expectedWidth * 2,           // Expected
		expectedWidth * 2 + 64,      // With 64-byte padding
		expectedWidth * 2 + 128,     // With 128-byte padding
		(expectedWidth * 2 + 255) & ~255,  // 256-byte aligned
		(expectedWidth * 2 + 511) & ~511,  // 512-byte aligned
		(expectedWidth * 2 + 1023) & ~1023, // 1024-byte aligned
		2048, 1024, 512              // Common fixed strides
	};

	printf("\nStride correlation test (lower = more similar rows):\n");

	int bestStride = expectedWidth * 2;
	int bestScore = 999999;

	for (int s = 0; s < (int)(sizeof(testStrides)/sizeof(testStrides[0])); s++) {
		int stride = testStrides[s];
		if (stride <= 0 || (size_t)stride >= dataSize / 2) continue;

		// Compare first row with second row at this stride
		int diff = 0;
		int count = 0;
		for (int i = 0; i < expectedWidth * 2 && i + stride < (int)dataSize; i += 4) {
			// Compare Y values (they should be similar in adjacent rows for most images)
			diff += abs((int)data[i] - (int)data[i + stride]);
			count++;
		}

		int avgDiff = count > 0 ? diff / count : 999999;
		printf("  stride=%4d: avg_diff=%3d %s\n", stride, avgDiff,
			stride == expectedWidth * 2 ? "(expected)" : "");

		if (avgDiff < bestScore) {
			bestScore = avgDiff;
			bestStride = stride;
		}
	}

	printf("\nBest matching stride: %d bytes", bestStride);
	if (bestStride != expectedWidth * 2) {
		printf(" (MISMATCH! expected %d)\n", expectedWidth * 2);
		printf("  -> Padding per row: %d bytes\n", bestStride - expectedWidth * 2);
		printf("  -> Effective width: %d pixels\n", bestStride / 2);
	} else {
		printf(" (correct)\n");
	}
}

// Analyze byte pattern to detect YUYV vs UYVY
void analyzeByteOrder(const uint8_t* data, size_t dataSize) {
	printf("\n=== Byte Order Analysis ===\n");

	if (dataSize < 32) {
		printf("ERROR: Not enough data\n");
		return;
	}

	// Print first 32 bytes
	printf("First 32 bytes:\n  ");
	for (int i = 0; i < 32; i++) {
		printf("%02x ", data[i]);
		if (i == 15) printf("\n  ");
	}
	printf("\n\n");

	// Analyze pattern
	// In YUYV: Y values (even positions) vary more, UV (odd) are near 128
	// In UYVY: UV values (even positions) near 128, Y (odd) vary
	int evenSum = 0, oddSum = 0;
	int evenVar = 0, oddVar = 0;

	for (int i = 0; i < 32; i += 2) {
		evenSum += data[i];
		oddSum += data[i + 1];
	}
	int evenAvg = evenSum / 16;
	int oddAvg = oddSum / 16;

	for (int i = 0; i < 32; i += 2) {
		evenVar += abs((int)data[i] - evenAvg);
		oddVar += abs((int)data[i + 1] - oddAvg);
	}

	printf("Even bytes (0,2,4...): avg=%d, variance=%d\n", evenAvg, evenVar);
	printf("Odd bytes (1,3,5...):  avg=%d, variance=%d\n", oddAvg, oddVar);

	if (oddVar < evenVar && abs(oddAvg - 128) < abs(evenAvg - 128)) {
		printf("Pattern suggests: YUYV (Y at even positions) - CORRECT\n");
	} else if (evenVar < oddVar && abs(evenAvg - 128) < abs(oddAvg - 128)) {
		printf("Pattern suggests: UYVY (U at even positions) - NEEDS SWAP!\n");
	} else {
		printf("Pattern unclear - might be corrupted data or edge case\n");
	}
}

// Check for frame start marker or offset issues
void analyzeFrameStart(const uint8_t* data, size_t dataSize, int width) {
	printf("\n=== Frame Start Analysis ===\n");

	// Look for UVC header remnants at start
	if (dataSize >= 12) {
		printf("First 12 bytes (potential UVC header):\n  ");
		for (int i = 0; i < 12; i++) {
			printf("%02x ", data[i]);
		}
		printf("\n");

		// UVC payload header starts with length byte (usually 0x0c = 12)
		if (data[0] == 0x0c) {
			printf("WARNING: Data starts with 0x0c - possible UVC header not stripped!\n");
			printf("  This would cause a 12-byte offset in the image\n");
		}
	}

	// Check if first row looks like valid YUY2
	printf("\nFirst row Y values (should be similar for uniform areas):\n  ");
	for (int i = 0; i < width * 2 && i < 32; i += 2) {
		printf("%3d ", data[i]);
	}
	printf("\n");
}

// Generate test pattern for comparison
void generateTestPattern(uint8_t* data, int width, int height) {
	printf("\n=== Generating Test Pattern ===\n");

	// Create vertical color bars in YUY2
	// 8 bars: White, Yellow, Cyan, Green, Magenta, Red, Blue, Black
	const int bars = 8;
	int barWidth = width / bars;

	// YUV values for each bar (Y, U, V)
	int barYUV[8][3] = {
		{235, 128, 128},  // White
		{210, 16, 146},   // Yellow
		{170, 166, 16},   // Cyan
		{145, 54, 34},    // Green
		{106, 202, 222},  // Magenta
		{81, 90, 240},    // Red
		{41, 240, 110},   // Blue
		{16, 128, 128}    // Black
	};

	for (int y = 0; y < height; y++) {
		uint8_t* row = data + y * width * 2;
		for (int x = 0; x < width; x += 2) {
			int bar = (x / barWidth) % bars;
			row[0] = barYUV[bar][0];  // Y0
			row[1] = barYUV[bar][1];  // U
			row[2] = barYUV[bar][0];  // Y1
			row[3] = barYUV[bar][2];  // V
			row += 4;
		}
	}

	printf("Test pattern generated: %dx%d YUY2 (%d bytes)\n",
		width, height, width * height * 2);
}

// Main analysis
int main(int argc, char* argv[]) {
	printf("========================================\n");
	printf("  YUY2 Shift Diagnostic Tool\n");
	printf("========================================\n");

	// Try to read frame dump
	const char* filename = "/boot/home/Desktop/frame_dump.yuv";
	int width = 320, height = 240;  // Default, change as needed

	if (argc >= 2) {
		filename = argv[1];
	}
	if (argc >= 4) {
		width = atoi(argv[2]);
		height = atoi(argv[3]);
	}

	printf("\nAnalyzing: %s\n", filename);
	printf("Assumed resolution: %dx%d\n", width, height);

	FILE* f = fopen(filename, "rb");
	if (!f) {
		printf("\nFile not found: %s\n", filename);
		printf("\nTo capture a frame, add this to UVCDeframer.cpp and rebuild:\n");
		printf("  FILE* dump = fopen(\"/boot/home/Desktop/frame_dump.yuv\", \"wb\");\n");
		printf("  if (dump) { fwrite(buffer, 1, size, dump); fclose(dump); }\n");
		printf("\nOr run: ./diagnose_shift <file> <width> <height>\n");

		// Generate test pattern for comparison
		printf("\n--- Generating reference test pattern ---\n");
		size_t testSize = width * height * 2;
		uint8_t* testData = (uint8_t*)malloc(testSize);
		generateTestPattern(testData, width, height);

		// Save test pattern
		const char* testFile = "/boot/home/Desktop/test_pattern.yuv";
		FILE* tf = fopen(testFile, "wb");
		if (tf) {
			fwrite(testData, 1, testSize, tf);
			fclose(tf);
			printf("Saved to: %s\n", testFile);
			printf("View with: ffplay -f rawvideo -pix_fmt yuyv422 -s %dx%d %s\n",
				width, height, testFile);
		}

		free(testData);
		return 1;
	}

	// Get file size
	fseek(f, 0, SEEK_END);
	size_t fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	printf("File size: %zu bytes\n", fileSize);

	// Detect resolution from file size
	if (fileSize == 614400) { width = 640; height = 480; }
	else if (fileSize == 202752) { width = 352; height = 288; }
	else if (fileSize == 153600) { width = 320; height = 240; }
	else if (fileSize == 50688) { width = 176; height = 144; }
	else if (fileSize == 38400) { width = 160; height = 120; }

	printf("Detected resolution: %dx%d\n", width, height);

	// Read data
	uint8_t* data = (uint8_t*)malloc(fileSize);
	fread(data, 1, fileSize, f);
	fclose(f);

	// Run analyses
	analyzeByteOrder(data, fileSize);
	analyzeFrameStart(data, fileSize, width);
	findActualStride(data, fileSize, width, height);

	// Suggestions
	printf("\n=== Recommendations ===\n");
	printf("If image is shifted diagonally:\n");
	printf("  -> Stride mismatch: actual stride != width*2\n");
	printf("If colors are wrong (green/purple tint):\n");
	printf("  -> UYVY vs YUYV byte order issue\n");
	printf("If image is offset by ~12 pixels:\n");
	printf("  -> UVC header not being stripped\n");
	printf("If image has random garbage:\n");
	printf("  -> USB transfer errors, try lower resolution\n");

	free(data);
	return 0;
}
