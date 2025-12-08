/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */

#define CD_COL "31"
#include "CamDeframer.h"
#include "CamDevice.h"
#include "CamDebug.h"
#include <Autolock.h>
#include <syslog.h>
#define MAX_TAG_LEN CAMDEFRAMER_MAX_TAG_LEN
#define MAXFRAMEBUF CAMDEFRAMER_MAX_QUEUED_FRAMES


CamDeframer::CamDeframer(CamDevice *device)
	: CamFilterInterface(device),
	fDevice(device),
	fState(ST_SYNC),
	fFrameSem(B_ERROR),
	fLocker("CamDeframer Framelist lock", true),
	fPoolHits(0),
	fPoolMisses(0),
	fSOFTags(NULL),
	fEOFTags(NULL),
	fNumSOFTags(0),
	fNumEOFTags(0),
	fLenSOFTags(0),
	fLenEOFTags(0),
	fSkipSOFTags(0),
	fSkipEOFTags(0)
{
	fMinFrameSize = fDevice->MinRawFrameSize();
	fMaxFrameSize = fDevice->MaxRawFrameSize();
	fFrameSem = create_sem(0, "CamDeframer sem");
	fCurrentFrame = AllocFrame();
}


CamDeframer::~CamDeframer()
{
	delete_sem(fFrameSem);
	BAutolock l(fLocker);

	// Delete current frame
	delete fCurrentFrame;
	fCurrentFrame = NULL;

	// Delete all queued frames
	while (fFrames.CountItems() > 0) {
		CamFrame* frame = (CamFrame*)fFrames.RemoveItem((int32)0);
		delete frame;
	}

	// Delete all pooled frames
	while (fFramePool.CountItems() > 0) {
		CamFrame* frame = (CamFrame*)fFramePool.RemoveItem((int32)0);
		delete frame;
	}

	// Log pool statistics
	if (fPoolHits > 0 || fPoolMisses > 0) {
		float hitRate = (fPoolHits + fPoolMisses) > 0
			? 100.0f * fPoolHits / (fPoolHits + fPoolMisses)
			: 0.0f;
		syslog(LOG_INFO, "CamDeframer: Pool stats - hits=%d misses=%d (%.1f%% reuse)\n",
			(int)fPoolHits, (int)fPoolMisses, hitRate);
	}
}


ssize_t
CamDeframer::Read(void *buffer, size_t size)
{
	BAutolock l(fLocker);
	CamFrame *f = (CamFrame *)fFrames.ItemAt(0);
	if (!f)
		return EIO;
	return f->Read(buffer, size);
}


ssize_t
CamDeframer::ReadAt(off_t pos, void *buffer, size_t size)
{
	BAutolock l(fLocker);
	CamFrame *f = (CamFrame *)fFrames.ItemAt(0);
	if (!f)
		return EIO;
	return f->ReadAt(pos, buffer, size);
}


off_t
CamDeframer::Seek(off_t position, uint32 seek_mode)
{
	BAutolock l(fLocker);
	CamFrame *f = (CamFrame *)fFrames.ItemAt(0);
	if (!f)
		return EIO;
	return f->Seek(position, seek_mode);
}


off_t
CamDeframer::Position() const
{
	BAutolock l((BLocker &)fLocker); // need to get rid of const here
	CamFrame *f = (CamFrame *)fFrames.ItemAt(0);
	if (!f)
		return EIO;
	return f->Position();
}


status_t
CamDeframer::SetSize(off_t size)
{
	(void)size;
	return EINVAL;
}


ssize_t
CamDeframer::Write(const void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
	return EINVAL;
}


ssize_t
CamDeframer::WriteAt(off_t pos, const void *buffer, size_t size)
{
	(void)pos;
	(void)buffer;
	(void)size;
	return EINVAL;
}


status_t
CamDeframer::WaitFrame(bigtime_t timeout)
{
	// Debug: log semaphore status
	static int32 sWaitCount = 0;
	if (++sWaitCount <= 3) {
		sem_info info;
		if (get_sem_info(fFrameSem, &info) == B_OK) {
			syslog(LOG_INFO, "CamDeframer::WaitFrame: sem_id=%d count=%d name=%s\n",
				fFrameSem, (int)info.count, info.name);
		} else {
			syslog(LOG_ERR, "CamDeframer::WaitFrame: INVALID semaphore id=%d\n", fFrameSem);
		}
	}
	return acquire_sem_etc(fFrameSem, 1, B_RELATIVE_TIMEOUT, timeout);
}


