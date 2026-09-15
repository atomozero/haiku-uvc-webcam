# Bugfix Roadmap Phase 6 — hot-path optimization (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP5.md`.
Each fix is one commit with static coverage guards.
No full `make` here (needs Haiku + libturbojpeg + USB kit).

Rules:
- One fix = one commit (code + docs + coverage guard).
- Simple English comments, Haiku style: tabs, max 100 cols,
  early return, BAutolock, no redundant NULL check before delete.
- Style check: `git diff --check` clean, no compile (not Haiku host).

## Fix list

| ID | Bug | Files | Severity | Status |
|----|-----|-------|----------|--------|
| FIX-45 | Packet counters read without atomics in FillFrameBuffer | addons/uvc/UVCCamDevice.cpp | High | done |
| FIX-46 | Per-packet atomics in pump, batch per transfer | CamDevice.cpp | Medium | done |
| FIX-47 | YUY2 row check hoist and 32-bit pad fill | addons/uvc/UVCCamDevice.cpp, UVCDeframer.cpp | Low | done |
| FIX-48 | Adaptive deframer pool and queue byte cap | CamDeframer.cpp,.h | Medium | done |
| FIX-49 | Probe size hint fast-path | addons/uvc/UVCCamDevice.h,.cpp | Medium | todo |
| FIX-50 | Dead frame-repeat cache copies every frame | addons/uvc/UVCCamDevice.h,.cpp | Medium | todo |
| FIX-51 | Zero-copy frame accumulation into CamFrame | addons/uvc/UVCDeframer.h,.cpp | High | todo |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh` (grep fixed pattern).
- Each FIX adds one guard so regressions are caught on Linux.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, `NULL` for pointers.
