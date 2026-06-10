// Simple YUY2 to PPM converter for debugging
// Compile: g++ -o convert_yuv convert_yuv.cpp
// Usage: ./convert_yuv input.yuv output.ppm width height

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static inline uint8_t clamp(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s input.yuv output.ppm width height [srcStride]\n", argv[0]);
        fprintf(stderr, "  srcStride: override source stride (default: width*2)\n");
        return 1;
    }

    const char* inputFile = argv[1];
    const char* outputFile = argv[2];
    int width = atoi(argv[3]);
    int height = atoi(argv[4]);
    int customStride = (argc > 5) ? atoi(argv[5]) : 0;

    size_t yuvSize = 153600; // Fixed size from actual file
    size_t rgbSize = width * height * 3;

    uint8_t* yuv = (uint8_t*)malloc(yuvSize);
    uint8_t* rgb = (uint8_t*)malloc(rgbSize);

    FILE* fin = fopen(inputFile, "rb");
    if (!fin) {
        perror("Cannot open input file");
        return 1;
    }

    size_t read = fread(yuv, 1, yuvSize, fin);
    fclose(fin);

    printf("Read %zu bytes (expected %zu)\n", read, yuvSize);

    // Convert YUY2 to RGB
    int srcStride = customStride > 0 ? customStride : width * 2;
    int dstStride = width * 3;
    int actualHeight = yuvSize / srcStride;

    printf("Using srcStride=%d (actual height=%d rows from %zu bytes)\n", srcStride, actualHeight, yuvSize);
    if (actualHeight < height) {
        printf("Warning: reducing height from %d to %d\n", height, actualHeight);
        height = actualHeight;
    }

    for (int row = 0; row < height; row++) {
        uint8_t* srcRow = yuv + row * srcStride;
        uint8_t* dstRow = rgb + row * dstStride;

        for (int x = 0; x < width; x += 2) {
            int y0 = srcRow[0];
            int u  = srcRow[1];
            int y1 = srcRow[2];
            int v  = srcRow[3];
            srcRow += 4;

            // BT.601 conversion
            int c0 = 298 * (y0 - 16);
            int c1 = 298 * (y1 - 16);
            int d = u - 128;
            int e = v - 128;

            // Pixel 0: RGB
            dstRow[0] = clamp((c0 + 409 * e + 128) >> 8);           // R
            dstRow[1] = clamp((c0 - 100 * d - 208 * e + 128) >> 8); // G
            dstRow[2] = clamp((c0 + 516 * d + 128) >> 8);           // B

            // Pixel 1: RGB
            dstRow[3] = clamp((c1 + 409 * e + 128) >> 8);           // R
            dstRow[4] = clamp((c1 - 100 * d - 208 * e + 128) >> 8); // G
            dstRow[5] = clamp((c1 + 516 * d + 128) >> 8);           // B

            dstRow += 6;
        }
    }

    // Write PPM
    FILE* fout = fopen(outputFile, "wb");
    if (!fout) {
        perror("Cannot open output file");
        return 1;
    }

    fprintf(fout, "P6\n%d %d\n255\n", width, height);
    fwrite(rgb, 1, rgbSize, fout);
    fclose(fout);

    printf("Converted %dx%d YUY2 to PPM: %s\n", width, height, outputFile);

    free(yuv);
    free(rgb);
    return 0;
}
