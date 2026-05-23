# Bug Report: Isochronous USB Transfer Data Corruption on Haiku

## Summary

USB Video Class (UVC) webcam isochronous transfers produce corrupted frame data on Haiku. The assembled video frames show consistent horizontal band tearing (offset squares), where sections of the image are shifted horizontally relative to each other. This occurs with the Microdia 0c45:6409 webcam on an Intel EHCI controller.

## System Information

- **OS**: Haiku R1~beta5+development hrev59722 (x86_64)
- **CPU**: Intel Core i3 M 370 @ 2.40GHz
- **USB Controller**: Intel EHCI (from listusb: "HAIKU Inc." "EHCI RootHub")
- **Webcam**: Microdia 0c45:6409 "Webcam" (Sonix SN9C292A chipset)
- **USB Speed**: High-Speed (480 Mbps)
- **Video Format**: YUY2 uncompressed, 320x240 @ 30fps
- **Endpoint**: Isochronous IN, alternate 3, maxPacketSize=800 bytes

## Problem Description

Video frames assembled from isochronous USB transfers show horizontal tearing/offset bands. The image appears divided into 3-5 horizontal bands, each shifted horizontally relative to the others. The visual effect looks like the image was cut into strips and reassembled with wrong horizontal alignment.

### Visual Example

The tearing pattern is consistent and reproducible. Frame capture verified via ffmpeg (completely independent from driver code) shows the same artifacts, confirming the raw YUY2 data is already corrupted before any driver-side processing.

## Root Cause Analysis

### What we verified works correctly:
1. **Display pipeline**: Test pattern generated without USB data displays perfectly
2. **YUY2→RGB32 conversion**: Verified identical output to reference implementation (0 pixel differences)
3. **Deframer logic**: Bypass test (concatenating raw payloads without deframer) produces identical tearing
4. **Inter-transfer gap**: Measured at 11-35μs (< 1 USB microframe of 125μs), essentially zero data loss
5. **PTS consistency**: All packets within a frame carry the same Presentation Time Stamp value, confirming no cross-frame data mixing

### What points to the USB stack:
1. **Raw concatenated payloads already show tearing**: Before ANY driver processing, the payload bytes extracted from `fBuffer[i * slotSize]` with `actual_length` bytes per packet, when concatenated, produce a torn image
2. **Variable actual_length values**: Packets show actual_length of 608, 652, 800 bytes (not always the full 800-byte slot size). Some of these values appear incorrect
3. **Pattern matches slot-boundary artifacts**: The horizontal tearing bands occur at intervals corresponding to USB transfer boundaries (~32 packets × ~788 bytes payload ≈ 25,216 bytes ≈ 39 rows at 640 bytes/row)

### Hypothesis:
The EHCI isochronous transfer implementation may report incorrect `actual_length` values for some packets in the `usb_iso_packet_descriptor` array, or may place packet data at incorrect offsets within the transfer buffer. This causes the driver to read bytes from wrong positions, producing the horizontal tearing.

## How to Reproduce

1. Connect a Microdia 0c45:6409 USB webcam (or similar UVC camera)
2. Use the UVC webcam driver from https://github.com/atomozero/haiku-uvc-webcam
3. Open CodyCam to start video capture
4. Observe horizontal tearing/offset bands in the video
5. Capture a frame to file and view with ShowImage - tearing is present in static image too

## Transfer Configuration

```
IsochronousTransfer() parameters:
  buffer = 25600 bytes (32 × 800)
  numPacketDescriptors = 32
  request_length per packet = 800 bytes
  slotSize = bufferLen / numPacketDescriptors = 800

Packet data read pattern:
  for i in 0..31:
    offset = i * 800
    read actual_length bytes from buffer[offset]
    strip 12-byte UVC header, pass payload to deframer
```

## Diagnostic Data

### Packet actual_length distribution (typical transfer with data):
```
Packets 0-7:  actual_length = 800 (full)
Packets 8-16: actual_length = 12 (header only, no payload)
Packets 17-31: actual_length = 12 (header only)
```

### Inter-transfer timing:
```
Transfer duration: ~5300μs (32 × 125μs microframes + overhead)
Gap between transfers: 11-35μs (essentially zero)
Lost microframes per gap: 0
```

### PTS (Presentation Time Stamp) analysis:
```
200+ consecutive data packets: same PTS value (same camera frame)
First PTS change: after ~8.3 MB of payload (~54 frames)
Conclusion: no cross-frame data mixing at USB level
```

## Comparison with Linux

The Linux UVC driver (`uvc_video.c`) handles the same camera correctly:
- Uses 5 async URBs queued simultaneously (vs. 1 synchronous transfer on Haiku)
- Uses FID/EOF bits for frame boundary detection (not byte count)
- Processes packets via callback (`uvc_video_decode_isoc()`)

However, our testing showed that the inter-transfer gap on Haiku is negligible (< 1 microframe), so the synchronous single-transfer approach should not cause data loss. The corruption appears to originate within the USB stack's handling of isochronous packet data and `actual_length` reporting.

## Files Referenced

- USB bus manager: `src/add-ons/kernel/bus_managers/usb/`
- EHCI driver: `src/add-ons/kernel/busses/usb/ehci*`
- XHCI driver: `src/add-ons/kernel/busses/usb/xhci*`
- BUSBEndpoint::IsochronousTransfer: USB3.h API

## Requested Investigation

1. Verify that EHCI `IsochronousTransfer()` places packet data at correct buffer offsets (`i * slotSize`)
2. Verify that `actual_length` in `usb_iso_packet_descriptor` reflects the true number of bytes received per packet
3. Check if the EHCI iTD (isochronous Transfer Descriptor) handling correctly reports per-packet byte counts
4. Compare with the USB bulk transfer path which works correctly for other devices
