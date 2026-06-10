# Bug Report: Kernel Panic in EHCI Isochronous Finish Thread

## Summary

Kernel panic (General Protection Exception) occurs in the EHCI USB driver's isochronous transfer completion thread after prolonged USB webcam streaming. The panic happens in `EHCI::FinishIsochronousTransfers()` when processing completed isochronous transfer descriptors (iTDs).

## System Information

- **OS**: Haiku R1~beta5+development hrev59722 (x86_64)
- **CPU**: Intel Core i3 M370 @ 2.40GHz (4 cores)
- **USB Controller**: Intel EHCI (Integrated Rate Matching Hub)
- **Laptop**: Sony VAIO VPCEB3K1E

## Device Triggering the Issue

- **USB Webcam**: AUKEY PC-LM1E (VID:1bcf PID:0001)
- **Format**: MJPEG 1280x720 @ 30fps
- **Endpoint**: Isochronous IN, alternate 6, maxPacketSize=944 bytes
- **Transfer**: 32 packets per isochronous transfer

## Steps to Reproduce

1. Connect an AUKEY PC-LM1E USB webcam (or similar UVC MJPEG webcam)
2. Open a video capture application (CodyCam or BubiCam)
3. Start streaming video at 1280x720 MJPEG
4. Wait approximately 2-3 minutes of continuous streaming
5. System panics with General Protection Exception

## Sequence of Events Before Panic

The syslog shows this progression before the crash:

```
# Normal streaming for ~3000+ frames
UVCDeframer stats: completed=3865 incomplete=0 (0.0%) FID=3865 overflow=0
USB Stats: success=1056471 errors=3625 loss=0.3% rate=5889 pkt/s

# EHCI host system error triggers
KERN: usb error ehci 1: host system error!

# All subsequent USB operations fail
KERN: usb error control pipe 52: timeout waiting for queued request to complete
KERN: usb error hub 51: error updating port status
# (repeats many times)

# Eventually: kernel panic
```

## Panic Details

```
PANIC: Unexpected exception "General Protection Exception" occurred in kernel mode! Error code: 0x0

Thread 24 "ehci isochronous finish thread" running on CPU 2

Stack trace:
Frame 7: EHCI::FinishIsochronousTransfers[clone .localias] () + 0xfc
Frame 8: EHCI::FinishIsochronousThread(void*) + 0xb9
Frame 9: common_thread_entry(void*) + 0x37
Frame 10: 242:ehci isochronous finish thread

Registers:
  rip 0xffffffff819626cc
  rsp 0xffffffff81dc3f40
  rflags 0x10282
  vector: 0xd, error code: 0x0
  rax 0xbabccc0c (likely corrupted pointer)
  rdx 0xe504a0ae40d05a52 (clearly invalid)
```

## Analysis

The panic occurs because:

1. The EHCI host controller encounters a "host system error" (bit 4 of USBSTS register), which indicates a serious bus error during DMA operations
2. After the host system error, the isochronous finish thread continues trying to process iTD descriptors
3. The iTD memory may be corrupted or invalidated by the host error, causing the thread to dereference invalid pointers
4. `rax=0xbabccc0c` and `rdx=0xe504a0ae40d05a52` suggest corrupted data structures being accessed

## Suggested Fix

In `EHCI::FinishIsochronousTransfers()` (ehci.cpp), the code should check for host system error status before processing iTD descriptors:

```cpp
// After reading frame index, check if controller is still functional
uint32 status = ReadOpReg(EHCI_USBSTS);
if (status & EHCI_USBSTS_HOSTSYSERR) {
    // Host system error - do not process descriptors, they may be corrupted
    TRACE_ERROR("EHCI host system error detected in isochronous finisher, aborting\n");
    // Cancel all pending isochronous transfers gracefully
    break;
}
```

Additionally, pointer validation before accessing iTD fields would prevent the GPF:

```cpp
ehci_itd *itd = fItdEntries[currentFrame];
if (itd == NULL || !IS_KERNEL_ADDRESS(itd)) {
    TRACE_ERROR("Invalid iTD pointer at frame %d\n", currentFrame);
    continue;
}
```

## Files Involved

- `src/add-ons/kernel/busses/usb/ehci.cpp` - `FinishIsochronousTransfers()` (~line 2120)
- `src/add-ons/kernel/busses/usb/ehci.h` - isochronous_transfer_data structure

## Workaround

No user-level workaround is available. The panic occurs after prolonged isochronous streaming and requires a system reboot. The issue is intermittent and may be related to thermal throttling or USB bandwidth pressure on the Intel EHCI controller.

## Additional Notes

- The webcam streams correctly for several thousand frames before the error occurs
- USB packet statistics show very low loss rate (0.3%) before the crash
- The issue was observed with an AUKEY PC-LM1E webcam but may occur with any device using isochronous transfers on EHCI
- A Microdia 0c45:6409 webcam on the same system also triggered `usb error ehci 1: host system error!` during extended streaming, suggesting this is controller-specific, not device-specific
