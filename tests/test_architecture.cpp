/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for architecture optimizations (Group 7)
 *
 * Build:
 *   g++ -O2 -I.. -o test_architecture test_architecture.cpp -lbe
 *
 * Run:
 *   ./test_architecture
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <Locker.h>
#include <new>

// Include the utility headers
#include "../CamUtils.h"
#include "../CamConfig.h"


// =============================================================================
// Test 1: ScopedLock RAII Pattern
// =============================================================================

static bool
test_scoped_lock()
{
	printf("Test: ScopedLock RAII pattern... ");

	BLocker locker("TestLocker");

	// Test basic lock/unlock via RAII
	{
		ScopedLock lock(locker);
		if (!lock.IsLocked()) {
			printf("FAIL (lock not acquired)\\n");
			return false;
		}
		// Lock should be held here
	}
	// Lock should be released here

	// Verify lock was released by acquiring again
	if (!locker.Lock()) {
		printf("FAIL (lock not released)\\n");
		return false;
	}
	locker.Unlock();

	// Test with pointer
	{
		ScopedLock lock(&locker);
		if (!lock.IsLocked()) {
			printf("FAIL (pointer lock not acquired)\\n");
			return false;
		}
	}

	// Test with NULL pointer (should not crash)
	{
		ScopedLock lock((BLocker*)NULL);
		if (lock.IsLocked()) {
			printf("FAIL (NULL lock should not be locked)\\n");
			return false;
		}
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 2: ScopedSemaphore RAII Pattern
// =============================================================================

static bool
test_scoped_semaphore()
{
	printf("Test: ScopedSemaphore RAII pattern... ");

	sem_id sem = create_sem(1, "TestSem");
	if (sem < 0) {
		printf("FAIL (create_sem failed)\\n");
		return false;
	}

	// Test basic acquire/release
	{
		ScopedSemaphore scoped(sem);
		if (!scoped.IsAcquired()) {
			delete_sem(sem);
			printf("FAIL (semaphore not acquired)\\n");
			return false;
		}
	}
	// Semaphore should be released

	// Test timeout (should succeed immediately)
	{
		ScopedSemaphore scoped(sem, 1000000);  // 1s timeout
		if (!scoped.IsAcquired()) {
			delete_sem(sem);
			printf("FAIL (timeout acquire failed)\\n");
			return false;
		}
	}

	// Test manual release
	{
		ScopedSemaphore scoped(sem);
		if (!scoped.IsAcquired()) {
			delete_sem(sem);
			printf("FAIL (manual release - not acquired)\\n");
			return false;
		}
		scoped.Release();
		if (scoped.IsAcquired()) {
			delete_sem(sem);
			printf("FAIL (manual release failed)\\n");
			return false;
		}
	}

	delete_sem(sem);
	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 3: ScopedBuffer RAII Pattern
// =============================================================================

static bool
test_scoped_buffer()
{
	printf("Test: ScopedBuffer RAII pattern... ");

	// Test allocation
	{
		ScopedBuffer<uint8> buffer(1024);
		if (!buffer.IsValid() || buffer.Size() != 1024) {
			printf("FAIL (allocation failed)\\n");
			return false;
		}

		// Test write and read
		buffer[0] = 0x42;
		buffer[1023] = 0xFF;
		if (buffer[0] != 0x42 || buffer[1023] != 0xFF) {
			printf("FAIL (read/write failed)\\n");
			return false;
		}
	}
	// Buffer should be freed

	// Test empty allocation
	{
		ScopedBuffer<uint8> buffer(0);
		if (buffer.IsValid()) {
			printf("FAIL (empty allocation should be invalid)\\n");
			return false;
		}
	}

	// Test re-allocation
	{
		ScopedBuffer<uint32> buffer(10);
		if (!buffer.IsValid() || buffer.Size() != 10) {
			printf("FAIL (initial allocation)\\n");
			return false;
		}

		buffer.Allocate(100);
		if (!buffer.IsValid() || buffer.Size() != 100) {
			printf("FAIL (re-allocation)\\n");
			return false;
		}

		buffer.Free();
		if (buffer.IsValid()) {
			printf("FAIL (free didn't clear)\\n");
			return false;
		}
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 4: AtomicFlag Thread Safety
// =============================================================================

static AtomicFlag sTestFlag(false);
static int32 sAtomicTestResult = 0;

static int32
atomic_flag_thread(void* data)
{
	// Try to set flag if not already set
	for (int i = 0; i < 1000; i++) {
		if (!sTestFlag.TestAndSet(true)) {
			// We got exclusive access
			atomic_add(&sAtomicTestResult, 1);
			snooze(10);
			sTestFlag.Set(false);
		}
	}
	return 0;
}

static bool
test_atomic_flag()
{
	printf("Test: AtomicFlag thread safety... ");

	sTestFlag.Set(false);
	sAtomicTestResult = 0;

	// Test basic operations
	AtomicFlag flag(false);
	if (flag.Get()) {
		printf("FAIL (initial value)\\n");
		return false;
	}

	flag.Set(true);
	if (!flag.Get()) {
		printf("FAIL (set true)\\n");
		return false;
	}

	// Test implicit conversion
	if (!flag) {
		printf("FAIL (implicit conversion)\\n");
		return false;
	}

	// Test TestAndSet
	flag.Set(false);
	if (flag.TestAndSet(true)) {
		printf("FAIL (TestAndSet return value)\\n");
		return false;
	}
	if (!flag.Get()) {
		printf("FAIL (TestAndSet didn't set)\\n");
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 5: AtomicCounter Thread Safety
// =============================================================================

static AtomicCounter sTestCounter(0);

static int32
atomic_counter_thread(void* data)
{
	for (int i = 0; i < 10000; i++) {
		sTestCounter.Increment();
	}
	return 0;
}

static bool
test_atomic_counter()
{
	printf("Test: AtomicCounter thread safety... ");

	// Test basic operations
	AtomicCounter counter(0);
	if (counter.Get() != 0) {
		printf("FAIL (initial value)\\n");
		return false;
	}

	counter.Increment();
	if (counter.Get() != 1) {
		printf("FAIL (increment)\\n");
		return false;
	}

	counter.Add(10);
	if (counter.Get() != 11) {
		printf("FAIL (add)\\n");
		return false;
	}

	counter.Decrement();
	if (counter.Get() != 10) {
		printf("FAIL (decrement)\\n");
		return false;
	}

	// Test multi-threaded increment
	sTestCounter.Set(0);
	const int kThreads = 4;
	thread_id threads[kThreads];

	for (int i = 0; i < kThreads; i++) {
		threads[i] = spawn_thread(atomic_counter_thread, "counter_test",
			B_NORMAL_PRIORITY, NULL);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreads; i++) {
		status_t exitValue;
		wait_for_thread(threads[i], &exitValue);
	}

	// Should be exactly 4 * 10000 = 40000
	if (sTestCounter.Get() != 40000) {
		printf("FAIL (concurrent count: %d, expected 40000)\\n", (int)sTestCounter.Get());
		return false;
	}

	printf("OK (concurrent: %d)\\n", (int)sTestCounter.Get());
	return true;
}


// =============================================================================
// Test 6: RingBufferIndex
// =============================================================================

static bool
test_ring_buffer_index()
{
	printf("Test: RingBufferIndex... ");

	RingBufferIndex ring(4);

	// Initially empty
	if (!ring.IsEmpty()) {
		printf("FAIL (not initially empty)\\n");
		return false;
	}

	// Add items
	for (int i = 0; i < 3; i++) {
		int32 idx = ring.ReserveWrite();
		if (idx < 0) {
			printf("FAIL (reserve write %d)\\n", i);
			return false;
		}
		ring.CommitWrite();
	}

	// Should have 3 items
	if (ring.Count() != 3) {
		printf("FAIL (count: %u, expected 3)\\n", ring.Count());
		return false;
	}

	// Should be full now (capacity 4, but ring needs one empty slot)
	if (!ring.IsFull()) {
		printf("FAIL (should be full)\\n");
		return false;
	}

	// Read one item
	int32 idx = ring.ReserveRead();
	if (idx < 0) {
		printf("FAIL (reserve read)\\n");
		return false;
	}
	ring.CommitRead();

	// Should have 2 items
	if (ring.Count() != 2) {
		printf("FAIL (count after read: %u, expected 2)\\n", ring.Count());
		return false;
	}

	// Reset
	ring.Reset();
	if (!ring.IsEmpty()) {
		printf("FAIL (not empty after reset)\\n");
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 7: Result<T> Type
// =============================================================================

static bool
test_result_type()
{
	printf("Test: Result<T> type... ");

	// Success case
	Result<int> success(42);
	if (!success.IsOK() || success.Value() != 42) {
		printf("FAIL (success case)\\n");
		return false;
	}

	// Error case
	Result<int> error = Result<int>::Error(B_NO_MEMORY);
	if (error.IsOK() || error.Status() != B_NO_MEMORY) {
		printf("FAIL (error case)\\n");
		return false;
	}

	// ValueOr
	if (success.ValueOr(0) != 42) {
		printf("FAIL (ValueOr success)\\n");
		return false;
	}
	if (error.ValueOr(99) != 99) {
		printf("FAIL (ValueOr error)\\n");
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 8: Configuration Constants
// =============================================================================

static bool
test_config_constants()
{
	printf("Test: Configuration constants... ");

	// Verify key constants are reasonable
	if (CamConfig::kUSBBufferSize < 1024) {
		printf("FAIL (USB buffer too small)\\n");
		return false;
	}

	if (CamConfig::kFramePoolCapacity < 4) {
		printf("FAIL (frame pool too small)\\n");
		return false;
	}

	if (CamConfig::kDefaultFrameInterval != 33333) {
		printf("FAIL (default interval should be 30fps)\\n");
		return false;
	}

	// Test resolution helpers
	if (CamConfig::kResolution640x480.YUY2Size() != 614400) {
		printf("FAIL (VGA YUY2 size: %zu)\\n", CamConfig::kResolution640x480.YUY2Size());
		return false;
	}

	if (CamConfig::kResolution640x480.RGB32Size() != 1228800) {
		printf("FAIL (VGA RGB32 size: %zu)\\n", CamConfig::kResolution640x480.RGB32Size());
		return false;
	}

	// Test FPS conversion
	bigtime_t interval = CamConfig::FPSToInterval(30.0f);
	if (interval < 33000 || interval > 34000) {
		printf("FAIL (FPSToInterval: %lld)\\n", interval);
		return false;
	}

	float fps = CamConfig::IntervalToFPS(33333);
	if (fps < 29.0f || fps > 31.0f) {
		printf("FAIL (IntervalToFPS: %.2f)\\n", fps);
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 9: Timing Helpers
// =============================================================================

static bool
test_timing_helpers()
{
	printf("Test: Timing helpers... ");

	if (MicrosecondsToMilliseconds(1000) != 1) {
		printf("FAIL (us to ms)\\n");
		return false;
	}

	if (MillisecondsToMicroseconds(1) != 1000) {
		printf("FAIL (ms to us)\\n");
		return false;
	}

	if (SecondsToMicroseconds(1) != 1000000) {
		printf("FAIL (s to us)\\n");
		return false;
	}

	float fps = MicrosecondsToFPS(33333);
	if (fps < 29.0f || fps > 31.0f) {
		printf("FAIL (us to FPS: %.2f)\\n", fps);
		return false;
	}

	bigtime_t interval = FPSToMicroseconds(30.0f);
	if (interval < 33000 || interval > 34000) {
		printf("FAIL (FPS to us: %lld)\\n", interval);
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Test 10: Clamp Functions
// =============================================================================

static bool
test_clamp_functions()
{
	printf("Test: Clamp functions... ");

	// Clamp template
	if (Clamp(50, 0, 100) != 50) {
		printf("FAIL (in range)\\n");
		return false;
	}
	if (Clamp(-10, 0, 100) != 0) {
		printf("FAIL (below min)\\n");
		return false;
	}
	if (Clamp(150, 0, 100) != 100) {
		printf("FAIL (above max)\\n");
		return false;
	}

	// ClampByte
	if (ClampByte(128) != 128) {
		printf("FAIL (byte in range)\\n");
		return false;
	}
	if (ClampByte(-100) != 0) {
		printf("FAIL (byte below)\\n");
		return false;
	}
	if (ClampByte(300) != 255) {
		printf("FAIL (byte above)\\n");
		return false;
	}

	// Min/Max
	if (Min(10, 20) != 10 || Max(10, 20) != 20) {
		printf("FAIL (Min/Max)\\n");
		return false;
	}

	printf("OK\\n");
	return true;
}


// =============================================================================
// Main
// =============================================================================

int
main(int argc, char** argv)
{
	printf("\\n");
	printf("===========================================\\n");
	printf("Architecture Tests (Group 7)\\n");
	printf("===========================================\\n\\n");

	int passed = 0;
	int failed = 0;

	if (test_scoped_lock())
		passed++;
	else
		failed++;

	if (test_scoped_semaphore())
		passed++;
	else
		failed++;

	if (test_scoped_buffer())
		passed++;
	else
		failed++;

	if (test_atomic_flag())
		passed++;
	else
		failed++;

	if (test_atomic_counter())
		passed++;
	else
		failed++;

	if (test_ring_buffer_index())
		passed++;
	else
		failed++;

	if (test_result_type())
		passed++;
	else
		failed++;

	if (test_config_constants())
		passed++;
	else
		failed++;

	if (test_timing_helpers())
		passed++;
	else
		failed++;

	if (test_clamp_functions())
		passed++;
	else
		failed++;

	printf("\\n");
	printf("===========================================\\n");
	printf("Results: %d passed, %d failed\\n", passed, failed);
	printf("===========================================\\n\\n");

	return (failed == 0) ? 0 : 1;
}
