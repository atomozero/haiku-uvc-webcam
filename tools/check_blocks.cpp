// Check for macroblock-like patterns in YUY2 data
// Some cameras internally use block-based processing even for "uncompressed" output
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <initializer_list>

int main() {
    int width = 320, height = 240;
    int rowBytes = width * 2;

    FILE* f = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    if (!f) { perror("open"); return 1; }
    uint8_t data[153600];
    fread(data, 1, 153600, f);
    fclose(f);

    // Get Y value at pixel (x,y)
    auto getY = [&](int x, int y) -> int {
        if (x < 0 || x >= width || y < 0 || y >= height) return -1;
        return data[y * rowBytes + x * 2];
    };

    // Check for block boundaries at 8x8 and 16x16 intervals
    printf("=== Block boundary analysis ===\n");
    for (int blockSize : {8, 16}) {
        double withinBlockDiff = 0;
        int withinCount = 0;
        double crossBlockDiff = 0;
        int crossCount = 0;

        for (int y = 1; y < height; y++) {
            for (int x = 1; x < width; x++) {
                int diff = abs(getY(x, y) - getY(x-1, y));
                bool crossesHBlock = (x % blockSize == 0);
                if (crossesHBlock) {
                    crossBlockDiff += diff;
                    crossCount++;
                } else {
                    withinBlockDiff += diff;
                    withinCount++;
                }
            }
        }
        printf("Block size %2d: within-block avg diff=%.2f, cross-block avg diff=%.2f (ratio=%.2f)\n",
            blockSize,
            withinCount > 0 ? withinBlockDiff / withinCount : 0,
            crossCount > 0 ? crossBlockDiff / crossCount : 0,
            (crossCount > 0 && withinCount > 0) ?
                (crossBlockDiff / crossCount) / (withinBlockDiff / withinCount) : 0);
    }

    // Check vertical block boundaries
    printf("\n=== Vertical block boundary analysis ===\n");
    for (int blockSize : {8, 16}) {
        double withinDiff = 0;
        int withinCount = 0;
        double crossDiff = 0;
        int crossCount = 0;

        for (int y = 1; y < height; y++) {
            bool crossesVBlock = (y % blockSize == 0);
            for (int x = 0; x < width; x++) {
                int diff = abs(getY(x, y) - getY(x, y-1));
                if (crossesVBlock) {
                    crossDiff += diff;
                    crossCount++;
                } else {
                    withinDiff += diff;
                    withinCount++;
                }
            }
        }
        printf("Block size %2d: within-block avg diff=%.2f, cross-block avg diff=%.2f (ratio=%.2f)\n",
            blockSize,
            withinCount > 0 ? withinDiff / withinCount : 0,
            crossCount > 0 ? crossDiff / crossCount : 0,
            (crossCount > 0 && withinCount > 0) ?
                (crossDiff / crossCount) / (withinDiff / withinCount) : 0);
    }

    // Dump a grid showing average Y per 16x16 block
    printf("\n=== 16x16 block average Y map (first 160x80) ===\n");
    printf("    ");
    for (int bx = 0; bx < 10; bx++) printf(" %3d", bx*16);
    printf("\n");
    for (int by = 0; by < 5; by++) {
        printf("%3d:", by*16);
        for (int bx = 0; bx < 10; bx++) {
            int sum = 0, count = 0;
            for (int y = by*16; y < by*16+16 && y < height; y++) {
                for (int x = bx*16; x < bx*16+16 && x < width; x++) {
                    sum += getY(x, y);
                    count++;
                }
            }
            printf(" %3d", count > 0 ? sum/count : 0);
        }
        printf("\n");
    }

    // Check specific Y values around a 16x16 boundary
    printf("\n=== Detail around block boundary at (16,16) ===\n");
    printf("Pixels (14,14) to (18,18) Y values:\n");
    for (int y = 14; y <= 18; y++) {
        printf("  y=%d: ", y);
        for (int x = 14; x <= 18; x++) {
            printf("%3d ", getY(x, y));
        }
        printf("%s\n", y == 16 ? " <-- block boundary" : "");
    }

    return 0;
}
