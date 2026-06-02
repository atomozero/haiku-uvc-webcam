// Convert RGB32 (BGRA) raw to 24-bit BMP
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

int main() {
    int width = 320, height = 240;

    FILE* fin = fopen("/boot/home/Desktop/frame_rgb32.raw", "rb");
    if (!fin) { perror("open"); return 1; }
    size_t rgb32Size = width * height * 4;
    uint8_t* data = (uint8_t*)malloc(rgb32Size);
    fread(data, 1, rgb32Size, fin);
    fclose(fin);

    // Write 24-bit BMP top-down
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
    int32_t negHeight = -height;
    memcpy(&header[22], &negHeight, 4);
    uint16_t planes = 1;
    memcpy(&header[26], &planes, 2);
    uint16_t bits = 24;
    memcpy(&header[28], &bits, 2);

    FILE* fout = fopen("/boot/home/Desktop/webcam_driver24.bmp", "wb");
    if (!fout) { perror("open out"); return 1; }
    fwrite(header, 1, 54, fout);

    uint8_t pad[4] = {0};
    for (int row = 0; row < height; row++) {
        uint8_t* src = data + row * width * 4;
        uint8_t rowBuf[960]; // 320*3
        for (int x = 0; x < width; x++) {
            rowBuf[x*3+0] = src[x*4+0]; // B
            rowBuf[x*3+1] = src[x*4+1]; // G
            rowBuf[x*3+2] = src[x*4+2]; // R
        }
        fwrite(rowBuf, 1, width * 3, fout);
        if (rowPad > 0) fwrite(pad, 1, rowPad, fout);
    }
    fclose(fout);
    free(data);
    printf("Saved /boot/home/Desktop/webcam_driver24.bmp\n");
    return 0;
}
