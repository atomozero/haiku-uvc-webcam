# Bugfix Roadmap — haiku-uvc-webcam audit (Sept 2026)

Source: deep audit of ~21k LOC (descriptors, threading, streaming, Media Kit).
Tests before fixes: `test_quirks 6/6`, `test_descriptors 53/53`,
`test_safety 49/49` pass on Linux. They cover only the portable
modules, not the Haiku-only paths below.

Rules for this batch:
- One fix = one commit.
- Each fix updates docs (`docs/CHANGELOG.md` or this file).
- Each fix runs `sh tests/coverage.sh --quick` (portable only, no Haiku build).
- No full `make` here (needs Haiku + `libturbojpeg` + USB kit).
- Haiku style: tabs, max 100 cols, `f`/`k`/`g` prefixes, simple English comments.

## Fix list

| ID | Bug | Files | Severity | Commit |
|----|-----|-------|----------|--------|
| FIX-01 | `_descriptor[11]` on struct ptr, breaks build | `addons/uvc/UVCCamDevice.cpp:1850` | Critical | done |
| FIX-02 | `NW80xCamDevice::ReadIIC` calls itself, stack overflow | `addons/NW80xCamDevice.cpp:197` | Critical | done |
| FIX-03 | Destructor frees after stall, wedged thread UAF (video + audio) | `CamDevice.cpp:209`, `UVCCamDevice.cpp:1307` | Critical | done |
| FIX-04 | `fCamDevice` plain ptr race on hot-unplug (video + audio) | `Producer.h/.cpp`, `AudioProducer.h/.cpp` | Critical | todo |
| FIX-05 | `Connect` destination leak + missing NULL/format checks | `Producer.cpp:800`, `AudioProducer.cpp:530` | High | todo |
| FIX-06 | Param ID collision + missing size/NULL checks | `UVCCamDevice.cpp:4521`, `CamDevice.cpp:587` | High | todo |
| FIX-07 | `w*h*bpp` overflow and div-by-zero in bandwidth math | `UVCCamDevice.cpp:2243,2635,3199` | High | todo |
| FIX-08 | Shared `tjhandle` without lock, destroy while pump runs | `UVCCamDevice.cpp:6579,1331` | High | todo |
| FIX-09 | Descriptor subtype read without length guard, raw counts | `UVCCamDevice.cpp:1828,1537,1601` | Medium | todo |
| FIX-10 | `SelectStream` race, `Unplugged` audio leak, deframer guards | `UVCCamDevice.cpp:6455`, `CamDevice.cpp:335` | Medium | todo |

## Coverage map

- Portable logic: `tests/test_safety.cpp`, `test_descriptors.cpp`, `fuzz_descriptors.cpp`.
- Haiku-only paths: static guards in `tests/coverage.sh` (grep for fixed pattern).
- Each FIX below adds or keeps a guard so `sh tests/coverage.sh --quick` stays green.

## Style checklist (Haiku)

- Tabs for indent, 4 spaces per tab, max 100 cols.
- `//` comments, short English, no names, no sentiments.
- Early return, no `else` after return.
- No `if (ptr != NULL) delete ptr`, no `(x)` around return.
- C++ casts, `NULL` for pointers, `status_t` for errors.
