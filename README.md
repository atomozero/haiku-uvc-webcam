# UVC Webcam Driver for Haiku

A USB Video Class (UVC) driver for Haiku OS, providing support for standard USB webcams with video and audio capabilities.

## Screenshots

![MJPEG streaming at 1280x720 with BubiCam](img/screenshot03.png)

![Webcam streaming in Cortex](img/screenshot01.png)

![Camera controls panel](img/screenshot02.png)

## Features

### Video Support
- **MJPEG** - Compressed format, recommended for HD resolutions (720p, 1080p)
- **Uncompressed formats**: YUY2/YUYV, UYVY, NV12, NV21, YV12, I420/IYUV, GREY/Y8
- **H.264 / H.265 / VP8 / M-JPEG2000**: detected and logged (not yet decoded)
- **Multi-stream cameras**: each VideoStreaming interface exposed as a separate
  media flavor (Intel RealSense color/depth/IR, Sonix SN9C292, stereo cameras)

### Camera Controls
- **Processing Unit**: Brightness, Contrast, Saturation, Sharpness, Gamma, Hue
- **White Balance**: Temperature, Auto mode
- **Exposure**: Auto/Manual modes, Exposure time
- **Focus**: Auto/Manual focus, Zoom
- **Other**: Backlight compensation, Gain, Power line frequency (anti-flicker)

### Audio Support
- USB Audio Class 1.0 for webcams with built-in microphones
- Automatic detection and separate audio node creation

### Advanced Features
- Multi-camera support with unique device naming
- Hot-plug with parameter persistence
- Automatic resolution fallback on bandwidth issues
- USB 3.0 (XHCI) and USB 2.0 (EHCI) controller optimization
- Extension Unit detection for vendor-specific features

## Supported Devices

| Device | VID:PID | Format | Status |
|--------|---------|--------|--------|
| AUKEY PC-LM1E | 1BCF:0001 | MJPEG + YUY2 | Working (MJPEG up to 1280x720, audio OK) |
| SuYin HP Truevision | Various | MJPEG + YUY2 | Tested (see crash workaround below) |
| Microdia Motion Eye | 0C45:6409 | YUY2 only | Tearing (Sonix chip, no MJPEG in firmware) |
| Realtek USB Camera | 0BDA:5843 | MJPEG + YUY2 | Supported |
| Generic UVC webcams | Various | Varies | Should work |

The driver auto-detects any UVC-compliant webcam. Devices not in the list may still work. **MJPEG webcams are recommended** — YUY2-only cameras may show tearing on USB 2.0 due to bandwidth constraints.

> **Note for older Haiku versions** (before hrev58000): a bug in Haiku's `BUSBInterface::SetAlternate()` can cause a crash when starting video capture. The driver includes a workaround, but if you experience crashes, update to a newer Haiku nightly or apply the kernel patch from `patches/`.

## Installation

### From Source

```bash
# Build
make

# Install to user add-ons
make install

# Restart media services so the new addon is picked up. Haiku's launch
# daemon respawns media_server automatically after the kill:
kill $(pidof media_server)
```

### Manual Installation

Copy `aukey_webcam_v4.media_addon` to:
```
/boot/home/config/non-packaged/add-ons/media/
```

## Build Requirements

- Haiku OS (R1/beta5 or nightly)
- GCC compiler
- libturbojpeg (`pkgman install devel:libturbojpeg`)

## Usage

After installation and media server restart:

1. Open **MediaPlayer** or **Cortex**
2. The webcam appears as a video producer node
3. Connect it to a video consumer (window, recorder, etc.)

### Testing with Cortex

1. Launch Cortex from Deskbar > Applications
2. Find your camera node (e.g., "AUKEY PC-LM1E USB Camera")
3. Drag a connection to "Video Window"
4. Double-click the camera node to access controls

## Troubleshooting

### Camera not detected

```bash
# Check if device is recognized
listusb | grep -i video

# Check driver loading
tail -f /var/log/syslog | grep -i uvc
```

### Poor video quality or stuttering

1. Try lower resolution (640x480)
2. Use MJPEG format instead of YUY2 for HD
3. Check USB connection (avoid hubs for USB 2.0 cameras)

### No audio from webcam microphone

- Ensure the webcam has a built-in microphone
- Check Media preferences for audio input selection
- Audio node appears as "[Camera Name] Audio"

### Kernel panic on resolution change

This is fixed in the driver. If you experience issues:
1. Avoid changing resolution while streaming
2. Stop the stream before changing settings

## Diagnostic Tools

The `tools/` directory contains standalone utilities for debugging:

| Tool | Purpose |
|------|---------|
| `sonix_xu_probe` | Probe Sonix webcam registers via UVC Extension Units |
| `analyze_tearing` | Analyze YUY2 frame data for horizontal tearing |
| `simulate_itd` | Simulate EHCI iTD offset calculations |
| `yuv2bmp` / `raw2bmp` | Convert raw YUY2/RGB32 frames to BMP |

