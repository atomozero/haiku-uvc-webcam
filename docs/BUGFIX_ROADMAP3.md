# Bugfix Roadmap Phase 3 — verification follow-up (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP.md` and `docs/BUGFIX_ROADMAP2.md`.
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
| FIX-19 | Suggest/Accept NULL deref and int overflow | addons/uvc/UVCCamDevice.cpp | High | done |
| FIX-20 | Audio single-instance blocks second mic | AudioProducer.cpp | High | done |
| FIX-21 | StopReconfigThread leaks sem on timeout | CamDevice.cpp | High | done |
| FIX-22 | fFrameSync TOCTOU release vs delete | Producer.cpp, AudioProducer.cpp | Medium | done |
| FIX-23 | Generator holds fLock across SendBuffer | Producer.cpp | Medium | done |
| FIX-24 | UVC Flush touches state without lock | addons/uvc/UVCDeframer.cpp | Medium | done |
| FIX-25 | CamDeframer pool/sem robustness | CamDeframer.cpp | Medium | done |
| FIX-26 | fStats garbage on first update | Producer.cpp | Low | done |
| FIX-27 | AddOn InitCheck NULL and CountFlavors error | AddOn.cpp | Low | done |
| FIX-28 | Backoff cap attempt to avoid long loop | addons/uvc/UVCSafety.cpp, CamDevice.cpp | Low | done |
| FIX-29 | Still capture NULL device guard | addons/uvc/UVCCamDevice.cpp | Medium | done |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh` (grep fixed pattern).
- Each FIX adds one guard so regressions are caught on Linux.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, `NULL` for pointers.
