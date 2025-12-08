// Analyze YUY2 data to find discontinuities
// Looks for where rows don't match the expected pattern
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s input.yuv\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 1; }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    printf("File size: %zu bytes\n", size);
    printf("Expected 320x240 YUY2: 153600 bytes\n\n");

    int width = 320;
    int height = 240;
    int stride = width * 2;  // 640

    // Method 1: Check row similarity
    printf("=== Row Similarity Analysis ===\n");
    printf("Comparing first 8 bytes of each row with expected offset...\n\n");

    for (int row = 0; row < 20 && row * stride + 8 < (int)size; row++) {
        int offset = row * stride;
        printf("Row %3d (offset %6d): ", row, offset);
        for (int i = 0; i < 8; i++) {
            printf("%02x ", data[offset + i]);
        }

        // Calculate Y average (luminance) for this row start
        int yAvg = (data[offset] + data[offset+2] + data[offset+4] + data[offset+6]) / 4;
        printf(" Y_avg=%3d\n", yAvg);
    }

    // Method 2: Find discontinuities by looking at sudden Y changes
    printf("\n=== Discontinuity Detection ===\n");
    printf("Looking for sudden luminance jumps between adjacent rows...\n\n");

    int lastYAvg = 0;
    for (int row = 0; row < height && (row+1) * stride < (int)size; row++) {
        int offset = row * stride;
        int yAvg = (data[offset] + data[offset+2] + data[offset+4] + data[offset+6]) / 4;

        if (row > 0) {
            int diff = abs(yAvg - lastYAvg);
            if (diff > 40) {  // Threshold for "sudden jump"
                printf("JUMP at row %3d: Y_avg changed from %3d to %3d (diff=%d)\n",
                       row, lastYAvg, yAvg, diff);
            }
        }
        lastYAvg = yAvg;
    }

    // Method 3: Auto-correlation to find actual stride
    printf("\n=== Stride Auto-Detection ===\n");
    printf("Testing correlation at different offsets...\n\n");

    // Test strides from 620 to 680
    for (int testStride = 620; testStride <= 680; testStride += 4) {
        long long totalDiff = 0;
        int count = 0;

        // Compare every 10th row at this stride
        for (int row = 0; row < height-1; row += 10) {
            int off1 = row * testStride;
            int off2 = (row + 1) * testStride;

            if (off2 + 8 >= (int)size) break;

            // Sum of absolute differences in Y values
            for (int i = 0; i < 8; i += 2) {
                totalDiff += abs((int)data[off1+i] - (int)data[off2+i]);
            }
            count++;
        }

        if (count > 0) {
            printf("Stride %3d: avg_diff = %.1f %s\n",
                   testStride, (float)totalDiff / count,
                   testStride == 640 ? "<-- expected" : "");
        }
    }

    // Method 4: Look for UVC header remnants (shouldn't be there)
    printf("\n=== Checking for UVC Header Remnants ===\n");
    printf("Looking for 0x0c (header length=12) at suspicious positions...\n\n");

    int headerCount = 0;
    for (size_t i = 0; i < size - 12; i++) {
        // UVC header pattern: first byte is length, second byte has specific flags
        if (data[i] == 0x0c && (data[i+1] & 0x80)) {  // 0x0c=12, bit7 set
            headerCount++;
            if (headerCount <= 10) {
                printf("Possible header at offset %zu: %02x %02x %02x %02x\n",
                       i, data[i], data[i+1], data[i+2], data[i+3]);
            }
        }
    }
    printf("Found %d possible header remnants\n", headerCount);

    free(data);
    return 0;
}
