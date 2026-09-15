/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * See UVCSafety.h for the rationale and fix coverage.
 */
#include "UVCSafety.h"


bool
UVCSafeMulSize(size_t a, size_t b, size_t* out)
{
	if (out == NULL)
		return false;
	if (a == 0 || b == 0)
		return false;
	if (a > (size_t)-1 / b)
		return false;
	*out = a * b;
	return true;
}


bool
UVCSafeMul3Size(size_t a, size_t b, size_t c, size_t* out)
{
	if (out == NULL)
		return false;
	size_t ab;
	if (!UVCSafeMulSize(a, b, &ab))
		return false;
	return UVCSafeMulSize(ab, c, out);
}


bool
UVCSafeYUY2Size(uint32 width, uint32 height, size_t* out)
{
	if (width == 0 || height == 0 || width > 8192 || height > 8192)
		return false;
	return UVCSafeMul3Size((size_t)width, (size_t)height, 2, out);
}


bool
UVCSafeRGB32Size(uint32 width, uint32 height, size_t* out)
{
	if (width == 0 || height == 0 || width > 8192 || height > 8192)
		return false;
	return UVCSafeMul3Size((size_t)width, (size_t)height, 4, out);
}


bool
UVCSafeProducerBufferSize(uint32 lineWidth, uint32 lineCount, size_t* out)
{
	// Same shape as the RGB32 frame: 4 bytes per pixel. Zero is rejected
	// so callers never reach malloc(0).
	return UVCSafeRGB32Size(lineWidth, lineCount, out);
}


bool
UVCSafeAudioRingSize(uint32 sampleRate, uint8 channels, size_t* out)
{
	if (out == NULL)
		return false;
	if (sampleRate == 0 || channels == 0)
		return false;

	uint64 size = (uint64)sampleRate * (uint64)channels * 2 * 2;
	const uint64 kMin = 16384;
	const uint64 kMax = 262144;
	if (size < kMin)
		size = kMin;
	if (size > kMax)
		size = kMax;
	*out = (size_t)size;
	return true;
}


bool
UVCSafeAudioBufferSize(uint32 maxPacketSize, uint32 packetCount, size_t* out)
{
	if (out == NULL)
		return false;
	if (maxPacketSize == 0 || packetCount == 0)
		return false;
	uint64 size = (uint64)maxPacketSize * (uint64)packetCount;
	if (size == 0 || size > 4u * 1024 * 1024)
		return false;
	*out = (size_t)size;
	return true;
}


bool
UVCSafeFieldInterval(float fieldRate, int64* outUsec)
{
	if (outUsec == NULL)
		return false;
	// (fieldRate > 1) excludes 0, negatives, NaN; upper bound excludes INF
	// and garbage frequencies that would underflow the interval.
	if (!(fieldRate > 1.0f && fieldRate < 1000.0f))
		return false;
	double interval = 1000000.0 / (double)fieldRate;
	if (!(interval >= 1000.0 && interval <= 1000000.0))
		return false;
	*outUsec = (int64)interval;
	return true;
}


bool
UVCSafeRetryConfig(int64 initialDelay, int64 maxDelay, float multiplier)
{
	if (initialDelay <= 0 || maxDelay <= 0)
		return false;
	if (!(multiplier >= 1.0f && multiplier <= 10.0f))
		return false;
	if (initialDelay > maxDelay)
		return false;
	return true;
}


int64
UVCSafeBackoffDelay(uint32 attempt, int64 initialDelay, int64 maxDelay,
	float multiplier)
{
	if (!UVCSafeRetryConfig(initialDelay, maxDelay, multiplier))
		return maxDelay > 0 ? maxDelay : 0;

	// Cap steps, huge attempt would spin the loop.
	if (attempt > 32)
		return maxDelay;

	double delay = (double)initialDelay;
	for (uint32 i = 0; i < attempt; i++) {
		delay *= (double)multiplier;
		if (delay >= (double)maxDelay)
			return maxDelay;
	}
	if (delay > (double)maxDelay)
		return maxDelay;
	return (int64)delay;
}


uint32
UVCGuardRingCapacity(uint32 capacity)
{
	return capacity == 0 ? 1 : capacity;
}


uint8
UVCSafeSelectorSources(uint8 numPins, size_t fixedLen, uint8 bLength)
{
	if ((size_t)bLength <= fixedLen)
		return 0;
	size_t room = (size_t)bLength - fixedLen;
	if (room > (size_t)numPins)
		room = numPins;
	return (uint8)room;
}


uint8
UVCSafeArrayCount(uint8 count, uint8 entrySize, size_t arrayOffset,
	uint8 bLength)
{
	if (entrySize == 0 || (size_t)bLength <= arrayOffset)
		return 0;
	size_t room = (size_t)bLength - arrayOffset;
	size_t fit = room / (size_t)entrySize;
	if (fit > (size_t)count)
		fit = count;
	return (uint8)fit;
}


bool
UVCSafeStillImageCounts(uint8 numSizes, uint8 numCompressions, uint8 bLength)
{
	// 6 fixed + 4 per size pattern + 1 compression count + M compressions.
	size_t need = (size_t)6 + (size_t)numSizes * 4 + 1
		+ (size_t)numCompressions;
	if (bLength < kUVCMinStillImageLen)
		return false;
	return need <= (size_t)bLength;
}


bool
UVCSafeAudioFormatICount(uint8 freqType, size_t avail)
{
	if (avail < kUVCMinAudioFormatTypeILen)
		return false;
	if (freqType == 0)
		return avail >= kUVCMinAudioFormatTypeILen + 6;
	size_t need = (size_t)kUVCMinAudioFormatTypeILen + (size_t)freqType * 3;
	return avail >= need;
}
