/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Distributed under the terms of the MIT License.
 *
 * UVCCamDevice frame fill, validate and convert methods, split from UVCCamDevice.cpp (RF-05).
 */

#include "UVCCamDevice.h"
#include "UVCDeframer.h"
#include "UVCQuirks.h"
#include "UVCDescriptors.h"
#include "CamDebug.h"
#include "CamConfig.h"

#include <new>
#include <stdio.h>
#include <Autolock.h>
#include <stdlib.h>
#include <syslog.h>
#include <Notification.h>
#include <ParameterWeb.h>
#include <String.h>
#include <media/Buffer.h>

#undef TRACE
#define TRACE(x...) do {} while(0)
//#define TRACE(x...) printf(x)

// FIX BUG 6: Counters are now instance members (see header)

// FillFrameBuffer convert stage: pre-fill if needed, then
// decompress MJPEG or dispatch the uncompressed converter.
void
UVCCamDevice::_ConvertFrame(BBuffer* buffer, size_t bufferSize, CamFrame* f,
	int32 w, int32 h, frame_validation_result validation)
{
	if (buffer->SizeAvailable() >= bufferSize) {
		unsigned char* dst = (unsigned char*)buffer->Data();

		// OPTIMIZATION: Only pre-fill buffer for incomplete/invalid frames
		// For valid frames, MJPEG decompression or YUY2 conversion will
		// overwrite the entire buffer, making pre-fill unnecessary.
		// This saves ~300KB of memory writes per frame at 320x240.
		bool needsPreFill = (validation != FRAME_VALID);

		if (needsPreFill) {
			// Use fast memset for pre-fill (dark blue pattern)
			// 0x40 gives a visible but not distracting background
			memset(dst, 0x40, bufferSize);
		}

		if (fIsMJPEG) {
			// For MJPEG, validation already happened above.
			// Partial MJPEG might still produce some valid data,
			// so decompress even when invalid.
			_DecompressMJPEGtoRGB32(dst,
				(const unsigned char*)f->Buffer(), f->BufferLength(), w, h);
		} else {
			// Uncompressed payload: dispatch on the detected pixel format.
			// UVC_FMT_UNKNOWN falls through to YUY2 for backwards compatibility
			// with devices whose GUID is not (yet) recognized.
			const unsigned char* srcData = (const unsigned char*)f->Buffer();
			size_t actualSize = f->BufferLength();
			size_t expectedSize = _UncompressedFrameSize(
				fUncompressedPixelFormat, w, h);

			if (actualSize < expectedSize) {
				static int32 sIncomplete = 0;
				if (++sIncomplete <= 20 || (sIncomplete % 100) == 0)
					syslog(LOG_WARNING,
						"FillFrameBuffer: Incomplete %s #%d: %zu/%zu bytes (%.1f%%)\n",
						_UncompressedFormatName(fUncompressedPixelFormat),
						(int)sIncomplete, actualSize, expectedSize,
						100.0f * actualSize / expectedSize);
			}

			switch (fUncompressedPixelFormat) {
				case UVC_FMT_UYVY:
					_ConvertUYVYtoRGB32(dst, srcData, actualSize, w, h);
					break;
				case UVC_FMT_NV12:
					_ConvertNV12toRGB32(dst, (unsigned char*)srcData,
						actualSize, w, h);
					break;
				case UVC_FMT_NV21:
					_ConvertNV21toRGB32(dst, srcData, actualSize, w, h);
					break;
				case UVC_FMT_I420:
					_ConvertI420toRGB32(dst, srcData, actualSize, w, h);
					break;
				case UVC_FMT_YV12:
					_ConvertYV12toRGB32(dst, srcData, actualSize, w, h);
					break;
				case UVC_FMT_GREY:
					_ConvertGREYtoRGB32(dst, srcData, actualSize, w, h);
					break;
				case UVC_FMT_YUY2:
				case UVC_FMT_UNKNOWN:
				default:
					_ConvertYUY2toRGB32(dst, (unsigned char*)srcData,
						actualSize, w, h);
					break;
			}
		}
	}
}


// FillFrameBuffer validation stage: check frame, track sizes,
// update stats and request fallback when the stream degrades.
frame_validation_result
UVCCamDevice::_ValidateFrame(CamFrame* f, int32 w, int32 h)
{
	// Feature 1: Frame Validation
	fValidationStats.frames_validated++;
	frame_validation_result validation;
	if (fIsMJPEG) {
		validation = _ValidateMJPEGFrame((const uint8*)f->Buffer(), f->BufferLength());

		// MJPEG frame size monitoring for auto-fallback
		size_t frameSize = f->BufferLength();
		fMJPEGFrameSizeSum += frameSize;
		fMJPEGFrameSizeCount++;

		// Minimum expected MJPEG size: 1% of raw YUY2 size.
		// MJPEG compression varies widely (10:1 to 50:1+), so use a low
		// threshold to avoid false positives on highly compressed streams.
		if (fExpectedMJPEGMinSize == 0) {
			fExpectedMJPEGMinSize = (size_t)w * h * 2 / 100;
			if (fExpectedMJPEGMinSize < 1024)
				fExpectedMJPEGMinSize = 1024;
		}

		// Check every 60 frames (skip first 60 to allow stream stabilization)
		bigtime_t now = system_time();
		if (fMJPEGFrameSizeCount >= 60 && (now - fLastFrameSizeCheck) > 10000000) {
			fLastFrameSizeCheck = now;
			size_t avgSize = fMJPEGFrameSizeSum / fMJPEGFrameSizeCount;

			if (avgSize < fExpectedMJPEGMinSize) {
				syslog(LOG_WARNING, "UVCCamDevice: MJPEG frames too small! avg=%zu, min=%zu\n",
					avgSize, fExpectedMJPEGMinSize);

				// Use RequestResolutionChange() for safe resolution change
				// The actual change happens in ReconfigThread, not here
				int32 maxLevel = _GetMaxResolutionLevel();
				if (fCurrentResolutionLevel < maxLevel && !HasPendingReconfigRequest()) {
					int32 targetLevel = fCurrentResolutionLevel + 1;
					uint32 newWidth, newHeight;
					_GetResolutionAtLevel(targetLevel, &newWidth, &newHeight);

					syslog(LOG_WARNING, "UVCCamDevice: Bandwidth insufficient, "
						"requesting fallback to %ux%u via worker thread\n",
						newWidth, newHeight);

					RequestResolutionChange(newWidth, newHeight);

					fCurrentResolutionLevel = targetLevel;
					fFallbackActive = true;
					fLastFallbackTime = system_time();
				} else if (fCurrentResolutionLevel >= maxLevel) {
					syslog(LOG_WARNING, "UVCCamDevice: Bandwidth insufficient, "
						"but already at minimum resolution\n");
				}

				// Reset counters
				fMJPEGFrameSizeSum = 0;
				fMJPEGFrameSizeCount = 0;
			} else {
				// Reset counters for next window
				fMJPEGFrameSizeSum = 0;
				fMJPEGFrameSizeCount = 0;
			}
		}
	} else {
		validation = _ValidateYUY2Frame((const uint8*)f->Buffer(), f->BufferLength(), w, h);
	}

	// Update validation statistics based on result
	switch (validation) {
		case FRAME_VALID:
			fValidationStats.frames_valid++;
			fValidationStats.last_valid_frame_time = system_time();
			fConsecutiveBadFrames = 0;
			break;
		case FRAME_INCOMPLETE:
			fValidationStats.frames_incomplete++;
			fConsecutiveBadFrames++;
			break;
		case FRAME_CORRUPTED_NO_SOI:
			fValidationStats.frames_no_soi++;
			fConsecutiveBadFrames++;
			break;
		case FRAME_CORRUPTED_NO_EOI:
			fValidationStats.frames_no_eoi++;
			fConsecutiveBadFrames++;
			break;
		default:
			fConsecutiveBadFrames++;
			break;
	}

	// Report validation stats periodically
	_ReportValidationStats();

	// Feature 3: Update packet loss stats from base class and evaluate
	// Use delta from last check to update the evaluation window.
	// Read with atomics, the pump writes them.
	uint32 currentSuccess = (uint32)atomic_get(&fPacketSuccessCount);
	uint32 currentError = (uint32)atomic_get(&fPacketErrorCount);

	// Guard against counter reset (e.g. after resolution change restarts stream)
	// If current < last, the base class reset its counters - resync
	if (currentSuccess < fLastPacketSuccessCount
		|| currentError < fLastPacketErrorCount) {
		fLastPacketSuccessCount = currentSuccess;
		fLastPacketErrorCount = currentError;
		fEvalWindowPackets = 0;
		fEvalWindowErrors = 0;
		fEvalWindowStartTime = system_time();
	}

	uint32 deltaSuccess = currentSuccess - fLastPacketSuccessCount;
	uint32 deltaError = currentError - fLastPacketErrorCount;
	fLastPacketSuccessCount = currentSuccess;
	fLastPacketErrorCount = currentError;
	fEvalWindowPackets += deltaSuccess + deltaError;
	fEvalWindowErrors += deltaError;
	_EvaluatePacketLoss();

	// Auto-downgrade resolution on too many consecutive bad frames
	if (fConsecutiveBadFrames == kMaxConsecutiveBadFrames) {
		int32 maxLevel = _GetMaxResolutionLevel();
		if (fCurrentResolutionLevel < maxLevel && !HasPendingReconfigRequest()) {
			int32 targetLevel = fCurrentResolutionLevel + 1;
			uint32 newWidth, newHeight;
			_GetResolutionAtLevel(targetLevel, &newWidth, &newHeight);
			syslog(LOG_WARNING, "UVCCamDevice: %u consecutive bad frames, "
				"auto-downgrading to %ux%u\n",
				fConsecutiveBadFrames, newWidth, newHeight);
			RequestResolutionChange(newWidth, newHeight);
			fCurrentResolutionLevel = targetLevel;
		} else if (fCurrentResolutionLevel >= maxLevel) {
			syslog(LOG_WARNING, "UVCCamDevice: %u consecutive bad frames at "
				"minimum resolution\n", fConsecutiveBadFrames);
		}
		fConsecutiveBadFrames = 0;  // Reset to allow retry
	}

	return validation;
}


