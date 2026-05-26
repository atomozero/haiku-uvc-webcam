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

## Known Device Quirks

- **Microdia 0c45:6409**: Stride quirk only when `srcSize > expectedSize`. Sonix XU readable (unit 4, GUID `7033f028`) but format registers read-only. YUY2-only, tearing is a firmware limitation.
- **AUKEY 1bcf:0001**: Hardcoded fallback resolutions when USB descriptor parsing fails. MJPEG preferred, works at 1280x720.

## MJPEG Size Monitoring

Threshold: 1% of raw YUY2 size (min 1024 bytes). Evaluation after 60 frames with 10s cooldown. Prevents false "bandwidth insufficient" alarms on highly compressed streams.

## Haiku USB Limitations

- EHCI high-bandwidth endpoints (mult>1) don't deliver payload data — disabled by default
- EHCI "host system error" after ~2 min sustained isochronous streaming on some Intel controllers
- `BUSBInterface::SetAlternate()` bug patched in `patches/` directory

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
