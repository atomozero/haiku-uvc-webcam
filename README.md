# UVC Webcam Driver for Haiku

[![tests](https://github.com/atomozero/haiku-uvc-webcam/actions/workflows/tests.yml/badge.svg)](https://github.com/atomozero/haiku-uvc-webcam/actions/workflows/tests.yml)

Native USB Video Class driver for Haiku OS. Plug in a UVC webcam and use it
with MediaPlayer, Cortex, BubiCam, or any media node consumer. Supports MJPEG
and uncompressed formats up to 1080p, audio capture from built-in microphones,
and the full UVC control surface (brightness, focus, exposure, white balance,
zoom, etc.).

![MJPEG streaming at 1280x720 with BubiCam](img/screenshot03.png)

If this driver saves you a Windows reboot, consider supporting development:
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)


## Features

* MJPEG (HD recommended), YUY2/YUYV, UYVY, NV12, NV21, YV12, I420, GREY
* Multi-stream cameras exposed as separate media nodes (RealSense, stereo)
* Built-in microphone via USB Audio Class 1.0
* Full UVC controls: brightness, contrast, saturation, focus, zoom, exposure,
  white balance, gain, backlight compensation, anti-flicker
* Multi-camera support with unique device naming and hot-plug
* Automatic resolution fallback when bandwidth is insufficient
* Extension Unit detection (Sonix, Microsoft H264, Logitech, Realtek)
* No external dependencies beyond Haiku system libraries and libturbojpeg

## Robustness

USB descriptors and Probe/Commit responses are device-controlled data — a buggy
or hostile camera can lie about lengths and sizes. This driver treats them as
untrusted:

* **Bounds-safe descriptor parsing** — frame, format and extension-unit
  descriptors are validated by pure functions that read only within the bytes
  actually present, never past a lying `bLength`. They are unit-tested against a
  corpus of real captures (AUKEY, Microdia) and continuously **fuzzed** with
  guard pages (5 validators × 500k inputs, zero out-of-bounds reads).
* **Probe/Commit sanitisation** — implausible negotiated sizes (a Microdia was
  seen reporting a ~2 GB max frame) are clamped before any bandwidth math or
  allocation uses them.
* **Wedged-device recovery** — if a camera stalls, the pump thread is abandoned
  rather than force-killed, the node is marked stalled and refuses restarts
  until re-enumeration, so a broken camera never hangs the media server.
* **Data-driven quirk registry** — per-device workarounds (e.g. the Sonix line
  stride) live in a table covered by tests, not scattered VID/PID branches.

## Quick start

```
make
make install
kill $(pidof media_server)
```

Open MediaPlayer, Cortex, or BubiCam — the webcam appears as a video node.

![Webcam streaming in Cortex](img/screenshot01.png)

![Camera controls panel](img/screenshot02.png)

## Supported devices

| Device | VID:PID | Status |
|---|---|---|
| AUKEY PC-LM1E | 1BCF:0001 | Working — MJPEG up to 1280x720, audio OK |
| Realtek USB Camera | 0BDA:5843 | Working — MJPEG + YUY2 |
| Chicony CNF8111 | 04F2:B119 | Working |
| SuYin HP Truevision | varies | Working with kernel patch |
| Microdia Motion Eye | 0C45:6409 | YUY2 tearing — firmware limitation |
| Generic UVC webcams | any | Auto-detected |

MJPEG webcams are recommended. YUY2-only cameras can show tearing on USB 2.0
due to bandwidth constraints.

## Build

Requires Haiku R1/beta5 or nightly, GCC, and `libturbojpeg`:

```
pkgman install libturbojpeg_devel
```

Then:

```
make              # build the media addon
make install      # install to ~/config/non-packaged/add-ons/media/
make test         # build and run the unit + fuzz tests (no camera needed)
make hpkg         # build a Haiku package (aukey_webcam_v4-*.hpkg)
make dist-zip     # build a distributable archive
```

`make test` builds only the pure parsing/quirk modules, so it also runs on a
plain Linux runner — that is what the CI badge above tracks.

### Install from a package

Grab the `.hpkg` from the [releases](https://github.com/atomozero/haiku-uvc-webcam/releases)
(or build it with `make hpkg`) and drop it into `~/config/packages/`:

```
cp aukey_webcam_v4-*.hpkg ~/config/packages/
```

The add-on activates immediately; restart the media server (or reboot) to load it.

## Troubleshooting

**Camera not detected**
```
listusb | grep -i video
tail -f /var/log/syslog | grep -i uvc
```

**Stuttering or tearing**
Switch to MJPEG format if available, or lower the resolution. Avoid USB 2.0
hubs for HD cameras.

**No audio**
The webcam node is video-only; the microphone shows up as a separate
"[Camera Name] Audio" node in Media preferences.

**Kernel panic on resolution change**
Affects older Haiku versions only (Haiku bug in `BUSBInterface::SetAlternate()`).
Update to a recent nightly, or apply the patches in `patches/`.

## Environment variables

| Variable | Effect |
|---|---|
| `WEBCAM_DEBUG=verbose` | Verbose probe and parameter logging |
| `WEBCAM_FORCE_HIGH_BANDWIDTH=1` | Allow `mult>1` isochronous endpoints (needs patched kernel) |
| `WEBCAM_DISABLE_HIGH_BANDWIDTH=1` | Force `mult=1` only (default) |
| `WEBCAM_MJPEG_QUALITY=N` | Pin MJPEG `wCompQuality` (0..10000) |
| `WEBCAM_PROBE_DELAY=N` | Extra ms before probe/commit (default 100) |

Set them in the shell that launches `media_server`.

## Diagnostic tools

The `tools/` directory contains standalone utilities for debugging webcam
issues — Sonix register probing via UVC Extension Units, EHCI iTD offset
simulation, YUV/RGB frame conversion, and tearing analysis. Build any tool
with `g++ -O2 -o NAME NAME.cpp -lbe -ldevice`.

## Be careful

> **Developer's Note**: This software may contain traces of peanuts and LLM.
> It has been developed with passion for the Haiku platform.

## Support

If you find this project useful, you can buy me a coffee:
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)

## Credits

Original driver by Ithamar Adema, Jérôme Duval, Gabriel Hartmann.
UVC improvements and optimizations 2024-2026.
