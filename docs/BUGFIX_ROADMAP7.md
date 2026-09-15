# Refactor Roadmap Phase 7 — structural cleanup (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP6.md`.
Each item is one commit with static coverage guards.
No full `make` here (needs Haiku + libturbojpeg + USB kit).

Rules:
- One item = one commit (code + docs + coverage guard).
- Simple English comments, Haiku style: tabs, max 100 cols,
  early return, BAutolock, no redundant NULL check before delete.
- Style check: `git diff --check` clean, no compile (not Haiku host).
- No behaviour change unless noted; refactors keep semantics.

## Item list

| ID | Refactor | Files | Status |
|----|----------|-------|--------|
| RF-01 | Drop-oldest helper, remove dead #if 0 blocks | UVCDeframer.h,.cpp, UVCCamDevice.cpp, CamDevice.cpp | done |
| RF-02 | Dead deframer subclasses out of the build | Makefile, CamBufferingDeframer.*, CamStreamingDeframer.* | done |
| RF-03 | printf toward syslog with throttle | addons/uvc/UVCCamDevice.cpp | done |
| RF-04 | Split FillFrameBuffer into helpers | addons/uvc/UVCCamDevice.h,.cpp | done |
| RF-05 | Split UVCCamDevice.cpp by area | addons/uvc/* | done |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh` (grep fixed pattern).
- Each item adds one guard so regressions are caught on Linux.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, `NULL` for pointers.
