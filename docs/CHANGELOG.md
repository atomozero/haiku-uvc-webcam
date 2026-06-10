# Changelog

This file summarises notable changes per topic. For the exact commits,
look up the IDs with `git log` — they are referenced in each entry.

## June 2026 — P1-P40 fix batch

A deep audit of the driver was performed against the goal "many UVC
webcams are not recognised and do not start streaming". 30+ issues
(`P1`-`P41`) were filed and addressed in this batch.

### Discovery & matching

- **P1** (`41c68dd`) — `CamDeviceAddon::Sniff`: NULL-safe walk of
  configurations / interfaces / alternates, active syslog logging of
  every match decision, verbose per-alternate logging gated on
  `WEBCAM_DEBUG`. The "Generic UVC" entry in `kSupportedDevices` was
  loosened so any interface with USB Video Class (0x0E) matches,
  regardless of subclass — composite IAD devices (Logitech C920+,
  OBSBOT, Razer Kiyo) now match reliably even when their device-level
  class is 0xEF.

### Format support

- **P7** (`9ac168d`) — uncompressed format converters extended beyond
  YUY2/NV12 to UYVY, NV21, YV12, I420/IYUV and GREY/Y8. The driver
  now picks the most preferred recognised format when the camera
  advertises several.
- **P8** (`c57827e`) — `VS_FORMAT_FRAME_BASED` and
  `VS_FRAME_FRAME_BASED` are parsed. H.264, H.265, VP8 and M-JPEG2000
  GUIDs are recognised. Streams are logged with their codec and
  resolutions but not decoded — MJPEG / uncompressed remains the
  active streaming format.

### Probe / commit negotiation

- **P12** (`00dc506`) — `fHeaderDescriptor` can be NULL when the
  hardcoded fallback path (AUKEY, Microdia) populates the frame lists
  without a VC header. Treat a missing header as UVC 1.0 in
  `_ProbeCommitFormat`.
- **P15** (`e30a33a`) — the camera's `bDefaultFrameIndex` is honoured
  when choosing the initial resolution, with the legacy
  nearest-target-pixel heuristic kept as fallback. Strict firmwares
  that only accept probes starting from the default now succeed.
- **P19** (`65b0071`) — UVC 1.1+ `GET_LEN` is queried before SET_CUR.
  The retry sweep covers 11 sizes including vendor-extended 28, 30,
  32, 36, 38, 40 and 44 alongside the spec-conformant 26, 34, 48.
- **P20** (`00dc506`) — `GET_MIN`, `GET_MAX` and `GET_DEF` are queried
  on the probe control. Frame interval is clamped to the advertised
  range. If SET_CUR fails across every probe size, the driver retries
  once with the camera's own GET_DEF values as a strict-firmware
  safety net.
- **P34** (`0ef630b`) — Logitech C270 (`046d:0825`) and C310
  (`046d:081b`) advertise `bcdUVC=0x0110` but only accept the 26-byte
  UVC 1.0 probe layout. Force `uvcVersion = 0x0100` for these PIDs to
  skip the failing 34-byte attempt and the retry latency it causes.

### Bandwidth & transfer

- **P16** (`1bcbab3`) — re-enabled the previously-dead "pass 1.5"
  high-bandwidth promotion in `_SelectBestAlternate`, gated on
  `_ShouldUseHighBandwidth()` so the default behaviour stays safe.
  `WEBCAM_FORCE_HIGH_BANDWIDTH=1` now actually selects a `mult>1`
  alternate when needed. On repeated failures the driver requests a
  resolution drop instead of leaving the stream stalled.
- **P25** (`5ad50e1`) — isochronous transfers are batched in groups
  of 64 packets (up from 32) to reduce the number of synchronous
  `IsochronousTransfer` round-trips. A proper kernel fix is still
  required for zero gap streaming, but the batching halves the loss
  rate in field testing.

### Multi-stream cameras (P3)

- **Phase A** (`ad0ac79`) — the constructor scores every VS interface
  and picks the highest-scoring one (MJPEG > recognised uncompressed >
  unknown uncompressed > encoded), instead of keeping the last one
  parsed. On Intel RealSense the color stream now wins over depth/IR.
- **Phase B** (`133f5ed`, `f1f5528`, `d592537`, `04a8238`) — every VS
  interface is recorded in `fVSStreams`. The addon exposes one media
  flavor per stream (`internal_id` carries the stream index in bits
  24-30). `InstantiateNodeFor` calls `UVCCamDevice::SelectStream`,
  which refuses while streaming, otherwise re-parses the requested VS
  interface in place.