// FillFrameBuffer timeout path: log, recover, give up.
status_t
UVCCamDevice::_HandleFillTimeout(status_t err)
{
	atomic_add(&fFillFrameTimeout, 1);

	// Log only first 5 and every 50th to reduce spam during EHCI errors
	int32 timeouts = atomic_get(&fFillFrameTimeout);
	if (timeouts <= 5 || (timeouts % 50) == 0) {
		syslog(LOG_WARNING, "UVCCamDevice::FillFrameBuffer: WaitFrame TIMEOUT #%d (err=%s)\n",
			(int)timeouts, strerror(err));
	}

	// After 10 consecutive timeouts, attempt automatic recovery.
	// This is typical of EHCI "host system error" on Intel controllers
	// after sustained isochronous streaming. Cycle the streaming alternate
	// (down to 0, back to streaming) to re-initialize the endpoint.
	if (atomic_get(&fFillFrameTimeout) == 10
		&& !fEHCIRecoveryInProgress.load()) {
		fEHCIRecoveryInProgress.store(true);
		syslog(LOG_WARNING, "UVCCamDevice: 10 consecutive frame timeouts - "
			"attempting recovery via alternate cycle\n");

		// Snapshot under lock, cycle outside so the USB
		// wait never blocks reader threads.
		uint8 streamAlt = 0;
		BUSBDevice* device = NULL;
		uint32 streamingIndex = 0;
		{
			BAutolock lock(Locker());
			if (lock.IsLocked() && fDevice != NULL) {
				streamAlt = (uint8)fCurrentVideoAlternate;
				device = fDevice;
				streamingIndex = fStreamingIndex;
				// Park endpoint before cycling, pump pauses
				// instead of using a freed endpoint.
				fIsoIn = NULL;
				fCurrentVideoAlternate = 0;
			}
		}
		if (streamAlt > 0 && device != NULL) {
			const BUSBConfiguration* cfg
				= device->ActiveConfiguration();
			if (cfg != NULL) {
				BUSBInterface* iface = const_cast<BUSBInterface*>(
					cfg->InterfaceAt(streamingIndex));
				if (iface != NULL) {
					// Bring the interface down to alt 0. Going N->0 is safe:
					// only the 0->N direction trips Haiku's SetAlternate
					// double-free.
					iface->SetAlternate(0);
				}
			}
			snooze(100000);

			// Re-select the streaming alternate through the normal
			// path. _SelectBestAlternate() applies the 0->N
			// double-free workaround AND re-fetches fIsoIn /
			// fIsoMaxPacketSize / fBuffer. A raw SetAlternate(streamAlt)
			// here would skip the workaround and leave fIsoIn pointing
			// at freed memory.
			BAutolock relock(Locker());
			if (relock.IsLocked() && fDevice != NULL) {
				status_t rs = _SelectBestAlternate();
				if (rs != B_OK) {
					syslog(LOG_ERR, "UVCCamDevice: recovery re-select "
						"failed: %s\n", strerror(rs));
				} else {
					syslog(LOG_INFO, "UVCCamDevice: recovery alt cycle "
						"complete (%u -> 0 -> %u)\n", streamAlt,
						fCurrentVideoAlternate);
				}
			}
		}
		fEHCIRecoveryInProgress.store(false);
	}

	// If recovery didn't help by 30 timeouts, give up and stop the pump.
	if (atomic_get(&fFillFrameTimeout) == 30) {
		syslog(LOG_ERR, "UVCCamDevice: recovery failed - stopping transfer. "
			"Please unplug and reconnect the camera.\n");
		StopTransfer();
	}

	if (fUsingHighBandwidth)
		_OnHighBandwidthFailure();

	return err;
}


