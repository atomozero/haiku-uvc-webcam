# Bugfix Roadmap Phase 5 — residual cleanup (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP4.md`.
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
| FIX-40 | EHCI recovery flag torn across threads | addons/uvc/UVCCamDevice.h,.cpp | Medium | done |
| FIX-41 | InstantiateNodeFor derefs camera before NULL check | AddOn.cpp | Medium | done |
| FIX-42 | Producer libc macros hide call sites | Producer.cpp | Low | todo |
| FIX-43 | Orphan Haiku-only tests unwired and stale | tests/ | Low | todo |
| FIX-44 | Legacy Italian comments | addons/uvc/UVCCamDevice.cpp,.h, Producer.cpp | Low | todo |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh` (grep fixed pattern).
- Each FIX adds one guard so regressions are caught on Linux.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, `NULL` for pointers.
