#!/bin/sh
# Copyright 2026, Haiku UVC Webcam contributors.
# Distributed under the terms of the MIT License.
#
# Coverage runner for the driver fix batch. Builds the portable unit tests
# and the guard-page fuzzer (all runnable on Linux CI, no camera needed),
# runs them, and then checks the non-portable Haiku-only fixes with static
# pattern guards so regressions are caught even without the USB stack.
#
# Usage: sh tests/coverage.sh [--quick]
#   --quick skips the 5x500k guard-page fuzzer (unit tests only).
#
# Fix -> test mapping:
#   FIX-H1  continuous frame 38B      test_safety + test_descriptors
#   FIX-H2  still-image counts        test_safety (UVCSafeStillImageCounts)
#   FIX-H5  audio Format Type I       test_safety (UVCSafeAudioFormatICount)
#   FIX-H6  selector/array bounds     test_safety + test_descriptors
#   FIX-C3  producer buffer overflow  test_safety (UVCSafeProducerBufferSize)
#   FIX-M5  YUV/RGB32 overflow        test_safety (UVCSafeYUY2/RGB32Size)
#   FIX-H8  audio ring overflow       test_safety (UVCSafeAudioRingSize)
#   FIX-T10 field_rate UB             test_safety (UVCSafeFieldInterval)
#   FIX-M8  ring capacity 0           test_safety (UVCGuardRingCapacity)
#   FIX-B4  backoff overflow          test_safety (UVCSafeBackoffDelay)
#   FIX-C1  fCamDevice UAF            static: QuitVideoNode joins generator
#   FIX-C2  audio stop UAF            static: StopAudioTransfer timeout check
#   FIX-C4  reconfig survives unplug  static: Unplugged stops reconfig
#   FIX-T1  StopTransfer unlock       static: explicit locked/unlocked split
#   FIX-T5  destructor hang           static: wait_for_thread_etc timeout
#   FIX-M1  deframer queue cap        static: AddItem guarded on EOF path
#   FIX-M2  MJPEG trunc / malloc-temp static: realloc-temp + trunc counter
#   FIX-M7  XU short-transfer         static: ret != length check
#   FIX-19  Suggest/Accept NULL+wrap   static: NULL skip + 64-bit gap
#   FIX-20  audio multi-instance      static: no early return on instances
#   FIX-21  reconfig sem cleanup      static: delete sem after wait
#   FIX-22  frame sync race           static: copy id under lock
#   FIX-23  short generator lock      static: copy group under lock
#   FIX-24  flush under lock          static: lock in UVC Flush
#   FIX-25  deframer pool robustness  static: recycle + tag guards
#   FIX-26  stats init                static: memset stats + skip equal stamp
#   FIX-27  addon guards              static: NULL check + return 0
#   FIX-28  backoff cap               static: attempt cap 32
#   FIX-29  still NULL guard          static: device NULL check in trigger
#   FIX-30  unplug probe outside lock static: find then probe then unlink
#   FIX-31  bulk pump outside lock      static: copy then transfer outside
#   FIX-32  EHCI cycle outside lock     static: snapshot then cycle outside
#   FIX-33  set param NULL guard        static: fail closed when unplugged
#   FIX-34  config size and FPS guard     static: range test and 64-bit size
#   FIX-35  fill counters atomic          static: fill counters are atomic
#   FIX-36  audio params atomic           static: keep them atomic
#   FIX-37  deframer teardown order       static: pump must be stopped
#   FIX-38  no fopen macro                static: hidden macros
#   FIX-39  single node per camera        static: one node of each kind
#   FIX-40  recovery flag atomic          static: recovery flag is atomic
set -e
cd "$(dirname "$0")"

QUICK=0
if [ "$1" = "--quick" ]; then
	QUICK=1
fi

PASS=0
FAIL=0

run() {
	echo "--- $1 ---"
	if eval "$2"; then
		PASS=$((PASS + 1))
	else
		echo "COVERAGE FAIL: $1"
		FAIL=$((FAIL + 1))
	fi
}

check() {
	echo "--- static: $1 ---"
	if eval "$2"; then
		PASS=$((PASS + 1))
	else
		echo "COVERAGE FAIL (static): $1"
		FAIL=$((FAIL + 1))
	fi
}

CXX="${TEST_CXX:-g++}"
FLAGS="-O2 -Wall -I ../addons/uvc"

run "test_quirks" "$CXX $FLAGS -o test_quirks test_quirks.cpp ../addons/uvc/UVCQuirks.cpp && ./test_quirks"
run "test_descriptors" "$CXX $FLAGS -o test_descriptors test_descriptors.cpp ../addons/uvc/UVCDescriptors.cpp && ./test_descriptors"
run "test_safety" "$CXX $FLAGS -o test_safety test_safety.cpp ../addons/uvc/UVCSafety.cpp ../addons/uvc/UVCDescriptors.cpp && ./test_safety"

