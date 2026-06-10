// Verify YUY2 row alignment by checking actual pixel values at row boundaries
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main() {
    FILE* f = fopen("/boot/home/Desktop/frame_dump.yuv", "rb");
    if (!f) {
        printf("No frame_dump.yuv found\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(fileSize);
    fread(data, 1, fileSize, f);
    fclose(f);

    int width = 320;
    int height = 240;
    int stride = width * 2;  // 640 bytes per row

    printf("=== Row Alignment Verification ===\n");
    printf("Expected stride: %d bytes per row\n\n", stride);

    // The camera image should have consistent structure:
    // - Bright areas should span multiple rows vertically
    // - Edge positions should be consistent across rows (not diagonal)

    printf("Checking pixel brightness at column 160 (center) for all rows:\n");
    printf("Expecting vertical consistency for a real image\n\n");

    int lastBrightness = -1;
    int bigJumps = 0;

    for (int row = 0; row < height; row++) {
        int offset = row * stride + 160 * 2;  // Center column
        int Y = data[offset];

        // Check for big brightness jumps (>100) which indicate misalignment
        if (lastBrightness >= 0 && abs(Y - lastBrightness) > 100) {
            printf("Row %3d: Y=%3d (jump of %+d from previous row!) <-- SUSPICIOUS\n",
                   row, Y, Y - lastBrightness);
            bigJumps++;
        } else if (row < 20 || row % 20 == 0) {
            printf("Row %3d: Y=%3d\n", row, Y);
        }
        lastBrightness = Y;
    }

    printf("\n=== Summary ===\n");
    printf("Big brightness jumps (>100): %d\n", bigJumps);
    if (bigJumps > 5) {
        printf("WARNING: Many jumps suggest row misalignment!\n");
    } else {
        printf("Row alignment appears OK\n");
    }

    // Also check horizontal consistency within a row
    printf("\n=== Horizontal Pattern Check (Row 50) ===\n");
    printf("Showing brightness every 20 pixels:\n");
    int row50offset = 50 * stride;
    printf("Row 50: ");
    for (int x = 0; x < width; x += 20) {
        int Y = data[row50offset + x * 2];
        printf("%3d ", Y);
    }
    printf("\n");

    // Compare with row 51
    int row51offset = 51 * stride;
    printf("Row 51: ");
    for (int x = 0; x < width; x += 20) {
        int Y = data[row51offset + x * 2];
        printf("%3d ", Y);
    }
    printf("\n");

    // Check if they're similar (they should be for adjacent rows in most images)
    int diff = 0;
    for (int x = 0; x < width; x += 4) {
        diff += abs((int)data[row50offset + x*2] - (int)data[row51offset + x*2]);
    }
    printf("Average Y difference between row 50 and 51: %.1f\n", diff / (width/4.0));

    // Now check something specific: find where brightness changes in a row
    // and see if that position is consistent across rows
    printf("\n=== Edge Detection Consistency ===\n");
    printf("Finding where brightness drops below 128 in each row:\n");

    int prevEdge = -1;
    int edgeShifts = 0;
    for (int row = 0; row < height; row += 5) {
        int offset = row * stride;
        int edge = -1;
        for (int x = 0; x < width && edge < 0; x++) {
            if (data[offset + x*2] < 128) {
                edge = x;
            }
        }

        if (prevEdge >= 0 && edge >= 0) {
            int shift = edge - prevEdge;
            if (abs(shift) > 5) {  // Significant shift
                printf("Row %3d: edge at x=%3d (shifted %+d from row %d)\n",
                       row, edge, shift, row-5);
                edgeShifts++;
            }
        }
        prevEdge = edge;
    }

    printf("\nSignificant edge shifts: %d\n", edgeShifts);
    if (edgeShifts > 10) {
        printf("DIAGNOSIS: Image has diagonal edges where vertical expected!\n");
        printf("This confirms stair-stepping in the source data.\n");
    }

    free(data);
    return 0;
}
