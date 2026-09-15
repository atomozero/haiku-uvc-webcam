# Bugfix Roadmap Phase 2 — residual bugs after FIX-01..10 (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP.md`. Each fix is one commit with
`sh tests/coverage.sh --quick` green. No full `make` here
(needs Haiku + libturbojpeg + USB kit).

Rules:
- One fix = one commit (code + docs in same commit).
- Simple English comments, Haiku style: tabs, max 100 cols,
  early return, BAutolock, no redundant NULL check before delete.

## Fix list

| ID | Bug | Files | Severity | Status |
|----|-----|-------|----------|--------|
| FIX-11 | GetParameterValue NULL/size deref (P_COLOR/P_INFO/audio) | Producer.cpp, AudioProducer.cpp | Critical | done |
| FIX-12 | UVCDeframer FID path unbounded queue, OOM on stall | addons/uvc/UVCDeframer.cpp | Critical | done |
| FIX-13 | fCamDevice unlocked reads, roster snooze delete | Producer.cpp, AudioProducer.cpp, CamRoster.cpp | Critical | done |
| FIX-14 | HandleStop abandon strands fLock, kill_thread with lock | Producer.cpp, AudioProducer.cpp | Critical | done |
| FIX-15 | Unplugged w/o lock, tjDestroy w/o generator join, reconfig volatile + unbounded join | CamDevice.cpp/.h, UVCCamDevice.cpp | High | done |
| FIX-16 | StartTransfer probe outside lock, audio Start/Stop race, ring free vs memcpy | UVCCamDevice.cpp | High | todo |
| FIX-17 | Deframer Write vs Flush race, sem drain livelock, DropFrame permit | CamDeframer.cpp, CamStreamingDeframer.cpp, UVCDeframer.cpp | Medium | todo |
| FIX-18 | streamIdx bit-pack overflow, fConnectedFormat race, audio static, FPSToMicroseconds NaN, UVCDesc wrap | AddOn.cpp, Producer.h, AudioProducer.cpp, CamUtils.h, UVCDescriptors.cpp | Medium | todo |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`, `fuzz_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh`, plus `git diff --check`.
- Each FIX keeps `sh tests/coverage.sh --quick` at 11 passed.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, C++ casts, `NULL` for pointers.
