// Test de-interlacing YUY2 data
// If camera sends fields separately (even rows, then odd rows),
// we need to interleave them back together
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static inline uint8_t clamp(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

void convertYUY2toRGB(uint8_t* yuv, uint8_t* rgb, int width, int height, int srcStride) {
    for (int row = 0; row < height; row++) {
        uint8_t* srcRow = yuv + row * srcStride;
        uint8_t* dstRow = rgb + row * width * 3;

        for (int x = 0; x < width; x += 2) {
            int y0 = srcRow[0], u = srcRow[1], y1 = srcRow[2], v = srcRow[3];
            srcRow += 4;

            int c0 = 298 * (y0 - 16), c1 = 298 * (y1 - 16);
            int d = u - 128, e = v - 128;

            dstRow[0] = clamp((c0 + 409 * e + 128) >> 8);
            dstRow[1] = clamp((c0 - 100 * d - 208 * e + 128) >> 8);
            dstRow[2] = clamp((c0 + 516 * d + 128) >> 8);
            dstRow[3] = clamp((c1 + 409 * e + 128) >> 8);
            dstRow[4] = clamp((c1 - 100 * d - 208 * e + 128) >> 8);
            dstRow[5] = clamp((c1 + 516 * d + 128) >> 8);
            dstRow += 6;
        }
    }
}

void writePPM(const char* filename, uint8_t* rgb, int width, int height) {
    FILE* f = fopen(filename, "wb");
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(rgb, 1, width * height * 3, f);
    fclose(f);
    printf("Wrote %s\n", filename);
}

int main() {
    const char* input = "/boot/home/Desktop/frame_dump.yuv";
    int width = 320, height = 240;
    int stride = width * 2;

    FILE* f = fopen(input, "rb");
    if (!f) { perror("open"); return 1; }

    uint8_t* original = (uint8_t*)malloc(153600);
    uint8_t* deinterlaced = (uint8_t*)malloc(153600);
    uint8_t* rgb = (uint8_t*)malloc(width * height * 3);

    fread(original, 1, 153600, f);
    fclose(f);

    // Test 1: Original (no change)
    printf("Test 1: Original data (stride=640)\n");
    convertYUY2toRGB(original, rgb, width, height, stride);
    writePPM("/boot/home/Desktop/test1_original.ppm", rgb, width, height);

    // Test 2: De-interlace (assume even rows first, then odd rows)
    // Input: rows 0,2,4,...,238 then rows 1,3,5,...,239
    // Output: rows 0,1,2,3,...,239
    printf("Test 2: De-interlaced (even first, then odd)\n");
    int halfHeight = height / 2;
    for (int i = 0; i < halfHeight; i++) {
        // Even row i*2 comes from position i in first half
        memcpy(deinterlaced + (i * 2) * stride, original + i * stride, stride);
        // Odd row i*2+1 comes from position i in second half
        memcpy(deinterlaced + (i * 2 + 1) * stride, original + (halfHeight + i) * stride, stride);
    }
    convertYUY2toRGB(deinterlaced, rgb, width, height, stride);
    writePPM("/boot/home/Desktop/test2_deinterlace_even_first.ppm", rgb, width, height);

    // Test 3: De-interlace (odd rows first, then even rows)
    printf("Test 3: De-interlaced (odd first, then even)\n");
    for (int i = 0; i < halfHeight; i++) {
        // Odd row i*2+1 comes from position i in first half
        memcpy(deinterlaced + (i * 2 + 1) * stride, original + i * stride, stride);
        // Even row i*2 comes from position i in second half
        memcpy(deinterlaced + (i * 2) * stride, original + (halfHeight + i) * stride, stride);
    }
    convertYUY2toRGB(deinterlaced, rgb, width, height, stride);
    writePPM("/boot/home/Desktop/test3_deinterlace_odd_first.ppm", rgb, width, height);

    // Test 4: Reverse row order (bottom to top)
    printf("Test 4: Reversed row order (bottom to top)\n");
    for (int i = 0; i < height; i++) {
        memcpy(deinterlaced + i * stride, original + (height - 1 - i) * stride, stride);
    }
    convertYUY2toRGB(deinterlaced, rgb, width, height, stride);
    writePPM("/boot/home/Desktop/test4_reversed.ppm", rgb, width, height);

    // Test 5: Try different stride (maybe camera uses 1024 byte rows?)
    printf("Test 5: Stride=1024 (150 rows from 153600 bytes)\n");
    convertYUY2toRGB(original, rgb, width, 150, 1024);
    writePPM("/boot/home/Desktop/test5_stride1024.ppm", rgb, width, 150);

    // Test 6: Analyze where discontinuities occur by looking at row patterns
    printf("\nTest 6: Analyzing row patterns...\n");
    printf("Looking for sudden changes in row content:\n\n");

    int lastAvgY = 0;
    for (int row = 0; row < height; row++) {
        uint8_t* rowData = original + row * stride;
        int avgY = (rowData[0] + rowData[2] + rowData[4] + rowData[6] +
                   rowData[stride-8] + rowData[stride-6] + rowData[stride-4] + rowData[stride-2]) / 8;

        if (row > 0 && abs(avgY - lastAvgY) > 30) {
            printf("Row %3d: avgY=%3d (jump of %+d from row %d)\n",
                   row, avgY, avgY - lastAvgY, row-1);
        }
        lastAvgY = avgY;
    }

    free(original);
    free(deinterlaced);
    free(rgb);

    printf("\nCheck the PPM files on Desktop to see which looks correct.\n");
    return 0;
}
