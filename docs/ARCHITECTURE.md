# Architecture

This document explains how the UVC webcam driver is structured, where
state lives, which threads touch what, and why several non-obvious
workarounds exist. Read it before touching the parsing, streaming or
deframer code — there are subtle invariants that are easy to break.

## Layered view

```
┌─────────────────────────────────────────────────────────────────┐
│                        Haiku Media Kit                          │
├─────────────────────────────────────────────────────────────────┤
│  WebCamMediaAddOn      (AddOn.cpp)                              │
│     │   one BMediaAddOn per process,                            │
│     │   exposes one or more flavor_info per camera              │
│     ▼                                                           │
│  VideoProducer / AudioProducer  (Producer.cpp, AudioProducer.cpp)│
│     │   one BBufferProducer node per stream a consumer opens    │
│     ▼                                                           │
├─────────────────────────────────────────────────────────────────┤
│  CamRoster   (CamRoster.cpp)                                    │
│     │   BUSBRoster — fires CamDevice for every connect/disconnect│
│     ▼                                                           │
│  CamDevice   (CamDevice.cpp)                                    │
│     │   USB plumbing: isochronous pump thread, control transfers │
│     ▼                                                           │
│  UVCCamDevice  (addons/uvc/UVCCamDevice.cpp)                    │
│     │   UVC protocol: descriptor parsing, probe/commit, formats  │
│     ▼                                                           │
│  UVCDeframer  (addons/uvc/UVCDeframer.cpp)                      │
│         frame assembly from isochronous packet payloads          │
├─────────────────────────────────────────────────────────────────┤
│                    BUSBKit / Haiku USB stack                    │
└─────────────────────────────────────────────────────────────────┘
```

## Data flow

USB isochronous IN → kernel USB stack → `BUSBEndpoint::IsochronousTransfer`
→ `CamDevice::DataPumpThread` copies packets into the deframer →
`UVCDeframer::Write` strips the UVC stream header from each packet and
accumulates the payload → frame complete (by EOF flag, by FID toggle, or
by reaching the expected size for fixed-size YUY2) → frame queued →
`UVCCamDevice::FillFrameBuffer` dequeues, runs the format-specific
converter (MJPEG decompress, YUV → BGRA32, etc.) → `VideoProducer`
delivers a `BBuffer` to the downstream consumer.

## Threads and ownership

| Thread | Lifetime | Touches |
|---|---|---|
| Roster thread | addon load → unload | `CamRoster::DeviceAdded/Removed`, instantiates `CamDevice` |
| Producer service thread (Media Kit) | node alive | `VideoProducer::HandleEvent`, downstream BBuffer dispatch |
| Data pump | `StartTransfer` → `StopTransfer` | `CamDevice::DataPumpThread`, single in-flight `IsochronousTransfer` |
| Audio pump | optional, if camera has UAC | `UVCCamDevice::AudioPumpThread`, separate iso endpoint |
| Consumer thread (downstream) | node connection | `FillFrameBuffer` via `BBufferProducer::AdditionalBufferRequested` |

The pump thread and the consumer thread access the same `UVCDeframer`,
gated by `UVCDeframer::fLocker` (a recursive `BLocker`).
`UVCCamDevice::fCamDevice` is captured by the producer before every use
to handle hot-unplug. `SetCamDevice(NULL)` must be called *before* the
`CamDevice` destructor runs, otherwise the producer can dereference a
freed pointer.

## Per-stream state

Most format-related state on `UVCCamDevice` belongs to a single
VideoStreaming interface ("the active stream"):

- `fStreamingIndex`, `fIsoIn`, `fCurrentVideoAlternate`
- `fUncompressedFrames`, `fMJPEGFrames`, `fFrameBasedFrames` (lists of
  `usb_video_frame_descriptor*` and `uvc_frame_based_resolution*`)
- `fUncompressedFormatIndex`, `fMJPEGFormatIndex`, `fFrameBasedFormatIndex`
- `fUncompressedPixelFormat`, `fIsMJPEG`, `fIsNV12`
- `fDefaultUncompressedFrameIndex`, `fDefaultMJPEGFrameIndex`
- `fStillCaptureMethod`, `fTriggerSupport`, `fTriggerUsage`
- `fNumFrameIntervals`, `fCurrentFrameIntervals[32]`

When a camera exposes more than one VS interface (Intel RealSense,
Sonix SN9C292, stereo cameras) the constructor scans all of them with
`score_streaming_interface()` and picks the highest-scoring one (MJPEG
> recognised uncompressed > unknown uncompressed > encoded). The whole
list of detected streams is also stored in `fVSStreams` so the addon
can emit a separate flavor per stream and `SelectStream(idx)` can
re-parse a different one on demand.

Switching the active stream:

1. The addon's `InstantiateNodeFor` decodes `streamIdx` from the high
   bits of `flavor_info::internal_id`.
