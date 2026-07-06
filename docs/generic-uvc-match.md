# Data-driven quirks and bounds-safe UVC descriptor parsing

Widen device support while shrinking the crash surface: compatibility is a
matter of **data** (quirk tables, generic UVC class match), and descriptor
parsing is done through **bounds-safe pure functions** that are unit-tested and
fuzzed. Supporting a new quirky camera becomes a table edit covered by tests
rather than a new code branch; malformed or hostile descriptors are rejected,
never crashed.

## Motivation

USB descriptors carry device-controlled length and count fields. Trusting them
is how a buggy or hostile camera walks the parser off the end of a buffer. The
worst offenders have genuinely variable-length, device-driven arrays
(extension-unit controls, VS-header `bmaControls`) whose element offsets are
computed from untrusted bytes. This work moves that parsing behind validated,
bounds-checked helpers and proves the helpers never over-read.

## Modules

Both are self-contained and pure, so they are testable without USB hardware.

- `addons/uvc/UVCQuirks.{h,cpp}` — `ResolveWebcamQuirks(vid, pid, entryQuirks)`
  resolves per-device quirks OR-ed with vendor-wide quirks from data tables,
  replacing the hard-coded `VendorID() == 0x0c45` Sonix-stride check. A
  per-device `quirks` field was added to the device table (trailing, so all
  existing rows default to 0).
- `addons/uvc/UVCDescriptors.{h,cpp}` — bounds-checked field readers
  (`UVCDescByte/LE16/LE32`), a bounds-checked descriptor walker
  (`UVCDescriptorCursor`), and validators for the frame, uncompressed-format,
  extension-unit and VS-header descriptors. Every read is bounded by an explicit
  `avail`; nothing trusts an internal `bLength` beyond it.

## Parser migration (`addons/uvc/UVCCamDevice.cpp`)

- Frame descriptor: `frame_interval_type` and dimensions validated.
- Uncompressed format: the 16-byte GUID is copied out safely instead of being
  read from a possibly-stale struct.
- Extension unit: the `Extension()`/`ControlSize()` computed offsets (driven by
  two untrusted count bytes) are replaced by a validated view; a truncated
  descriptor keeps its usable identity (GUID) instead of over-reading.
- VS input/output header: `bmaControls[num_formats][control_size]` iteration is
  bounded to the entries that actually fit `bLength` (and can't zero-stride
  loop).

The generic UVC class match (claim any device exposing a VideoControl
interface) already existed in the device table, so broad recognition was
already in place; this work lands the quirk half plus the descriptor-safety
work.

## Testing

- `tests/test_quirks.cpp` — 6 cases.
- `tests/test_descriptors.cpp` — 39 cases (validators, readers, walker, and a
  full invariant sweep).
- `tests/fuzz_descriptors.cpp` — 5 guard-page phases. Each input is placed flush
  against a `PROT_NONE` page so any over-read faults immediately; 500k mutated
  inputs per phase across the frame validator, walker, format validator,
  extension-unit validator and VS-header iteration report no out-of-bounds reads
  and no invariant violations.

Build and run:

```
cd tests
g++ -O2 -o test_quirks test_quirks.cpp ../addons/uvc/UVCQuirks.cpp -I../addons/uvc -lbe && ./test_quirks
g++ -O2 -o test_descriptors test_descriptors.cpp ../addons/uvc/UVCDescriptors.cpp -I../addons/uvc -lbe && ./test_descriptors
g++ -O2 -o fuzz_descriptors fuzz_descriptors.cpp ../addons/uvc/UVCDescriptors.cpp -I../addons/uvc -lbe && ./fuzz_descriptors
```

## Follow-ups

- Migrate the remaining lower-risk descriptors (frame-based/H.264 format, MJPEG
  format, audio) onto the same helpers, each covered by the fuzzer.
- Save a corpus of real device descriptors as regression fixtures.
