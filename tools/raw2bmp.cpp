// Convert raw RGB32 (BGRA) frame to BMP file
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

int main(int argc, char* argv[])
{
	if (argc < 5) {
		fprintf(stderr, "Usage: %s <input.raw> <output.bmp> <width> <height>\n", argv[0]);
		return 1;
	}

	const char* inFile = argv[1];
	const char* outFile = argv[2];
	int width = atoi(argv[3]);
	int height = atoi(argv[4]);
	int bpp = 4; // BGRA

	FILE* fin = fopen(inFile, "rb");
	if (!fin) { perror("open input"); return 1; }

	size_t dataSize = width * height * bpp;
	uint8_t* data = (uint8_t*)malloc(dataSize);
	if (fread(data, 1, dataSize, fin) != dataSize) {
		fprintf(stderr, "Short read\n");
		fclose(fin);
		free(data);
		return 1;
	}
	fclose(fin);

	// BMP header (bottom-up, 32-bit)
	uint32_t fileSize = 54 + dataSize;
	uint8_t header[54];
	memset(header, 0, 54);

	// File header
	header[0] = 'B'; header[1] = 'M';
	memcpy(&header[2], &fileSize, 4);
	uint32_t offset = 54;
	memcpy(&header[10], &offset, 4);

	// Info header
	uint32_t infoSize = 40;
	memcpy(&header[14], &infoSize, 4);
	memcpy(&header[18], &width, 4);
	int32_t negHeight = -height; // top-down
	memcpy(&header[22], &negHeight, 4);
	uint16_t planes = 1;
	memcpy(&header[26], &planes, 2);
	uint16_t bits = 32;
	memcpy(&header[28], &bits, 2);

	FILE* fout = fopen(outFile, "wb");
	if (!fout) { perror("open output"); return 1; }
	fwrite(header, 1, 54, fout);
	fwrite(data, 1, dataSize, fout);
	fclose(fout);
	free(data);

	printf("Saved %s (%dx%d, %zu bytes)\n", outFile, width, height, dataSize + 54);
	return 0;
}