status_t
CamDeframer::GetFrame(CamFrame **frame, bigtime_t *stamp)
{
	PRINT((CH "()" CT));
	BAutolock l(fLocker);
	CamFrame *f = (CamFrame *)fFrames.RemoveItem((int32)0);
	if (!f)
		return ENOENT;
	*frame = f;
	*stamp = f->Stamp();
	return B_OK;
}


status_t
CamDeframer::DropFrame()
{
	PRINT((CH "()" CT));
	BAutolock l(fLocker);
	CamFrame *f = (CamFrame *)fFrames.RemoveItem((int32)0);
	if (!f)
		return ENOENT;
	delete f;
	return B_OK;
}


status_t
CamDeframer::Flush()
{
	BAutolock l(fLocker);

	// Clear all pending frames from queue
	while (fFrames.CountItems() > 0) {
		CamFrame *f = (CamFrame *)fFrames.RemoveItem((int32)0);
		delete f;
	}

	// Reset current frame
	if (fCurrentFrame) {
		fCurrentFrame->Seek(0, SEEK_SET);
		fCurrentFrame->SetSize(0);
	}

	// Reset semaphore by acquiring any pending counts
	while (acquire_sem_etc(fFrameSem, 1, B_RELATIVE_TIMEOUT, 0) == B_OK)
		;

	fState = ST_SYNC;
	return B_OK;
}


status_t
CamDeframer::RegisterSOFTags(const uint8 **tags, int count, size_t len, size_t skip)
{
	if (fSOFTags)
		return EALREADY;
	if (len > MAX_TAG_LEN)
		return EINVAL;
	if (count > 16)
		return EINVAL;
	fSOFTags = tags;
	fNumSOFTags = count;
	fLenSOFTags = len;
	fSkipSOFTags = skip;
	return B_OK;
}


status_t
CamDeframer::RegisterEOFTags(const uint8 **tags, int count, size_t len, size_t skip)
{
	if (fEOFTags)
		return EALREADY;
	if (len > MAX_TAG_LEN)
		return EINVAL;
	if (count > 16)
		return EINVAL;
	fEOFTags = tags;
	fNumEOFTags = count;
	fLenEOFTags = len;
	fSkipEOFTags = skip;
	return B_OK;
}


int
CamDeframer::FindTags(const uint8 *buf, size_t buflen, const uint8 **tags, int tagcount, size_t taglen, size_t skiplen, int *which)
{
	int i, t;
	// Prevent unsigned underflow if buflen < skiplen
	if (buflen < skiplen)
		return -1;
	for (i = 0; i < (int)(buflen - skiplen + 1); i++) {
		for (t = 0; t < tagcount; t++) {
			if (!memcmp(buf+i, tags[t], taglen)) {
				if (which)
					*which = t;
				return i;
			}
		}
	}
	return -1;
}


int
CamDeframer::FindSOF(const uint8 *buf, size_t buflen, int *which)
{
	return FindTags(buf, buflen, fSOFTags, fNumSOFTags, fLenSOFTags, fSkipSOFTags, which);
}


int
CamDeframer::FindEOF(const uint8 *buf, size_t buflen, int *which)
{
	return FindTags(buf, buflen, fEOFTags, fNumEOFTags, fLenEOFTags, fSkipEOFTags, which);
}


CamFrame *
CamDeframer::AllocFrame()
{
	BAutolock l(fLocker);

	// Try to reuse a frame from the pool first
	if (fFramePool.CountItems() > 0) {
		CamFrame* frame = (CamFrame*)fFramePool.RemoveItem(
			fFramePool.CountItems() - 1);
		if (frame != NULL) {
			// Reset frame for reuse
			frame->Seek(0, SEEK_SET);
			frame->SetSize(0);
			frame->fStamp = system_time();
			fPoolHits++;
			return frame;
		}
	}

	// No pooled frame available, allocate new one
	fPoolMisses++;
	return new CamFrame();
}


void
CamDeframer::RecycleFrame(CamFrame* frame)
{
	if (frame == NULL)
		return;

	BAutolock l(fLocker);

	// Add to pool if not full, otherwise delete
	if (fFramePool.CountItems() < CAMDEFRAMER_FRAME_POOL_SIZE) {
		// Reset and add to pool
		frame->Seek(0, SEEK_SET);
		frame->SetSize(0);
		fFramePool.AddItem(frame);
	} else {
		// Pool full, delete the frame
		delete frame;
	}
}


int32
CamDeframer::PoolSize() const
{
	return fFramePool.CountItems();
}


int32
CamDeframer::PoolCapacity() const
{
	return CAMDEFRAMER_FRAME_POOL_SIZE;
}
