/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include "UVCDeframer.h"

#include "CamDebug.h"
#include "CamDevice.h"

#include <Autolock.h>
#include <syslog.h>

#define MAX_TAG_LEN CAMDEFRAMER_MAX_TAG_LEN
#define MAXFRAMEBUF CAMDEFRAMER_MAX_QUEUED_FRAMES


UVCDeframer::UVCDeframer(CamDevice* device)
	: CamDeframer(device),
	fFrameCount(0),
	fID(0),
	fExpectedFrameSize(0),
	fFixedBuffer(NULL),
	fFixedBufferSize(0),
	fFixedBufferPos(0),
	fFramesCompleted(0),
	fFramesIncomplete(0),
	fFIDChanges(0),
	fQueueOverflows(0),
	fPacketsThisFrame(0),
	fTotalBytesThisFrame(0),
	fLastDiagReport(0)
{
	// Allocate fixed buffer for YUY2 frames (max 1920x1080x2 = 4MB)
	fFixedBufferSize = 4 * 1024 * 1024;
	fFixedBuffer = (uint8*)malloc(fFixedBufferSize);
	if (fFixedBuffer) {
		syslog(LOG_INFO, "UVCDeframer: Allocated %zu byte fixed buffer\n", fFixedBufferSize);
	} else {
		syslog(LOG_ERR, "UVCDeframer: Failed to allocate fixed buffer!\n");
	}
}


UVCDeframer::~UVCDeframer()
{
	if (fFixedBuffer) {
		free(fFixedBuffer);
		fFixedBuffer = NULL;
	}
}


deframer_stats
UVCDeframer::GetStats() const
{
	deframer_stats stats;
	stats.frames_completed = fFramesCompleted;
	stats.frames_incomplete = fFramesIncomplete;
	stats.fid_changes = fFIDChanges;
	stats.queue_overflows = fQueueOverflows;
	stats.last_report_time = fLastDiagReport;
	stats.expected_frame_size = fExpectedFrameSize;
	return stats;
}


void
UVCDeframer::ResetStats()
{
	fFramesCompleted = 0;
	fFramesIncomplete = 0;
	fFIDChanges = 0;
	fQueueOverflows = 0;
	fLastDiagReport = system_time();
}


void
UVCDeframer::SetExpectedFrameSize(size_t size)
{
	fExpectedFrameSize = size;
	syslog(LOG_INFO, "UVCDeframer: SetExpectedFrameSize(%zu) this=%p fFrameSem=%d\n",
		size, (void*)this, fFrameSem);
}


status_t
UVCDeframer::Flush()
{
	// First call base class to clear queued frames
	status_t err = CamDeframer::Flush();

	// Also clear our local input buffer
	fInputBuffer.Seek(0, SEEK_SET);
	fInputBuffer.SetSize(0);
	fFixedBufferPos = 0;  // Reset fixed buffer too

	// Reset UVC-specific state
	fID = 0;
	fPacketsThisFrame = 0;

	syslog(LOG_INFO, "UVCDeframer: Flush complete (completed=%d, incomplete=%d)\n",
		(int)fFramesCompleted, (int)fFramesIncomplete);

	return err;
}


