// Find horizontal shifts in YUY2 data by pattern matching
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char** argv) {
    FILE* f = fopen("/boot/home/Desktop/frame_dump.yuv", "rb");
    if (!f) { perror("open"); return 1; }

    uint8_t data[153600];
    fread(data, 1, 153600, f);
    fclose(f);

    int width = 320;
    int stride = 640;

    printf("=== Detailed Row Analysis ===\n");
    printf("Looking for horizontal shifts by checking byte patterns...\n\n");

    // For each row, check if the pattern at the start looks like it should
    // be offset from where it is (i.e., data from previous/next row)

    // First, establish what a "good" row transition looks like
    // by checking rows 0-9 where data looks clean

    printf("Rows with unusual patterns (checking every 5th row):\n\n");

    int prevY[4] = {0};

    for (int row = 0; row < 240; row++) {
        int off = row * stride;

        // Get first 4 Y values of this row
        int y[4] = {data[off], data[off+2], data[off+4], data[off+6]};

        // Calculate how different this row start is from expected
        // In a normal image, row starts are similar to previous row
        int diff = 0;
        if (row > 0) {
            for (int i = 0; i < 4; i++) {
                diff += abs(y[i] - prevY[i]);
            }
        }

        // Also check the last bytes of previous row vs first bytes of this row
        if (row > 0 && diff > 60) {
            int prevOff = (row-1) * stride + stride - 8;
            printf("Row %3d: Y=[%3d %3d %3d %3d] diff_from_prev=%3d | prev_row_end: %02x %02x %02x %02x\n",
                   row, y[0], y[1], y[2], y[3], diff,
                   data[prevOff], data[prevOff+2], data[prevOff+4], data[prevOff+6]);
        }

        memcpy(prevY, y, sizeof(prevY));
    }

    // Now let's look at specific byte patterns to find if data is shifted
    printf("\n=== Checking for Byte-Level Shifts ===\n");
    printf("Comparing actual vs expected positions based on pixel patterns...\n\n");

    // Look for a distinctive pattern in the first frame and see where it appears
    // Take a sample from row 50 and look for it in nearby rows

    int sampleRow = 50;
    int sampleOff = sampleRow * stride;
    uint8_t sample[16];
    memcpy(sample, &data[sampleOff], 16);

    printf("Sample from row %d: ", sampleRow);
    for (int i = 0; i < 16; i++) printf("%02x ", sample[i]);
    printf("\n\n");

    // Look for this sample at different offsets
    printf("Searching for this pattern at different offsets...\n");
    for (int testOffset = sampleOff - 32; testOffset <= sampleOff + 32; testOffset += 2) {
        if (testOffset < 0 || testOffset + 16 > 153600) continue;

        int match = 1;
        for (int i = 0; i < 16; i++) {
            if (data[testOffset + i] != sample[i]) {
                match = 0;
                break;
            }
        }
        if (match) {
            int shift = testOffset - sampleOff;
            printf("  Found at offset %d (shift = %+d bytes = %+d pixels)\n",
                   testOffset, shift, shift/2);
        }
    }

    // Check for systematic offset accumulation
    printf("\n=== Packet Boundary Analysis ===\n");
    printf("Known packet sizes from log: 3060, 3052, 536-640\n");
    printf("Checking data at these boundaries...\n\n");

    int packetOffsets[] = {0, 3060, 6112, 6648, 7224, 7792, 8368, 8936, 9512, 10080, 10656, 11232};
    int numPackets = sizeof(packetOffsets) / sizeof(int);

    for (int i = 0; i < numPackets; i++) {
        int off = packetOffsets[i];
        int row = off / stride;
        int col = (off % stride) / 2;  // Column in pixels

        printf("Packet %2d starts at offset %5d (row %3d, col %3d): ",
               i, off, row, col);
        for (int j = 0; j < 8 && off+j < 153600; j++) {
            printf("%02x ", data[off + j]);
        }
        printf("\n");
    }

    return 0;
}
