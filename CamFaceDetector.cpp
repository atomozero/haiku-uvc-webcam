/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * See CamFaceDetector.h for the design rationale (heuristic, dependency-free
 * skin-region face detection; bounding boxes only, no identification).
 */
#include "CamFaceDetector.h"

#include <stdlib.h>
#include <string.h>

// Downscaled working width. Detection runs on a small copy of the frame so it
// stays cheap regardless of the capture resolution. The height is derived to
// preserve aspect ratio.
static const int32 kWorkWidth = 160;

// Skin thresholds in YCbCr (Hsu/Abdel-Mottaleb/Jain style). These are broad on
// purpose: the geometry filters below reject most non-face skin regions.
static const int32 kYMin = 45;			// lowered from 60: keep shadowed skin
static const int32 kCbMin = 77;
static const int32 kCbMax = 127;
static const int32 kCrMin = 133;
static const int32 kCrMax = 173;


CamFaceDetector::CamFaceDetector()
	:
	fMask(NULL),
	fLabels(NULL),
	fWorkW(0),
	fWorkH(0),
	fCapacity(0)
{
}


CamFaceDetector::~CamFaceDetector()
{
	_FreeBuffers();
}


void
CamFaceDetector::_FreeBuffers()
{
	free(fMask);
	free(fLabels);
	fMask = NULL;
	fLabels = NULL;
	fCapacity = 0;
	fWorkW = fWorkH = 0;
}


void
CamFaceDetector::_EnsureBuffers(int32 workW, int32 workH)
{
	int32 needed = workW * workH;
	if (needed > fCapacity) {
		free(fMask);
		free(fLabels);
		fMask = (uint8*)malloc(needed * sizeof(uint8));
		fLabels = (int32*)malloc(needed * sizeof(int32));
		fCapacity = (fMask != NULL && fLabels != NULL) ? needed : 0;
	}
	fWorkW = workW;
	fWorkH = workH;
}


// --- Union-find over the working grid (indices are pixel positions) --------

int32
CamFaceDetector::_Find(int32 x)
{
	// Path-halving find.
	while (fLabels[x] != x) {
		fLabels[x] = fLabels[fLabels[x]];
		x = fLabels[x];
	}
	return x;
}


void
CamFaceDetector::_Union(int32 a, int32 b)
{
	int32 ra = _Find(a);
	int32 rb = _Find(b);
	if (ra != rb)
		fLabels[rb] = ra;
}


