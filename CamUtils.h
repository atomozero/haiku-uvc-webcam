/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Utility classes for RAII, thread safety, and common patterns.
 * Group 7: Architecture Optimizations
 */
#ifndef _CAM_UTILS_H
#define _CAM_UTILS_H


#include <OS.h>
#include <Locker.h>


// =============================================================================
// RAII Scoped Lock (Alternative to BAutolock for custom behavior)
// =============================================================================

class ScopedLock {
public:
	explicit ScopedLock(BLocker& locker)
		:
		fLocker(&locker),
		fLocked(locker.Lock())
	{
	}

	explicit ScopedLock(BLocker* locker)
		:
		fLocker(locker),
		fLocked(locker != NULL && locker->Lock())
	{
	}

	~ScopedLock()
	{
		if (fLocked && fLocker != NULL)
			fLocker->Unlock();
	}

	bool IsLocked() const { return fLocked; }

	// Prevent copying
	ScopedLock(const ScopedLock&) = delete;
	ScopedLock& operator=(const ScopedLock&) = delete;

private:
	BLocker*	fLocker;
	bool		fLocked;
};


// =============================================================================
// RAII Semaphore Acquire/Release
// =============================================================================

class ScopedSemaphore {
public:
	explicit ScopedSemaphore(sem_id sem, bigtime_t timeout = B_INFINITE_TIMEOUT)
		:
		fSemaphore(sem),
		fAcquired(false)
	{
		if (fSemaphore >= 0) {
			status_t err = timeout == B_INFINITE_TIMEOUT
				? acquire_sem(fSemaphore)
				: acquire_sem_etc(fSemaphore, 1, B_RELATIVE_TIMEOUT, timeout);
			fAcquired = (err == B_OK);
		}
	}

	~ScopedSemaphore()
	{
		if (fAcquired && fSemaphore >= 0)
			release_sem(fSemaphore);
	}

	bool IsAcquired() const { return fAcquired; }
	status_t Status() const { return fAcquired ? B_OK : B_TIMED_OUT; }

	// Manual release (for early release)
	void Release()
	{
		if (fAcquired && fSemaphore >= 0) {
			release_sem(fSemaphore);
			fAcquired = false;
		}
	}

	// Prevent copying
	ScopedSemaphore(const ScopedSemaphore&) = delete;
	ScopedSemaphore& operator=(const ScopedSemaphore&) = delete;

private:
	sem_id		fSemaphore;
	bool		fAcquired;
};


// =============================================================================
// RAII Buffer Guard (automatic cleanup of allocated memory)
// =============================================================================

template<typename T>
class ScopedBuffer {
public:
	explicit ScopedBuffer(size_t count = 0)
		:
		fBuffer(NULL),
		fSize(0)
	{
		if (count > 0)
			Allocate(count);
	}

	~ScopedBuffer()
	{
		Free();
	}

	bool Allocate(size_t count)
	{
		Free();
		if (count > 0) {
			fBuffer = new(std::nothrow) T[count];
			if (fBuffer != NULL)
				fSize = count;
		}
		return fBuffer != NULL;
	}

	void Free()
	{
		delete[] fBuffer;
		fBuffer = NULL;
		fSize = 0;
	}

	T* Get() const { return fBuffer; }
	size_t Size() const { return fSize; }
	bool IsValid() const { return fBuffer != NULL; }

	T& operator[](size_t index) { return fBuffer[index]; }
	const T& operator[](size_t index) const { return fBuffer[index]; }

	// Prevent copying
	ScopedBuffer(const ScopedBuffer&) = delete;
	ScopedBuffer& operator=(const ScopedBuffer&) = delete;

private:
	T*		fBuffer;
	size_t	fSize;
};


// =============================================================================
// Atomic Flag (for thread-safe boolean operations)
// =============================================================================

class AtomicFlag {
public:
	AtomicFlag(bool initialValue = false)
		:
		fValue(initialValue ? 1 : 0)
	{
	}

	bool Get() const
	{
		return atomic_get(&fValue) != 0;
	}

	void Set(bool value)
	{
		atomic_set(&fValue, value ? 1 : 0);
	}

	bool TestAndSet(bool newValue)
	{
		int32 old = atomic_test_and_set(&fValue, newValue ? 1 : 0, !newValue ? 1 : 0);
		return old != 0;
	}

	// Implicit conversion for convenience
	operator bool() const { return Get(); }

private:
	mutable int32	fValue;
};


// =============================================================================
// Atomic Counter (for thread-safe counting)
// =============================================================================

class AtomicCounter {
public:
	AtomicCounter(int32 initialValue = 0)
		:
		fValue(initialValue)
	{
	}

