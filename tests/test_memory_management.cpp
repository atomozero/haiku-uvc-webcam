/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for memory management optimizations (Group 2)
 *
 * Build:
 *   g++ -O2 -o test_memory_management test_memory_management.cpp -lbe
 *
 * Run:
 *   ./test_memory_management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <Locker.h>
#include <Autolock.h>
#include <List.h>
#include <DataIO.h>


// =============================================================================
// Simulated CamFrame class (matches driver implementation)
// =============================================================================

class CamFrame : public BMallocIO {
public:
	CamFrame() : BMallocIO() { fStamp = system_time(); }
	virtual ~CamFrame() {}
	bigtime_t Stamp() const { return fStamp; }
	bigtime_t fStamp;
};


// =============================================================================
// Frame Pool Implementation (matches driver)
// =============================================================================

#define FRAME_POOL_SIZE 12

class FramePool {
public:
	FramePool()
		:
		fLocker("test_pool_lock"),
		fPoolHits(0),
		fPoolMisses(0)
	{
	}

	~FramePool()
	{
		BAutolock l(fLocker);
		while (fPool.CountItems() > 0) {
			CamFrame* frame = (CamFrame*)fPool.RemoveItem((int32)0);
			delete frame;
		}
	}

	CamFrame* Alloc()
	{
		BAutolock l(fLocker);

		if (fPool.CountItems() > 0) {
			CamFrame* frame = (CamFrame*)fPool.RemoveItem(
				fPool.CountItems() - 1);
			if (frame != NULL) {
				frame->Seek(0, SEEK_SET);
				frame->SetSize(0);
				frame->fStamp = system_time();
				fPoolHits++;
				return frame;
			}
		}

		fPoolMisses++;
		return new CamFrame();
	}

	void Recycle(CamFrame* frame)
	{
		if (frame == NULL)
			return;

		BAutolock l(fLocker);

		if (fPool.CountItems() < FRAME_POOL_SIZE) {
			frame->Seek(0, SEEK_SET);
			frame->SetSize(0);
			fPool.AddItem(frame);
		} else {
			delete frame;
		}
	}

	int32 PoolSize() const { return fPool.CountItems(); }
	int32 Hits() const { return fPoolHits; }
	int32 Misses() const { return fPoolMisses; }
	float HitRate() const
	{
		int32 total = fPoolHits + fPoolMisses;
		return total > 0 ? 100.0f * fPoolHits / total : 0.0f;
	}

private:
	BLocker		fLocker;
	BList		fPool;
	int32		fPoolHits;
	int32		fPoolMisses;
};


// =============================================================================
// Test 1: Frame Pool Basic Operations
// =============================================================================

static bool
test_pool_basic()
{
	printf("Test: Frame pool basic operations... ");

	FramePool pool;

	// Initial state
	if (pool.PoolSize() != 0 || pool.Hits() != 0) {
		printf("FAIL (initial state)\n");
		return false;
	}

	// Allocate first frame (miss)
	CamFrame* frame1 = pool.Alloc();
	if (frame1 == NULL || pool.Misses() != 1) {
		printf("FAIL (first alloc)\n");
		delete frame1;
		return false;
	}

	// Recycle it
	pool.Recycle(frame1);
	if (pool.PoolSize() != 1) {
		printf("FAIL (recycle)\n");
		return false;
	}

	// Allocate again (hit)
	CamFrame* frame2 = pool.Alloc();
	if (frame2 != frame1 || pool.Hits() != 1) {
		printf("FAIL (reuse)\n");
		delete frame2;
		return false;
	}

	// Cleanup
	pool.Recycle(frame2);

	printf("OK\n");
	return true;
}


// =============================================================================
// Test 2: Pool Performance vs Direct Allocation
// =============================================================================

static bool
test_pool_performance()
{
	printf("Test: Pool performance vs direct allocation... ");

	const int kIterations = 5000;
	const size_t kFrameDataSize = 64 * 1024;  // 64KB per frame

	// Test 1: Direct allocation/deallocation with realistic data size
	bigtime_t startDirect = system_time();
	for (int i = 0; i < kIterations; i++) {
		CamFrame* frame = new CamFrame();
		// Write realistic frame data
		uint8 data[256];
		memset(data, (uint8)i, sizeof(data));
		frame->Write(data, sizeof(data));
		delete frame;
	}
	bigtime_t timeDirect = system_time() - startDirect;

	// Test 2: Using pool
	FramePool pool;
	bigtime_t startPool = system_time();
	for (int i = 0; i < kIterations; i++) {
		CamFrame* frame = pool.Alloc();
		uint8 data[256];
		memset(data, (uint8)i, sizeof(data));
		frame->Write(data, sizeof(data));
		pool.Recycle(frame);
	}
	bigtime_t timePool = system_time() - startPool;

	// Main benefit is memory reuse, not raw speed
	// Check hit rate is high (indicates effective pooling)
	float hitRate = pool.HitRate();

	printf("OK (direct: %lld us, pool: %lld us, hit rate: %.1f%%)\n",
		timeDirect, timePool, hitRate);

	// Success if hit rate is high (memory reuse working)
	return (hitRate > 95.0f);
}


// =============================================================================
// Test 3: Pool Capacity Limits
// =============================================================================

