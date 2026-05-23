// Direct YUY2 to BMP converter (independent from driver code)
// Standard YUY2/YUYV: byte order is Y0 U0 Y1 V0 per 2 pixels
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <algorithm>

static uint8_t clamp(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

int main(int argc, char* argv[]) {
    int width = 320, height = 240;
    if (argc > 2) { width = atoi(argv[1]); height = atoi(argv[2]); }

    FILE* fin = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    if (!fin) { perror("open yuy2"); return 1; }

    size_t yuy2Size = width * height * 2;
    uint8_t* yuy2 = (uint8_t*)malloc(yuy2Size);
    size_t n = fread(yuy2, 1, yuy2Size, fin);
    fclose(fin);
    printf("Read %zu YUY2 bytes (expected %zu)\n", n, yuy2Size);

    // Convert YUY2 to BGR (BMP format)
    size_t rgbSize = width * height * 3;
    uint8_t* rgb = (uint8_t*)malloc(rgbSize);

    for (int row = 0; row < height; row++) {
        uint8_t* srcRow = yuy2 + row * width * 2;
        uint8_t* dstRow = rgb + row * width * 3;

        for (int x = 0; x < width; x += 2) {
            int y0 = srcRow[0];
            int u  = srcRow[1];
            int y1 = srcRow[2];
            int v  = srcRow[3];
            srcRow += 4;

            // Standard YUV to RGB conversion (BT.601)
            int c0 = y0 - 16;
            int c1 = y1 - 16;
            int d = u - 128;
            int e = v - 128;

            // Pixel 0 (BGR for BMP)
            dstRow[0] = clamp((298 * c0 + 516 * d + 128) >> 8);            // B
            dstRow[1] = clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8);  // G
            dstRow[2] = clamp((298 * c0 + 409 * e + 128) >> 8);            // R

            // Pixel 1 (BGR for BMP)
            dstRow[3] = clamp((298 * c1 + 516 * d + 128) >> 8);            // B
            dstRow[4] = clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8);  // G
            dstRow[5] = clamp((298 * c1 + 409 * e + 128) >> 8);            // R
            dstRow += 6;
        }
    }

    // Write BMP (24-bit, top-down)
    int rowPad = (4 - (width * 3 % 4)) % 4;
    int bmpRowSize = width * 3 + rowPad;
    uint32_t dataSize = bmpRowSize * height;
    uint32_t fileSize = 54 + dataSize;

    uint8_t header[54];
    memset(header, 0, 54);
    header[0] = 'B'; header[1] = 'M';
    memcpy(&header[2], &fileSize, 4);
    uint32_t offset = 54;
    memcpy(&header[10], &offset, 4);
    uint32_t infoSize = 40;
    memcpy(&header[14], &infoSize, 4);
    memcpy(&header[18], &width, 4);
    int32_t negHeight = -height; // top-down
    memcpy(&header[22], &negHeight, 4);
    uint16_t planes = 1;
    memcpy(&header[26], &planes, 2);
    uint16_t bits = 24;
    memcpy(&header[28], &bits, 2);

    FILE* fout = fopen("/boot/home/Desktop/webcam_direct.bmp", "wb");
    if (!fout) { perror("open bmp"); return 1; }
    fwrite(header, 1, 54, fout);

    uint8_t pad[4] = {0};
    for (int row = 0; row < height; row++) {
        fwrite(rgb + row * width * 3, 1, width * 3, fout);
        if (rowPad > 0) fwrite(pad, 1, rowPad, fout);
    }
    fclose(fout);
    free(rgb);
    free(yuy2);

    printf("Saved /boot/home/Desktop/webcam_direct.bmp (%dx%d, 24-bit)\n", width, height);

    // Also try with stride 704 (Microdia quirk)
    fin = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    yuy2 = (uint8_t*)malloc(yuy2Size);
    fread(yuy2, 1, yuy2Size, fin);
    fclose(fin);

    // With 704-byte stride, effective height is 153600/704 = 218 rows
    int quirkyHeight = yuy2Size / 704;
    int quirkyWidth = 352;
    printf("Also trying stride 704: effective %dx%d\n", quirkyWidth, quirkyHeight);

    rgbSize = quirkyWidth * quirkyHeight * 3;
    rgb = (uint8_t*)malloc(rgbSize);
    for (int row = 0; row < quirkyHeight; row++) {
        uint8_t* srcRow = yuy2 + row * 704;
        uint8_t* dstRow = rgb + row * quirkyWidth * 3;
        for (int x = 0; x < quirkyWidth; x += 2) {
            if ((size_t)(srcRow - yuy2 + 4) > yuy2Size) break;
            int y0 = srcRow[0], u = srcRow[1], y1 = srcRow[2], v = srcRow[3];
            srcRow += 4;
            int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;
            dstRow[0] = clamp((298*c0 + 516*d + 128) >> 8);
            dstRow[1] = clamp((298*c0 - 100*d - 208*e + 128) >> 8);
            dstRow[2] = clamp((298*c0 + 409*e + 128) >> 8);
            dstRow[3] = clamp((298*c1 + 516*d + 128) >> 8);
            dstRow[4] = clamp((298*c1 - 100*d - 208*e + 128) >> 8);
            dstRow[5] = clamp((298*c1 + 409*e + 128) >> 8);
            dstRow += 6;
        }
    }

    rowPad = (4 - (quirkyWidth * 3 % 4)) % 4;
    bmpRowSize = quirkyWidth * 3 + rowPad;
    dataSize = bmpRowSize * quirkyHeight;
    fileSize = 54 + dataSize;
    memset(header, 0, 54);
    header[0] = 'B'; header[1] = 'M';
    memcpy(&header[2], &fileSize, 4);
    memcpy(&header[10], &offset, 4);
    memcpy(&header[14], &infoSize, 4);
    memcpy(&header[18], &quirkyWidth, 4);
    negHeight = -quirkyHeight;
    memcpy(&header[22], &negHeight, 4);
    memcpy(&header[26], &planes, 2);
    memcpy(&header[28], &bits, 2);

    fout = fopen("/boot/home/Desktop/webcam_stride704.bmp", "wb");
    if (!fout) { perror("open bmp2"); return 1; }
    fwrite(header, 1, 54, fout);
    for (int row = 0; row < quirkyHeight; row++) {
        fwrite(rgb + row * quirkyWidth * 3, 1, quirkyWidth * 3, fout);
        if (rowPad > 0) fwrite(pad, 1, rowPad, fout);
    }
    fclose(fout);
    free(rgb);
    free(yuy2);
    printf("Saved /boot/home/Desktop/webcam_stride704.bmp (%dx%d)\n", quirkyWidth, quirkyHeight);

    return 0;
}