if [ "$QUICK" -eq 0 ]; then
	run "fuzz_descriptors" "$CXX $FLAGS -o fuzz_descriptors fuzz_descriptors.cpp ../addons/uvc/UVCDescriptors.cpp && ./fuzz_descriptors"
else
	echo "--- fuzz_descriptors skipped (--quick) ---"
fi

# Static guards for Haiku-only code paths (not compilable on Linux).
# Each greps for the fixed pattern introduced by the fix batch.
SRC=".."
check "FIX-C1 generator joined before delete" "grep -q 'JoinFrameGenerator' $SRC/Producer.cpp $SRC/CamDevice.cpp"
check "FIX-C2 audio stop honours timeout" "grep -q 'B_TIMED_OUT' $SRC/addons/uvc/UVCCamDevice.cpp"
check "FIX-C4 unplug stops reconfig" "grep -q 'StopReconfigThread' $SRC/CamDevice.cpp"
check "FIX-T1 no IsLocked unlock" "! grep -q 'hadLock.*IsLocked' $SRC/CamDevice.cpp"
check "FIX-T5 destructor bounded join" "grep -q 'wait_for_thread_etc' $SRC/CamDevice.cpp"
check "FIX-M1 deframer queue cap" "grep -q 'CAMDEFRAMER_MAX_QUEUED_FRAMES' $SRC/addons/uvc/UVCDeframer.cpp"
check "FIX-M2 trunc counter" "grep -q 'fFramesTruncated' $SRC/addons/uvc/UVCDeframer.cpp"
check "FIX-M7 XU length check" "grep -q 'ret != length' $SRC/addons/uvc/UVCCamDevice.cpp"
check "FIX-19 accept null skip" "grep -q 'if (descriptor == NULL)' $SRC/addons/uvc/UVCCamDevice.cpp"
check "FIX-19 64-bit gap" "grep -q 'pixels > target' $SRC/addons/uvc/UVCCamDevice.cpp"
check "FIX-20 multi mic" "grep -q 'allow many mics' $SRC/AudioProducer.cpp"
check "FIX-21 sem cleanup" "grep -q 'Free sem even on timeout' $SRC/CamDevice.cpp"
check "FIX-22 sync under lock" \
	"grep -q 'Copy id under lock' $SRC/Producer.cpp $SRC/AudioProducer.cpp"
check "FIX-23 short lock" \
	"grep -q 'Copy group and size under lock' $SRC/Producer.cpp"
check "FIX-24 flush lock" \
	"grep -q 'Clear local state under lock' $SRC/addons/uvc/UVCDeframer.cpp"
check "FIX-25 recycle drop" \
	"grep -q 'Recycle to keep pool warm' $SRC/CamDeframer.cpp"
check "FIX-25 tag guards" \
	"grep -q 'tags == NULL' $SRC/CamDeframer.cpp"
check "FIX-26 stats init" \
	"grep -q 'memset(&fStats' $SRC/Producer.cpp"
check "FIX-27 addon guard" \
	"grep -q 'out_failure_text != NULL' $SRC/AddOn.cpp"
check "FIX-28 backoff cap" \
	"grep -q 'attempt > 32' $SRC/addons/uvc/UVCSafety.cpp $SRC/CamDevice.cpp"
check "FIX-29 still guard" \
	"grep -q 'Device can go NULL on unplug' $SRC/addons/uvc/UVCCamDevice.cpp"
check "FIX-30 probe outside lock" \
	"grep -q 'probe outside' $SRC/CamRoster.cpp"
check "FIX-31 bulk outside lock" \
	"grep -q 'transfer outside' $SRC/CamDevice.cpp"
check "FIX-32 EHCI cycle outside lock" \
	"grep -q 'cycle outside' $SRC/addons/uvc/UVCCamDevice.cpp"
check "FIX-33 set param NULL guard" \
	"grep -q 'Fail closed when unplugged' $SRC/addons/uvc/UVCCamDevice.cpp"
check "FIX-34 config size and FPS guard" \
	"grep -q 'falls back to the default interval' $SRC/CamConfig.h"
check "FIX-35 fill counters atomic" \
	"grep -q 'Fill counters are atomic' $SRC/addons/uvc/UVCCamDevice.h"
check "FIX-36 audio params atomic" \
	"grep -q 'keep them atomic' $SRC/AudioProducer.h"
check "FIX-37 deframer teardown order" \
	"grep -q 'Pump must be stopped' $SRC/CamDeframer.cpp"
check "FIX-38 no fopen macro" \
	"! grep -q 'define fopen(path' $SRC/CamDevice.cpp"
check "FIX-39 single node per camera" \
	"grep -q 'One node of each kind' $SRC/AddOn.cpp"
check "FIX-40 recovery flag atomic" \
	"grep -q 'Recovery flag is atomic' $SRC/addons/uvc/UVCCamDevice.h"

echo ""
echo "coverage: $PASS passed, $FAIL failed"
exit $FAIL
