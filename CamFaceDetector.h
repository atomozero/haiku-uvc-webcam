/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * CamFaceDetector - lightweight, dependency-free face *detection* (not
 * recognition) for the UVC webcam driver.
 *
 * This is deliberately a heuristic detector, not a neural network: it stays
 * inside the driver's "no external dependencies beyond Haiku + libturbojpeg"
 * rule and cheap enough to run inside the media pipeline. It works on the
 * decoded BGRA frame by finding compact skin-coloured regions of roughly
 * face-like size and shape. It reports bounding boxes only; it never
 * identifies who a person is.
 *
 * Detection is OFF by default and is meant to run at a reduced cadence
 * (every Nth frame) so it does not eat the per-frame latency budget of the
 * real-time BBufferProducer.
 */
#ifndef _CAM_FACE_DETECTOR_H
#define _CAM_FACE_DETECTOR_H

#include <SupportDefs.h>
#include <Rect.h>

class CamFaceDetector {
public:
								CamFaceDetector();
								~CamFaceDetector();

	// Maximum number of face rectangles a single Detect() call reports.
	static const int32			kMaxFaces = 8;

	// Run detection on a decoded BGRA frame (4 bytes/pixel, B,G,R,A order,
	// as produced by FillFrameBuffer). 'stride' is the byte pitch of a row.
	// Fills 'outFaces' (caller-provided, at least kMaxFaces entries) with
	// bounding boxes in full-resolution pixel coordinates, ordered by
	// descending area. Returns the number of faces found (0..kMaxFaces).
			int32				Detect(const uint8* bgra, int32 width,
									int32 height, int32 stride,
									BRect* outFaces);

	// Draw simple rectangles into a BGRA frame to visualise detections.
	// Safe to call with rectangles produced at a different resolution only
	// if they were scaled accordingly; normally pass the boxes from Detect().
			void				DrawBoxes(uint8* bgra, int32 width,
									int32 height, int32 stride,
									const BRect* faces, int32 count);

private:
			void				_EnsureBuffers(int32 workW, int32 workH);
			void				_FreeBuffers();
			int32				_Find(int32 x);
			void				_Union(int32 a, int32 b);

			// Working (downscaled) buffers, reused across frames.
			uint8*				fMask;		// 1 = skin pixel
			int32*				fLabels;	// union-find parent per pixel
			int32				fWorkW;
			int32				fWorkH;
			int32				fCapacity;	// allocated pixel count
};

#endif /* _CAM_FACE_DETECTOR_H */