2. It calls `UVCCamDevice::SelectStream(streamIdx)`.
3. `SelectStream` refuses if streaming is active (`B_BUSY`), otherwise
   frees the per-stream lists, calls `_ReparseVSInterface` on the new
   VS interface, rebuilds the sorted resolution list and resets the
   resolution selector.
4. The `VideoProducer` is constructed afterwards.

## Frame deframing

`UVCDeframer::Write` is called once per USB isochronous packet (only
packets with `actual_length > 0` reach it). Each packet starts with the
UVC stream header described in section 2.4.3.3 of the spec:

- Byte 0: `bHeaderLength` — offset where payload starts.
- Byte 1: `bmHeaderInfo` — flags. Bits we care about:
  - bit 0: Frame ID (FID) — toggles between consecutive frames
  - bit 1: End of Frame (EOF)
  - bit 6: Error
  - bits 2 & 3: PTS/SCR present (extends the header by 4 / 6 bytes)

We **trust the camera's `bHeaderLength`** when locating the payload.
The PTS/SCR layout check is informational: if it disagrees with the
expected size, syslog gets a rate-limited warning but the offset still
comes from `buf[0]`.

Frame completion is triggered by:

| Trigger | When | Resets fixedBufferPos / fTotalBytesThisFrame |
|---|---|---|
| FID toggle | next packet's FID bit differs from current | yes |
| EOF flag | header bit 1 set | yes |
| SIZE | accumulated payload ≥ `fExpectedFrameSize` (YUY2 only) | yes (P32 fix, June 2026) |

Before the P32 fix, the SIZE path left `fTotalBytesThisFrame` untouched
across frames. On Microdia 0c45:6409 at 320×240 the FID bit never
toggled, so the counter grew linearly and the oversize-detection
warning fired with bogus deltas. Resetting the counter on the SIZE path
fixed both the false-positive warning and the deframer stats logging.

Payload is buffered into `fFixedBuffer` (a 4 MB pre-allocation in
`UVCDeframer::SetExpectedFrameSize`). MJPEG and uncompressed both use
the same buffer; the older `fInputBuffer` (a `BMallocIO`) is no longer
used for data.

## Format selection and probe/commit

The negotiation sequence in `UVCCamDevice::_ProbeCommitFormat`:

1. **SetAlternate(0)** — reset the streaming interface so the camera
   accepts new probe values. We always do this even on first call
   because the camera's state may be undefined after USB errors.
2. **GET_LEN** on UVC 1.1+ — discover the real probe/commit payload
   size. If the camera answers, the value is used directly (limited to
   `[22, 64]`). If not, fall back to the UVC-version-based guess
   (`version > 0x0100 ? 34 : 26`).
3. **GET_MIN / GET_MAX / GET_DEF** on the probe control — three
   informational control transfers. We use them to:
   - log the bounds for diagnostics;
   - clamp our requested `dwFrameInterval` to `[min, max]`;
   - keep `GET_DEF` as a safety net for SET_CUR failures.
4. **SET_CUR PROBE** with the (possibly clamped) request, retrying up
   to 5 times per size and sweeping through eleven known probe sizes
   (`{34, 26, 48, 28, 30, 32, 36, 38, 40, 44, 22}`) — vendor firmwares
   often pick an in-between value that the spec doesn't list.
5. **GET_DEF fallback** — if every probe size failed and GET_DEF
   returned valid values earlier, retry SET_CUR once with the camera's
   own defaults. We lose the requested format/frame but get a working
   stream; the consumer can renegotiate via `AcceptVideoFrame`.
6. **GET_CUR PROBE** reads the negotiated values back.
7. **SET_CUR COMMIT** uses those negotiated values (not the original
   request) — the camera may have adjusted parameters.

The probe buffer is a `union { struct fields; uint8 raw[64]; }`. Some
vendor probes are larger than `sizeof(usb_video_probe_and_commit_controls)`;
the union lets us zero-pad the trailing bytes safely. UVC 1.5 fields
like `bUsage` accept zero as "use default", so zero padding is benign.

## Alternate selection (bandwidth)

`UVCCamDevice::_SelectBestAlternate` runs a two-pass scan:

- **Pass 1** picks the highest-bandwidth single-transaction (`mult=1`)
  alternate. This is the safe default on Haiku because both EHCI and
  xHCI have known bugs with `mult>1` isochronous endpoints.
- **Pass 1.5** (gated by `_ShouldUseHighBandwidth()`) promotes to a
  `mult>1` alternate when the probe payload exceeds what mult=1 can
  carry. Off by default; opt in with `WEBCAM_FORCE_HIGH_BANDWIDTH=1`
  on a patched kernel.
- **Pass 2** scans `mult>1` endpoints as a last resort when no mult=1
  alternate exists at all and the user has forced high bandwidth.

If transfers fail repeatedly while we're on a high-bandwidth alternate
(`_OnHighBandwidthFailure`), the flag is flipped off and the next
stream restart drops to a lower resolution to free up bandwidth, so
the user doesn't see a stalled stream.

