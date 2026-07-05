/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * Guard-page fuzzer for the UVC frame-descriptor validator. Each input is
 * placed so its last byte sits flush against an inaccessible guard page, so
 * ANY read past the declared `avail` faults immediately (caught and reported)
 * rather than silently reading adjacent memory. Deterministic (fixed seed).
 *
 * Build & run:
 *   g++ -O2 -o fuzz_descriptors fuzz_descriptors.cpp \
 *       ../addons/uvc/UVCDescriptors.cpp -I../addons/uvc -lbe && ./fuzz_descriptors
 */
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>

#include "UVCDescriptors.h"

static const long kIterations = 500000;
static const uint32 kMaxLen = 48;	// inputs range 0..kMaxLen bytes

static sigjmp_buf sJmp;
static void
OnFault(int)
{
	siglongjmp(sJmp, 1);
}


// Place `len` bytes flush against a guard page: reading byte[len] faults.
// Returns the writable buffer pointer; *mapBase/*mapLen describe the mapping.
static uint8*
GuardedBuffer(size_t len, void** mapBase, size_t* mapLen)
{
	long page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
	size_t total = (size_t)page * 2;
	uint8* base = (uint8*)mmap(NULL, total, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED)
		return NULL;
	// Make the second page inaccessible.
	if (mprotect(base + page, page, PROT_NONE) != 0) {
		munmap(base, total);
		return NULL;
	}
	*mapBase = base;
	*mapLen = total;
	return base + ((size_t)page - len);	// ends exactly at the guard boundary
}