### Streaming robustness

- **P14** (`26bfb6f`) — discrete frame intervals cap raised from 8 to
  32 (`kMaxFrameIntervals`). Logitech BRIO at 720p advertises 9, some
  industrial cameras advertise 12+; both were truncated.
- **P22** (`0aac705`) — MJPEG-only cameras with no `libturbojpeg`
  available fail Init with a loud message instead of silently
  producing blue placeholders. `SuggestVideoFrame`/`AcceptVideoFrame`
  fall back to uncompressed when the decoder is missing but the
  camera also offers YUY2.
- **P29** (`368a784`) — UVC packet header layout validation no longer
  stops after the first 15 packets; bad firmwares now surface in
  syslog past the warm-up window with bounded log noise.
- **P30** (`b409bb5`) — frame queue depth raised from 8 to 16
  (`CAMDEFRAMER_MAX_QUEUED_FRAMES`). On overflow the **oldest** frame
  is dropped, not the newest — paused-then-resumed consumers see fresh
  data instead of a half-second stale burst.
- **P32** (`037f15c`, `30f0311`) — oversize-YUY2 warning surfaces
  stride mismatches; the per-frame byte counter resets on the
  SIZE-complete deframer path so the warning is accurate and the
  "Previous frame total bytes" stats line stays meaningful after
  resolution changes (Microdia 0c45:6409 at 320×240).

### Diagnostics & UX

- **P40** (`4175276`) — when an attached UVC camera fails to initialise,
  the addon posts a desktop `BNotification` with the VID:PID and a
  short reason; previously the device just disappeared from Media
  Settings with no user-facing hint.
- **P11** (`1a9d38b`) — duplicate `VC_HEADER` descriptors go to syslog
  instead of `printf`.
- **P41** (`b761a71`) — unknown VS descriptor subtypes go to syslog so
  vendor extensions show up in `/var/log/syslog`.
- **P4** (`c9de6b5`) — dead `fIsoIn` lookup on the VS zero-bandwidth
  alternate removed.
- **P10** (`12855ad`) — frame descriptors with absurd dimensions or
  `max_video_frame_buffer_size > 50 MB` are skipped rather than added
  to the list.

### Quirks

- **P33** (`de519f7`) — the Microdia 0c45:6409 stride quirk is now
  armed for any device under Sonix VID 0x0c45; the runtime gate
  (`srcSize > expectedSize`) still prevents it from firing when not
  needed.
- **P21** (`0d129aa`) — probe `bmHint` policy documented; added
  `WEBCAM_MJPEG_QUALITY` (0..10000, UVC units) for advanced users
  who want to pin the MJPEG compression quality.
- **P2** (`ca46dd9`) — the constructor no longer calls
  `SetConfiguration` once per USB configuration. It picks the
  configuration that owns the VideoControl interface up front and
  only switches to it if the device isn't already there. Composite
  devices with config 1 = HID + config 2 = UVC stop getting reset
  unnecessarily.

### Merged from external branch

- **XU babble fix** (`fad8a5f`) — XU control endpoint queries first
  call `GET_LEN` to learn the actual control length, then `GET_CUR`
  with the exact size. On Logitech C920 firmware a guessed length
  triggered an xHCI babble error that halted the control pipe.

## June 2026 — repository hygiene

- Removed AI-tooling files from the public repo (`CLAUDE.md`,
  `.claude/`). The architecture content survives in
  `docs/ARCHITECTURE.md`.
- Moved planning docs under `docs/` (`XHCI_IMPLEMENTATION_TASKS.md`).
- Tracked the EHCI panic patch and its analysis report. Removed the
  redundant `USBInterface.cpp.fixed` snapshot in favour of the
  applicable `.patch`.
- Updated `README.md` to reflect the current feature set, environment
  variables and limitations.
- Reorganised `tests/`: 11 diagnostic data tools moved to `tools/`,
  20 stale pattern-grep tests deleted. 4 self-contained unit tests
  remain.
- Added READMEs for `bin/`, `tests/`, `patches/`.

## Before this batch

For older changes see `git log` directly. The driver started as a
port of the original Haiku UVC work by Ithamar Adema, Jérôme Duval
and Gabriel Hartmann; the AUKEY-specific fallback paths and the
audio class plumbing predate this changelog.