	int32 Get() const
	{
		return atomic_get(&fValue);
	}

	void Set(int32 value)
	{
		atomic_set(&fValue, value);
	}

	int32 Increment()
	{
		return atomic_add(&fValue, 1) + 1;
	}

	int32 Decrement()
	{
		return atomic_add(&fValue, -1) - 1;
	}

	int32 Add(int32 delta)
	{
		return atomic_add(&fValue, delta) + delta;
	}

	// Implicit conversion for convenience
	operator int32() const { return Get(); }

private:
	mutable int32	fValue;
};


// =============================================================================
// Ring Buffer Index (lock-free circular buffer index management)
// =============================================================================

class RingBufferIndex {
public:
	RingBufferIndex(uint32 capacity)
		:
		fCapacity(capacity),
		fHead(0),
		fTail(0)
	{
	}

	bool IsEmpty() const
	{
		return atomic_get(&fHead) == atomic_get(&fTail);
	}

	bool IsFull() const
	{
		uint32 nextHead = (atomic_get(&fHead) + 1) % fCapacity;
		return nextHead == (uint32)atomic_get(&fTail);
	}

	uint32 Count() const
	{
		int32 head = atomic_get(&fHead);
		int32 tail = atomic_get(&fTail);
		if (head >= tail)
			return head - tail;
		return fCapacity - tail + head;
	}

	// Returns index to write to, or -1 if full
	int32 ReserveWrite()
	{
		int32 head = atomic_get(&fHead);
		uint32 nextHead = (head + 1) % fCapacity;
		if (nextHead == (uint32)atomic_get(&fTail))
			return -1;  // Full
		return head;
	}

	void CommitWrite()
	{
		int32 head = atomic_get(&fHead);
		atomic_set(&fHead, (head + 1) % fCapacity);
	}

	// Returns index to read from, or -1 if empty
	int32 ReserveRead()
	{
		int32 tail = atomic_get(&fTail);
		if (tail == atomic_get(&fHead))
			return -1;  // Empty
		return tail;
	}

	void CommitRead()
	{
		int32 tail = atomic_get(&fTail);
		atomic_set(&fTail, (tail + 1) % fCapacity);
	}

	void Reset()
	{
		atomic_set(&fHead, 0);
		atomic_set(&fTail, 0);
	}

private:
	uint32			fCapacity;
	mutable int32	fHead;
	mutable int32	fTail;
};


// =============================================================================
// Result<T> - Success/Error wrapper (similar to std::expected)
// =============================================================================

template<typename T>
class Result {
public:
	// Success constructor
	Result(const T& value)
		:
		fValue(value),
		fStatus(B_OK)
	{
	}

	// Error constructor
	static Result Error(status_t error)
	{
		Result r;
		r.fStatus = error;
		return r;
	}

	bool IsOK() const { return fStatus == B_OK; }
	status_t Status() const { return fStatus; }

	const T& Value() const { return fValue; }
	T& Value() { return fValue; }

	// Get value or default
	T ValueOr(const T& defaultValue) const
	{
		return IsOK() ? fValue : defaultValue;
	}

private:
	Result() : fStatus(B_ERROR) {}

	T			fValue;
	status_t	fStatus;
};


// =============================================================================
// Timing Helpers
// =============================================================================

inline bigtime_t
MicrosecondsToMilliseconds(bigtime_t us)
{
	return us / 1000;
}

inline bigtime_t
MillisecondsToMicroseconds(bigtime_t ms)
{
	return ms * 1000;
}

inline bigtime_t
SecondsToMicroseconds(int32 seconds)
{
	return (bigtime_t)seconds * 1000000;
}

inline float
MicrosecondsToFPS(bigtime_t interval)
{
	if (interval <= 0)
		return 0.0f;
	return 1000000.0f / interval;
}

inline bigtime_t
FPSToMicroseconds(float fps)
{
	if (fps <= 0.0f)
		return 0;
	return (bigtime_t)(1000000.0f / fps);
}


// =============================================================================
// Clamping and Math Helpers
// =============================================================================

template<typename T>
inline T
Clamp(T value, T minVal, T maxVal)
{
	if (value < minVal)
		return minVal;
	if (value > maxVal)
		return maxVal;
	return value;
}

template<typename T>
inline T
Min(T a, T b)
{
	return a < b ? a : b;
}

template<typename T>
inline T
Max(T a, T b)
{
	return a > b ? a : b;
}

// Branchless byte clamp for RGB conversion (optimized for uint8 output)
inline uint8
ClampByte(int32 value)
{
	if (value < 0)
		return 0;
	if (value > 255)
		return 255;
	return (uint8)value;
}


#endif /* _CAM_UTILS_H */
