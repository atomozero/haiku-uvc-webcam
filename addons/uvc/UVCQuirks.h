/*
 * Copyright 2026, Haiku UVC Webcam contributors.
 * Distributed under the terms of the MIT License.
 *
 * Data-driven device quirks. Vendor/device-specific workarounds are resolved
 * from tables here instead of being scattered as ad-hoc VID/PID checks through
 * the streaming code, so adding a quirky camera is a data change (and stays
 * covered by tests/test_quirks.cpp) rather than a new code branch.
 */
#ifndef _UVC_QUIRKS_H
#define _UVC_QUIRKS_H


#include <SupportDefs.h>


// Behaviour quirks, combined as a bitmask.
enum uvc_quirk_flags {
	UVC_QUIRK_NONE			= 0,

	// Sonix-bridge cameras keep a 352-pixel internal line stride; the YUY2
	// converter compensates at runtime when the delivered frame is larger than
	// the nominal size (see UVCCamDevice::_ConvertYUY2toRGB32).
	UVC_QUIRK_SONIX_STRIDE	= 1 << 0,

	// (room to grow without touching streaming code, e.g.
	//  UVC_QUIRK_FORCE_YUY2, UVC_QUIRK_NO_HIGH_BANDWIDTH, UVC_QUIRK_FIX_BW ...)
};


// Resolve the effective quirk mask for a device.
//
// `entryQuirks` is the per-device quirk mask from the matched
// kSupportedDevices[] entry (0 when the device matched only the generic UVC
// class fallback, or has no quirks). Vendor-wide quirks are OR-ed in so a
// camera built on a quirky bridge still gets the workaround even when it was
// recognised only by the generic class entry (i.e. it needs no explicit row).
uint32 ResolveWebcamQuirks(uint16 vendorID, uint16 productID,
	uint32 entryQuirks);


#endif	// _UVC_QUIRKS_H
