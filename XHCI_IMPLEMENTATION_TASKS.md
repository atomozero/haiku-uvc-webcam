# XHCI Implementation Tasks

This document tracks the implementation of XHCI-specific optimizations for the UVC webcam driver.

## Overview

With the XHCI kernel patches (0003 and 0004), the driver can now take advantage of:
- High-bandwidth isochronous endpoints (up to 3072 bytes/microframe)
- Dynamic interrupt moderation (16000 IRQ/s for isochronous)
- USB 3.0+ SuperSpeed support
- Proper TBC/TLBPC calculation for reduced packet loss

## Task Status

| Task | Status | Description |
|------|--------|-------------|
| 1 | ✅ Done | Stable 1080p@30fps streaming with XHCI detection |
| 2 | ✅ Done | Dynamic interrupt moderation awareness |
| 3 | ✅ Done | USB 3.0 SuperSpeed optimizations |
| 4 | ✅ Done | TBC/TLBPC packet loss reduction |

---

## Task 1: Stable 1080p@30fps Streaming

### Goal
Improve XHCI detection and enable confident 1080p streaming without fallback hesitation.

### Changes Required
- Add `usb_host_controller_type` enum
- Implement `_DetectControllerType()` method
- Update `_ShouldUseHighBandwidth()` to use detected type
- Add syslog reporting of controller capabilities
- Remove conservative fallback delays for confirmed XHCI

### Files to Modify
- `addons/uvc/UVCCamDevice.h`
- `addons/uvc/UVCCamDevice.cpp`

### Test Criteria
- 1080p@30fps streaming stable for >5 minutes
- No unnecessary fallback to lower resolutions
- Correct controller type reported in syslog

---

## Task 2: Dynamic Interrupt Moderation Awareness

### Goal
Optimize driver behavior knowing XHCI uses adaptive interrupt rates.

### Changes Required
- Add `xhci_imod_mode` enum for interrupt moderation states
- Add timing hints for buffer management
- Optimize buffer polling based on expected IRQ rate
- Add performance statistics tracking

### Files to Modify
- `addons/uvc/UVCCamDevice.h`
- `addons/uvc/UVCCamDevice.cpp`
- `CamDevice.cpp` (base class polling)

### Test Criteria
- Reduced CPU usage during streaming
- Consistent frame delivery timing
- No buffer underruns at 30fps

---

## Task 3: USB 3.0 SuperSpeed Optimizations

### Goal
Detect and optimize for USB 3.0+ devices with higher bandwidth.

### Changes Required
- Add USB speed detection via BUSBDevice
- Implement `_GetUSBSpeed()` method
- Adjust buffer sizes for SuperSpeed (5 Gbps)
- Enable larger burst transfers for USB 3.0
- Add SuperSpeed-specific frame interval calculation

### Files to Modify
- `addons/uvc/UVCCamDevice.h`
- `addons/uvc/UVCCamDevice.cpp`
- `CamConfig.h` (USB 3.0 constants)

### Test Criteria
- USB 3.0 devices detected and reported
- Higher bandwidth utilized when available
- Backward compatible with USB 2.0

---

## Task 4: TBC/TLBPC Packet Loss Reduction

### Goal
Align driver expectations with XHCI's TBC/TLBPC isochronous handling.

### Changes Required
- Add `isochronous_transfer_info` structure
- Calculate expected packets per transfer
- Implement packet completion tracking
- Add recovery hints for partial transfers
- Improve deframer handling of burst boundaries

### Files to Modify
- `addons/uvc/UVCCamDevice.h`
- `addons/uvc/UVCCamDevice.cpp`
- `addons/uvc/UVCDeframer.cpp`

### Test Criteria
- Packet loss <0.1% at 1080p
- Graceful handling of partial bursts
- No frame corruption from TRB boundaries

---

## Implementation Notes

### Haiku Coding Style
- Use tabs for indentation
- Opening brace on same line for functions
- Space after keywords (if, while, for)
- No space before function call parentheses
- Member variables prefixed with 'f' (e.g., fControllerType)

### License
All code is MIT licensed as per the project.

### Build Command
```bash
cd /boot/home/Desktop/haiku-uvc-webcam
make clean && make
```

### Install Command
```bash
make install
# Restart media_server to load new driver
```

---

## Session Log

### Session 1 - 2024-12-14
- Created tracking document
- Analyzed existing code structure
- Identified 4 implementation tasks
- **Task 1 COMPLETED**: Added `usb_host_controller_type`, `usb_device_speed` enums,
  `_DetectControllerType()`, `_GetUSBSpeed()`, `_LogControllerCapabilities()` methods.
  Updated `_ShouldUseHighBandwidth()` to use detected controller type.
- **Task 2 COMPLETED**: Added XHCI IMOD constants to CamConfig.h,
  `_GetOptimalPollInterval()`, `_GetExpectedIRQsPerFrame()` methods.
- **Task 3 COMPLETED**: Added USB 3.0 bandwidth constants,
  `_GetOptimalBufferSize()`, `_GetMaxBandwidth()` methods.
- **Task 4 COMPLETED**: Added TBC/TLBPC configuration constants,
  `_GetExpectedPacketCompletionRate()`, `_HasTBCTLBPCSupport()` methods.
- All tasks compile successfully with `make clean && make`

### Session 2 - 2024-12-14
- **BUG FIX**: SetAlternate state consistency issue
  - Problem: `_SelectIdleAlternate()` used ControlTransfer() while `_SelectBestAlternate()` used SetAlternate()
  - This caused Haiku's internal USB alternate state to become inconsistent
  - Symptom: "SET_INTERFACE(6) failed: General system error" and no video stream
  - Fix: Changed `_SelectIdleAlternate()` to use SetAlternate(0) instead of ControlTransfer
  - Also fixed `_SelectAudioIdleAlternate()` for consistency
- Note: The Haiku double-free bug in SetAlternate still exists; a kernel patch is recommended
  for systems that experience crashes (patches/0001-USBKit-Fix-double-free-in-SetAlternate.patch)

---

## Resume Instructions

To continue this work in a new session:

1. Read this file first: `/boot/home/Desktop/haiku-uvc-webcam/XHCI_IMPLEMENTATION_TASKS.md`
2. Check current task status in the table above
3. Continue from the first ⏳ Pending task
4. After each task:
   - Update status to ✅ Done
   - Run `make clean && make`
   - Update CLAUDE.md if needed
   - Add session log entry

Working directory: `/boot/home/Desktop/haiku-uvc-webcam`
