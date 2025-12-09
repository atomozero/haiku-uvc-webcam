# Haiku OS Patches

This directory contains patches for bugs and missing features found in Haiku OS during the development of the UVC webcam driver.

## Patches Overview

| Patch | Component | Description |
|-------|-----------|-------------|
| 0001 | USBKit | Fix double-free in SetAlternate |
| 0002 | EHCI | High-bandwidth isochronous support |
| 0003 | XHCI | High-bandwidth isochronous support |

---

## 0001-USBKit-Fix-double-free-in-SetAlternate.patch

**Component:** `src/kits/device/USBInterface.cpp` (libdevice.so / USBKit)

**Description:** Fixes memory corruption bugs in `BUSBInterface::SetAlternate()` that cause crashes when switching between USB interface alternates with different endpoint counts.

**Bug Details:**
1. `_UpdateDescriptorAndEndpoints()` used the NEW descriptor's `num_endpoints` to delete the OLD endpoint array
2. `SetAlternate()` didn't update `fAlternate` before fetching the new descriptor
3. Constructor didn't initialize `fDescriptor`, causing garbage reads on first use

**Impact:** Critical for USB Video Class (UVC) drivers where alternate 0 has 0 endpoints (zero-bandwidth) and alternate N has 1+ endpoints for streaming.

**How to Apply:**
```bash
cd /path/to/haiku/source
git apply /path/to/0001-USBKit-Fix-double-free-in-SetAlternate.patch
```

---

## 0002-EHCI-High-bandwidth-isochronous-support.patch

**Component:** `src/add-ons/kernel/busses/usb/ehci.cpp` (EHCI USB driver)

**Description:** Implements proper support for USB 2.0 high-bandwidth isochronous endpoints which can transfer up to 3 packets per microframe (mult=3).

**Changes:**
- Parse wMaxPacketSize correctly (bits 10:0 = base size, bits 12:11 = mult)
- Program iTD transaction slots correctly for high-bandwidth
- Add 64-bit addressing support detection
- Fix buffer_phy[2] Multi field programming
- Implement CancelQueuedIsochronousTransfers() properly
- Fix FinishIsochronousTransfers() safety issues
- Fix ReadIsochronousDescriptorChain() for high-bandwidth
- Implement LightReset() for controller recovery
- Implement NotifyPipeChange() for interrupt pipes
- Optimize InterruptPollThread() to reduce CPU when idle
- Add safety checks in UnlinkITDescriptors()

**Impact:** Enables UVC webcams to stream at 720p/1080p resolutions which require high-bandwidth isochronous endpoints.

**How to Apply:**
```bash
cd /path/to/haiku/source
git apply /path/to/0002-EHCI-High-bandwidth-isochronous-support.patch
jam -q @alpha-raw ehci
```

---

## 0003-XHCI-High-bandwidth-isochronous-support.patch

**Component:** `src/add-ons/kernel/busses/usb/xhci.cpp` (XHCI USB driver)

**Description:** Implements proper support for USB 2.0 high-bandwidth isochronous endpoints on xHCI controllers and includes various reliability improvements.

**Changes:**
- Enable MSI-X interrupt support (previously disabled)
- Fix high-bandwidth endpoint configuration in ConfigureEndpoint()
- Fix isochronous TRB programming (TBC, TLBPC calculation)
- Improve debug transfer handling with timeout protection
- Improve error handling in Interrupt() with recovery attempts
- Fix spurious error messages in HandleTransferComplete()

**Impact:** Enables UVC webcams to stream at 720p/1080p resolutions on systems with xHCI controllers.

**How to Apply:**
```bash
cd /path/to/haiku/source
git apply /path/to/0003-XHCI-High-bandwidth-isochronous-support.patch
jam -q @alpha-raw xhci
```

---

## Reference Files

- `USBInterface.cpp.fixed` - Complete patched USBKit source file for reference

## Testing

After applying patches, test with:
1. UVC webcam at multiple resolutions (VGA, 720p, 1080p)
2. Start/stop streaming multiple times
3. Hot-plug/unplug webcam during streaming
4. Monitor syslog for errors: `tail -f /var/log/syslog | grep -i usb`

## Status

| Patch | Submitted | Tested | Haiku Version |
|-------|-----------|--------|---------------|
| 0001  | No        | Yes    | R1/beta5 (hrev57937) |
| 0002  | No        | Yes    | R1/beta5 (hrev57937) |
| 0003  | No        | Yes    | R1/beta5 (hrev57937) |

Submit patches to: https://dev.haiku-os.org/
