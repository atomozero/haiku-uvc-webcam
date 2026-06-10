// Analyze YUY2 frame data for embedded UVC headers
// If headers are found inside the data, it means header stripping is failing
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main() {
    FILE* f = fopen("/boot/home/Desktop/frame_dump.yuv", "rb");
    if (!f) {
        printf("No frame_dump.yuv found - start CodyCam with YUY2 first\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(fileSize);
    fread(data, 1, fileSize, f);
    fclose(f);

    printf("=== UVC Header Analysis ===\n");
    printf("File size: %zu bytes\n\n", fileSize);

    // UVC payload header format:
    // Byte 0: bHeaderLength (2-12 typically)
    // Byte 1: bmHeaderInfo (bit 7=EOH, bit 6=ERR, bit 3=SCR, bit 2=PTS, bit 1=EOF, bit 0=FID)
    //
    // Common header patterns:
    // 0x02 0x8x - 2 byte header (no PTS/SCR)
    // 0x0c 0x8x - 12 byte header (with PTS and SCR)

    printf("=== Searching for UVC header patterns ===\n");
    int headerCount = 0;

    for (size_t i = 0; i < fileSize - 12; i++) {
        uint8_t len = data[i];
        uint8_t flags = data[i + 1];

        // Check for valid UVC header:
        // - Length is 2, 6, 8, or 12 (common sizes)
        // - Flags byte has EOH bit set (0x80)
        // - FID bit is 0 or 1
        bool validLength = (len == 2 || len == 6 || len == 8 || len == 12);
        bool hasEOH = (flags & 0x80) != 0;
        bool validFlags = (flags & 0x30) == 0;  // Reserved bits should be 0

        // Additional check: if PTS bit set, len should be >= 6
        // If SCR bit set, len should be >= 8 (or 12 with PTS)
        bool ptsValid = !(flags & 0x04) || (len >= 6);
        bool scrValid = !(flags & 0x08) || (len >= 8);

        if (validLength && hasEOH && validFlags && ptsValid && scrValid) {
            int row = i / 640;
            int col = (i % 640) / 2;

            if (headerCount < 20) {
                printf("Possible UVC header at offset %5zu (row %3d, col %3d): ", i, row, col);
                printf("len=%d flags=0x%02x", len, flags);

                // Decode flags
                printf(" [");
                if (flags & 0x01) printf("FID ");
                if (flags & 0x02) printf("EOF ");
                if (flags & 0x04) printf("PTS ");
                if (flags & 0x08) printf("SCR ");
                if (flags & 0x40) printf("ERR ");
                printf("]");

                // Show next few bytes
                printf(" data: ");
                for (int j = 0; j < 16 && i + j < fileSize; j++) {
                    printf("%02x ", data[i + j]);
                }
                printf("\n");
            }
            headerCount++;
        }
    }

    if (headerCount == 0) {
        printf("No UVC headers found in frame data - header stripping is working\n");
    } else {
        printf("\nTotal: %d possible UVC headers found\n", headerCount);
        printf("WARNING: Headers in frame data indicate stripping may be failing!\n");
    }

    // Also check row alignment by looking at row boundaries
    printf("\n=== Row Boundary Analysis ===\n");
    printf("Checking if rows start at expected positions (stride=640)...\n\n");

    int stride = 640;
    int height = fileSize / stride;

    // Sample some rows and check for discontinuities
    printf("First 4 bytes of each row (sample):\n");
    for (int row = 0; row < height && row < 20; row++) {
        int offset = row * stride;
        printf("Row %3d (offset %6d): Y0=%3d U=%3d Y1=%3d V=%3d",
               row, offset,
               data[offset], data[offset+1], data[offset+2], data[offset+3]);

        // Check if this looks like YUY2 data (U and V should be around 128 for gray)
        // High Y values (>200) with U/V near 128 = bright
        // Low Y values (<50) with U/V near 128 = dark
        int u = data[offset + 1];
        int v = data[offset + 3];
        if (u < 100 || u > 156 || v < 100 || v > 156) {
            printf(" <- unusual U/V (not gray)");
        }
        printf("\n");
    }

    // Calculate cumulative offset error
    printf("\n=== Cumulative Shift Detection ===\n");
    printf("Looking for progressive row shifts...\n\n");

    // If data is shifted, each row will appear offset from where it should be
    // We can detect this by looking for repeating patterns at wrong positions

    // Take a pattern from row 0 and look for similar patterns in other rows
    // If there's a cumulative shift, the pattern will appear at increasing offsets

    int patternLen = 8;
    uint8_t* pattern = &data[0];  // First 8 bytes of row 0

    printf("Looking for pattern from row 0: ");
    for (int i = 0; i < patternLen; i++) printf("%02x ", pattern[i]);
    printf("\n\n");

    for (int row = 0; row < 20; row++) {
        int expectedOffset = row * stride;

        // Search around expected position for the pattern
        int bestMatch = -1;
        int bestScore = 0;

        for (int testOffset = expectedOffset - 50; testOffset <= expectedOffset + 50; testOffset++) {
            if (testOffset < 0 || testOffset + patternLen > (int)fileSize) continue;

            // Calculate similarity to row 0 pattern (assuming similar rows)
            // Actually, let's look for YUY2 structure instead
        }
    }

    // Simpler approach: check if adjacent pixels have expected YUY2 structure
    printf("Checking YUY2 byte order consistency:\n");
    int orderErrors = 0;
    for (size_t i = 0; i < fileSize - 4; i += 4) {
        // In YUY2: Y0 U Y1 V
        // Y values vary widely, U/V tend to cluster around 128
        int y0 = data[i], u = data[i+1], y1 = data[i+2], v = data[i+3];

        // If bytes are shifted, we might see Y values where U/V should be
        // U and V typically have lower variance than Y
        // This is a heuristic check
    }

    free(data);
    return 0;
}