status_t
UVCCamDevice::FillFrameBuffer(BBuffer* buffer, bigtime_t* stamp)
{
	atomic_add(&fFillFrameCount, 1);

	// Fast-fail on a stalled device: its endpoint only recovers on physical
	// re-enumeration, so there is nothing to deliver and no point touching it.
	if (IsStalled())
		return B_DEVICE_NOT_FOUND;

	// Debug: verify fDeframer
	static int32 sDeframerCheck = 0;
	if (++sDeframerCheck <= 3) {
		syslog(LOG_INFO, "FillFrameBuffer: fDeframer=%p this=%p\n",
			(void*)fDeframer, (void*)this);
	}

	if (fDeframer == NULL) {
		syslog(LOG_ERR, "FillFrameBuffer: fDeframer is NULL!\n");
		return B_ERROR;
	}

	status_t err = fDeframer->WaitFrame(2000000);
	if (err < B_OK)
		return _HandleFillTimeout(err);

	// Reset timeout counter on successful frame
	if (atomic_get(&fFillFrameTimeout) > 0)
		atomic_set(&fFillFrameTimeout, 0);

	CamFrame* f;
	err = fDeframer->GetFrame(&f, stamp);
	if (err < B_OK)
		return err;

	atomic_add(&fFillFrameSuccess, 1);

	int32 w = (int32)(VideoFrame().right - VideoFrame().left + 1);
	int32 h = (int32)(VideoFrame().bottom - VideoFrame().top + 1);
	size_t bufferSize = (size_t)w * h * 4;

	// DEBUG: Log buffer size info to check for stride issues
	static int32 sBufSizeLog = 0;
	if (++sBufSizeLog <= 3) {
		size_t available = buffer->SizeAvailable();
		size_t expectedStride = (size_t)w * 4;
		size_t actualStride = (h > 1) ? (available / h) : expectedStride;
		syslog(LOG_INFO, "Buffer info: available=%zu needed=%zu w=%d h=%d expectedStride=%zu actualStride=%zu\n",
			available, bufferSize, (int)w, (int)h, expectedStride, actualStride);
		if (actualStride != expectedStride) {
			syslog(LOG_WARNING, "Buffer STRIDE MISMATCH! expected=%zu actual=%zu diff=%d\n",
				expectedStride, actualStride, (int)(actualStride - expectedStride));
		}
	}

	/* Task 6: Check if buffer is large enough for current resolution */
	if (buffer->SizeAvailable() < bufferSize) {
		static int32 sBufferTooSmall = 0;
		if (++sBufferTooSmall <= 5 || (sBufferTooSmall % 100) == 0) {
			syslog(LOG_WARNING, "FillFrameBuffer: Buffer too small #%d: need %zu, have %zu (%dx%d)\n",
				(int)sBufferTooSmall, bufferSize, buffer->SizeAvailable(), (int)w, (int)h);
			syslog(LOG_WARNING, "FillFrameBuffer: Resolution may have changed - restart stream for new buffers\n");
		}
		// Recycle frame back to pool instead of deleting
		if (fDeframer != NULL)
			fDeframer->RecycleFrame(f);
		else
			delete f;
		return B_ERROR;
	}

	frame_validation_result validation = _ValidateFrame(f, w, h);

	_ConvertFrame(buffer, bufferSize, f, w, h, validation);

	// Recycle frame back to pool for reuse (reduces allocations)
	if (fDeframer != NULL)
		fDeframer->RecycleFrame(f);
	else
		delete f;

	return B_OK;
}


// Optimized inline clamp function using branchless technique
static inline uint8
clamp255(int32 v)
{
	// Branchless clamp: faster than conditional on most CPUs
	v = v < 0 ? 0 : v;
	return (uint8)(v > 255 ? 255 : v);
}


void
UVCCamDevice::_ConvertYUY2toRGB32(unsigned char* dst, unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// Optimized YUY2 to RGB32 conversion using pre-computed lookup tables.
	// Eliminates per-pixel multiplications - only table lookups and additions.
	// YUY2 format: Y0 U Y1 V (4 bytes = 2 pixels)

	if (!dst || !src || width <= 0 || height <= 0)
		return;

	// Need at least 8 bytes for diagnostics and 4 bytes for one YUY2 macro-pixel
	if (srcSize < 8)
		return;

	// Ensure lookup tables are initialized
	if (!gYuvRgbTables.initialized) {
		gYuvRgbTables.Initialize();
	}

	// Cache table pointers for faster access in inner loop
	const int32* yTable = gYuvRgbTables.y_table;
	const int32* uBTable = gYuvRgbTables.u_b_table;
	const int32* uGTable = gYuvRgbTables.u_g_table;
	const int32* vRTable = gYuvRgbTables.v_r_table;
	const int32* vGTable = gYuvRgbTables.v_g_table;

	size_t expectedSize = (size_t)width * height * 2;
	size_t srcStride = (size_t)width * 2;  // YUY2: 2 bytes per pixel (default)
	size_t dstStride = (size_t)width * 4;  // RGB32: 4 bytes per pixel

	// STRIDE QUIRK: Microdia 0c45:6409 may use 352-pixel internal buffer width
	// Only apply when srcSize indicates actual padding (srcSize > expectedSize)
	if (fMicrodiaQuirk && width < 352 && srcSize > expectedSize) {
		size_t paddedStride = 352 * 2;
		if (srcSize >= paddedStride * height) {
			srcStride = paddedStride;
			static bool sQuirkApplied = false;
			if (!sQuirkApplied) {
				syslog(LOG_INFO, "YUY2: Applying Microdia stride quirk: %dx%d using stride %zu (srcSize=%zu)\n",
					(int)width, (int)height, srcStride, srcSize);
				sQuirkApplied = true;
			}
		}
	}
	// STRIDE FIX: Detect row padding (some cameras add padding)
	else if (srcSize > expectedSize && height > 1) {
		size_t actualStride = srcSize / height;
		// Only use if it's larger than expected and aligned reasonably
		if (actualStride > srcStride && actualStride <= srcStride + 256) {
			static bool sStrideWarned = false;
			if (!sStrideWarned) {
				syslog(LOG_WARNING, "YUY2: Detected row padding! expected stride=%zu, actual=%zu (padding=%zu bytes/row)\n",
					srcStride, actualStride, actualStride - srcStride);
				sStrideWarned = true;
			}
			srcStride = actualStride;
		}
	}

	// Enhanced YUY2 diagnostics to detect byte order issues.
	// First-frames-only analysis, VERBOSE gated (used to spam ~20 lines).
	static int32 sYUY2Diag = 0;
	if (sYUY2Diag < 5
		&& gWebcamDebugLevel >= WEBCAM_DEBUG_VERBOSE) {
		++sYUY2Diag;
		// Analyze first few pixels to detect YUYV vs UYVY
		// In YUYV: Y values vary widely (0-255), U/V are more stable (around 128 for gray)
		// In UYVY: positions are swapped
		uint8 b0 = src[0], b1 = src[1], b2 = src[2], b3 = src[3];
		uint8 b4 = src[4], b5 = src[5], b6 = src[6], b7 = src[7];

		// Calculate variance of even vs odd bytes across first 8 bytes
		int evenSum = b0 + b2 + b4 + b6;
		int oddSum = b1 + b3 + b5 + b7;
		int evenAvg = evenSum / 4;
		int oddAvg = oddSum / 4;

		// In YUYV: even bytes are Y (vary), odd bytes are U/V (near 128)
		// In UYVY: even bytes are U/V (near 128), odd bytes are Y (vary)
		bool probablyYUYV = (abs(oddAvg - 128) < abs(evenAvg - 128));

		WEBCAM_VERBOSE("YUY2 diag #%d: bytes=[%02x %02x %02x %02x | %02x %02x %02x %02x]\n",
			(int)sYUY2Diag, b0, b1, b2, b3, b4, b5, b6, b7);
		WEBCAM_VERBOSE("YUY2 diag #%d: evenAvg=%d oddAvg=%d -> likely %s\n",
			(int)sYUY2Diag, evenAvg, oddAvg, probablyYUYV ? "YUYV" : "UYVY");
		WEBCAM_VERBOSE("YUY2 diag #%d: srcSize=%zu expected=%zu srcStride=%zu dstStride=%zu\n",
			(int)sYUY2Diag, srcSize, (size_t)width * height * 2, srcStride, dstStride);

		// Detect if camera pads rows to alignment boundaries
		// Check if srcSize is larger than expected and find actual stride
		if (srcSize > (size_t)width * height * 2) {
			size_t actualStride = srcSize / height;
			WEBCAM_VERBOSE("YUY2 diag #%d: Source has PADDING! srcSize=%zu > expected=%zu, actualStride=%zu (expected %zu)\n",
				(int)sYUY2Diag, srcSize, (size_t)width * height * 2, actualStride, srcStride);
		}

		// Also check if first row pattern repeats at unexpected offset
		// This detects row padding even when total size is correct (truncated)
		if (srcSize >= (size_t)width * 4) {  // Need at least 2 rows
			// Look for similar Y values at different offsets to find actual row start
			int expectedRowOffset = width * 2;
			// Check some common alignment values: 512, 1024, 2048
			int testOffsets[] = {512, 1024, 2048, expectedRowOffset};
			for (int i = 0; i < 4 && srcSize > (size_t)testOffsets[i] + 8; i++) {
				int off = testOffsets[i];
				// Compare first few pixels of row 0 with potential row 1
				int diff = abs((int)src[0] - (int)src[off]) + abs((int)src[2] - (int)src[off+2]) +
				           abs((int)src[4] - (int)src[off+4]) + abs((int)src[6] - (int)src[off+6]);
				WEBCAM_VERBOSE("YUY2 stride test: offset=%d diff=%d (low=similar rows)\n", off, diff);
			}
		}
	}

	// Row-by-row conversion for proper stride handling
	size_t rowDataBytes = (size_t)width * 2;  // bytes of YUY2 data we read per row
	// Hoist the per-row bounds check: rows past the source end
	// would break out anyway, compute the last full row once.
	int32 maxRows = height;
	if (rowDataBytes > srcSize)
		maxRows = 0;
	else {
		size_t fullRows = (srcSize - rowDataBytes) / srcStride + 1;
		if (fullRows < (size_t)maxRows)
			maxRows = (int32)fullRows;
	}
	for (int32 row = 0; row < maxRows; row++) {
		size_t rowOffset = (size_t)row * srcStride;

		const unsigned char* srcRow = src + rowOffset;
		unsigned char* dstRow = dst + row * dstStride;

		// Process full macro-pixels first. UVC widths are even in
		// practice, so hoisting the odd-width tail out of the loop
		// removes a per-macro-pixel branch (~10% measured on x86_64).
		for (int32 x = 0; x + 1 < width; x += 2) {
			// Read the luma + shared chroma-U of this macro-pixel.
			uint8 y0 = srcRow[0];
			uint8 u  = srcRow[1];
			uint8 y1 = srcRow[2];
			uint8 v  = srcRow[3];

			// Lookup pre-computed values (no multiplications!)
			int32 yVal0 = yTable[y0];
			int32 yVal1 = yTable[y1];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			// Pixel 0: BGRA (combine Y with U/V contributions, then shift)
			dstRow[0] = clamp255((yVal0 + uB + 128) >> 8);           // B
			dstRow[1] = clamp255((yVal0 + uG + vG + 128) >> 8);      // G
			dstRow[2] = clamp255((yVal0 + vR + 128) >> 8);           // R
			dstRow[3] = 255;                                          // A

			// Pixel 1: BGRA
			dstRow[4] = clamp255((yVal1 + uB + 128) >> 8);           // B
			dstRow[5] = clamp255((yVal1 + uG + vG + 128) >> 8);      // G
			dstRow[6] = clamp255((yVal1 + vR + 128) >> 8);           // R
			dstRow[7] = 255;                                          // A
			dstRow += 8;
			srcRow += 4;
		}
		if (width & 1) {
			// Odd width: the lone trailing pixel has no paired chroma-V
			// in the source. Reading srcRow[2..3] would over-read the row
			// (and, on the last row, the dst buffer) — emit just this
			// pixel with neutral V instead.
			uint8 y0 = srcRow[0];
			uint8 u  = srcRow[1];
			int32 yVal0 = yTable[y0];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[128];
			int32 vG = vGTable[128];
			dstRow[0] = clamp255((yVal0 + uB + 128) >> 8);           // B
			dstRow[1] = clamp255((yVal0 + uG + vG + 128) >> 8);      // G
			dstRow[2] = clamp255((yVal0 + vR + 128) >> 8);           // R
			dstRow[3] = 255;                                          // A
		}
	}
}


