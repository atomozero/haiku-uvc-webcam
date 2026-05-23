// Try different data layouts for the YUY2 frame
// Goal: find the correct interpretation of the camera's internal tiling
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

static uint8_t clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void yuy2_pixel_to_rgb(int y, int u, int v, uint8_t* r, uint8_t* g, uint8_t* b) {
    int c = y - 16, d = u - 128, e = v - 128;
    *r = clamp((298*c + 409*e + 128) >> 8);
    *g = clamp((298*c - 100*d - 208*e + 128) >> 8);
    *b = clamp((298*c + 516*d + 128) >> 8);
}

static void write_bmp(const char* path, uint8_t* rgb, int w, int h) {
    int rowPad = (4 - (w * 3 % 4)) % 4;
    int bmpRow = w * 3 + rowPad;
    uint32_t ds = bmpRow * h;
    uint32_t fs = 54 + ds;
    uint8_t hdr[54] = {};
    hdr[0]='B'; hdr[1]='M';
    memcpy(&hdr[2], &fs, 4);
    uint32_t off=54; memcpy(&hdr[10], &off, 4);
    uint32_t is=40; memcpy(&hdr[14], &is, 4);
    memcpy(&hdr[18], &w, 4);
    int32_t nh = -h; memcpy(&hdr[22], &nh, 4);
    uint16_t p=1; memcpy(&hdr[26], &p, 2);
    uint16_t b=24; memcpy(&hdr[28], &b, 2);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(hdr, 1, 54, f);
    uint8_t pad[4]={};
    for (int row = 0; row < h; row++) {
        fwrite(rgb + row * w * 3, 1, w * 3, f);
        if (rowPad) fwrite(pad, 1, rowPad, f);
    }
    fclose(f);
    printf("  Saved %s\n", path);
}

// Get YUY2 pixel at (x,y) with given stride (bytes per row in source)
static void get_yuy2_pixel(const uint8_t* src, size_t srcSize, int x, int y, int srcStride,
    uint8_t* r, uint8_t* g, uint8_t* b)
{
    int offset = y * srcStride + (x & ~1) * 2;
    if (offset + 4 > (int)srcSize) { *r=*g=*b=0; return; }
    int y0 = src[offset], u = src[offset+1], y1 = src[offset+2], v = src[offset+3];
    int yVal = (x & 1) ? y1 : y0;
    yuy2_pixel_to_rgb(yVal, u, v, r, g, b);
}

