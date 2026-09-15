# Bugfix Roadmap Phase 8 — residual audit follow-up (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP7.md`.
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
| FIX-52 | Get param derefs device without NULL check | addons/uvc/UVCControls.cpp | High | done |
| FIX-53 | Audio start TOCTOU on device plus rate clamp | addons/uvc/UVCAudio.cpp | Medium | todo |
| FIX-54 | Throwing new without OOM check | Producer.cpp, AudioProducer.cpp, AddOn.cpp | Low | todo |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh` (grep fixed pattern).
- Each FIX adds one guard so regressions are caught on Linux.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, `NULL` for pointers.