void
UVCCamDevice::_ConvertNV12toRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// NV12 to RGB32 conversion using pre-computed lookup tables.
	// NV12 format: Y plane (width*height bytes) followed by UV plane (width*height/2 bytes)
	// UV plane is interleaved: U0 V0 U1 V1 ... (subsampled 2x2)

	if (!dst || !src || width <= 0 || height <= 0)
		return;

	// FIX: NV12 chroma is subsampled 2x2, so odd dimensions have no well
	// defined layout and the col+1 read below would walk off the UV row.
	// UVC devices always advertise even sizes; fail closed otherwise.
	if ((width & 1) || (height & 1)) {
		memset(dst, 0, (size_t)width * height * 4);
		return;
	}

	// Ensure lookup tables are initialized
	if (!gYuvRgbTables.initialized) {
		gYuvRgbTables.Initialize();
	}

	// Validate NV12 data size (Y plane + UV plane)
	size_t expectedSize = (size_t)width * height * 3 / 2;
	if (srcSize < expectedSize) {
		syslog(LOG_WARNING, "NV12 conversion: srcSize %zu < expected %zu\n",
			srcSize, expectedSize);
		// Fill with black and return
		// FIX-M5: 32-bit product wrapped for hostile dimensions.
		memset(dst, 0, (size_t)width * height * 4);
		return;
	}

	// Cache table pointers for faster access in inner loop
	const int32* yTable = gYuvRgbTables.y_table;
	const int32* uBTable = gYuvRgbTables.u_b_table;
	const int32* uGTable = gYuvRgbTables.u_g_table;
	const int32* vRTable = gYuvRgbTables.v_r_table;
	const int32* vGTable = gYuvRgbTables.v_g_table;

	const unsigned char* yPlane = src;
	const unsigned char* uvPlane = src + (width * height);
	size_t dstStride = (size_t)width * 4;

	// Process two rows at a time (they share the same UV values)
	for (int32 row = 0; row < height; row += 2) {
		unsigned char* dstRow0 = dst + row * dstStride;
		unsigned char* dstRow1 = (row + 1 < height) ? dst + (row + 1) * dstStride : dstRow0;
		const unsigned char* yRow0 = yPlane + row * width;
		const unsigned char* yRow1 = (row + 1 < height) ? yPlane + (row + 1) * width : yRow0;
		const unsigned char* uvRow = uvPlane + (row / 2) * width;

		for (int32 col = 0; col < width; col += 2) {
			// Get UV values (shared by 2x2 block)
			uint8 u = uvRow[col];
			uint8 v = uvRow[col + 1];

			// Pre-compute UV contributions
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			// Process 2x2 block
			for (int dy = 0; dy < 2 && (row + dy) < height; dy++) {
				unsigned char* dstPixel = (dy == 0) ? dstRow0 + col * 4 : dstRow1 + col * 4;
				const unsigned char* yPixel = (dy == 0) ? yRow0 + col : yRow1 + col;

				for (int dx = 0; dx < 2 && (col + dx) < width; dx++) {
					uint8 y = yPixel[dx];
					int32 yVal = yTable[y];

					// BGRA output
					dstPixel[dx * 4 + 0] = clamp255((yVal + uB + 128) >> 8);      // B
					dstPixel[dx * 4 + 1] = clamp255((yVal + uG + vG + 128) >> 8); // G
					dstPixel[dx * 4 + 2] = clamp255((yVal + vR + 128) >> 8);      // R
					dstPixel[dx * 4 + 3] = 255;                                    // A
				}
			}
		}
	}
}


