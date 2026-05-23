// Analyze raw USB diagnostic data captured by CamDevice
// Reconstructs YUY2 frame from raw packets (stripping UVC headers)
// and compares with deframer output to find where tearing originates
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

static uint8_t clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void write_bmp(const char* path, const uint8_t* yuy2, size_t yuy2Size, int w, int h) {
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
    uint8_t pad[4]={}, rowBuf[4096];
    for (int row = 0; row < h; row++) {
        const uint8_t* src = yuy2 + row * w * 2;
        if ((size_t)(row * w * 2 + w * 2) > yuy2Size) break;
        for (int x = 0; x < w; x += 2) {
            int y0=src[0], u=src[1], y1=src[2], v=src[3]; src+=4;
            int c0=y0-16, c1=y1-16, d=u-128, e=v-128;
            rowBuf[x*3+0]=clamp((298*c0+516*d+128)>>8);
            rowBuf[x*3+1]=clamp((298*c0-100*d-208*e+128)>>8);
            rowBuf[x*3+2]=clamp((298*c0+409*e+128)>>8);
            rowBuf[(x+1)*3+0]=clamp((298*c1+516*d+128)>>8);
            rowBuf[(x+1)*3+1]=clamp((298*c1-100*d-208*e+128)>>8);
            rowBuf[(x+1)*3+2]=clamp((298*c1+409*e+128)>>8);
        }
        fwrite(rowBuf, 1, w * 3, f);
        if (rowPad) fwrite(pad, 1, rowPad, f);
    }
    fclose(f);
    printf("  Saved %s (%dx%d)\n", path, w, h);
}

