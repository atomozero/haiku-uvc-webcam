# Bugfix Roadmap Phase 4 — residual audit (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP3.md`.
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
| FIX-30 | DeviceRemoved blocks on USB under roster lock | CamRoster.cpp | High | done |
| FIX-31 | Bulk pump holds lock during retry + bad log | CamDevice.cpp | High | done |
| FIX-32 | EHCI recovery clears endpoint under lock | addons/uvc/UVCCamDevice.cpp | High | done |
| FIX-33 | Set param derefs device without NULL check | addons/uvc/UVCCamDevice.cpp | High | done |
| FIX-34 | CamConfig raw size math and FPS NaN/INF | CamConfig.h | Medium | done |
| FIX-35 | Fill counters torn across threads | addons/uvc/UVCCamDevice.h,.cpp | Medium | done |
| FIX-36 | Audio volume/mute torn across threads | AudioProducer.h,.cpp | Medium | done |
| FIX-37 | Deframer read/dtor needs stopped pump | CamDeframer.cpp | Medium | done |
| FIX-38 | fopen macro hides call sites | CamDevice.cpp | Low | done |
| FIX-39 | Unlimited nodes need explicit guard | AddOn.cpp | Low | done |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh` (grep fixed pattern).
- Each FIX adds one guard so regressions are caught on Linux.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, `NULL` for pointers.