void
UVCCamDevice::_ConvertUYVYtoRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// UYVY (YUV 4:2:2 packed) is the byte-swapped variant of YUY2.
	// Byte order: U Y0 V Y1 (vs YUY2's Y0 U Y1 V).
	if (!dst || !src || width <= 0 || height <= 0)
		return;
	if (!gYuvRgbTables.initialized)
		gYuvRgbTables.Initialize();

	const int32* yTable = gYuvRgbTables.y_table;
	const int32* uBTable = gYuvRgbTables.u_b_table;
	const int32* uGTable = gYuvRgbTables.u_g_table;
	const int32* vRTable = gYuvRgbTables.v_r_table;
	const int32* vGTable = gYuvRgbTables.v_g_table;

	const size_t expectedSize = (size_t)width * height * 2;
	size_t srcStride = (size_t)width * 2;
	const size_t dstStride = (size_t)width * 4;

	if (srcSize > expectedSize && height > 1) {
		size_t actualStride = srcSize / height;
		if (actualStride > srcStride && actualStride <= srcStride + 256)
			srcStride = actualStride;
	}

	for (int32 row = 0; row < height; row++) {
		const unsigned char* srcRow = src + row * srcStride;
		unsigned char* dstRow = dst + row * dstStride;
		if ((size_t)(srcRow - src) + srcStride > srcSize)
			break;
		// Full macro-pixels first; odd-width tail hoisted out of the
		// loop (same ~10% win as the YUY2 path, identical shape).
		for (int32 x = 0; x + 1 < width; x += 2) {
			uint8 u  = srcRow[0];
			uint8 y0 = srcRow[1];
			uint8 v  = srcRow[2];
			uint8 y1 = srcRow[3];

			int32 yVal0 = yTable[y0];
			int32 yVal1 = yTable[y1];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			dstRow[0] = clamp255((yVal0 + uB + 128) >> 8);
			dstRow[1] = clamp255((yVal0 + uG + vG + 128) >> 8);
			dstRow[2] = clamp255((yVal0 + vR + 128) >> 8);
			dstRow[3] = 255;
			dstRow[4] = clamp255((yVal1 + uB + 128) >> 8);
			dstRow[5] = clamp255((yVal1 + uG + vG + 128) >> 8);
			dstRow[6] = clamp255((yVal1 + vR + 128) >> 8);
			dstRow[7] = 255;
			dstRow += 8;
			srcRow += 4;
		}
		if (width & 1) {
			// Odd width: lone trailing pixel, no paired chroma-V available.
			uint8 u  = srcRow[0];
			uint8 y0 = srcRow[1];
			int32 yVal0 = yTable[y0];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[128];
			int32 vG = vGTable[128];
			dstRow[0] = clamp255((yVal0 + uB + 128) >> 8);
			dstRow[1] = clamp255((yVal0 + uG + vG + 128) >> 8);
			dstRow[2] = clamp255((yVal0 + vR + 128) >> 8);
			dstRow[3] = 255;
		}
	}
}


void
UVCCamDevice::_ConvertNV21toRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// NV21: identical layout to NV12, but the interleaved chroma plane
	// stores V before U (V0 U0 V1 U1 ...).
	if (!dst || !src || width <= 0 || height <= 0)
		return;
	// FIX: see NV12 — odd dimensions have no defined 4:2:0 layout.
	if ((width & 1) || (height & 1)) {
		memset(dst, 0, (size_t)width * height * 4);
		return;
	}
	if (!gYuvRgbTables.initialized)
		gYuvRgbTables.Initialize();

	const size_t expectedSize = (size_t)width * height * 3 / 2;
	if (srcSize < expectedSize) {
		memset(dst, 0, (size_t)width * height * 4);
		return;
	}

	const int32* yTable = gYuvRgbTables.y_table;
	const int32* uBTable = gYuvRgbTables.u_b_table;
	const int32* uGTable = gYuvRgbTables.u_g_table;
	const int32* vRTable = gYuvRgbTables.v_r_table;
	const int32* vGTable = gYuvRgbTables.v_g_table;

	const unsigned char* yPlane = src;
	const unsigned char* vuPlane = src + (size_t)width * height;
	const size_t dstStride = (size_t)width * 4;

	for (int32 row = 0; row < height; row += 2) {
		unsigned char* dstRow0 = dst + row * dstStride;
		unsigned char* dstRow1 = (row + 1 < height)
			? dst + (row + 1) * dstStride : dstRow0;
		const unsigned char* yRow0 = yPlane + row * width;
		const unsigned char* yRow1 = (row + 1 < height)
			? yPlane + (row + 1) * width : yRow0;
		const unsigned char* vuRow = vuPlane + (row / 2) * width;

		for (int32 col = 0; col < width; col += 2) {
			uint8 v = vuRow[col];
			uint8 u = vuRow[col + 1];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			for (int dy = 0; dy < 2 && (row + dy) < height; dy++) {
				unsigned char* dstPixel = (dy == 0)
					? dstRow0 + col * 4 : dstRow1 + col * 4;
				const unsigned char* yPixel = (dy == 0)
					? yRow0 + col : yRow1 + col;
				for (int dx = 0; dx < 2 && (col + dx) < width; dx++) {
					int32 yVal = yTable[yPixel[dx]];
					dstPixel[dx * 4 + 0] = clamp255((yVal + uB + 128) >> 8);
					dstPixel[dx * 4 + 1] = clamp255((yVal + uG + vG + 128) >> 8);
					dstPixel[dx * 4 + 2] = clamp255((yVal + vR + 128) >> 8);
					dstPixel[dx * 4 + 3] = 255;
				}
			}
		}
	}
}


