# Pre-built Haiku kernel binaries

This directory ships pre-built Haiku USB controller drivers with the
patches from `../patches/` applied. They are provided as a convenience
for users who want the bug-fixes that the webcam driver relies on but
cannot rebuild a Haiku kernel themselves.

| File | Component | Patches applied |
|---|---|---|
| `ehci.zip` | `ehci.kdrv` (EHCI USB 2.0 host controller) | 0002 (high-bandwidth iso) |
| `xhci.zip` | `xhci.kdrv` (xHCI USB 3.0+ host controller) | 0003 + 0004 (high-bandwidth, quirks, USB 3.1) |

Each zip contains a single ELF kernel add-on binary.

## Compatibility

- **Built against**: Haiku R1~beta5+development, hrev57937
- **Architecture**: x86_64
- **Other architectures or older/newer Haiku revisions**: rebuild from
  the patches in `../patches/` against your tree.

## Install

```sh
unzip ehci.zip
sudo cp ehci /system/add-ons/kernel/busses/usb/ehci
# reboot, or unload/reload usb_stack
```

The EHCI panic fix from `../patches/ehci-isochronous-host-error-panic-fix.patch`
is **not** included in these binaries yet — apply it separately on top
of the high-bandwidth patch if you also want that protection.
