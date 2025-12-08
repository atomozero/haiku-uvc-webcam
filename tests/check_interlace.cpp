// Check if YUY2 data appears to be interlaced
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

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
    int stride = width * 2;

    printf("=== Interlace Detection ===\n\n");

    // Hypothesis 1: Data is stored as two fields (even rows, then odd rows)
    // If true: bytes 0-76799 = even rows (0,2,4...), bytes 76800-153599 = odd rows (1,3,5...)

    printf("Test 1: Field-based interlace (even field first, then odd field)\n");
    printf("If interlaced, first half should be rows 0,2,4... and second half rows 1,3,5...\n\n");

    int halfSize = fileSize / 2;
    int halfHeight = height / 2;

    // Compare row 0 with what would be row 1 in field-based layout
    // In field layout: row 0 data at offset 0, row 1 data at offset halfSize

    printf("Comparing row patterns:\n");
    printf("Offset 0 (should be row 0):       ");
    for (int i = 0; i < 8; i++) printf("%3d ", data[i]);
    printf("\n");

    printf("Offset %d (field2 row 0 = actual row 1?): ", halfSize);
    for (int i = 0; i < 8; i++) printf("%3d ", data[halfSize + i]);
    printf("\n");

    printf("Offset %d (should be row 1 if progressive): ", stride);
    for (int i = 0; i < 8; i++) printf("%3d ", data[stride + i]);
    printf("\n\n");

    // Test 2: Check if rows 0 and 2 are more similar than rows 0 and 1
    // In interlaced content, even rows should be similar to each other

    printf("Test 2: Row similarity analysis\n");
    printf("In interlaced video, row 0 should be similar to row 2, not row 1\n\n");

    int diff_0_1 = 0, diff_0_2 = 0, diff_1_2 = 0;
    for (int x = 0; x < stride; x++) {
        diff_0_1 += abs((int)data[x] - (int)data[stride + x]);
        diff_0_2 += abs((int)data[x] - (int)data[2*stride + x]);
        diff_1_2 += abs((int)data[stride + x] - (int)data[2*stride + x]);
    }

    printf("Row 0 vs Row 1 difference: %d (avg %.1f per byte)\n", diff_0_1, diff_0_1/(float)stride);
    printf("Row 0 vs Row 2 difference: %d (avg %.1f per byte)\n", diff_0_2, diff_0_2/(float)stride);
    printf("Row 1 vs Row 2 difference: %d (avg %.1f per byte)\n", diff_1_2, diff_1_2/(float)stride);

    if (diff_0_2 < diff_0_1 * 0.5) {
        printf("-> Row 0 is MORE similar to row 2 than row 1 - suggests interlacing!\n");
    } else {
        printf("-> No strong evidence of field-based interlacing\n");
    }

    // Test 3: Try to deinterlace and see if it makes sense
    printf("\n=== Test 3: Attempting de-interlace ===\n");
    printf("Assuming first half = even rows, second half = odd rows\n\n");

    uint8_t* deinterlaced = (uint8_t*)malloc(fileSize);

    // Interpretation: first 120 rows in file = rows 0,2,4,...238
    //                 second 120 rows in file = rows 1,3,5,...239
    for (int i = 0; i < halfHeight; i++) {
        // Even row (2*i) comes from first half, position i
        memcpy(deinterlaced + (2*i) * stride, data + i * stride, stride);
        // Odd row (2*i+1) comes from second half, position i
        memcpy(deinterlaced + (2*i+1) * stride, data + halfSize + i * stride, stride);
    }

    // Save deinterlaced YUV
    FILE* out = fopen("/boot/home/Desktop/frame_deinterlaced.yuv", "wb");
    if (out) {
        fwrite(deinterlaced, 1, fileSize, out);
        fclose(out);
        printf("Saved deinterlaced frame to /boot/home/Desktop/frame_deinterlaced.yuv\n");
    }

    // Convert to PPM for viewing
    FILE* ppm = fopen("/boot/home/Desktop/frame_deinterlaced.ppm", "wb");
    if (ppm) {
        fprintf(ppm, "P6\n%d %d\n255\n", width, height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x += 2) {
                uint8_t* p = deinterlaced + y * stride + x * 2;
                uint8_t y0 = p[0], u = p[1], y1 = p[2], v = p[3];

                // YUV to RGB
                int c0 = y0 - 16, c1 = y1 - 16;
                int d = u - 128, e = v - 128;

                #define CLAMP(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

                // Pixel 0
                int r = CLAMP((298*c0 + 409*e + 128) >> 8);
                int g = CLAMP((298*c0 - 100*d - 208*e + 128) >> 8);
                int b = CLAMP((298*c0 + 516*d + 128) >> 8);
                fputc(r, ppm); fputc(g, ppm); fputc(b, ppm);

                // Pixel 1
                r = CLAMP((298*c1 + 409*e + 128) >> 8);
                g = CLAMP((298*c1 - 100*d - 208*e + 128) >> 8);
                b = CLAMP((298*c1 + 516*d + 128) >> 8);
                fputc(r, ppm); fputc(g, ppm); fputc(b, ppm);
            }
        }
        fclose(ppm);
        printf("Saved deinterlaced PPM to /boot/home/Desktop/frame_deinterlaced.ppm\n");
    }

    free(deinterlaced);
    free(data);

    printf("\nPlease check frame_deinterlaced.ppm - if it looks correct, the camera sends interlaced data!\n");

    return 0;
}
