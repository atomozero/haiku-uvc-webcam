/*
 * Copyright 2026, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for CamFaceDetector (heuristic skin-region face detection).
 * Builds the real detector and runs it against synthetic BGRA frames, so
 * it needs no webcam and can run in CI.
 *
 * Build:
 *   g++ -O2 -o test_face_detector test_face_detector.cpp \
 *       ../CamFaceDetector.cpp -lbe
 *
 * Run:
 *   ./test_face_detector
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <Rect.h>

#include "../CamFaceDetector.h"


static int sPass = 0;
static int sFail = 0;

#define CHECK(cond, msg) \
	do { \
		if (cond) { \
			sPass++; \
		} else { \
			sFail++; \
			printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); \
		} \
	} while (0)


// Paint a filled skin-coloured ellipse into a BGRA frame.
static void
PutSkinEllipse(uint8* buf, int w, int h, int stride, int cx, int cy,
	int rx, int ry)
{
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float dx = (x - cx) / (float)rx;
			float dy = (y - cy) / (float)ry;
			if (dx * dx + dy * dy <= 1.0f) {
				uint8* p = buf + y * stride + x * 4;
				p[0] = 120; p[1] = 150; p[2] = 200; p[3] = 255;	// BGRA skin
			}
		}
	}
}


static void
FillBackground(uint8* buf, int w, int h, int stride)
{
	// Bluish, non-skin.
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8* p = buf + y * stride + x * 4;
			p[0] = 180; p[1] = 90; p[2] = 40; p[3] = 255;
		}
	}
}


// Does any reported box roughly contain the given centre?
static bool
BoxNear(const BRect* faces, int n, int cx, int cy)
{
	for (int i = 0; i < n; i++) {
		if (cx >= faces[i].left && cx <= faces[i].right
			&& cy >= faces[i].top && cy <= faces[i].bottom) {
			return true;
		}
	}
	return false;
}


static void
TestTwoFaces()
{
	printf("TestTwoFaces\n");
	const int W = 640, H = 480, stride = W * 4;
	uint8* buf = (uint8*)malloc(W * H * 4);
	FillBackground(buf, W, H, stride);
	PutSkinEllipse(buf, W, H, stride, 200, 220, 55, 75);	// upright face
	PutSkinEllipse(buf, W, H, stride, 470, 180, 40, 55);	// smaller face

	CamFaceDetector det;
	BRect faces[CamFaceDetector::kMaxFaces];
	int32 n = det.Detect(buf, W, H, stride, faces);

	CHECK(n == 2, "should detect exactly two faces");
	CHECK(BoxNear(faces, n, 200, 220), "box around first face");
	CHECK(BoxNear(faces, n, 470, 180), "box around second face");
	// Sorted by area: the larger (first) face comes first.
	if (n == 2)
		CHECK(faces[0].Width() >= faces[1].Width(), "largest face first");

	free(buf);
}


static void
TestRejectsWideBand()
{
	printf("TestRejectsWideBand\n");
	const int W = 640, H = 480, stride = W * 4;
	uint8* buf = (uint8*)malloc(W * H * 4);
	FillBackground(buf, W, H, stride);
	// A wide, short skin band (e.g. a wall / arm): wrong aspect ratio.
	for (int y = 380; y < 410; y++)
		for (int x = 100; x < 540; x++) {
			uint8* p = buf + y * stride + x * 4;
			p[0] = 120; p[1] = 150; p[2] = 200; p[3] = 255;
		}

	CamFaceDetector det;
	BRect faces[CamFaceDetector::kMaxFaces];
	int32 n = det.Detect(buf, W, H, stride, faces);
	CHECK(n == 0, "wide band rejected by aspect-ratio filter");

	free(buf);
}


static void
TestNoSkin()
{
	printf("TestNoSkin\n");
	const int W = 320, H = 240, stride = W * 4;
	uint8* buf = (uint8*)malloc(W * H * 4);
	FillBackground(buf, W, H, stride);

	CamFaceDetector det;
	BRect faces[CamFaceDetector::kMaxFaces];
	int32 n = det.Detect(buf, W, H, stride, faces);
	CHECK(n == 0, "no skin -> no faces");

	free(buf);
}


static void
TestEdgeCases()
{
	printf("TestEdgeCases\n");
	const int W = 320, H = 240, stride = W * 4;
	uint8* buf = (uint8*)malloc(W * H * 4);
	FillBackground(buf, W, H, stride);

	CamFaceDetector det;
	BRect faces[CamFaceDetector::kMaxFaces];

	CHECK(det.Detect(NULL, W, H, stride, faces) == 0, "null buffer -> 0");
	CHECK(det.Detect(buf, W, H, stride, NULL) == 0, "null output -> 0");
	CHECK(det.Detect(buf, 4, 4, 16, faces) == 0, "tiny frame -> 0");

	// DrawBoxes must clip and never crash, even with an off-frame rect.
	BRect r[1];
	r[0].Set(-10, -10, W + 50, H + 50);
	det.DrawBoxes(buf, W, H, stride, r, 1);
	det.DrawBoxes(NULL, W, H, stride, r, 1);	// null-safe
	CHECK(true, "DrawBoxes clips without crashing");

	free(buf);
}


static void
TestDrawBoxesPaints()
{
	printf("TestDrawBoxesPaints\n");
	const int W = 64, H = 64, stride = W * 4;
	uint8* buf = (uint8*)malloc(W * H * 4);
	memset(buf, 0, W * H * 4);

	BRect r[1];
	r[0].Set(10, 10, 40, 40);
	CamFaceDetector det;
	det.DrawBoxes(buf, W, H, stride, r, 1);

	// Top-left corner of the rectangle should now be green (BGRA 0,255,0).
	uint8* p = buf + 10 * stride + 10 * 4;
	CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0, "rectangle drawn in green");

	free(buf);
}


int
main()
{
	printf("=== CamFaceDetector test suite ===\n");
	TestTwoFaces();
	TestRejectsWideBand();
	TestNoSkin();
	TestEdgeCases();
	TestDrawBoxesPaints();

	printf("\n%d passed, %d failed\n", sPass, sFail);
	return sFail == 0 ? 0 : 1;
}