ssize_t
UVCDeframer::Write(const void* buffer, size_t size)
{
	const uint8* buf = (const uint8*)buffer;

	// Track packets for this frame
	fPacketsThisFrame++;

	// Validate buffer and header length
	// UVC header requires at least 2 bytes: length byte + flags byte
	if (size < 2 || buf[0] < 2 || buf[0] > size) {
		// Log occasional header errors
		static int32 sHeaderErrors = 0;
		if (++sHeaderErrors <= 5)
			syslog(LOG_WARNING, "UVCDeframer: Invalid header size=%zu hdr[0]=%d\n",
				size, size > 0 ? buf[0] : -1);
		return B_ERROR;
	}

	int payloadSize = size - buf[0];

	// Header-only packet
	if (payloadSize == 0)
		return size;

	// Check for UVC error bit (bit 6 of header byte 1)
	if (buf[1] & 0x40) {
		static int32 sUvcErrors = 0;
		if (++sUvcErrors <= 10)
			syslog(LOG_WARNING, "UVCDeframer: UVC error bit set in header\n");
	}

	// Debug: Log first few packets of first few frames to check header structure
	static int32 sDebugFrames = 0;
	static int32 sDebugPackets = 0;
	if (sDebugFrames < 3 && sDebugPackets < 15) {
		bool hasPTS = (buf[1] & 0x04) != 0;
		bool hasSCR = (buf[1] & 0x08) != 0;
		int expectedHeaderLen = 2 + (hasPTS ? 4 : 0) + (hasSCR ? 6 : 0);
		if (buf[0] != expectedHeaderLen) {
			syslog(LOG_WARNING, "UVCDeframer: Header mismatch! bHeaderLength=%d expected=%d (PTS=%d SCR=%d) pkt=%zu\n",
				buf[0], expectedHeaderLen, hasPTS ? 1 : 0, hasSCR ? 1 : 0, size);
		}
		sDebugPackets++;
	}

	// Detect FID (Frame ID) changes for BOTH YUY2 and MJPEG
	// FID bit toggles when a new frame starts
	bool eof = (buf[1] & 0x02) != 0;
	bool fidChanged = (buf[1] & 0x01) != fID;

	// If FID changed, this is start of a NEW frame
	if (fidChanged) {
		fFIDChanges++;
		fID = buf[1] & 0x01;

		// Log previous frame's total bytes (before reset)
		static int32 sBytesLog = 0;
		if (++sBytesLog <= 10 && fTotalBytesThisFrame > 0) {
			syslog(LOG_INFO, "UVCDeframer: Previous frame total bytes=%zu (expected=%zu diff=%d)\n",
				fTotalBytesThisFrame, fExpectedFrameSize,
				(int)((ssize_t)fTotalBytesThisFrame - (ssize_t)fExpectedFrameSize));
		}

		// Track debug frames and dump first packet header
		if (sDebugFrames < 3) {
			sDebugFrames++;
			syslog(LOG_INFO, "UVCDeframer: New frame #%d started (FID=%d pkts=%d bufSize=%zu)\n",
				(int)sDebugFrames, fID, (int)fPacketsThisFrame, fFixedBufferPos);
			// Dump first 16 bytes of this packet (header + start of payload)
			char hexbuf[80];
			int dumpLen = (size < 16) ? size : 16;
			snprintf(hexbuf, sizeof(hexbuf),
				"First pkt header[%d]: %02x %02x %02x %02x %02x %02x %02x %02x "
				"%02x %02x %02x %02x %02x %02x %02x %02x",
				(int)size,
				dumpLen > 0 ? buf[0] : 0, dumpLen > 1 ? buf[1] : 0,
				dumpLen > 2 ? buf[2] : 0, dumpLen > 3 ? buf[3] : 0,
				dumpLen > 4 ? buf[4] : 0, dumpLen > 5 ? buf[5] : 0,
				dumpLen > 6 ? buf[6] : 0, dumpLen > 7 ? buf[7] : 0,
				dumpLen > 8 ? buf[8] : 0, dumpLen > 9 ? buf[9] : 0,
				dumpLen > 10 ? buf[10] : 0, dumpLen > 11 ? buf[11] : 0,
				dumpLen > 12 ? buf[12] : 0, dumpLen > 13 ? buf[13] : 0,
				dumpLen > 14 ? buf[14] : 0, dumpLen > 15 ? buf[15] : 0);
			syslog(LOG_INFO, "UVCDeframer: %s\n", hexbuf);
		}

		// For YUY2: discard incomplete previous frame data and start fresh
		// For MJPEG: complete previous frame if we have data
		if (fExpectedFrameSize == 0 && fInputBuffer.BufferLength() > 0) {
			// MJPEG: complete previous frame
			BAutolock l(fLocker);
			int32 queueCount = fFrames.CountItems();

			if (queueCount >= MAXFRAMEBUF) {
				fQueueOverflows++;
				if (fQueueOverflows <= 10 || (fQueueOverflows % 100) == 0)
					syslog(LOG_WARNING, "UVCDeframer: Queue overflow #%d (MAXFRAMEBUF=%d)\n",
						(int)fQueueOverflows, MAXFRAMEBUF);
			}

			if (fCurrentFrame == NULL && queueCount < MAXFRAMEBUF)
				fCurrentFrame = AllocFrame();

			if (fCurrentFrame != NULL) {
				const uint8* frameData = (const uint8*)fInputBuffer.Buffer();
				size_t frameSize = fInputBuffer.BufferLength();

				fFrameCount++;
				fFramesCompleted++;

				if (frameData != NULL && frameSize > 0)
					fCurrentFrame->Write(frameData, frameSize);

				fFrames.AddItem(fCurrentFrame);
				release_sem(fFrameSem);
				fCurrentFrame = NULL;
			}
		}

		// Reset buffer for new frame (both YUY2 and MJPEG)
		fInputBuffer.Seek(0, SEEK_SET);
		fInputBuffer.SetSize(0);
		fFixedBufferPos = 0;  // Also reset fixed buffer
		fPacketsThisFrame = 1;
		fTotalBytesThisFrame = 0;  // Reset byte counter for new frame
	}

	// Allocate frame if needed
	if (fCurrentFrame == NULL) {
		BAutolock l(fLocker);
		if (fFrames.CountItems() < MAXFRAMEBUF)
			fCurrentFrame = AllocFrame();
		else {
			fQueueOverflows++;
			return size;  // Drop - queue full
		}
	}

	// Track total payload bytes received (before truncation)
	fTotalBytesThisFrame += payloadSize;

	// Log packet details for first frame to analyze assembly
	static int32 sPacketLog = 0;
	static size_t sRunningOffset = 0;
	if (sPacketLog < 50) {  // Log first 50 packets
		syslog(LOG_INFO, "PKT#%d: hdr=%d payload=%d offset=%zu total=%zu\n",
			(int)sPacketLog, buf[0], payloadSize, sRunningOffset, fTotalBytesThisFrame);
		sPacketLog++;
	}
	sRunningOffset += payloadSize;

	// For YUY2 (fixed size), truncate payload if it would exceed expected size
	size_t bytesToWrite = payloadSize;
	if (fExpectedFrameSize > 0) {
		size_t currentSize = fFixedBufferPos;  // Use fixed buffer position
		size_t spaceLeft = (currentSize < fExpectedFrameSize)
			? (fExpectedFrameSize - currentSize) : 0;
		if (bytesToWrite > spaceLeft) {
			bytesToWrite = spaceLeft;
			// Log occasional truncations
			static int32 sTruncations = 0;
			if (++sTruncations <= 5)
				syslog(LOG_INFO, "UVCDeframer: Truncating payload %zu->%zu (frame nearly complete)\n",
					(size_t)payloadSize, bytesToWrite);
		}
	}

	// Write payload to FIXED BUFFER (bypassing BMallocIO to test if it's causing corruption)
	if (bytesToWrite > 0 && fFixedBuffer != NULL) {
		// Direct memcpy to fixed buffer - no BMallocIO involved
		if (fFixedBufferPos + bytesToWrite <= fFixedBufferSize) {
			memcpy(fFixedBuffer + fFixedBufferPos, &buf[buf[0]], bytesToWrite);

			// Debug: Log first few writes
			static int32 sWriteDebug = 0;
			if (++sWriteDebug <= 10) {
				syslog(LOG_INFO, "FixedBuf Write #%d: pos %zu->%zu, bytes=%zu, first4=[%02x %02x %02x %02x]\n",
					(int)sWriteDebug, fFixedBufferPos, fFixedBufferPos + bytesToWrite, bytesToWrite,
					buf[buf[0]], buf[buf[0]+1], buf[buf[0]+2], buf[buf[0]+3]);
			}

			fFixedBufferPos += bytesToWrite;
		} else {
			syslog(LOG_ERR, "UVCDeframer: Fixed buffer overflow! pos=%zu + write=%zu > size=%zu\n",
				fFixedBufferPos, bytesToWrite, fFixedBufferSize);
		}
	}

	// Determine if frame is complete
	bool frameComplete = false;
	size_t currentSize = fFixedBufferPos;  // Use fixed buffer position

	// Size-based detection (for YUY2)
	if (fExpectedFrameSize > 0) {
		// Log EOF occurrences for YUY2 to understand frame boundaries
		static int32 sEofLog = 0;
		if (eof && ++sEofLog <= 10) {
			syslog(LOG_INFO, "UVCDeframer: EOF at size=%zu (expected=%zu, diff=%d)\n",
				currentSize, fExpectedFrameSize,
				(int)((ssize_t)currentSize - (ssize_t)fExpectedFrameSize));
		}

		// FIX: For YUY2 (uncompressed), require EXACT frame size to prevent
		// stair-step artifacts from incomplete data. Incomplete frames cause
		// horizontal offset because linear YUY2->RGB conversion reads bytes
		// that belong to later rows as if they were earlier rows.
		//
		// If EOF arrives before expected size, pad with zeros to maintain
		// correct row alignment.
		if (eof) {
			if (currentSize < fExpectedFrameSize && fFixedBuffer != NULL) {
				// Pad incomplete frame with zeros (black in YUY2: Y=0, U=128, V=128)
				// Pattern: 0x00 0x80 0x00 0x80 for black pixels
				size_t paddingNeeded = fExpectedFrameSize - currentSize;
				static int32 sPadLog = 0;
				if (++sPadLog <= 10) {
					syslog(LOG_INFO, "UVCDeframer: Padding YUY2 frame with %zu bytes (%.1f%% complete)\n",
						paddingNeeded, 100.0f * currentSize / fExpectedFrameSize);
				}
				// Write padding pattern directly to fixed buffer
				uint8 padPattern[4] = {0x00, 0x80, 0x00, 0x80};  // Y U Y V for 2 black pixels
				while (fFixedBufferPos < fExpectedFrameSize) {
					size_t remaining = fExpectedFrameSize - fFixedBufferPos;
					size_t toWrite = (remaining < 4) ? remaining : 4;
					memcpy(fFixedBuffer + fFixedBufferPos, padPattern, toWrite);
					fFixedBufferPos += toWrite;
				}
			}
			frameComplete = true;
			static int32 sEofComplete = 0;
			if (++sEofComplete <= 5)
				syslog(LOG_INFO, "UVCDeframer: YUY2 frame complete by EOF! size=%zu expected=%zu\n",
					fFixedBufferPos, fExpectedFrameSize);
		} else if (currentSize >= fExpectedFrameSize) {
			frameComplete = true;
			// Log first few frame completions
			static int32 sCompletedLog = 0;
			if (++sCompletedLog <= 5)
				syslog(LOG_INFO, "UVCDeframer: Frame complete by SIZE! size=%zu expected=%zu\n",
					currentSize, fExpectedFrameSize);
		}
	}
	// EOF detection for MJPEG (when fExpectedFrameSize == 0)
	// Don't complete if we just processed a FID change (frame was already completed above)
	else if (eof && !fidChanged) {
		frameComplete = true;
	}

	// Complete frame - add to queue
	if (frameComplete) {
		fFrameCount++;
		fFramesCompleted++;

		// Use fixed buffer for YUY2, BMallocIO for MJPEG
		const uint8* frameData;
		size_t frameSize;
		if (fExpectedFrameSize > 0 && fFixedBuffer != NULL) {
			// YUY2: read from fixed buffer
			frameData = fFixedBuffer;
			frameSize = fFixedBufferPos;
		} else {
			// MJPEG: read from BMallocIO (backwards compat)
			frameData = (const uint8*)fInputBuffer.Buffer();
			frameSize = fInputBuffer.BufferLength();
		}

		// Debug: dump first frame to file for analysis
		static int32 sDumpCount = 0;
		if (++sDumpCount == 1 && frameData != NULL && frameSize > 0) {
			// Save raw YUY2 frame to file
			FILE* f = fopen("/boot/home/Desktop/frame_dump.yuv", "wb");
			if (f) {
				fwrite(frameData, 1, frameSize, f);
				fclose(f);
				syslog(LOG_INFO, "UVCDeframer: Saved frame to /boot/home/Desktop/frame_dump.yuv (%zu bytes) [FixedBuffer]\n", frameSize);
			}
			// Also log first 64 bytes
			char hexbuf[256];
			snprintf(hexbuf, sizeof(hexbuf),
				"Frame first 64 bytes:\n"
				"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n"
				"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n"
				"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n"
				"%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
				frameData[0], frameData[1], frameData[2], frameData[3],
				frameData[4], frameData[5], frameData[6], frameData[7],
				frameData[8], frameData[9], frameData[10], frameData[11],
				frameData[12], frameData[13], frameData[14], frameData[15],
				frameData[16], frameData[17], frameData[18], frameData[19],
				frameData[20], frameData[21], frameData[22], frameData[23],
				frameData[24], frameData[25], frameData[26], frameData[27],
				frameData[28], frameData[29], frameData[30], frameData[31],
				frameData[32], frameData[33], frameData[34], frameData[35],
				frameData[36], frameData[37], frameData[38], frameData[39],
				frameData[40], frameData[41], frameData[42], frameData[43],
				frameData[44], frameData[45], frameData[46], frameData[47],
				frameData[48], frameData[49], frameData[50], frameData[51],
				frameData[52], frameData[53], frameData[54], frameData[55],
				frameData[56], frameData[57], frameData[58], frameData[59],
				frameData[60], frameData[61], frameData[62], frameData[63]);
			syslog(LOG_INFO, "UVCDeframer: %s\n", hexbuf);
		}

		// Validate YUY2 frame completeness
		if (fExpectedFrameSize > 0 && frameSize < fExpectedFrameSize) {
			fFramesIncomplete++;
			if (fFramesIncomplete <= 10 || (fFramesIncomplete % 50) == 0)
				syslog(LOG_WARNING, "UVCDeframer: Incomplete frame #%d: got %zu, expected %zu (%.1f%%)\n",
					(int)fFramesIncomplete, frameSize, fExpectedFrameSize,
					100.0f * frameSize / fExpectedFrameSize);
		}

		if (frameData != NULL && frameSize > 0)
			fCurrentFrame->Write(frameData, frameSize);

		fFrames.AddItem(fCurrentFrame);
		release_sem(fFrameSem);
		fCurrentFrame = NULL;

		// Reset for next frame
		fInputBuffer.Seek(0, SEEK_SET);
		fInputBuffer.SetSize(0);
		fFixedBufferPos = 0;  // Also reset fixed buffer
		fPacketsThisFrame = 0;
	}

	// Periodic diagnostic report (every 30 seconds)
	bigtime_t now = system_time();
	if (now - fLastDiagReport > 30000000) {
		if (fFramesCompleted > 0 || fFramesIncomplete > 0) {
			float incompleteRate = fFramesCompleted > 0
				? 100.0f * fFramesIncomplete / (fFramesCompleted + fFramesIncomplete)
				: 0.0f;
			syslog(LOG_INFO, "UVCDeframer stats: completed=%d incomplete=%d (%.1f%%) FID=%d overflow=%d\n",
				(int)fFramesCompleted, (int)fFramesIncomplete, incompleteRate,
				(int)fFIDChanges, (int)fQueueOverflows);
		}
		fLastDiagReport = now;
	}

	return size;
}
