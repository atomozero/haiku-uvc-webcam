// Search for embedded UVC headers in raw YUY2 frame data
// If USB packets contain multiple UVC sub-packets, their headers
// will appear as corrupted video data in the frame buffer
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

int main() {
    FILE* f = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    if (!f) { perror("open"); return 1; }
    uint8_t data[153600];
    size_t n = fread(data, 1, 153600, f);
    fclose(f);
    printf("Read %zu bytes\n\n", n);

    // Search for UVC header pattern:
    // byte[0] = 0x0C (12 = header length with PTS+SCR)
    // byte[1] = 0x8C or 0x8D (EOH=1, PTS=1, SCR=1, FID=0 or 1)
    // We know PTS was 595601411 = 0x23875A83
    // So bytes 2-5 should be: 0x83, 0x5A, 0x87, 0x23 (little-endian)

    uint8_t ptsBytes[4] = {0x83, 0x5A, 0x87, 0x23};

    printf("=== Searching for PTS value 595601411 (0x23875A83) ===\n");
    for (size_t i = 0; i < n - 6; i++) {
        if (memcmp(&data[i], ptsBytes, 4) == 0) {
            int row = i / 640;
            int col = (i % 640) / 2;
            printf("  Found PTS at byte %zu (row %d, col %d)\n", i, row, col);
            printf("    context: %02x %02x [%02x %02x %02x %02x] %02x %02x %02x %02x\n",
                i >= 2 ? data[i-2] : 0, i >= 1 ? data[i-1] : 0,
                data[i], data[i+1], data[i+2], data[i+3],
                data[i+4], data[i+5],
                i+6 < n ? data[i+6] : 0, i+7 < n ? data[i+7] : 0);
        }
    }

    // Search for 0x0C 0x8C or 0x0C 0x8D pattern (UVC header start)
    printf("\n=== Searching for UVC header signature (0x0C 0x8x) ===\n");
    int headerCount = 0;
    for (size_t i = 0; i < n - 12; i++) {
        if (data[i] == 0x0C && (data[i+1] & 0xFC) == 0x8C) {
            // Check if bytes 2-5 look like a PTS (reasonable timestamp)
            uint32_t pts = data[i+2] | (data[i+3] << 8) | (data[i+4] << 16) | (data[i+5] << 24);
            if (pts > 500000000 && pts < 700000000) { // Reasonable range
                int row = i / 640;
                int col = (i % 640) / 2;
                printf("  Likely UVC header at byte %zu (row %d, col %d) PTS=%u\n",
                    i, row, col, pts);
                printf("    bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    data[i], data[i+1], data[i+2], data[i+3],
                    data[i+4], data[i+5], data[i+6], data[i+7],
                    data[i+8], data[i+9], data[i+10], data[i+11]);
                headerCount++;
            }
        }
    }
    printf("Total likely embedded headers: %d\n", headerCount);

    // Also just look for 0x0C as first byte followed by 0x8x
    printf("\n=== All 0x0C 0x8x patterns ===\n");
    for (size_t i = 0; i < n - 2; i++) {
        if (data[i] == 0x0C && (data[i+1] & 0xF0) == 0x80) {
            int row = i / 640;
            int col = (i % 640) / 2;
            if (headerCount < 50)
                printf("  byte %6zu (row %3d, col %3d): %02x %02x %02x %02x\n",
                    i, row, col, data[i], data[i+1],
                    i+2 < n ? data[i+2] : 0, i+3 < n ? data[i+3] : 0);
            headerCount++;
        }
    }
    printf("Total 0x0C 0x8x patterns: %d (expected ~0 if no embedded headers)\n", headerCount);

    return 0;
}
