# tests/

These files are **pattern-grep tests on the source code**, not runtime
unit tests. Each test reads a `.cpp`/`.h` file in the project and
verifies that specific strings, constants or constructs are present.

They were written at various points during the development of this
driver as smoke checks before installing the addon, and they have **not
been kept in sync** with the recent batch of fixes (P1, P3, P7, P8,
P12, P14, P15, P16, P19, P20, P22, P25, P29, P30, P32, P33, P34, P40,
P41 — June 2026). Several tests now fail on current code simply
because the patterns they search for have been rewritten:

| Test | Why it is probably stale |
|---|---|
| `test_probe_commit_size.cpp` | hard-codes `{ 34, 26, 48, 22 }` — the list is now 11 sizes |
| `test_format_selection.cpp` | predates UYVY/NV21/YV12/I420/GREY support and per-stream selection |
| `test_resolution_ordering.cpp` | predates `bDefaultFrameIndex` honour |
| `test_nv12_support.cpp` | overlaps with the unified converter table |
| `test_new_features.cpp` / `test_architecture.cpp` | broad pattern sweeps that drifted |

## How to build & run a test

```sh
cd tests
g++ -O2 -o test_<name> test_<name>.cpp -lbe
./test_<name>
```

Most tests do not need a webcam — they read the source files via
relative paths. A few (`test_deframer*.cpp`, `test_video_conversion.cpp`)
operate on synthetic byte buffers.

## Status

- Kept in-tree as historical reference and starting points for new
  tests.
- Don't treat a failure as a regression unless the failing pattern is
  still meaningful for the current code.
- If you take ownership of a test, please update it in the same commit
  that touches the source file it inspects.