Build any tool with:
```bash
cd tools
g++ -O2 -o <tool_name> <tool_name>.cpp -lbe -ldevice
```

### Environment variables

| Variable | Effect |
|---|---|
| `WEBCAM_DEBUG=verbose` | Verbose Sniff/probe logging in syslog |
| `WEBCAM_FORCE_HIGH_BANDWIDTH=1` | Allow mult>1 isochronous endpoints (requires kernel patches in `patches/`) |
| `WEBCAM_DISABLE_HIGH_BANDWIDTH=1` | Force mult=1 only (default) |
| `WEBCAM_MJPEG_QUALITY=N` | Pin MJPEG `wCompQuality` to N (0..10000, UVC units) |
| `WEBCAM_PROBE_DELAY=N` | Extra delay (ms) before probe/commit (default 100 ms) |

Set them before restarting `media_server`. Logs go to `/var/log/syslog`:
```sh
tail -f /var/log/syslog | grep -iE "UVCCamDevice|Sniff|UVCDeframer"
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                  Media Kit                          │
├─────────────────────────────────────────────────────┤
│  VideoProducer    │    AudioProducer               │
├───────────────────┴─────────────────────────────────┤
│                   CamDevice                         │
│  (Frame buffering, USB transfers, deframing)        │
├─────────────────────────────────────────────────────┤
│                 UVCCamDevice                        │
│  (UVC protocol, format negotiation, controls)       │
├─────────────────────────────────────────────────────┤
│                   CamRoster                         │
│  (USB device discovery, hot-plug handling)          │
├─────────────────────────────────────────────────────┤
│               Haiku USB Stack                       │
└─────────────────────────────────────────────────────┘
```

## Performance Optimizations

- **YUV-RGB lookup tables**: pre-computed conversion tables, no per-pixel multiplications
- **Frame pool recycling**: reduces memory allocation overhead
- **Larger isochronous batches**: 64 packets/transfer to limit the number of
  blocking round-trips through the synchronous `IsochronousTransfer` API
- **Drop-oldest queue**: a paused consumer gets the freshest frame instead of
  a half-second stale burst on resume
- **Adaptive FPS**: YUY2 streams clamp `frame_interval` to the bandwidth the
  selected alternate can actually carry
- **`GET_LEN` + extended probe size sweep**: 11 known probe/commit sizes,
  with GET_LEN queried first on UVC 1.1+ firmwares
- **`GET_MIN`/`GET_MAX`/`GET_DEF`**: probe bounds queried and used as a
  strict-firmware safety net before SET_CUR

## Documentation

- `docs/haiku-ehci-isochronous-panic-report.md` — kernel-side EHCI panic analysis
- `docs/haiku-xhci-isochronous-bug-report.md` — XHCI bandwidth allocation bug
- `docs/XHCI_IMPLEMENTATION_TASKS.md` — XHCI optimization tracking
- `patches/README.md` — kernel patches catalogue
- `bin/README.md` — pre-built kernel binaries (USB host controllers)
- `tests/README.md` — staleness notes on the source-pattern tests

## Known Limitations

- **EHCI host system error**: sustained isochronous streaming (>~2 min) on
  some Intel EHCI controllers can panic the host. Kernel patch in
  `patches/ehci-isochronous-host-error-panic-fix.patch`; the userspace
  driver cannot work around this.
- **`IsochronousTransfer` is synchronous on Haiku**: only one transfer in
  flight at a time. We batch 64 microframes/call to minimise the gap, but
  a proper fix would require URB queueing in the kernel USB stack.
- **High-bandwidth endpoints (mult>1)**: disabled by default — both EHCI
  and xHCI have known issues. Opt in via `WEBCAM_FORCE_HIGH_BANDWIDTH=1`
  on a patched kernel; the driver auto-falls back to a lower resolution
  if transfers start failing.
- **Encoded streams** (H.264/H.265/VP8/M-JPEG2000): the driver detects
  these formats but does not yet decode them. MJPEG and uncompressed
  remain the active streaming formats.
- **YUY2-only cameras** at HD resolutions may show tearing or dropped
  frames on USB 2.0 due to bandwidth limits.
- **Microdia 0c45:6409**: YUY2-only firmware; format change registers are
  write-protected. Tearing is firmware-side, not a driver bug.
- **Still image capture**: detected, exposed in syslog, but no hardware
  trigger plumbing yet.

## License

Distributed under the MIT License. See source files for details.

## Credits

- Original driver: Ithamar Adema, Jérôme Duval, Gabriel Hartmann
- UVC improvements and optimizations: 2024-2025 contributors

## Links

- [Haiku OS](https://www.haiku-os.org/)
- [USB Video Class Specification](https://www.usb.org/document-library/video-class-v15-document-set)