void
UVCCamDevice::_ConvertPlanar420toRGB32(unsigned char* dst,
	const unsigned char* yPlane, const unsigned char* uPlane,
	const unsigned char* vPlane, int32 width, int32 height)
{
	// Shared planar 4:2:0 conversion. Caller computes plane pointers per
	// format (I420 vs YV12 swap U and V); chroma planes are width/2 wide.
	// FIX: odd dimensions under-allocate the chroma planes (ySize/4
	// truncation) and make col/2 walk off; fail closed like NV12/NV21.
	if (dst == NULL || yPlane == NULL || uPlane == NULL || vPlane == NULL
		|| width <= 0 || height <= 0 || (width & 1) || (height & 1)) {
		if (dst != NULL && width > 0 && height > 0
			&& width <= 8192 && height <= 8192) {
			memset(dst, 0, (size_t)width * height * 4);
		}
		return;
	}
	if (!gYuvRgbTables.initialized)
		gYuvRgbTables.Initialize();

	const int32* yTable = gYuvRgbTables.y_table;
	const int32* uBTable = gYuvRgbTables.u_b_table;
	const int32* uGTable = gYuvRgbTables.u_g_table;
	const int32* vRTable = gYuvRgbTables.v_r_table;
	const int32* vGTable = gYuvRgbTables.v_g_table;

	const int32 chromaWidth = width / 2;
	const size_t dstStride = (size_t)width * 4;

	for (int32 row = 0; row < height; row += 2) {
		unsigned char* dstRow0 = dst + row * dstStride;
		unsigned char* dstRow1 = (row + 1 < height)
			? dst + (row + 1) * dstStride : dstRow0;
		const unsigned char* yRow0 = yPlane + row * width;
		const unsigned char* yRow1 = (row + 1 < height)
			? yPlane + (row + 1) * width : yRow0;
		const unsigned char* uRow = uPlane + (row / 2) * chromaWidth;
		const unsigned char* vRow = vPlane + (row / 2) * chromaWidth;

		for (int32 col = 0; col < width; col += 2) {
			uint8 u = uRow[col / 2];
			uint8 v = vRow[col / 2];
			int32 uB = uBTable[u];
			int32 uG = uGTable[u];
			int32 vR = vRTable[v];
			int32 vG = vGTable[v];

			for (int dy = 0; dy < 2 && (row + dy) < height; dy++) {
				unsigned char* dstPixel = (dy == 0)
					? dstRow0 + col * 4 : dstRow1 + col * 4;
				const unsigned char* yPixel = (dy == 0)
					? yRow0 + col : yRow1 + col;
				for (int dx = 0; dx < 2 && (col + dx) < width; dx++) {
					int32 yVal = yTable[yPixel[dx]];
					dstPixel[dx * 4 + 0] = clamp255((yVal + uB + 128) >> 8);
					dstPixel[dx * 4 + 1] = clamp255((yVal + uG + vG + 128) >> 8);
					dstPixel[dx * 4 + 2] = clamp255((yVal + vR + 128) >> 8);
					dstPixel[dx * 4 + 3] = 255;
				}
			}
		}
	}
}


void
UVCCamDevice::_ConvertI420toRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// I420 / IYUV: planar Y, then U plane, then V plane.
	if (!dst || !src || width <= 0 || height <= 0)
		return;
	const size_t ySize = (size_t)width * height;
	const size_t chromaSize = ySize / 4;
	if (srcSize < ySize + 2 * chromaSize) {
		memset(dst, 0, (size_t)width * height * 4);
		return;
	}
	_ConvertPlanar420toRGB32(dst, src, src + ySize, src + ySize + chromaSize,
		width, height);
}


void
UVCCamDevice::_ConvertYV12toRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// YV12: planar Y, then V plane, then U plane (U/V swapped vs I420).
	if (!dst || !src || width <= 0 || height <= 0)
		return;
	const size_t ySize = (size_t)width * height;
	const size_t chromaSize = ySize / 4;
	if (srcSize < ySize + 2 * chromaSize) {
		memset(dst, 0, (size_t)width * height * 4);
		return;
	}
	_ConvertPlanar420toRGB32(dst, src, src + ySize + chromaSize, src + ySize,
		width, height);
}


void
UVCCamDevice::_ConvertGREYtoRGB32(unsigned char* dst, const unsigned char* src,
	size_t srcSize, int32 width, int32 height)
{
	// 8-bit monochrome: each source byte is the Y luma; replicate to BGR.
	if (!dst || !src || width <= 0 || height <= 0)
		return;
	const size_t expected = (size_t)width * height;
	if (srcSize < expected) {
		memset(dst, 0, (size_t)width * height * 4);
		return;
	}
	const size_t pixels = expected;
	for (size_t i = 0; i < pixels; i++) {
		uint8 y = src[i];
		dst[i * 4 + 0] = y;
		dst[i * 4 + 1] = y;
		dst[i * 4 + 2] = y;
		dst[i * 4 + 3] = 255;
	}
}


const char*
UVCCamDevice::_UncompressedFormatName(uvc_uncompressed_format fmt) const
{
	switch (fmt) {
		case UVC_FMT_YUY2: return "YUY2";
		case UVC_FMT_UYVY: return "UYVY";
		case UVC_FMT_NV12: return "NV12";
		case UVC_FMT_NV21: return "NV21";
		case UVC_FMT_YV12: return "YV12";
		case UVC_FMT_I420: return "I420";
		case UVC_FMT_GREY: return "GREY";
		default: return "UNKNOWN";
	}
}


const char*
UVCCamDevice::_FrameBasedCodecName(uvc_frame_based_codec codec) const
{
	switch (codec) {
		case UVC_CODEC_H264:		return "H.264";
		case UVC_CODEC_H265:		return "H.265";
		case UVC_CODEC_VP8:			return "VP8";
		case UVC_CODEC_MJPEG2000:	return "M-JPEG2000";
		default:					return "UNKNOWN";
	}
}


// P3 Fase B
// ----------------------------------------------------------------------------

int32
UVCCamDevice::NumStreams() const
{
	return fVSStreams.CountItems();
}


void
UVCCamDevice::GetStreamName(int32 idx, BString* out) const
{
	if (out == NULL)
		return;
	const uvc_vs_stream* s = (const uvc_vs_stream*)fVSStreams.ItemAt(idx);
	if (s == NULL) {
		out->SetTo("(invalid stream)");
		return;
	}
	const char* kind = "Stream";
	if (s->mjpeg_count > 0 && s->uncompressed_count > 0)
		kind = "MJPEG+YUV";
	else if (s->mjpeg_count > 0)
		kind = "MJPEG";
	else if (s->uncompressed_count > 0)
		kind = "Uncompressed";
	else if (s->frame_based_count > 0)
		kind = "Encoded";
	out->SetToFormat("Stream %d (%s, intf=%u)", (int)idx, kind,
		(unsigned)s->interface_index);
}


void
UVCCamDevice::_ResetStreamFormatState()
{
	for (int32 i = 0; i < fUncompressedFrames.CountItems(); i++)
		delete (usb_video_frame_descriptor*)fUncompressedFrames.ItemAt(i);
	fUncompressedFrames.MakeEmpty();
	for (int32 i = 0; i < fMJPEGFrames.CountItems(); i++)
		delete (usb_video_frame_descriptor*)fMJPEGFrames.ItemAt(i);
	fMJPEGFrames.MakeEmpty();
	for (int32 i = 0; i < fFrameBasedFrames.CountItems(); i++)
		delete (uvc_frame_based_resolution*)fFrameBasedFrames.ItemAt(i);
	fFrameBasedFrames.MakeEmpty();

	fUncompressedFormatIndex = 0;
	fUncompressedFrameIndex = 0;
	fUncompressedPixelFormat = UVC_FMT_UNKNOWN;
	fIsNV12 = false;
	fDefaultUncompressedFrameIndex = 0;

	fMJPEGFormatIndex = 0;
	fMJPEGFrameIndex = 0;
	fDefaultMJPEGFrameIndex = 0;

	fFrameBasedCodec = UVC_CODEC_UNKNOWN;
	fFrameBasedFormatIndex = 0;
	fFrameBasedBitsPerPixel = 0;

	fStillCaptureMethod = STILL_CAPTURE_NONE;
	fTriggerSupport = false;
	fTriggerUsage = false;

	fNumFrameIntervals = 0;
	fSelectedFrameIntervalIndex = 0;
	fSortedMJPEGCount = 0;
	fSortedUncompressedCount = 0;
}