int
main()
{
	printf("=== UVCDescriptors guard-page fuzzer ===\n");
	printf("iterations: %ld\n", kIterations);

	if (signal(SIGSEGV, OnFault) == SIG_ERR) {
		printf("cannot install SIGSEGV handler\n");
		return 2;
	}

	srand(0xC0FFEE);
	long valid = 0, invalid = 0, badInvariant = 0;

	// A reusable valid-ish template for the mutation strategy.
	uint8 tmpl[kMaxLen];

	for (long it = 0; it < kIterations; it++) {
		size_t len = (size_t)(rand() % (kMaxLen + 1));

		void* mapBase = NULL;
		size_t mapLen = 0;
		uint8* buf = GuardedBuffer(len, &mapBase, &mapLen);
		if (buf == NULL) {
			printf("mmap failed at iter %ld\n", it);
			return 2;
		}

		// Three input strategies.
		int strat = rand() % 3;
		if (strat == 0) {
			for (size_t i = 0; i < len; i++)
				buf[i] = (uint8)(rand() & 0xff);
		} else if (strat == 1 && len >= 26) {
			// Structured: a plausible frame descriptor, then a few byte flips.
			memset(tmpl, 0, sizeof(tmpl));
			tmpl[0] = (uint8)len;			// bLength = len
			tmpl[1] = 0x24; tmpl[2] = 0x05;	// CS_INTERFACE, VS_FRAME_UNCOMPRESSED
			uint16 w = (uint16)(1 + rand() % 2000);
			uint16 h = (uint16)(1 + rand() % 2000);
			tmpl[5] = (uint8)w; tmpl[6] = (uint8)(w >> 8);
			tmpl[7] = (uint8)h; tmpl[8] = (uint8)(h >> 8);
			tmpl[25] = (uint8)((len - 26) / 4);	// ftype at capacity
			int flips = rand() % 4;
			for (int f = 0; f < flips; f++)
				tmpl[rand() % len] = (uint8)(rand() & 0xff);
			memcpy(buf, tmpl, len);
		} else {
			// Edge: mostly zeros with an aggressive bLength/ftype.
			for (size_t i = 0; i < len; i++)
				buf[i] = 0;
			if (len > 0) buf[0] = (uint8)(rand() & 0xff);	// bLength
			if (len > 25) buf[25] = (uint8)(rand() & 0xff);	// ftype
		}

		UVCFrameDescCheck r;
		if (sigsetjmp(sJmp, 1) != 0) {
			// A fault means the validator read past the buffer: the bug we
			// exist to catch. Report the offending input and fail hard.
			printf("\nOUT-OF-BOUNDS READ at iter %ld: len=%zu", it, len);
			if (len > 0) printf(" bLength=%u", buf[0]);
			if (len > 25) printf(" ftype=%u", buf[25]);
			printf("\n");
			munmap(mapBase, mapLen);
			return 1;
		}

		r = UVCCheckFrameDescriptor(buf, len);

		// Invariants that must hold for any accepted descriptor.
		if (r.valid) {
			valid++;
			bool ok = r.width >= 1 && r.width <= 8192
				&& r.height >= 1 && r.height <= 8192
				&& r.maxVideoFrameSize <= 50u * 1024 * 1024
				// Buffer-safe interval count: the fixed header plus the discrete
				// intervals must fit within both the declared bLength and avail.
				&& (kUVCFrameDescFixedLen
						+ (size_t)r.frameIntervalType * 4) <= len
				&& (size_t)buf[0] <= len
				&& (kUVCFrameDescFixedLen
						+ (size_t)r.frameIntervalType * 4) <= (size_t)buf[0];
			if (!ok) {
				badInvariant++;
				printf("\nINVARIANT VIOLATION at iter %ld: len=%zu w=%u h=%u "
					"maxbuf=%u ftype=%u bLength=%u\n", it, len, r.width,
					r.height, r.maxVideoFrameSize, r.frameIntervalType, buf[0]);
				munmap(mapBase, mapLen);
				return 1;
			}
		} else {
			invalid++;
		}

		munmap(mapBase, mapLen);
	}

	printf("frame validator: no OOB reads, no invariant violations "
		"(accepted=%ld rejected=%ld bad=%ld)\n", valid, invalid, badInvariant);

	// Phase 2: fuzz the safe descriptor walker over arbitrary buffers. Any read
	// past the buffer faults on the guard page; we also assert every yielded
	// descriptor lies fully within the buffer and that the walk terminates.
	long walked = 0;
	for (long it = 0; it < kIterations; it++) {
		size_t len = (size_t)(rand() % (kMaxLen + 1));
		void* mapBase = NULL;
		size_t mapLen = 0;
		uint8* buf = GuardedBuffer(len, &mapBase, &mapLen);
		if (buf == NULL) {
			printf("mmap failed at walker iter %ld\n", it);
			return 2;
		}
		for (size_t i = 0; i < len; i++)
			buf[i] = (uint8)(rand() & 0xff);

		if (sigsetjmp(sJmp, 1) != 0) {
			printf("\nWALKER OUT-OF-BOUNDS READ at iter %ld: len=%zu\n", it, len);
			munmap(mapBase, mapLen);
			return 1;
		}

		UVCDescriptorCursor cur(buf, len);
		const uint8* d;
		size_t l;
		size_t iters = 0;
		bool ok = true;
		while (cur.Next(&d, &l)) {
			// Every yielded descriptor must lie fully within [buf, buf+len).
			if (!(d >= buf && (size_t)(d - buf) + l <= len && l >= 2)) {
				ok = false;
				break;
			}
			walked++;
			// Termination guard: a descriptor is >= 2 bytes, so a buffer of
			// `len` bytes can yield at most len/2 of them.
			if (++iters > len) {
				ok = false;
				break;
			}
		}
		if (!ok) {
			printf("\nWALKER INVARIANT VIOLATION at iter %ld: len=%zu\n", it, len);
			munmap(mapBase, mapLen);
			return 1;
		}
		munmap(mapBase, mapLen);
	}
	printf("descriptor walker: no OOB reads, all walks bounded "
		"(descriptors walked=%ld)\n", walked);

	printf("\nPASS\n");
	return 0;
}
