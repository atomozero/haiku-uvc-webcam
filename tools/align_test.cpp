/*
 * Offline YUY2 frame alignment test
 * Reads frame_dump.yuv, applies alignment, outputs BMP for visual verification.
 *
 * Build: g++ -O2 -o align_test align_test.cpp
 * Run:   align_test
 */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

static uint8_t clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

static void
write_bmp(const char* path, const uint8_t* yuy2, int w, int h)
{
	int rowPad = (4 - (w * 3 % 4)) % 4;
	uint32_t ds = (w * 3 + rowPad) * h;
	uint32_t fs = 54 + ds;
	uint8_t hdr[54] = {};
	hdr[0] = 'B'; hdr[1] = 'M';
	memcpy(&hdr[2], &fs, 4);
	uint32_t off = 54; memcpy(&hdr[10], &off, 4);
	uint32_t is = 40; memcpy(&hdr[14], &is, 4);
	memcpy(&hdr[18], &w, 4);
	int32_t nh = -h; memcpy(&hdr[22], &nh, 4);
	uint16_t p = 1; memcpy(&hdr[26], &p, 2);
	uint16_t b = 24; memcpy(&hdr[28], &b, 2);
	FILE* f = fopen(path, "wb");
	if (!f) return;
	fwrite(hdr, 1, 54, f);
	uint8_t pad[4] = {}, row[4096];
	for (int y = 0; y < h; y++) {
		const uint8_t* s = yuy2 + y * w * 2;
		for (int x = 0; x < w; x += 2) {
			int y0 = s[0], u = s[1], y1 = s[2], v = s[3]; s += 4;
			int c0 = y0-16, c1 = y1-16, d = u-128, e = v-128;
			row[x*3] = clamp((298*c0+516*d+128)>>8);
			row[x*3+1] = clamp((298*c0-100*d-208*e+128)>>8);
			row[x*3+2] = clamp((298*c0+409*e+128)>>8);
			row[(x+1)*3] = clamp((298*c1+516*d+128)>>8);
			row[(x+1)*3+1] = clamp((298*c1-100*d-208*e+128)>>8);
			row[(x+1)*3+2] = clamp((298*c1+409*e+128)>>8);
		}
		fwrite(row, 1, w * 3, f);
		if (rowPad) fwrite(pad, 1, rowPad, f);
	}
	fclose(f);
}


static int
row_sad(const uint8_t* rowA, const uint8_t* rowB, int rowBytes, int shift)
{
	int sad = 0, count = 0;
	for (int x = 0; x < rowBytes; x += 8) {
		int sx = x + shift;
		if (sx < 0 || sx >= rowBytes) continue;
		sad += abs((int)rowA[x] - (int)rowB[sx]);
		count++;
	}
	return count > 0 ? sad / count : 999;
}


struct TearPoint {
	int row;
	int shift;
	int sadBefore;
	int sadAfter;
};