status_t
UVCCamDevice::_ReparseVSInterface(uint32 ifaceIndex)
{
	if (fDevice == NULL)
		return B_NO_INIT;
	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	if (config == NULL)
		return B_NO_INIT;
	const BUSBInterface* target = NULL;
	for (uint32 j = 0; j < config->CountInterfaces(); j++) {
		const BUSBInterface* candidate = config->InterfaceAt(j);
		if (candidate != NULL && candidate->Index() == ifaceIndex) {
			target = candidate;
			break;
		}
	}
	if (target == NULL)
		return B_BAD_INDEX;

	uint8 buffer[1024];
	usb_descriptor* generic = (usb_descriptor*)buffer;

	for (uint32 k = 0; target->OtherDescriptorAt(k, generic, sizeof(buffer))
			== B_OK; k++) {
		if (generic->generic.descriptor_type
				!= (USB_REQTYPE_CLASS | USB_DESCRIPTOR_INTERFACE))
			continue;
		_ParseVideoStreaming((const usbvc_class_descriptor*)generic,
			generic->generic.length);
	}

	if (fUncompressedFrames.CountItems() == 0
			&& fMJPEGFrames.CountItems() == 0) {
		// Some firmwares only expose the class-specific descriptors on
		// one of the bandwidth-allocating alternates.
		for (uint32 alt = 0; alt < target->CountAlternates(); alt++) {
			const BUSBInterface* alternate = target->AlternateAt(alt);
			if (alternate == NULL)
				continue;
			for (uint32 k = 0; alternate->OtherDescriptorAt(k, generic,
					sizeof(buffer)) == B_OK; k++) {
				if (generic->generic.descriptor_type
						!= (USB_REQTYPE_CLASS | USB_DESCRIPTOR_INTERFACE))
					continue;
				_ParseVideoStreaming(
					(const usbvc_class_descriptor*)generic,
					generic->generic.length);
			}
			if (fUncompressedFrames.CountItems() > 0
					|| fMJPEGFrames.CountItems() > 0)
				break;
		}
	}
	return B_OK;
}


status_t
UVCCamDevice::SelectStream(int32 idx)
{
	if (idx < 0 || idx >= fVSStreams.CountItems())
		return B_BAD_INDEX;
	// Hold the device lock for check and switch.
	// StartTransfer uses the same lock, so no start can slip in.
	BAutolock deviceLock(Locker());
	if (atomic_get(&fTransferEnabled) != 0)
		return B_BUSY;
	if (idx == fActiveStreamIdx)
		return B_OK;
	const uvc_vs_stream* target
		= (const uvc_vs_stream*)fVSStreams.ItemAt(idx);
	if (target == NULL)
		return B_BAD_INDEX;

	syslog(LOG_INFO, "UVCCamDevice: SelectStream(%d): switching from "
		"intf=%u to intf=%u\n",
		(int)idx, (unsigned)fStreamingIndex,
		(unsigned)target->interface_index);

	// FIX: keep the old stream alive until the new one proves parseable.
	// The previous code reset the format lists and switched fStreamingIndex
	// before the reparse, so a failure left empty lists + a new index.
	uint32 oldStreamingIndex = fStreamingIndex;
	int32 oldActiveIdx = fActiveStreamIdx;
	_ResetStreamFormatState();
	fStreamingIndex = target->interface_index;
	fIsoIn = NULL;
	fIsoMaxPacketSize = 0;
	fCurrentVideoAlternate = 0;

	status_t err = _ReparseVSInterface(target->interface_index);
	if (err != B_OK
		|| (fUncompressedFrames.CountItems() == 0
			&& fMJPEGFrames.CountItems() == 0)) {
		syslog(LOG_ERR, "UVCCamDevice: SelectStream(%d): reparse failed: %s — "
			"restoring stream %d\n",
			(int)idx, strerror(err), (int)oldActiveIdx);
		_ResetStreamFormatState();
		fStreamingIndex = oldStreamingIndex;
		fIsoIn = NULL;
		fIsoMaxPacketSize = 0;
		fCurrentVideoAlternate = 0;
		_ReparseVSInterface(oldStreamingIndex);
		_BuildSortedResolutionList();
		return err != B_OK ? err : B_ERROR;
	}

	fActiveStreamIdx = idx;

	// Decide MJPEG vs uncompressed exactly like the ctor would.
	if (fMJPEGFrames.CountItems() > 0 && fJpegDecompressor != NULL)
		fIsMJPEG = true;
	else
		fIsMJPEG = false;
	_BuildSortedResolutionList();

	// Reset the resolution selector to whatever the new stream advertises
	// as default; the consumer can override later via AcceptVideoFrame.
	fSelectedResolutionIndex = 0;
	return B_OK;
}


size_t
UVCCamDevice::_UncompressedFrameSize(uvc_uncompressed_format fmt,
	int32 width, int32 height) const
{
	// Reject bad sizes, negative would wrap as huge size_t.
	if (width <= 0 || height <= 0)
		return 0;
	switch (fmt) {
		case UVC_FMT_YUY2:
		case UVC_FMT_UYVY:
			return (size_t)width * height * 2;		// 4:2:2 packed
		case UVC_FMT_NV12:
		case UVC_FMT_NV21:
		case UVC_FMT_I420:
		case UVC_FMT_YV12:
			return (size_t)width * height * 3 / 2;	// 4:2:0 planar
		case UVC_FMT_GREY:
			return (size_t)width * height;
		default:
			return (size_t)width * height * 2;		// safe default
	}
}


// FIX BUG 6: MJPEG counters are now instance members (see header)

