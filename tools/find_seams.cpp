// Find horizontal discontinuities (seams) in YUY2 frame data
// These indicate misalignment between USB packets and image rows
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

int main() {
    int width = 320, height = 240;
    int rowBytes = width * 2;  // 640

    FILE* f = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    if (!f) { perror("open"); return 1; }
    size_t total = width * height * 2;
    uint8_t* data = (uint8_t*)malloc(total);
    fread(data, 1, total, f);
    fclose(f);

    // 1. Scan for horizontal seams: places where adjacent pixels on the same
    //    row have a larger jump than adjacent pixels on neighboring rows
    printf("=== Scanning for horizontal seams (Y value jumps within rows) ===\n");
    int seamCount = 0;
    for (int row = 0; row < height; row++) {
        int maxJump = 0;
        int maxJumpCol = 0;
        for (int col = 2; col < width; col += 2) {
            int offset = row * rowBytes + col * 2;
            if (offset + 2 >= (int)total) break;
            int y0 = data[offset - 2]; // prev pixel Y
            int y1 = data[offset];     // this pixel Y
            int jump = abs(y1 - y0);
            if (jump > maxJump) {
                maxJump = jump;
                maxJumpCol = col;
            }
        }
        // Report rows with significant internal jumps
        if (maxJump > 50) {
            int bytePos = row * rowBytes + maxJumpCol * 2;
            printf("  Row %3d col %3d (byte %6d): Y jump=%d",
                row, maxJumpCol, bytePos, maxJump);
            // Check if this aligns with a packet boundary
            int pktPayload = 788;
            for (int pkt = 1; pkt < 200; pkt++) {
                int pktBoundary = pkt * pktPayload;
                if (abs(bytePos - pktBoundary) < 4) {
                    printf("  <-- PACKET BOUNDARY (pkt %d)", pkt);
                    break;
                }
            }
            printf("\n");
            seamCount++;
        }
    }
    printf("Total rows with Y jump > 50: %d\n\n", seamCount);

    // 2. Try different starting offsets to see which one eliminates seams
    printf("=== Testing alignment offsets (lower score = better alignment) ===\n");
    for (int testOffset = 0; testOffset < 800; testOffset += 4) {
        double totalScore = 0;
        int rowCount = 0;
        // Treat data starting at testOffset as row-major 320x240
        for (int row = 0; row < height - 1 && (size_t)(testOffset + (row + 1) * rowBytes + 16) < total; row++) {
            // Compare adjacent rows (vertically adjacent pixels should be similar)
            int base = testOffset + row * rowBytes;
            int next = base + rowBytes;
            double rowDiff = 0;
            for (int i = 0; i < rowBytes; i += 2) {
                rowDiff += abs((int)data[base + i] - (int)data[next + i]);
            }
            totalScore += rowDiff / (rowBytes / 2);
            rowCount++;
        }
        if (rowCount > 0) {
            double avgScore = totalScore / rowCount;
            // Only print interesting offsets
            if (testOffset == 0 || testOffset % 100 == 0 || avgScore < 1.5) {
                printf("  Offset %4d: avg row-to-row Y diff = %.3f%s\n",
                    testOffset, avgScore,
                    avgScore < 1.5 ? " *** GOOD" : "");
            }
        }
    }

    // 3. Check specifically for packet-boundary related offsets
    printf("\n=== Packet-boundary offset test (788 byte payload) ===\n");
    int pktPayload = 788;
    for (int nPkts = 0; nPkts <= 5; nPkts++) {
        int testOffset = (nPkts * pktPayload) % rowBytes;
        double totalScore = 0;
        int rowCount = 0;
        for (int row = 0; row < height - 1 && (size_t)(testOffset + (row + 1) * rowBytes + 16) < total; row++) {
            int base = testOffset + row * rowBytes;
            int next = base + rowBytes;
            if ((size_t)(next + rowBytes) > total) break;
            double rowDiff = 0;
            for (int i = 0; i < rowBytes; i += 2) {
                rowDiff += abs((int)data[base + i] - (int)data[next + i]);
            }
            totalScore += rowDiff / (rowBytes / 2);
            rowCount++;
        }
        if (rowCount > 0)
            printf("  %d lost pkts (offset %d bytes = %.1f pixels): score=%.3f\n",
                nPkts, testOffset, testOffset / 2.0, totalScore / rowCount);
    }

    free(data);
    return 0;
}
