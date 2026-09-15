# Bugfix Roadmap Phase 9 — deframer exclusion (Sept 2026)

Follow-up to `docs/BUGFIX_ROADMAP8.md`.
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
| FIX-55 | Reselect and Flush race frame accumulation | addons/uvc/UVCDeframer.cpp | High | done |
| FIX-56 | Addon dtor NULL after OOM early return | AddOn.cpp | High | done |

## Coverage map

- Portable: `tests/test_safety.cpp`, `test_descriptors.cpp`.
- Haiku-only: static guards in `tests/coverage.sh` (grep fixed pattern).
- Each FIX adds one guard so regressions are caught on Linux.

## Style checklist (Haiku)

- Tabs, 4 spaces per tab, max 100 cols.
- `//` short English, no names, no sentiments.
- Early return, no else after return.
- `BAutolock` for locks, `NULL` for pointers.