void
UVCCamDevice::_DecompressMJPEGtoRGB32(unsigned char* dst,
                                       const unsigned char* src,
                                       size_t srcSize,
                                       int32 width, int32 height)
{
	atomic_add(&fMjpegAttempts, 1);

	// FIX: a 1-byte frame reached jpegStart[1] out of bounds below.
	if (!fJpegDecompressor || !dst || !src || srcSize < 2 || width <= 0 || height <= 0)
		return;

	// Shared handle is not thread safe, hold the lock for decode.
	BAutolock jpegLock(fJpegLock);

	// Find JPEG SOI marker (0xFF 0xD8) - UVC may have header before JPEG data
	const unsigned char* jpegStart = src;
	size_t jpegSize = srcSize;
	size_t scanLimit = srcSize < 2048 ? srcSize : 2048;

	for (size_t i = 0; i < scanLimit - 1; i++) {
		if (src[i] == 0xFF && src[i + 1] == 0xD8) {
			jpegStart = src + i;
			jpegSize = srcSize - i;
			break;
		}
	}

	// Verify we found JPEG data
	if (jpegStart[0] != 0xFF || jpegStart[1] != 0xD8) {
		atomic_add(&fMjpegNoSOI, 1);
		// Log first few failures and then periodically
		int32 noSOI = atomic_get(&fMjpegNoSOI);
		if (noSOI <= 5 || (noSOI % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: No SOI marker #%d, srcSize=%zu, first bytes=[%02x %02x %02x %02x]\n",
				(int)noSOI, srcSize,
				srcSize > 0 ? src[0] : 0, srcSize > 1 ? src[1] : 0,
				srcSize > 2 ? src[2] : 0, srcSize > 3 ? src[3] : 0);
		}
		return;
	}

	/* Task 5: Enhanced MJPEG decompression for various resolutions */
	/* First, get actual JPEG dimensions from header */
	int jpegWidth = 0, jpegHeight = 0, jpegSubsamp = 0, jpegColorspace = 0;
	int headerResult = tjDecompressHeader3(fJpegDecompressor, jpegStart, jpegSize,
		&jpegWidth, &jpegHeight, &jpegSubsamp, &jpegColorspace);

	if (headerResult != 0) {
		atomic_add(&fMjpegDecompressErrors, 1);
		int32 headerErrors = atomic_get(&fMjpegDecompressErrors);
		if (headerErrors <= 5 || (headerErrors % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: Header decode failed #%d: %s\n",
				(int)headerErrors, tjGetErrorStr2(fJpegDecompressor));
		}
		return;
	}

	/* Warn if JPEG dimensions don't match expected output */
	if (jpegWidth != width || jpegHeight != height) {
		/* Check if we're in resolution transition grace period (500ms after change) */
		bigtime_t now = system_time();
		bool inTransition = (fResolutionTransitionStart > 0) &&
		                    (now - fResolutionTransitionStart < 500000);

		if (inTransition) {
			/* Silently skip frames during transition - camera is still switching */
			static int32 sTransitionSkipped = 0;
			if (++sTransitionSkipped <= 3) {
				syslog(LOG_INFO, "MJPEG: Skipping transition frame #%d (JPEG=%dx%d, expected=%dx%d)\n",
					(int)sTransitionSkipped, jpegWidth, jpegHeight, (int)width, (int)height);
			}
			return;
		}

		static int32 sDimensionMismatch = 0;
		if (++sDimensionMismatch <= 5 || (sDimensionMismatch % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: Dimension mismatch #%d: JPEG=%dx%d, expected=%dx%d\n",
				(int)sDimensionMismatch, jpegWidth, jpegHeight, (int)width, (int)height);
		}
		/* If JPEG is larger than buffer, we cannot decompress - skip frame */
		if (jpegWidth > width || jpegHeight > height) {
			syslog(LOG_ERR, "MJPEG: JPEG too large for buffer: JPEG=%dx%d, buffer=%dx%d, skipping\n",
				jpegWidth, jpegHeight, (int)width, (int)height);
			return;
		}
	}

	/* FIX: Use actual JPEG dimensions for decompression, not expected dimensions.
	 * When JPEG dimensions differ from expected, using expected width for pitch
	 * causes horizontal offset/stair-step artifacts because TurboJPEG writes
	 * jpegWidth pixels per row but the pitch assumes width pixels per row.
	 *
	 * Strategy: Decompress to actual JPEG dimensions with matching pitch.
	 * If JPEG is smaller, it will appear in top-left corner on the dark blue
	 * background (buffer was pre-filled in FillFrameBuffer).
	 */
	int decompressWidth = jpegWidth;
	int decompressHeight = jpegHeight;
	int decompressPitch = decompressWidth * 4;

	// If JPEG matches expected, use expected dimensions (normal case)
	if (jpegWidth == width && jpegHeight == height) {
		decompressWidth = width;
		decompressHeight = height;
		decompressPitch = width * 4;
	}

	// Decompress directly to BGRA (RGB32 on Haiku)
	int result = tjDecompress2(fJpegDecompressor, jpegStart, jpegSize, dst,
	              decompressWidth, decompressPitch, decompressHeight, TJPF_BGRA,
	              TJFLAG_FASTDCT | TJFLAG_NOREALLOC);

	if (result == 0) {
		atomic_add(&fMjpegSuccess, 1);

		/* Clear resolution transition state on first successful frame */
		if (fResolutionTransitionStart > 0) {
			bigtime_t transitionDuration = system_time() - fResolutionTransitionStart;
			syslog(LOG_INFO, "MJPEG: Resolution transition complete after %lld ms, first valid %dx%d frame\n",
				transitionDuration / 1000, (int)width, (int)height);
			fResolutionTransitionStart = 0;
		}

		/* Log periodic success stats at high resolutions */
		int32 decoded = atomic_get(&fMjpegSuccess);
		if (width >= 1280 && (decoded % 300) == 0) {
			syslog(LOG_INFO, "MJPEG %dx%d: %d frames decoded (errors: %d, no SOI: %d)\n",
				(int)width, (int)height, (int)decoded,
				(int)atomic_get(&fMjpegDecompressErrors),
				(int)atomic_get(&fMjpegNoSOI));
		}
	} else {
		atomic_add(&fMjpegDecompressErrors, 1);
		int32 decodeErrors = atomic_get(&fMjpegDecompressErrors);
		if (decodeErrors <= 5 || (decodeErrors % 100) == 0) {
			syslog(LOG_WARNING, "MJPEG: Decompress failed #%d at %dx%d: %s (src=%zu bytes)\n",
				(int)decodeErrors, (int)width, (int)height,
				tjGetErrorStr2(fJpegDecompressor), jpegSize);
		}
	}
}


// =============================================================================
// Feature 1: Frame Validation Methods
// =============================================================================


frame_validation_result
UVCCamDevice::_ValidateMJPEGFrame(const uint8* data, size_t size)
{
	// Check minimum size
	if (size < kMinMJPEGFrameSize) {
		return FRAME_CORRUPTED_TRUNCATED;
	}

	// Check for SOI marker (0xFF 0xD8) at start
	if (data[0] != 0xFF || data[1] != 0xD8) {
		return FRAME_CORRUPTED_NO_SOI;
	}

	// Check for EOI marker (0xFF 0xD9) near end
	// Search in last 32 bytes for robustness
	bool foundEOI = false;
	size_t searchStart = (size > 32) ? size - 32 : 0;
	for (size_t i = searchStart; i < size - 1; i++) {
		if (data[i] == 0xFF && data[i + 1] == 0xD9) {
			foundEOI = true;
			break;
		}
	}

	if (!foundEOI) {
		return FRAME_CORRUPTED_NO_EOI;
	}

	return FRAME_VALID;
}


frame_validation_result
UVCCamDevice::_ValidateYUY2Frame(const uint8* data, size_t size,
	int32 width, int32 height)
{
	(void)data;  // Unused for now, just size check
	size_t expectedSize = (size_t)width * height * 2;  // YUY2 is 2 bytes per pixel

	if (size < (expectedSize * kMinYUY2FramePercent / 100)) {
		return FRAME_INCOMPLETE;
	}

	return FRAME_VALID;
}


bool
UVCCamDevice::_FindJpegMarker(const uint8* data, size_t size,
	uint8 marker, size_t* position)
{
	// FIX: size == 0 made size - 1 wrap to SIZE_MAX and walk off the buffer.
	// (Currently dead code — no callers — but safe for future reuse.)
	if (data == NULL || size < 2)
		return false;
	for (size_t i = 0; i + 1 < size; i++) {
		if (data[i] == 0xFF && data[i + 1] == marker) {
			if (position)
				*position = i;
			return true;
		}
	}
	return false;
}


void
UVCCamDevice::_ReportValidationStats()
{
	bigtime_t now = system_time();

	// Report every kFrameValidationReportInterval seconds if there are errors
	if ((now - fValidationStats.last_stats_report_time) <
		(bigtime_t)kFrameValidationReportInterval * 1000000) {
		return;
	}

	fValidationStats.last_stats_report_time = now;

	uint32 totalErrors = fValidationStats.frames_incomplete +
		fValidationStats.frames_no_soi + fValidationStats.frames_no_eoi;

	if (totalErrors > 0) {
		syslog(LOG_INFO, "UVCCamDevice: Frame validation - valid: %u, "
			"incomplete: %u, no_soi: %u, no_eoi: %u, repeated: %u\n",
			fValidationStats.frames_valid,
			fValidationStats.frames_incomplete,
			fValidationStats.frames_no_soi,
			fValidationStats.frames_no_eoi,
			fValidationStats.frames_repeated);
	}
}