int32
CamFaceDetector::Detect(const uint8* bgra, int32 width, int32 height,
	int32 stride, BRect* outFaces)
{
	if (bgra == NULL || outFaces == NULL || width < 16 || height < 16)
		return 0;

	// Work resolution: downscale so the longer processing stays bounded.
	int32 scale = width / kWorkWidth;
	if (scale < 1)
		scale = 1;
	int32 workW = width / scale;
	int32 workH = height / scale;
	if (workW < 8 || workH < 8)
		return 0;

	_EnsureBuffers(workW, workH);
	if (fCapacity == 0)
		return 0;	// allocation failed

	// 1) Build skin mask on the downscaled grid (nearest-neighbour sample).
	for (int32 wy = 0; wy < workH; wy++) {
		const uint8* srcRow = bgra + (wy * scale) * stride;
		uint8* maskRow = fMask + wy * workW;
		for (int32 wx = 0; wx < workW; wx++) {
			const uint8* p = srcRow + (wx * scale) * 4;
			int32 b = p[0];
			int32 g = p[1];
			int32 r = p[2];

			// BT.601 RGB -> YCbCr (integer approximation).
			int32 y = (77 * r + 150 * g + 29 * b) >> 8;
			int32 cb = 128 + ((-43 * r - 85 * g + 128 * b) >> 8);
			int32 cr = 128 + ((128 * r - 107 * g - 21 * b) >> 8);

			bool skin = y >= kYMin
				&& cb >= kCbMin && cb <= kCbMax
				&& cr >= kCrMin && cr <= kCrMax;
			maskRow[wx] = skin ? 1 : 0;
		}
	}

	// 2) Connected-component labelling (8-connectivity) via union-find.
	for (int32 i = 0; i < workW * workH; i++)
		fLabels[i] = i;

	for (int32 wy = 0; wy < workH; wy++) {
		for (int32 wx = 0; wx < workW; wx++) {
			int32 idx = wy * workW + wx;
			if (fMask[idx] == 0)
				continue;
			// Union with already-visited skin neighbours (W, NW, N, NE).
			if (wx > 0 && fMask[idx - 1])
				_Union(idx, idx - 1);
			if (wy > 0) {
				if (fMask[idx - workW])
					_Union(idx, idx - workW);
				if (wx > 0 && fMask[idx - workW - 1])
					_Union(idx, idx - workW - 1);
				if (wx < workW - 1 && fMask[idx - workW + 1])
					_Union(idx, idx - workW + 1);
			}
		}
	}

	// 3) Accumulate per-component bounding box and pixel count. We keep the
	//    stats in-place by scanning once and hashing roots into small arrays.
	//    Component count is small for real frames; cap it to stay bounded.
	const int32 kMaxComponents = 256;
	int32 roots[kMaxComponents];
	int32 minX[kMaxComponents];
	int32 minY[kMaxComponents];
	int32 maxX[kMaxComponents];
	int32 maxY[kMaxComponents];
	int32 area[kMaxComponents];
	int32 compCount = 0;

	for (int32 wy = 0; wy < workH; wy++) {
		for (int32 wx = 0; wx < workW; wx++) {
			int32 idx = wy * workW + wx;
			if (fMask[idx] == 0)
				continue;
			int32 root = _Find(idx);

			// Linear lookup: component tables stay tiny.
			int32 c = -1;
			for (int32 k = 0; k < compCount; k++) {
				if (roots[k] == root) {
					c = k;
					break;
				}
			}
			if (c < 0) {
				if (compCount >= kMaxComponents)
					continue;	// too fragmented; ignore extras
				c = compCount++;
				roots[c] = root;
				minX[c] = maxX[c] = wx;
				minY[c] = maxY[c] = wy;
				area[c] = 0;
			}
			if (wx < minX[c]) minX[c] = wx;
			if (wx > maxX[c]) maxX[c] = wx;
			if (wy < minY[c]) minY[c] = wy;
			if (wy > maxY[c]) maxY[c] = wy;
			area[c]++;
		}
	}

	// 4) Filter components by size, fill ratio and aspect ratio to keep the
	//    ones that plausibly bound an upright face.
	int32 minArea = (workW * workH) / 300;	// >= ~0.33% of the frame
	if (minArea < 12)
		minArea = 12;

	struct Cand { int32 x, y, w, h, a; };
	Cand cand[kMaxComponents];
	int32 candCount = 0;

	for (int32 c = 0; c < compCount; c++) {
		int32 bw = maxX[c] - minX[c] + 1;
		int32 bh = maxY[c] - minY[c] + 1;
		if (area[c] < minArea)
			continue;
		// Fill ratio: a face region is fairly solid, not a thin scatter.
		// Loosened to 28%: glasses/beard/uneven light fragment the skin blob.
		if (area[c] * 100 < bw * bh * 28)
			continue;
		// Aspect ratio: faces are roughly as wide as tall. Loosened to
		// 0.62..2.5 so a wide forehead+cheeks blob (shorter than wide) or a
		// face+neck blob (taller) still qualifies, while thin wide bands
		// (walls, arms) are still rejected.
		int32 ratio = bh * 100 / bw;	// height/width * 100
		if (ratio < 62 || ratio > 250)
			continue;
		// Reject regions that are implausibly large (likely a background).
		// Raised caps: on a webcam the face often fills most of the frame,
		// so only reject a blob that covers essentially the whole image.
		if (bw * 100 > workW * 96 || bh * 100 > workH * 98)
			continue;

		cand[candCount].x = minX[c];
		cand[candCount].y = minY[c];
		cand[candCount].w = bw;
		cand[candCount].h = bh;
		cand[candCount].a = area[c];
		candCount++;
	}

	// 5) Sort candidates by area (descending) and emit up to kMaxFaces,
	//    scaled back to full-resolution coordinates.
	for (int32 i = 0; i < candCount; i++) {
		for (int32 j = i + 1; j < candCount; j++) {
			if (cand[j].a > cand[i].a) {
				Cand tmp = cand[i];
				cand[i] = cand[j];
				cand[j] = tmp;
			}
		}
	}

	int32 faces = candCount < kMaxFaces ? candCount : kMaxFaces;
	for (int32 i = 0; i < faces; i++) {
		float left = (float)(cand[i].x * scale);
		float top = (float)(cand[i].y * scale);
		float right = (float)((cand[i].x + cand[i].w) * scale - 1);
		float bottom = (float)((cand[i].y + cand[i].h) * scale - 1);
		if (right > width - 1)
			right = width - 1;
		if (bottom > height - 1)
			bottom = height - 1;
		outFaces[i].Set(left, top, right, bottom);
	}
	return faces;
}


void
CamFaceDetector::DrawBoxes(uint8* bgra, int32 width, int32 height,
	int32 stride, const BRect* faces, int32 count)
{
	if (bgra == NULL || faces == NULL)
		return;

	const uint8 kB = 0, kG = 255, kR = 0;	// green (BGRA)
	const int32 kThick = 2;

	for (int32 i = 0; i < count; i++) {
		int32 l = (int32)faces[i].left;
		int32 t = (int32)faces[i].top;
		int32 r = (int32)faces[i].right;
		int32 b = (int32)faces[i].bottom;
		if (l < 0) l = 0;
		if (t < 0) t = 0;
		if (r > width - 1) r = width - 1;
		if (b > height - 1) b = height - 1;
		if (r <= l || b <= t)
			continue;

		// Top and bottom edges.
		for (int32 e = 0; e < kThick; e++) {
			int32 yTop = t + e;
			int32 yBot = b - e;
			if (yTop <= b) {
				uint8* row = bgra + yTop * stride;
				for (int32 x = l; x <= r; x++) {
					uint8* p = row + x * 4;
					p[0] = kB; p[1] = kG; p[2] = kR;
				}
			}
			if (yBot >= t) {
				uint8* row = bgra + yBot * stride;
				for (int32 x = l; x <= r; x++) {
					uint8* p = row + x * 4;
					p[0] = kB; p[1] = kG; p[2] = kR;
				}
			}
		}
		// Left and right edges.
		for (int32 e = 0; e < kThick; e++) {
			int32 xLeft = l + e;
			int32 xRight = r - e;
			for (int32 y = t; y <= b; y++) {
				uint8* row = bgra + y * stride;
				if (xLeft <= r) {
					uint8* p = row + xLeft * 4;
					p[0] = kB; p[1] = kG; p[2] = kR;
				}
				if (xRight >= l) {
					uint8* p = row + xRight * 4;
					p[0] = kB; p[1] = kG; p[2] = kR;
				}
			}
		}
	}
}
