/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * See UVCQuirks.h for the design rationale.
 */
#include "UVCQuirks.h"

#include <stddef.h>


namespace {

// Vendor-wide quirks: applied to every device of the vendor, including ones
// recognised only by the generic UVC class fallback (so they need no explicit
// kSupportedDevices[] row). Use per-device entry quirks for anything that must
// be narrower than a whole vendor.
struct VendorQuirk {
	uint16	vendorID;
	uint32	quirks;
};

const VendorQuirk kVendorQuirks[] = {
	// Sonix bridge — Microdia- and Sonix-branded cameras (0x0c45) all carry
	// the 352-pixel internal stride. The runtime gate (srcSize > expectedSize)
	// keeps the compensation from firing on frames that are already correct,
	// so arming it vendor-wide is safe.
	{ 0x0c45, UVC_QUIRK_SONIX_STRIDE },
};

const size_t kVendorQuirkCount = sizeof(kVendorQuirks) / sizeof(kVendorQuirks[0]);

}	// namespace


uint32
ResolveWebcamQuirks(uint16 vendorID, uint16 productID, uint32 entryQuirks)
{
	(void)productID;	// reserved for future per-product refinement

	uint32 quirks = entryQuirks;
	for (size_t i = 0; i < kVendorQuirkCount; i++) {
		if (kVendorQuirks[i].vendorID == vendorID)
			quirks |= kVendorQuirks[i].quirks;
	}
	return quirks;
}