static void
align_frame(uint8_t* data, int size, int width, int height)
{
	int rowBytes = width * 2;
	int maxRows = size / rowBytes;
	if (maxRows > height) maxRows = height;

	printf("Aligning %dx%d frame (%d bytes)...\n", width, height, size);

	// PASS 1: Detect tear boundaries on UNMODIFIED data
	TearPoint tears[64];
	int tearCount = 0;

	// Block-based SAD: average SAD across multiple rows for robustness
	auto blockSAD = [&](int startRow, int shift, int blockSize) -> int {
		int totalSAD = 0, count = 0;
		for (int r = 0; r < blockSize; r++) {
			int rA = startRow - blockSize + r;
			int rB = startRow + r;
			if (rA < 0 || rB >= maxRows) continue;
			totalSAD += row_sad(data + rA * rowBytes,
				data + rB * rowBytes, rowBytes, shift);
			count++;
		}
		return count > 0 ? totalSAD / count : 999;
	};

	int lastTearRow = -30;
	for (int row = 3; row < maxRows - 3 && tearCount < 64; row++) {
		if (row - lastTearRow < 20)
			continue;

		int sad0 = blockSAD(row, 0, 3);
		if (sad0 < 30)
			continue;

		int bestShift = 0, bestSAD = sad0;
		for (int shift = -rowBytes + 4; shift < rowBytes; shift += 4) {
			if (shift == 0) continue;
			if (abs(shift) > rowBytes - 20) continue;
			int sad = blockSAD(row, shift, 3);
			if (sad < bestSAD) {
				bestSAD = sad;
				bestShift = shift;
			}
		}

		if (bestShift != 0 && bestSAD * 3 < sad0) {
			tears[tearCount++] = {row, bestShift, sad0, bestSAD};
			lastTearRow = row;
			printf("  TEAR at row %3d: SAD %3d -> %3d, shift %+4d bytes\n",
				row, sad0, bestSAD, bestShift);
		}
	}

	printf("Detected %d tear boundaries\n", tearCount);
	if (tearCount == 0) {
		printf("No tears to fix\n");
		return;
	}

	// PASS 2: Build aligned frame by reading from original at corrected offsets
	uint8_t* orig = (uint8_t*)malloc(size);
	memcpy(orig, data, size);

	// Each tear adds a cumulative byte offset
	// Positive shift = data is late, need to read from further ahead
	int cumOffset = 0;
	int tearIdx = 0;

	for (int row = 0; row < maxRows; row++) {
		// Check if this row starts a new tear
		if (tearIdx < tearCount && row == tears[tearIdx].row) {
			cumOffset += tears[tearIdx].shift;
			printf("  Row %d: cumulative offset now %+d bytes\n",
				row, cumOffset);
			tearIdx++;
		}

		int srcStart = row * rowBytes + cumOffset;
		int dstStart = row * rowBytes;

		// Copy row from source at offset position
		for (int x = 0; x < rowBytes; x++) {
			int srcPos = srcStart + x;
			if (srcPos >= 0 && srcPos < size)
				data[dstStart + x] = orig[srcPos];
			else {
				// Black YUY2 for out-of-bounds
				data[dstStart + x] = (x & 1) ? 0x80 : 0x00;
			}
		}
	}

	free(orig);
	printf("Applied %d fixes (cumulative offset: %+d bytes)\n",
		tearCount, cumOffset);
}


int main()
{
	int width = 320, height = 240;
	int rowBytes = width * 2;
	int frameSize = rowBytes * height;

	// Read raw frame
	FILE* f = fopen("/boot/home/Desktop/frame_dump.yuv", "rb");
	if (!f) { printf("Cannot open frame_dump.yuv\n"); return 1; }
	uint8_t* data = (uint8_t*)malloc(frameSize);
	fread(data, 1, frameSize, f);
	fclose(f);

	// Save original as BMP
	write_bmp("/boot/home/Desktop/align_before.bmp", data, width, height);
	printf("Saved align_before.bmp\n");

	// Apply alignment
	uint8_t* aligned = (uint8_t*)malloc(frameSize);
	memcpy(aligned, data, frameSize);
	align_frame(aligned, frameSize, width, height);

	// Save aligned as BMP
	write_bmp("/boot/home/Desktop/align_after.bmp", aligned, width, height);
	printf("Saved align_after.bmp\n");

	// Convert both to PNG for viewing
	system("ffmpeg -y -i /boot/home/Desktop/align_before.bmp "
		"-vf scale=640:480:flags=neighbor "
		"/boot/home/Desktop/align_before.png 2>/dev/null");
	system("ffmpeg -y -i /boot/home/Desktop/align_after.bmp "
		"-vf scale=640:480:flags=neighbor "
		"/boot/home/Desktop/align_after.png 2>/dev/null");
	printf("Saved PNG versions\n");

	free(data);
	free(aligned);
	return 0;
}