## SetAlternate workaround

`BUSBInterface::SetAlternate()` has a double-free bug in
`_UpdateDescriptorAndEndpoints()` when going from zero-endpoint
alternate 0 to an alternate with ≥ 1 endpoint. To avoid the 0 → N
transition, `_SelectBestAlternate` first switches to an intermediate
alternate with > 0 endpoints, then to the target. The bug is fixed in
`patches/0001-USBKit-Fix-double-free-in-SetAlternate.patch`; the
workaround stays so the driver also works on unpatched kernels.

## Extension Units (XU)

`_ParseVideoControl` collects every VC_EXTENSION_UNIT descriptor into
`fExtensionUnits` (an `extension_unit_info` list). At `AddParameters`
time the driver iterates every selector of every XU with `GET_INFO` +
`GET_LEN` + `GET_CUR` so the parameter web reflects the camera's
current state. GET_LEN comes first to avoid the babble error on
Logitech C920 firmware that returns the full multi-byte payload
regardless of the requested length.

`_FindXU(vendor)`, `_XUSetCur`, `_XUGetCur`, `_XUGetMin`, `_XUGetMax`,
`_XUGetInfo`, `_XUGetLen` are the primitives. `_SonixAsicRead` /
`_SonixAsicWrite` are higher-level Sonix-bridge helpers using XU
selector 0x01 (ASIC_RW).

## USB controller and speed detection

`_DetectControllerType` inspects `bcdUSB` and isochronous endpoint
descriptors to decide whether we're on XHCI (USB 3.0+), an XHCI
controller in USB 2.0 mode, or EHCI. The result drives:

- IRQ moderation hints,
- whether mult>1 is allowed by default,
- diagnostic log noise (rate, batch sizes, etc.).

Haiku's XHCI does not currently support high-bandwidth iso safely —
the controller info marks `high_bandwidth_safe = false` regardless of
the detected type until the relevant kernel patches land.

## Known device quirks

| VID:PID | Quirk | Mitigation |
|---|---|---|
| 0c45:* (Sonix family) | Some chips use a 352-pixel internal buffer width even at 320×240 | P33 — quirk armed on the whole Sonix VID; applied at runtime only when `srcSize > expectedSize` |
| 0c45:6409 (Microdia Motion Eye) | YUY2-only firmware; FID does not toggle at 320×240; tearing is firmware-side | Sonix stride quirk + P32 byte-counter reset |
| 1bcf:0001 (AUKEY PC-LM1E) | Hardcoded fallback resolution list when descriptor parsing returns no frames | Vendor-specific fallback table in constructor |
| 046d:0825 (Logitech C270), 046d:081b (C310) | Advertise `bcdUVC=0x0110` but only accept the 26-byte UVC 1.0 probe layout | P34 — force `uvcVersion = 0x0100` for these PIDs |
| 046d:082d (Logitech C920) and similar | XU GET_CUR with a guessed length babbles | XU branch uses GET_LEN first (merged from `fix/uvc-xu-probe-babble`) |

## Haiku-side limitations the driver cannot work around

- **`IsochronousTransfer` is synchronous and single-buffer.** The pump
  thread blocks until the kernel reports completion, then must process
  the packets before issuing the next call. Microframes that arrive in
  the gap are lost. Linux uvcvideo uses URB queues to keep multiple
  transfers in flight; Haiku does not. We mitigate by batching 64
  microframes per call (the buffer is sized to
  `fIsoMaxPacketSize * 64`).
- **EHCI host system error after ~2 min** of sustained isochronous
  streaming on some Intel controllers. Kernel patch in
  `patches/ehci-isochronous-host-error-panic-fix.patch`.
- **High-bandwidth (mult>1) iso endpoints** are blocked by kernel bugs
  on both EHCI and xHCI. Patches in `patches/`.
- **`BUSBInterface::SetAlternate()` double-free** when going from a
  zero-endpoint alternate to a multi-endpoint alternate. Workaround in
  `_SelectBestAlternate`; patch 0001 in `patches/`.

## Where to look first when something breaks

| Symptom | Likely place |
|---|---|
| Camera not detected at all | `CamDeviceAddon::Sniff` in `CamDevice.cpp` (verbose with `WEBCAM_DEBUG=1`) |
| `Init FAILED` | constructor of `UVCCamDevice` near the descriptor parse loop |
| `SET_CUR Probe failed` | `_ProbeCommitFormat`; check the GET_LEN / GET_MIN / GET_MAX log lines |
| `WaitFrame TIMEOUT` storm | data pump thread; usually downstream EHCI/USB stack issue |
| Image shifted / tearing | deframer header parsing or YUV conversion stride |
| Wrong colours | uncompressed format detection (`identify_uncompressed_format`) |
| Resolution stuck at default | `AcceptVideoFrame` / `SuggestVideoFrame` and the sorted resolution list |
