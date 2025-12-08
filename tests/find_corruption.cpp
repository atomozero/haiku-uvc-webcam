// Find exact byte positions where image corruption/shift occurs
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main() {
    FILE* f = fopen("/boot/home/Desktop/frame_dump.yuv", "rb");
    if (!f) { perror("open"); return 1; }

    uint8_t data[153600];
    fread(data, 1, 153600, f);
    fclose(f);

    int width = 320, height = 240, stride = 640;

    printf("=== Looking for pattern breaks ===\n\n");

    // In a good YUY2 image, consecutive rows should be similar
    // A "shift" occurs when row N+1 looks like row N shifted by some pixels

    // For each row, calculate a "signature" based on Y values at specific positions
    // Then look for rows where the signature suddenly changes in a way that suggests offset

    printf("Checking for horizontal shifts between rows...\n\n");

    for (int row = 1; row < height; row++) {
        uint8_t* prev = data + (row - 1) * stride;
        uint8_t* curr = data + row * stride;

        // Check if current row looks like previous row shifted by some amount
        for (int shift = -32; shift <= 32; shift += 2) {
            if (shift == 0) continue;

            int matchCount = 0;
            int totalChecks = 0;

            // Compare pixels at several positions
            for (int x = 32; x < width - 32; x += 8) {
                int prevOffset = x * 2;
                int currOffset = (x * 2) + shift;

                if (currOffset < 0 || currOffset >= stride - 4) continue;

                // Compare Y values
                int diff = abs((int)prev[prevOffset] - (int)curr[currOffset]) +
                          abs((int)prev[prevOffset + 2] - (int)curr[currOffset + 2]);

                if (diff < 10) matchCount++;
                totalChecks++;
            }

            // If more than 70% match with a shift, this row is probably shifted
            if (totalChecks > 0 && matchCount * 100 / totalChecks > 70) {
                printf("Row %3d appears SHIFTED by %+d bytes (%+d pixels) relative to row %d\n",
                       row, shift, shift/2, row - 1);
                printf("  Match rate: %d/%d (%.1f%%)\n", matchCount, totalChecks,
                       100.0f * matchCount / totalChecks);

                // Show the byte values around this transition
                printf("  Row %d end:   ...%02x %02x %02x %02x\n",
                       row-1, prev[stride-4], prev[stride-3], prev[stride-2], prev[stride-1]);
                printf("  Row %d start: %02x %02x %02x %02x...\n\n",
                       row, curr[0], curr[1], curr[2], curr[3]);
                break;
            }
        }
    }

    // Also look for UVC header remnants (0x0c followed by 0x8?)
    printf("\n=== Searching for UVC header patterns in data ===\n");
    for (int i = 0; i < 153600 - 12; i++) {
        // UVC header: first byte is length (often 12), second byte has bit 7 set
        if (data[i] == 0x0c && (data[i+1] & 0x80) && (data[i+1] & 0x0f) < 4) {
            int row = i / stride;
            int col = (i % stride) / 2;
            printf("Possible UVC header at offset %d (row %d, col %d): %02x %02x %02x %02x\n",
                   i, row, col, data[i], data[i+1], data[i+2], data[i+3]);
        }
    }

    // Check for 0x00 sequences that might indicate buffer initialization
    printf("\n=== Looking for suspicious zero sequences ===\n");
    int zeroRuns = 0;
    for (int i = 0; i < 153600 - 8; i++) {
        if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 0 &&
            data[i+4] == 0 && data[i+5] == 0 && data[i+6] == 0 && data[i+7] == 0) {
            if (zeroRuns++ < 5) {
                int row = i / stride;
                printf("Zero sequence at offset %d (row %d)\n", i, row);
            }
        }
    }
    if (zeroRuns > 5) printf("...and %d more zero sequences\n", zeroRuns - 5);
    if (zeroRuns == 0) printf("No suspicious zero sequences found\n");

    return 0;
}
