# CLAUDE.md

Guidance for working with this codebase.

## Build

```bash
make              # Build the media add-on
make clean        # Clean
make install      # Install to /boot/home/config/non-packaged/add-ons/media/
```

Requires libturbojpeg and Haiku private headers.

## Architecture

```
WebCamMediaAddOn (AddOn.cpp)  →  CamRoster (USB discovery)
    ↓                                ↓
VideoProducer (Producer.cpp)     CamDevice (base class)
AudioProducer (AudioProducer.cpp)    ↓
                                 UVCCamDevice (addons/uvc/)
                                     ↓
                                 UVCDeframer → frame assembly
```

**Data flow**: USB isochronous → CamDevice pump thread → UVCDeframer → FillFrameBuffer (YUY2→RGB32 or MJPEG→RGB32) → VideoProducer → BBuffer to consumers.

## Key Files

| File | Role |
|------|------|
| `AddOn.cpp/h` | BMediaAddOn, flavor enumeration, node instantiation |
| `Producer.cpp/h` | Video BBufferProducer, format negotiation, frame delivery |
| `AudioProducer.cpp/h` | Audio BBufferProducer, ring buffer consumer |
| `CamDevice.cpp/h` | Base class: USB transfers, data pump thread, hot-plug |
| `CamRoster.cpp/h` | BUSBRoster: device discovery, connect/disconnect |
| `addons/uvc/UVCCamDevice.cpp/h` | UVC protocol, probe/commit, format parsing, MJPEG/YUY2 |
| `addons/uvc/UVCDeframer.cpp/h` | Frame boundary detection (FID/EOF/SIZE), payload assembly |

## Thread Safety Rules

- `fCamDevice` in VideoProducer: capture to local variable before use, check NULL
- `SetCamDevice(NULL)` must be called BEFORE CamDevice destruction
- Audio ring buffer: `fAudioRingHead` written by pump thread, `fAudioRingTail` by consumer, both via `atomic_set/get`
- `fAudioRingSem`: producer calls `release_sem` after write, consumer `acquire_sem_etc` with timeout
- `fTransferEnabled`: atomic flag for stopping data pump thread
- `release_sem(fFrameSync)`: always guard with `fFrameSync >= 0`

## Frame Deframing

**MJPEG**: frame complete on FID toggle or EOF. Data from `fFixedBuffer`.
**YUY2**: frame complete when `fFixedBufferPos >= fExpectedFrameSize`. Overflow truncated.

Both formats write payload to `fFixedBuffer` (4MB pre-allocated). The old `fInputBuffer` (BMallocIO) is NOT used for data — only `fFixedBuffer`.

## Extension Unit (XU) Controls

UVC Extension Units allow vendor-specific features beyond the standard controls.

**Primitives** (in UVCCamDevice):
- `_XUSetCur(unitId, selector, data, len)` / `_XUGetCur()` / `_XUGetMin()` / `_XUGetMax()` — standard UVC class requests to XU
- `_XUGetInfo(unitId, selector, &info)` — query capability bitmap (bit 0=GET, bit 1=SET)
- `_FindXU(vendor)` — look up extension_unit_info by vendor enum

**Sonix helpers**:
- `_SonixAsicRead(addr, &val)` / `_SonixAsicWrite(addr, val)` — read/write bridge registers via XU selector 0x01 (ASIC_RW)

**Auto-discovery**: during AddParameters(), each XU's selectors are probed with GET_INFO + GET_CUR and logged via syslog.

**Exposed controls**:
- Sonix LED toggle (BParameterWeb "Vendor Features" group)
- Parameter IDs use dynamic allocation (fXULedParameterID) consistent with Camera Terminal controls

**Known XU GUIDs**:
| Vendor | GUID prefix | Unit IDs |
|--------|------------|----------|
| Sonix SYS | `7033f028` | typically 3 or 4 |
| Sonix USR | `9473dfdd` or `3fae1228` | typically 4 or 5 |
| Microsoft H264 | `a9c86c04` | varies |
| Logitech | `82066163` | varies |
| Realtek | `70ea6d28` | varies |

## Known Device Quirks

- **Microdia 0c45:6409**: Stride quirk only when `srcSize > expectedSize`. Sonix XU readable (unit 4, GUID `7033f028`) but format registers read-only. YUY2-only, tearing is a firmware limitation.
- **AUKEY 1bcf:0001**: Hardcoded fallback resolutions when USB descriptor parsing fails. MJPEG preferred, works at 1280x720.

## MJPEG Size Monitoring

Threshold: 1% of raw YUY2 size (min 1024 bytes). Evaluation after 60 frames with 10s cooldown. Prevents false "bandwidth insufficient" alarms on highly compressed streams.

## SetAlternate Crash Workaround

Haiku's `BUSBInterface::SetAlternate()` has a double-free bug in `_UpdateDescriptorAndEndpoints()` when switching between alternates with different endpoint counts (0→N). The driver works around this by first switching to an intermediate alternate with >0 endpoints before switching to the target. This avoids the 0→N transition. Reported on hrev57937 with SuYin HP Truevision webcam.

## Haiku USB Limitations

- EHCI high-bandwidth endpoints (mult>1) don't deliver payload data — disabled by default
- EHCI "host system error" after ~2 min sustained isochronous streaming on some Intel controllers
- `BUSBInterface::SetAlternate()` double-free bug — workaround in `_SelectBestAlternate()`, kernel patch in `patches/`
- Isochronous transfers are synchronous (single-buffer) — no multi-URB queuing like Linux

## Microdia 0c45:6409 Investigation Summary

Extensive testing confirmed the YUY2 tearing on this camera is NOT a driver bug:
- USB data arrives correctly (same PTS, no gaps between transfers)
- EHCI kernel driver offset calculations are correct (verified by simulation)
- The chip is a Sonix variant with UVC-only firmware (does not respond to SN9C20x proprietary protocol)
- Sonix XU ASIC registers are readable (unit 4) but format registers are write-protected
- On Linux, this device has no specific driver either (not in uvcvideo, gspca, or sn9c20x)
- The Windows driver `snp2uvc.sys` likely uses proprietary initialization not publicly documented

## AUKEY 1bcf:0001 (Working Reference Camera)

Verified working with MJPEG streaming at 320x240 through 1280x720:
- MJPEG: 6 resolutions up to 1920x1080, FID toggles correctly per-frame
- YUY2: 6 resolutions up to 1920x1080
- Audio: 2 channel, 16-bit, 32000 Hz (UAC)
- UVC controls: Brightness, Contrast, Gain, Hue, Saturation, Sharpness, Gamma, WB Temperature, Backlight Compensation, Auto-Exposure, Exposure Time
- No zoom/pan/tilt (not supported by hardware)
- Streaming crashes after ~2 min due to EHCI host system error (Intel controller bug, not driver)

## Diagnostic Tools

Located in `tools/`:
- `sonix_probe` — probe Sonix bridge registers via vendor USB transfers
- `sonix_xu_probe` — probe Sonix XU via UVC Extension Unit protocol
- `uvc_controls_probe` — discover supported UVC camera/processing controls
- `analyze_tearing` — analyze YUY2 frame for horizontal tearing artifacts
- `simulate_itd` — simulate EHCI iTD offset calculation

## Running Tests

```bash
cd tests
g++ -O2 -o test_deframer_fix test_deframer_fix.cpp -lbe && ./test_deframer_fix
```

## Debugging

```bash
tail -f /var/log/syslog | grep -i "UVC\|MJPEG\|webcam\|Producer\|Deframer"
export WEBCAM_DEBUG=verbose   # before media server restart
```