int main() {
    // Read the diagnostic binary file
    FILE* f = fopen("/boot/home/Desktop/usb_raw_diag.bin", "rb");
    if (!f) {
        printf("ERROR: /boot/home/Desktop/usb_raw_diag.bin not found.\n");
        printf("Run the driver with CodyCam first to capture diagnostic data.\n");
        return 1;
    }

    // Frame buffer for reconstructed YUY2 (max 640x480)
    const int MAX_FRAME = 640 * 480 * 2;
    uint8_t* frameBuffer = (uint8_t*)calloc(1, MAX_FRAME);
    size_t framePos = 0;

    int transferCount = 0;
    int totalPackets = 0;
    int totalPayloadBytes = 0;
    int ptsChanges = 0;
    uint32_t lastPTS = 0;
    bool hasPTS = false;

    printf("=== Analyzing raw USB diagnostic data ===\n\n");

    while (!feof(f)) {
        // Read transfer header
        uint32_t hdr[3];
        if (fread(hdr, sizeof(uint32_t), 3, f) != 3) break;

        uint32_t transferIdx = hdr[0];
        uint32_t numPackets = hdr[1];
        uint32_t slotSize = hdr[2];

        if (numPackets > 64 || slotSize > 8192) {
            printf("ERROR: Invalid header (transfer=%u packets=%u slotSize=%u)\n",
                transferIdx, numPackets, slotSize);
            break;
        }

        // Read packet descriptors
        int16_t* pktActual = (int16_t*)malloc(numPackets * sizeof(int16_t));
        int16_t* pktStatus = (int16_t*)malloc(numPackets * sizeof(int16_t));
        for (uint32_t i = 0; i < numPackets; i++) {
            fread(&pktActual[i], sizeof(int16_t), 1, f);
            fread(&pktStatus[i], sizeof(int16_t), 1, f);
        }

        // Read raw buffer
        size_t bufSize = slotSize * numPackets;
        uint8_t* rawBuf = (uint8_t*)malloc(bufSize);
        if (fread(rawBuf, 1, bufSize, f) != bufSize) {
            printf("ERROR: Short read on transfer %u buffer\n", transferIdx);
            free(pktActual); free(pktStatus); free(rawBuf);
            break;
        }

        printf("Transfer #%u: %u packets, slotSize=%u\n", transferIdx, numPackets, slotSize);

        int dataPackets = 0;
        int emptyPackets = 0;

        for (uint32_t i = 0; i < numPackets; i++) {
            int al = pktActual[i];
            int st = pktStatus[i];
            size_t offset = i * slotSize;

            if (st != 0 || al <= 0) {
                emptyPackets++;
                continue;
            }

            // Parse UVC header
            uint8_t* pkt = rawBuf + offset;
            int hdrLen = pkt[0];
            int flags = pkt[1];

            if (hdrLen < 2 || hdrLen > al) {
                printf("  pkt[%u] INVALID header: hdrLen=%d actual=%d\n", i, hdrLen, al);
                continue;
            }

            int payloadSize = al - hdrLen;
            bool ptsPresent = (flags & 0x04) != 0;
            bool eof = (flags & 0x02) != 0;
            int fid = flags & 0x01;

            // Extract PTS if present
            uint32_t pts = 0;
            if (ptsPresent && hdrLen >= 6) {
                pts = (uint32_t)pkt[2] | ((uint32_t)pkt[3] << 8) |
                      ((uint32_t)pkt[4] << 16) | ((uint32_t)pkt[5] << 24);
                if (hasPTS && pts != lastPTS) {
                    ptsChanges++;
                    printf("  pkt[%u] *** PTS CHANGE: %u -> %u (framePos=%zu) ***\n",
                        i, lastPTS, pts, framePos);
                }
                lastPTS = pts;
                hasPTS = true;
            }

            // Copy payload to frame buffer
            if (payloadSize > 0 && framePos + payloadSize <= (size_t)MAX_FRAME) {
                memcpy(frameBuffer + framePos, pkt + hdrLen, payloadSize);
                framePos += payloadSize;
                totalPayloadBytes += payloadSize;
            }

            dataPackets++;
            totalPackets++;

            // Log interesting packets
            if (eof || (i < 3 && transferIdx <= 2)) {
                printf("  pkt[%u] actual=%d hdr=%d payload=%d FID=%d EOF=%d PTS=%u framePos=%zu\n",
                    i, al, hdrLen, payloadSize, fid, eof ? 1 : 0, pts, framePos);
            }
        }

        printf("  data=%d empty=%d totalPayload=%d framePos=%zu\n",
            dataPackets, emptyPackets, totalPayloadBytes, framePos);

        free(pktActual);
        free(pktStatus);
        free(rawBuf);
        transferCount++;
    }
    fclose(f);

    printf("\n=== SUMMARY ===\n");
    printf("Transfers analyzed: %d\n", transferCount);
    printf("Total data packets: %d\n", totalPackets);
    printf("Total payload bytes: %d\n", totalPayloadBytes);
    printf("PTS changes: %d\n", ptsChanges);
    printf("Final frame position: %zu\n", framePos);

    // Try to save reconstructed frame as BMP
    // Try common resolutions
    int resolutions[][2] = {{320,240}, {160,120}, {352,288}, {640,480}};
    for (int r = 0; r < 4; r++) {
        int w = resolutions[r][0], h = resolutions[r][1];
        size_t expectedSize = (size_t)w * h * 2;
        if (framePos >= expectedSize) {
            char path[256];
            snprintf(path, sizeof(path),
                "/boot/home/Desktop/reconstructed_%dx%d.bmp", w, h);
            write_bmp(path, frameBuffer, framePos, w, h);
            printf("\nReconstructed frame at %dx%d from raw USB packets.\n", w, h);
            printf("Compare with deframer output to identify tearing source.\n");
            break;
        }
    }

    if (ptsChanges > 0) {
        printf("\n*** PTS CHANGES DETECTED: %d ***\n", ptsChanges);
        printf("This means data from DIFFERENT camera frames is mixed.\n");
        printf("If PTS changes happen within the USB buffer (before deframing),\n");
        printf("the tearing originates at the USB transfer level.\n");
    } else {
        printf("\nNo PTS changes detected in captured data.\n");
        printf("This suggests all captured packets belong to the same camera frame.\n");
        printf("If the reconstructed image still shows tearing, the issue is in\n");
        printf("how packets are placed in the USB buffer (XHCI driver).\n");
    }

    free(frameBuffer);
    return 0;
}