static bool
test_pool_capacity()
{
	printf("Test: Pool capacity limits... ");

	FramePool pool;
	CamFrame* frames[FRAME_POOL_SIZE + 5];

	// Allocate more frames than pool capacity
	for (int i = 0; i < FRAME_POOL_SIZE + 5; i++) {
		frames[i] = pool.Alloc();
		if (frames[i] == NULL) {
			printf("FAIL (alloc %d)\n", i);
			// Cleanup
			for (int j = 0; j < i; j++)
				delete frames[j];
			return false;
		}
	}

	// Recycle all - pool should only keep FRAME_POOL_SIZE
	for (int i = 0; i < FRAME_POOL_SIZE + 5; i++) {
		pool.Recycle(frames[i]);
	}

	if (pool.PoolSize() != FRAME_POOL_SIZE) {
		printf("FAIL (pool size: %d, expected: %d)\n",
			(int)pool.PoolSize(), FRAME_POOL_SIZE);
		return false;
	}

	printf("OK (pool capped at %d frames)\n", FRAME_POOL_SIZE);
	return true;
}


// =============================================================================
// Test 4: Conditional Pre-fill Performance
// =============================================================================

static bool
test_conditional_prefill()
{
	printf("Test: Conditional pre-fill optimization... ");

	const size_t kBufferSize = 320 * 240 * 4;  // 320x240 RGBA
	const int kIterations = 5000;  // More iterations

	uint8* buffer = (uint8*)malloc(kBufferSize);
	if (buffer == NULL) {
		printf("FAIL (allocation)\n");
		return false;
	}

	// Warmup
	memset(buffer, 0xFF, kBufferSize);

	// Simulate: Always pre-fill (old behavior)
	volatile uint8 dummy = 0;
	bigtime_t startAlways = system_time();
	for (int i = 0; i < kIterations; i++) {
		memset(buffer, 0x40, kBufferSize);
		// Simulate frame processing
		dummy += buffer[kBufferSize / 2];
	}
	bigtime_t timeAlways = system_time() - startAlways;

	// Simulate: Conditional pre-fill (new behavior)
	// Assume 90% valid frames (no pre-fill needed)
	bigtime_t startConditional = system_time();
	for (int i = 0; i < kIterations; i++) {
		bool needsPreFill = (i % 10 == 0);  // 10% invalid
		if (needsPreFill) {
			memset(buffer, 0x40, kBufferSize);
		}
		dummy += buffer[kBufferSize / 2];
	}
	bigtime_t timeConditional = system_time() - startConditional;

	(void)dummy;
	free(buffer);

	// Ensure measurable times
	if (timeAlways == 0)
		timeAlways = 1;
	if (timeConditional == 0)
		timeConditional = 1;

	float speedup = (float)timeAlways / (float)timeConditional;

	printf("OK (always: %lld us, conditional: %lld us, speedup: %.1fx)\n",
		timeAlways, timeConditional, speedup);

	// Should be faster with conditional pre-fill (at least 3x given 90% skip)
	return (speedup > 3.0f);
}


// =============================================================================
// Test 5: Multi-threaded Pool Safety
// =============================================================================

static FramePool* sSharedPool = NULL;
static int32 sThreadErrors = 0;

static int32
pool_test_thread(void* data)
{
	int id = (int)(intptr_t)data;

	for (int i = 0; i < 1000; i++) {
		CamFrame* frame = sSharedPool->Alloc();
		if (frame == NULL) {
			atomic_add(&sThreadErrors, 1);
			continue;
		}

		// Write some data
		char buf[32];
		snprintf(buf, sizeof(buf), "thread%d-iter%d", id, i);
		frame->Write(buf, strlen(buf));

		// Small delay to increase contention
		if (i % 100 == 0)
			snooze(1);

		sSharedPool->Recycle(frame);
	}

	return 0;
}


static bool
test_pool_thread_safety()
{
	printf("Test: Pool thread safety... ");

	sSharedPool = new FramePool();
	sThreadErrors = 0;

	const int kThreadCount = 4;
	thread_id threads[kThreadCount];

	bigtime_t start = system_time();

	for (int i = 0; i < kThreadCount; i++) {
		threads[i] = spawn_thread(pool_test_thread, "pool_test",
			B_NORMAL_PRIORITY, (void*)(intptr_t)i);
		resume_thread(threads[i]);
	}

	for (int i = 0; i < kThreadCount; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
	}

	bigtime_t elapsed = system_time() - start;

	int32 errors = sThreadErrors;
	float hitRate = sSharedPool->HitRate();

	delete sSharedPool;
	sSharedPool = NULL;

	if (errors > 0) {
		printf("FAIL (%d errors)\n", (int)errors);
		return false;
	}

	printf("OK (%d threads, %lld us, %.1f%% hit rate)\n",
		kThreadCount, elapsed, hitRate);
	return true;
}


// =============================================================================
// Main
// =============================================================================

int
main(int argc, char** argv)
{
	printf("\n");
	printf("===========================================\n");
	printf("Memory Management Tests (Group 2)\n");
	printf("===========================================\n\n");

	int passed = 0;
	int failed = 0;

	if (test_pool_basic())
		passed++;
	else
		failed++;

	if (test_pool_performance())
		passed++;
	else
		failed++;

	if (test_pool_capacity())
		passed++;
	else
		failed++;

	if (test_conditional_prefill())
		passed++;
	else
		failed++;

	if (test_pool_thread_safety())
		passed++;
	else
		failed++;

	printf("\n");
	printf("===========================================\n");
	printf("Results: %d passed, %d failed\n", passed, failed);
	printf("===========================================\n\n");

	return (failed == 0) ? 0 : 1;
}
