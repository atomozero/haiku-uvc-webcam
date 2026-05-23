#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cmath>

int main() {
    FILE* f = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    if (!f) { perror("open"); return 1; }

    uint8_t data[153600];
    size_t n = fread(data, 1, 153600, f);
    fclose(f);
    printf("Read %zu bytes\n", n);

    // Check for discontinuities at packet boundaries (every 788 bytes of payload)
    // UVC header is 12 bytes, so each USB packet = 12+788=800 bytes
    // In the assembled frame, packet boundaries are at multiples of 788
    int packetPayload = 788;
    printf("\n=== Packet boundary analysis (payload=788) ===\n");
    for (int pkt = 1; pkt < 15; pkt++) {
        int boundary = pkt * packetPayload;
        if (boundary + 4 > (int)n) break;

        int yBefore = data[boundary - 2];
        int yAfter = data[boundary];
        int jump = abs(yAfter - yBefore);

        // Also check what row this falls in
        int row = boundary / 640;
        int col = (boundary % 640) / 2;
        printf("Pkt %2d (byte %5d, row %3d col %3d): Y before=%3d after=%3d jump=%2d%s\n",
            pkt, boundary, row, col, yBefore, yAfter, jump,
            jump > 40 ? " ***" : "");
    }

    // Row consistency
    printf("\n=== Row Y averages ===\n");
    int rowBytes = 640;
    for (int row = 0; row < 20; row++) {
        int offset = row * rowBytes;
        if (offset + 32 > (int)n) break;
        int sum = 0;
        for (int i = 0; i < 32; i += 2) sum += data[offset + i];
        printf("Row %3d: avgY=%3d\n", row, sum / 16);
    }

    // Stride analysis
    printf("\n=== Stride correlation (lower=better match) ===\n");
    int strides[] = {640, 704, 512, 768, 1024};
    for (int s = 0; s < 5; s++) {
        int stride = strides[s];
        double totalDiff = 0;
        int count = 0;
        for (int row = 0; row < 20; row++) {
            int off1 = row * stride;
            int off2 = (row + 1) * stride;
            if (off2 + 32 > (int)n) break;
            for (int i = 0; i < 32; i += 2) {
                totalDiff += abs((int)data[off1 + i] - (int)data[off2 + i]);
                count++;
            }
        }
        if (count > 0)
            printf("  Stride %4d: avg diff = %.2f\n", stride, totalDiff / count);
    }

    return 0;
}
