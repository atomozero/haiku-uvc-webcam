/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Distributed under the terms of the MIT License.
 */
#ifndef _UVC_DEFRAMER_H
#define _UVC_DEFRAMER_H


#include "CamDeframer.h"

#include <USB3.h>


// =============================================================================
// Deframer Statistics (Group 6: Deframer Optimization)
// =============================================================================

struct deframer_stats {
	uint32		frames_completed;
	uint32		frames_incomplete;
	uint32		fid_changes;
	uint32		queue_overflows;
	bigtime_t	last_report_time;
	size_t		expected_frame_size;

	float GetCompletionRate() const {
		uint32 total = frames_completed + frames_incomplete;
		if (total == 0)
			return 100.0f;
		return 100.0f * frames_completed / total;
	}

	float GetIncompleteRate() const {
		uint32 total = frames_completed + frames_incomplete;
		if (total == 0)
			return 0.0f;
		return 100.0f * frames_incomplete / total;
	}
};


class UVCDeframer : public CamDeframer {
public:
								UVCDeframer(CamDevice *device);
	virtual 					~UVCDeframer();
					// BPositionIO interface
					// write from usb transfers
	virtual ssize_t				Write(const void *buffer, size_t size);
	virtual status_t			Flush();  // Override to also clear input buffer
					// Set expected frame size for frame boundary detection
			void				SetExpectedFrameSize(size_t size);

					// Statistics methods (Group 6: Deframer Optimization)
			deframer_stats		GetStats() const;
			void				ResetStats();

private:
	void						_PrintBuffer(const void* buffer, size_t size);

	int32						fFrameCount;
	int32						fID;
	BMallocIO					fInputBuffer;
	size_t						fExpectedFrameSize;  // Expected size for complete frame

	// FIX: Simple fixed buffer to bypass potential BMallocIO issues
	uint8*						fFixedBuffer;
	size_t						fFixedBufferSize;
	size_t						fFixedBufferPos;

	// Frame quality diagnostics
	int32						fFramesCompleted;
	int32						fFramesIncomplete;
	int32						fFIDChanges;
	int32						fQueueOverflows;
	int32						fPacketsThisFrame;
	size_t						fTotalBytesThisFrame;  // Total payload bytes (including truncated)
	bigtime_t					fLastDiagReport;
};

#endif /* _UVC_DEFRAMER_H */

