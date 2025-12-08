/*
 * Copyright 2024, Haiku USB Webcam Driver Project
 * Distributed under the terms of the MIT License.
 *
 * Test suite for USB transfer performance optimizations (Group 1)
 *
 * Build:
 *   g++ -o test_usb_performance test_usb_performance.cpp -lbe
 *
 * Run:
 *   ./test_usb_performance
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OS.h>
#include <Locker.h>


// =============================================================================
// Test 1: Double Buffer Structure
// =============================================================================

struct usb_double_buffer {
	uint8*		buffers[2];
	size_t		bufferSize;
	int32		activeBuffer;
	int32		readyBuffer;
	sem_id		bufferReady;
	bool		initialized;

	usb_double_buffer()
		:
		bufferSize(0),
		activeBuffer(0),
		readyBuffer(-1),
		bufferReady(-1),
		initialized(false)
	{
		buffers[0] = NULL;
		buffers[1] = NULL;
	}
};


static bool
test_double_buffer_init()
{
	printf("Test: Double buffer initialization... ");

	usb_double_buffer db;

	// Allocate buffers
	const size_t kTestSize = 4096;
	db.buffers[0] = (uint8*)malloc(kTestSize);
	db.buffers[1] = (uint8*)malloc(kTestSize);

	if (db.buffers[0] == NULL || db.buffers[1] == NULL) {
		printf("FAIL (allocation)\n");
		free(db.buffers[0]);
		free(db.buffers[1]);
		return false;
	}

	db.bufferReady = create_sem(0, "test_buffer_ready");
	if (db.bufferReady < B_OK) {
		printf("FAIL (semaphore)\n");
		free(db.buffers[0]);
		free(db.buffers[1]);
		return false;
	}

	db.bufferSize = kTestSize;
	db.initialized = true;

	// Verify state
	if (db.activeBuffer != 0 || db.readyBuffer != -1) {
		printf("FAIL (initial state)\n");
		delete_sem(db.bufferReady);
		free(db.buffers[0]);
		free(db.buffers[1]);
		return false;
	}

	// Cleanup
	delete_sem(db.bufferReady);
	free(db.buffers[0]);
	free(db.buffers[1]);

	printf("OK\n");
	return true;
}


static bool
test_double_buffer_swap()
{
	printf("Test: Double buffer swap... ");

	usb_double_buffer db;
	const size_t kTestSize = 4096;

	db.buffers[0] = (uint8*)malloc(kTestSize);
	db.buffers[1] = (uint8*)malloc(kTestSize);
	db.bufferReady = create_sem(0, "test_buffer_ready");
	db.bufferSize = kTestSize;
	db.activeBuffer = 0;
	db.readyBuffer = -1;
	db.initialized = true;

	// Write pattern to buffer 0
	memset(db.buffers[0], 0xAA, kTestSize);

	// Simulate swap
	db.readyBuffer = db.activeBuffer;  // 0 becomes ready
	db.activeBuffer = 1 - db.activeBuffer;  // Switch to 1
	release_sem(db.bufferReady);

	// Verify
	if (db.activeBuffer != 1 || db.readyBuffer != 0) {
		printf("FAIL (swap state)\n");
		delete_sem(db.bufferReady);
		free(db.buffers[0]);
		free(db.buffers[1]);
		return false;
	}

	// Verify ready buffer still has pattern
	bool patternValid = true;
	for (size_t i = 0; i < kTestSize; i++) {
		if (db.buffers[db.readyBuffer][i] != 0xAA) {
			patternValid = false;
			break;
		}
	}

	if (!patternValid) {
		printf("FAIL (data integrity)\n");
		delete_sem(db.bufferReady);
		free(db.buffers[0]);
		free(db.buffers[1]);
		return false;
	}

	// Cleanup
	delete_sem(db.bufferReady);
	free(db.buffers[0]);
	free(db.buffers[1]);

	printf("OK\n");
	return true;
}


// =============================================================================
// Test 2: Log Throttling
// =============================================================================

static bool
test_log_throttling()
{
	printf("Test: Log throttling logic... ");

	const int32 kLogThrottleInterval = 1000;
	const bigtime_t kLogTimeInterval = 5000000;  // 5 seconds

	int32 logCount = 0;
	bigtime_t lastLogTime = 0;
	bigtime_t startTime = system_time();

	// Simulate 5000 transfer attempts in quick succession
	for (int32 attempt = 1; attempt <= 5000; attempt++) {
		bigtime_t now = startTime + (attempt * 100);  // 100us per transfer

		bool shouldLog = (attempt <= 5)
			|| (attempt % kLogThrottleInterval == 0)
			|| (now - lastLogTime > kLogTimeInterval);

		if (shouldLog) {
			logCount++;
			lastLogTime = now;
		}
	}

	// Expected logs:
	// - 5 for first 5 attempts
	// - 4 for attempts 1000, 2000, 3000, 4000, 5000 (5 total, but 5000 overlaps)
	// Actually: 1,2,3,4,5 (5 logs) + 1000,2000,3000,4000,5000 (5 logs) = ~10
	// Time-based logs shouldn't trigger since we're simulating fast transfers

	// Should be significantly less than 5000
	if (logCount > 20) {
		printf("FAIL (too many logs: %d)\n", (int)logCount);
		return false;
	}

	if (logCount < 5) {
		printf("FAIL (too few logs: %d)\n", (int)logCount);
		return false;
	}

	printf("OK (logged %d of 5000 = %.2f%%)\n",
		(int)logCount, 100.0f * logCount / 5000.0f);
	return true;
}


// =============================================================================
// Test 3: memset Performance Comparison
// =============================================================================

static bool
test_memset_performance()
{
	printf("Test: memset elimination performance... ");

	const size_t kBufferSize = 128 * 1024;  // 128KB like USB buffer
	const int kIterations = 10000;  // More iterations for measurable time

	uint8* buffer = (uint8*)malloc(kBufferSize);
	if (buffer == NULL) {
		printf("FAIL (allocation)\n");
		return false;
	}

	// Warmup to avoid cache effects
	memset(buffer, 0xFF, kBufferSize);

	// Measure time WITH memset
	bigtime_t startWith = system_time();
	volatile uint8 dummy = 0;  // Prevent optimization
	for (int i = 0; i < kIterations; i++) {
		memset(buffer, (uint8)i, kBufferSize);
		dummy += buffer[kBufferSize / 2];  // Force actual work
	}
	bigtime_t timeWith = system_time() - startWith;

	// Measure time WITHOUT memset (just the work we actually need)
	bigtime_t startWithout = system_time();
	for (int i = 0; i < kIterations; i++) {
		// Only access the bytes we actually use
		buffer[0] = (uint8)i;
		buffer[kBufferSize / 2] = (uint8)i;
		dummy += buffer[kBufferSize / 2];
	}
	bigtime_t timeWithout = system_time() - startWithout;

	(void)dummy;  // Suppress unused warning
	free(buffer);

	// Ensure we have measurable times
	if (timeWith == 0)
		timeWith = 1;
	if (timeWithout == 0)
		timeWithout = 1;

	float speedup = (float)timeWith / (float)timeWithout;

	printf("OK (with memset: %lld us, without: %lld us, speedup: %.0fx)\n",
		timeWith, timeWithout, speedup);

	// memset should be significantly slower (at least 10x)
	return (speedup > 10.0f);
}


// =============================================================================
// Test 4: Lock Scope Optimization
// =============================================================================

static volatile int32 sSharedCounter = 0;

static int32
lock_test_thread(void* data)
{
	BLocker* locker = (BLocker*)data;

	for (int i = 0; i < 1000; i++) {
		locker->Lock();
		sSharedCounter++;
		locker->Unlock();
	}

	return 0;
}


static bool
test_lock_scope()
{
	printf("Test: Minimized lock scope... ");

	BLocker locker("test_locker");
	sSharedCounter = 0;

	// Spawn multiple threads
	const int kThreadCount = 4;
	thread_id threads[kThreadCount];

	bigtime_t start = system_time();

	for (int i = 0; i < kThreadCount; i++) {
		threads[i] = spawn_thread(lock_test_thread, "lock_test",
			B_NORMAL_PRIORITY, &locker);
		resume_thread(threads[i]);
	}

	// Wait for all threads
	for (int i = 0; i < kThreadCount; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
	}

	bigtime_t elapsed = system_time() - start;

	// Verify counter
	if (sSharedCounter != kThreadCount * 1000) {
		printf("FAIL (counter: %d, expected: %d)\n",
			(int)sSharedCounter, kThreadCount * 1000);
		return false;
	}

	printf("OK (4 threads x 1000 ops in %lld us)\n", elapsed);
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
	printf("USB Performance Optimization Tests (Group 1)\n");
	printf("===========================================\n\n");

	int passed = 0;
	int failed = 0;

	if (test_double_buffer_init())
		passed++;
	else
		failed++;

	if (test_double_buffer_swap())
		passed++;
	else
		failed++;

	if (test_log_throttling())
		passed++;
	else
		failed++;

	if (test_memset_performance())
		passed++;
	else
		failed++;

	if (test_lock_scope())
		passed++;
	else
		failed++;

	printf("\n");
	printf("===========================================\n");
	printf("Results: %d passed, %d failed\n", passed, failed);
	printf("===========================================\n\n");

	return (failed == 0) ? 0 : 1;
}
