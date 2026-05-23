// Compare driver's RGB32 output with direct YUY2->RGB conversion
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

static uint8_t clamp(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

int main() {
    int width = 320, height = 240;

    // Read driver's RGB32 output
    FILE* f = fopen("/boot/home/Desktop/frame_rgb32.raw", "rb");
    if (!f) { perror("rgb32"); return 1; }
    size_t rgb32Size = width * height * 4;
    uint8_t* driverRGB = (uint8_t*)malloc(rgb32Size);
    fread(driverRGB, 1, rgb32Size, f);
    fclose(f);

    // Read raw YUY2
    f = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    if (!f) { perror("yuy2"); return 1; }
    size_t yuy2Size = width * height * 2;
    uint8_t* yuy2 = (uint8_t*)malloc(yuy2Size);
    fread(yuy2, 1, yuy2Size, f);
    fclose(f);

    // Convert YUY2 to RGB32 (reference - same format as driver output: BGRA)
    uint8_t* refRGB = (uint8_t*)malloc(rgb32Size);
    for (int row = 0; row < height; row++) {
        uint8_t* src = yuy2 + row * width * 2;
        uint8_t* dst = refRGB + row * width * 4;
        for (int x = 0; x < width; x += 2) {
            int y0 = src[0], u = src[1], y1 = src[2], v = src[3];
            src += 4;
            int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;
            dst[0] = clamp((298*c0 + 516*d + 128) >> 8);           // B
            dst[1] = clamp((298*c0 - 100*d - 208*e + 128) >> 8);   // G
            dst[2] = clamp((298*c0 + 409*e + 128) >> 8);           // R
            dst[3] = 255;                                            // A
            dst[4] = clamp((298*c1 + 516*d + 128) >> 8);           // B
            dst[5] = clamp((298*c1 - 100*d - 208*e + 128) >> 8);   // G
            dst[6] = clamp((298*c1 + 409*e + 128) >> 8);           // R
            dst[7] = 255;                                            // A
            dst += 8;
        }
    }

    // Compare pixel by pixel
    int totalDiff = 0;
    int maxDiff = 0;
    int mismatchPixels = 0;
    int firstMismatchRow = -1, firstMismatchCol = -1;
    int offsetMismatch = -1;

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int offset = (row * width + col) * 4;
            int diffB = abs((int)driverRGB[offset] - (int)refRGB[offset]);
            int diffG = abs((int)driverRGB[offset+1] - (int)refRGB[offset+1]);
            int diffR = abs((int)driverRGB[offset+2] - (int)refRGB[offset+2]);
            int pixDiff = diffR + diffG + diffB;

            if (pixDiff > 10) {  // threshold for "significant" difference
                mismatchPixels++;
                if (firstMismatchRow < 0) {
                    firstMismatchRow = row;
                    firstMismatchCol = col;
                    offsetMismatch = offset;
                }
            }
            totalDiff += pixDiff;
            if (pixDiff > maxDiff) maxDiff = pixDiff;
        }
    }

    int totalPixels = width * height;
    printf("=== Comparison Results ===\n");
    printf("Total pixels: %d\n", totalPixels);
    printf("Mismatched pixels (diff>10): %d (%.1f%%)\n", mismatchPixels,
        100.0 * mismatchPixels / totalPixels);
    printf("Average diff per pixel: %.2f\n", (double)totalDiff / totalPixels);
    printf("Max diff: %d\n", maxDiff);

    if (firstMismatchRow >= 0) {
        printf("\nFirst significant mismatch at row=%d col=%d (byte offset %d):\n",
            firstMismatchRow, firstMismatchCol, offsetMismatch);
        printf("  Driver:    B=%d G=%d R=%d A=%d\n",
            driverRGB[offsetMismatch], driverRGB[offsetMismatch+1],
            driverRGB[offsetMismatch+2], driverRGB[offsetMismatch+3]);
        printf("  Reference: B=%d G=%d R=%d A=%d\n",
            refRGB[offsetMismatch], refRGB[offsetMismatch+1],
            refRGB[offsetMismatch+2], refRGB[offsetMismatch+3]);
    }

    // Check if driver output looks like it has a different stride
    // Try to find where driver's row N matches reference's row N
    printf("\n=== Row alignment check ===\n");
    for (int row = 0; row < 20; row++) {
        int driverOffset = row * width * 4;
        // Check if this matches reference at the same position
        int matchDiff = 0;
        for (int i = 0; i < 32; i++)
            matchDiff += abs((int)driverRGB[driverOffset + i] - (int)refRGB[driverOffset + i]);
        printf("Row %3d: same-offset diff=%d", row, matchDiff);

        // Also try to find where this row ACTUALLY matches in reference
        if (matchDiff > 100) {
            // Search nearby offsets
            for (int tryOffset = -5120; tryOffset <= 5120; tryOffset += 4) {
                int testOff = driverOffset + tryOffset;
                if (testOff < 0 || testOff + 32 > (int)rgb32Size) continue;
                int testDiff = 0;
                for (int i = 0; i < 32; i++)
                    testDiff += abs((int)driverRGB[driverOffset + i] - (int)refRGB[testOff + i]);
                if (testDiff < 20) {
                    printf(" -> matches ref at offset %d (shift=%d bytes = %.1f rows)",
                        testOff, tryOffset, (double)tryOffset / (width * 4));
                    break;
                }
            }
        }
        printf("\n");
    }

    free(driverRGB);
    free(yuy2);
    free(refRGB);
    return 0;
}
