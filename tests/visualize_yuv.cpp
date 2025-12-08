// Visualize YUY2 frame as ASCII art to detect alignment issues
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main() {
    FILE* f = fopen("/boot/home/Desktop/frame_dump.yuv", "rb");
    if (!f) {
        printf("No frame_dump.yuv found\n");
        return 1;
    }

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(fileSize);
    fread(data, 1, fileSize, f);
    fclose(f);

    int width = 320;
    int height = 240;
    int stride = width * 2;  // YUY2: 2 bytes per pixel

    printf("=== YUY2 Frame Visualization ===\n");
    printf("Size: %dx%d, stride=%d, fileSize=%zu\n\n", width, height, stride, fileSize);

    // Show brightness as ASCII characters
    // Sample every 4 pixels horizontally, every 4 rows vertically
    const char* chars = " .:-=+*#%@";  // 10 levels of brightness

    printf("ASCII visualization (each char = 4x4 pixels):\n");
    printf("   ");
    for (int x = 0; x < width; x += 8) {
        printf("%d", (x / 10) % 10);
    }
    printf("\n");

    for (int y = 0; y < height; y += 4) {
        printf("%3d ", y);
        for (int x = 0; x < width; x += 8) {
            int offset = y * stride + x * 2;
            if (offset < (int)fileSize) {
                int Y = data[offset];  // Y0 value
                int charIdx = Y * 9 / 255;  // Map 0-255 to 0-9
                if (charIdx < 0) charIdx = 0;
                if (charIdx > 9) charIdx = 9;
                printf("%c", chars[charIdx]);
            }
        }
        printf("\n");
    }

    printf("\n=== Checking for horizontal patterns ===\n");

    // If image is shifted, we'd see diagonal lines where there should be vertical ones
    // Check if brightness changes happen at consistent horizontal positions

    printf("\nRow-by-row first edge position (where Y drops below 200):\n");
    for (int y = 0; y < height; y += 10) {
        int offset = y * stride;
        int edgeX = -1;

        for (int x = 0; x < width && edgeX < 0; x++) {
            int Y = data[offset + x * 2];
            if (Y < 200) {
                edgeX = x;
            }
        }

        printf("Row %3d: edge at x=%d", y, edgeX);
        if (edgeX >= 0) {
            // Print visual indicator
            printf(" |");
            for (int i = 0; i < edgeX / 4 && i < 40; i++) printf(" ");
            printf("*");
        }
        printf("\n");
    }

    printf("\n=== Checking diagonal shift pattern ===\n");
    printf("If there's a cumulative shift, edges will form a diagonal line\n\n");

    // Find the first dark pixel in each row and see if it shifts progressively
    int lastEdge = -1;
    int shiftSum = 0;
    int shiftCount = 0;

    for (int y = 0; y < height; y++) {
        int offset = y * stride;
        int edgeX = -1;

        for (int x = 0; x < width && edgeX < 0; x++) {
            int Y = data[offset + x * 2];
            if (Y < 150) {  // Dark pixel
                edgeX = x;
            }
        }

        if (edgeX >= 0 && lastEdge >= 0) {
            int shift = edgeX - lastEdge;
            if (shift != 0) {
                shiftSum += shift;
                shiftCount++;
                if (shiftCount <= 20) {
                    printf("Row %3d: edge shifted %+d pixels (x=%d -> x=%d)\n",
                           y, shift, lastEdge, edgeX);
                }
            }
        }
        lastEdge = edgeX;
    }

    if (shiftCount > 0) {
        printf("\nTotal shifts detected: %d, average: %.2f pixels/row\n",
               shiftCount, (float)shiftSum / shiftCount);
    } else {
        printf("\nNo significant shifts detected - edges are vertically aligned\n");
    }

    free(data);
    return 0;
}
