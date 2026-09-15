# tests/

Self-contained C++ unit tests. Each file replicates a small piece of
driver logic with synthetic data so it can be built and run without a
USB camera, kernel patches, or even the rest of the driver compiled.

| File | What it exercises |
|---|---|
| `test_quirks.cpp` | Quirk resolution (portable, CI) |
| `test_descriptors.cpp` | Bounds-safe descriptor validators + real captures |
| `test_safety.cpp` | Overflow-safe helpers (FIX-C3/M5/H8/T10/M8/B4) + FIX-H1/H2/H5/H6 |
| `fuzz_descriptors.cpp` | Guard-page fuzzer (5 validators x 500k) |
| `test_deframer.cpp` | Deframer statistics counters and rate-limited logging |
| `test_deframer_fix.cpp` | Frame boundary detection via EOF and FID toggle |
| `test_memory_management.cpp` | CamFrame pool reuse, BMallocIO lifecycle |
| `test_video_conversion.cpp` | YUV→RGB lookup tables (correctness and speed) |
| `coverage.sh` | Runs all portable tests + static guards for Haiku-only fixes |

## Build & run

```sh
make test          # quirks + descriptors + safety + fuzzer
sh tests/coverage.sh --quick   # same without the fuzzer
sh tests/coverage.sh           # full, with fuzzer
```

Each test prints PASS/FAIL summaries; non-zero exit means at least one
assertion failed.

## What used to live here

Up until June 2026 `tests/` also held two other kinds of files:

- **Pattern-grep tests** (~20 files) that opened the driver source as
  text and searched for hard-coded strings or constants. After the
  P1-P40 fix batch the patterns no longer match and the tests fail for
  cosmetic reasons. They have been deleted; git history still has them
  if anyone needs to revive one.

- **Diagnostic data tools** (~11 files) that operate on YUY2 frame
  dumps or hardware — `analyze_*`, `convert_*`, `find_*`,
  `verify_alignment`, `visualize_yuv`, `uvc_benchmark`. They have been
  moved to `tools/` next to their siblings.

## Adding a new test

Keep the tests in this directory honest:

- No grep on `../addons/uvc/UVCCamDevice.cpp`. If you need to verify a
  source-level invariant, write a real assertion against a small
  reproduction of the data path.
- No dependency on a connected webcam — tests must run in CI.
- Update this README when you add a file so the table stays accurate.