int main() {
    int W = 320, H = 240;

    FILE* f = fopen("/boot/home/Desktop/frame_raw_yuy2.raw", "rb");
    if (!f) { perror("open"); return 1; }
    size_t total = W * H * 2;
    uint8_t* src = (uint8_t*)malloc(total);
    fread(src, 1, total, f);
    fclose(f);

    uint8_t* rgb = (uint8_t*)malloc(W * H * 3);

    // Layout 1: De-interlaced (even rows first, then odd rows)
    printf("Layout 1: De-interlaced\n");
    {
        uint8_t* deint = (uint8_t*)malloc(total);
        for (int row = 0; row < H; row++) {
            int srcRow;
            if (row < H/2)
                srcRow = row * 2;      // first half = even rows
            else
                srcRow = (row - H/2) * 2 + 1; // second half = odd rows
            memcpy(deint + srcRow * W * 2, src + row * W * 2, W * 2);
        }
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t r, g, b;
                get_yuy2_pixel(deint, total, x, y, W*2, &r, &g, &b);
                rgb[(y*W+x)*3+0] = b; rgb[(y*W+x)*3+1] = g; rgb[(y*W+x)*3+2] = r;
            }
        write_bmp("/boot/home/Desktop/layout_deinterlaced.bmp", rgb, W, H);
        free(deint);
    }

    // Layout 2: Tiled 160x120 (2x2 tiles)
    printf("Layout 2: Tiled 160x120\n");
    {
        int tileW = 160, tileH = 120;
        int tilesX = W / tileW; // 2
        int tilesY = H / tileH; // 2
        int tileBytes = tileW * 2 * tileH;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int tx = x / tileW, ty = y / tileH;
                int tileIdx = ty * tilesX + tx;
                int localX = x % tileW, localY = y % tileH;
                int srcOffset = tileIdx * tileBytes + localY * tileW * 2 + (localX & ~1) * 2;
                if (srcOffset + 4 > (int)total) { rgb[(y*W+x)*3]=rgb[(y*W+x)*3+1]=rgb[(y*W+x)*3+2]=0; continue; }
                int y0=src[srcOffset], u=src[srcOffset+1], y1=src[srcOffset+2], v=src[srcOffset+3];
                int yVal = (localX & 1) ? y1 : y0;
                uint8_t r, g, b;
                yuy2_pixel_to_rgb(yVal, u, v, &r, &g, &b);
                rgb[(y*W+x)*3+0] = b; rgb[(y*W+x)*3+1] = g; rgb[(y*W+x)*3+2] = r;
            }
        write_bmp("/boot/home/Desktop/layout_tile160x120.bmp", rgb, W, H);
    }

    // Layout 3: Tiled 80x60 (4x4 tiles)
    printf("Layout 3: Tiled 80x60\n");
    {
        int tileW = 80, tileH = 60;
        int tilesX = W / tileW; // 4
        int tilesY = H / tileH; // 4
        int tileBytes = tileW * 2 * tileH;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int tx = x / tileW, ty = y / tileH;
                int tileIdx = ty * tilesX + tx;
                int localX = x % tileW, localY = y % tileH;
                int srcOffset = tileIdx * tileBytes + localY * tileW * 2 + (localX & ~1) * 2;
                if (srcOffset + 4 > (int)total) { rgb[(y*W+x)*3]=rgb[(y*W+x)*3+1]=rgb[(y*W+x)*3+2]=0; continue; }
                int y0=src[srcOffset], u=src[srcOffset+1], y1=src[srcOffset+2], v=src[srcOffset+3];
                int yVal = (localX & 1) ? y1 : y0;
                uint8_t r, g, b;
                yuy2_pixel_to_rgb(yVal, u, v, &r, &g, &b);
                rgb[(y*W+x)*3+0] = b; rgb[(y*W+x)*3+1] = g; rgb[(y*W+x)*3+2] = r;
            }
        write_bmp("/boot/home/Desktop/layout_tile80x60.bmp", rgb, W, H);
    }

    // Layout 4: Column-interleaved (pixel columns interleaved)
    // Maybe even columns first, then odd columns?
    printf("Layout 4: Rows reversed\n");
    {
        for (int y = 0; y < H; y++) {
            int srcRow = H - 1 - y;
            for (int x = 0; x < W; x++) {
                uint8_t r, g, b;
                get_yuy2_pixel(src, total, x, srcRow, W*2, &r, &g, &b);
                rgb[(y*W+x)*3+0] = b; rgb[(y*W+x)*3+1] = g; rgb[(y*W+x)*3+2] = r;
            }
        }
        write_bmp("/boot/home/Desktop/layout_reversed.bmp", rgb, W, H);
    }

    // Layout 5: NV12/NV21 interpretation (Y plane then UV interleaved)
    printf("Layout 5: NV12 interpretation (Y+UV planar)\n");
    {
        // If the data is actually NV12: first W*H bytes = Y, then W*H/2 bytes = UV
        // Total = W*H*1.5 = 115200, but we have 153600... doesn't match
        // Try anyway for 320x240: Y = 76800, UV = 38400, total = 115200 (not 153600)
        printf("  (Skipped - NV12 size mismatch: 115200 != 153600)\n");
    }

    // Layout 6: Stride 352 (Microdia internal stride)
    // The camera sends 320 valid pixels per row but with 352-pixel stride
    // Total with stride: 352*2*240 = 168960 bytes > 153600
    // NOT possible with our data size. But what if height is smaller?
    // 153600 / (352*2) = 218.18 rows
    printf("Layout 6: Stride 352, 218 rows\n");
    {
        int srcStride = 352 * 2; // 704
        int rows = total / srcStride; // 218
        int outW = 352, outH = rows;
        uint8_t* rgb6 = (uint8_t*)malloc(outW * outH * 3);
        for (int y = 0; y < outH; y++)
            for (int x = 0; x < outW; x++) {
                uint8_t r, g, b;
                get_yuy2_pixel(src, total, x, y, srcStride, &r, &g, &b);
                rgb6[(y*outW+x)*3+0] = b; rgb6[(y*outW+x)*3+1] = g; rgb6[(y*outW+x)*3+2] = r;
            }
        write_bmp("/boot/home/Desktop/layout_stride352.bmp", rgb6, outW, outH);
        free(rgb6);
    }

    // Layout 7: Try 256-pixel stride (power of 2 alignment)
    printf("Layout 7: Stride 256 pixels (512 bytes)\n");
    {
        int srcStride = 256 * 2;
        int rows = total / srcStride;
        int outW = 256, outH = rows;
        uint8_t* rgb7 = (uint8_t*)malloc(outW * outH * 3);
        for (int y = 0; y < outH; y++)
            for (int x = 0; x < outW; x++) {
                uint8_t r, g, b;
                get_yuy2_pixel(src, total, x, y, srcStride, &r, &g, &b);
                rgb7[(y*outW+x)*3+0] = b; rgb7[(y*outW+x)*3+1] = g; rgb7[(y*outW+x)*3+2] = r;
            }
        write_bmp("/boot/home/Desktop/layout_stride256.bmp", rgb7, outW, outH);
        free(rgb7);
    }

    free(src);
    free(rgb);
    printf("\nDone! Check Desktop for layout_*.bmp files\n");
    return 0;
}
