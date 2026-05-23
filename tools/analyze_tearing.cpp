/*
 * Tearing analysis tool
 * Reads a raw YUY2 frame and outputs a tearing score + shift map.
 * Used by the automated alignment tuning loop.
 *
 * Build: g++ -O2 -o analyze_tearing analyze_tearing.cpp
 * Usage: analyze_tearing <input.yuv> <width> <height>
 */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

int main(int argc, char* argv[])
{
	const char* path = "/boot/home/Desktop/frame_dump.yuv";
	int width = 320, height = 240;

	if (argc > 1) path = argv[1];
	if (argc > 2) width = atoi(argv[2]);
	if (argc > 3) height = atoi(argv[3]);

	int rowBytes = width * 2;
	int frameSize = rowBytes * height;

	FILE* f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "Cannot open %s\n", path); return 1; }
	uint8_t* data = (uint8_t*)malloc(frameSize);
	size_t n = fread(data, 1, frameSize, f);
	fclose(f);
	if ((int)n < frameSize) {
		fprintf(stderr, "Short read: %zu/%d\n", n, frameSize);
		free(data);
		return 1;
	}

	// Compute row-to-row SAD for Y channel at various shifts
	int tearCount = 0;
	int totalScore = 0;

	printf("ROW  SAD0  BEST_SHIFT BEST_SAD  STATUS\n");
	for (int row = 1; row < height; row++) {
		uint8_t* prev = data + (row - 1) * rowBytes;
		uint8_t* cur = data + row * rowBytes;

		// SAD at shift=0 (Y values only, sample every 4th pixel)
		int sad0 = 0, count = 0;
		for (int x = 0; x < rowBytes; x += 8) {
			sad0 += abs((int)cur[x] - (int)prev[x]);
			count++;
		}
		sad0 = count > 0 ? sad0 / count : 0;

		// Find best shift
		int bestShift = 0, bestSAD = sad0;
		if (sad0 > 15) {
			for (int shift = -width; shift <= width; shift += 4) {
				if (shift == 0) continue;
				int sad = 0, cnt = 0;
				for (int x = 0; x < rowBytes; x += 8) {
					int sx = x + shift;
					if (sx < 0 || sx >= rowBytes) continue;
					sad += abs((int)cur[sx] - (int)prev[x]);
					cnt++;
				}
				if (cnt > count / 2) {
					sad = sad / cnt;
					if (sad < bestSAD) {
						bestSAD = sad;
						bestShift = shift;
					}
				}
			}
		}

		const char* status = "ok";
		if (sad0 > 15 && bestShift != 0 && bestSAD < sad0 / 2) {
			status = "TEAR";
			tearCount++;
		} else if (sad0 > 30) {
			status = "edge";
		}

		if (sad0 > 15 || strcmp(status, "TEAR") == 0) {
			printf("%3d  %4d  %+5d      %4d      %s\n",
				row, sad0, bestShift, bestSAD, status);
		}
		totalScore += sad0;
	}

	printf("\n=== TEARING SCORE ===\n");
	printf("Tear boundaries: %d\n", tearCount);
	printf("Average row SAD: %d\n", totalScore / (height - 1));
	printf("Score (lower=better): %d\n", tearCount * 100 + totalScore / (height - 1));

	free(data);
	return 0;
}
