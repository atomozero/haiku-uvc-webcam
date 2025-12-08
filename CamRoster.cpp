/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */

#include "CamRoster.h"

#include "AddOn.h"
#include "CamDevice.h"
#include "CamDebug.h"

#include <new>
#include <OS.h>

#undef B_WEBCAM_MKINTFUNC
#define B_WEBCAM_MKINTFUNC(modname) \
extern "C" status_t get_webcam_addon_##modname(WebCamMediaAddOn* webcam, CamDeviceAddon **addon);
#include "CamInternalAddons.h"
#undef B_WEBCAM_MKINTFUNC


CamRoster::CamRoster(WebCamMediaAddOn* _addon)
	: BUSBRoster(),
	fLocker("WebcamRosterLock"),
	fAddon(_addon)
{
	PRINT((CH "()" CT));
	LoadInternalAddons();
	LoadExternalAddons();
}


CamRoster::~CamRoster()
{
	// Clean up device parameter cache
	for (int32 i = 0; i < fDeviceCache.CountItems(); i++) {
		device_params_cache* cache =
			(device_params_cache*)fDeviceCache.ItemAt(i);
		delete cache;
	}
	fDeviceCache.MakeEmpty();
}


status_t
CamRoster::DeviceAdded(BUSBDevice* _device)
{
	PRINT((CH "()" CT));

	// SAFETY: Check if device pointer is valid
	if (_device == NULL) {
		fprintf(stderr, "CamRoster::DeviceAdded: NULL device pointer!\n");
		return B_ERROR;
	}

	// PHASE 3: Clean up old cache entries periodically
	CleanupOldCache();

	status_t err;
	for (int16 i = fCamerasAddons.CountItems() - 1; i >= 0; --i ) {
		CamDeviceAddon *ao = (CamDeviceAddon *)fCamerasAddons.ItemAt(i);

		// SAFETY: Check if addon pointer is valid
		if (ao == NULL) {
			fprintf(stderr, "CamRoster::DeviceAdded: NULL addon at index %d!\n", i);
			continue;
		}

		PRINT((CH ": checking %s for support..." CT, ao->BrandName()));
		err = ao->Sniff(_device);
		if (err < B_OK)
			continue;

		CamDevice *cam = ao->Instantiate(*this, _device);

		// SAFETY: Check if camera was instantiated successfully
		if (cam == NULL) {
			fprintf(stderr, "CamRoster::DeviceAdded: Instantiate() returned NULL for addon %s!\n",
				ao->BrandName());
			continue;
		}

		PRINT((CH ": found camera %s:%s!" CT, cam->BrandName(), cam->ModelName()));
		err = cam->InitCheck();
		if (err >= B_OK) {
			// PHASE 3: Check for cached params from previous connection
			device_identity identity = GetDeviceIdentity(_device);
			device_params_cache* cache = FindCachedParams(identity);
			if (cache != NULL) {
				PRINT((CH ": found cached params for reconnected device" CT));
				RestoreDeviceParams(cam, cache);
			}

			fCameras.AddItem(cam);
			fAddon->CameraAdded(cam);
			return B_OK;
		}
		PRINT((CH " error 0x%08" B_PRIx32 CT, err));

		// CLEANUP: Delete failed camera instance to prevent memory leak
		delete cam;
	}
	return B_ERROR;
}


void
CamRoster::DeviceRemoved(BUSBDevice* _device)
{
	PRINT((CH "()" CT));
	for (int32 i = 0; i < fCameras.CountItems(); ++i) {
		CamDevice* cam = (CamDevice *)fCameras.ItemAt(i);
		if (cam == NULL)
			continue;
		if (cam->Matches(_device)) {
			PRINT((CH ": camera %s:%s removed" CT, cam->BrandName(), cam->ModelName()));

			// PHASE 3: Cache device params before cleanup
			// (device pointer is still valid at this point)
			CacheDeviceParams(cam);

			fCameras.RemoveItem(i);

			// PHASE 2: Proper cleanup on device disconnect
			// First, stop all transfers and wait for threads to finish
			// This is now safe because Unplugged() waits for the data pump thread
			cam->Unplugged();

			// Notify Media Kit that the flavor is no longer available
			// This should trigger node cleanup
			fAddon->CameraRemoved(cam);

			// Now safe to delete: Unplugged() has stopped all threads
			// and cleared all USB pointers
			delete cam;
			return;
		}
	}
}


uint32
CamRoster::CountCameras()
{
	int32 count;
	fLocker.Lock();
	PRINT((CH "(): %" B_PRId32 " cameras" CT, fCameras.CountItems()));
	count = fCameras.CountItems();
	fLocker.Unlock();
	return count;
}


bool
CamRoster::Lock()
{
	PRINT((CH "()" CT));
	return fLocker.Lock();
}


void
CamRoster::Unlock()
{
	PRINT((CH "()" CT));
	fLocker.Unlock();
	return;
}


CamDevice*
CamRoster::CameraAt(int32 index)
{
	PRINT((CH "()" CT));
	return (CamDevice *)fCameras.ItemAt(index);
}


status_t
CamRoster::LoadInternalAddons()
{
	PRINT((CH "()" CT));
	CamDeviceAddon *addon;
	status_t err;

#undef B_WEBCAM_MKINTFUNC
#define B_WEBCAM_MKINTFUNC(modname) \
	err = get_webcam_addon_##modname(fAddon, &addon); \
	if (err >= B_OK) { \
		fCamerasAddons.AddItem(addon); \
		PRINT((CH ": registered %s addon" CT, addon->BrandName())); \
	}

#include "CamInternalAddons.h"
#undef B_WEBCAM_MKINTFUNC


	return B_OK;
}


status_t
CamRoster::LoadExternalAddons()
{
	PRINT((CH "()" CT));
	// TODO implement external add-ons
	return B_ERROR;
#if 0
	int32 index;
	int32 sclass;
	status_t err;
	CamDeviceAddon *addon;
	status_t (*get_webcam_addon_func)(WebCamMediaAddOn* webcam, CamDeviceAddon **addon);
	for (index = 0; get_nth_image_symbol(fAddon->ImageID(),
										index, NULL, NULL,
										&sclass,
										(void **)&get_webcam_addon_func) == B_OK; index++) {
		PRINT((CH ": got sym" CT));
//		if (sclass != B_SYMBOL_TYPE_TEXT)
//			continue;
		err = (*get_webcam_addon_func)(fAddon, &addon);
		PRINT((CH ": Loaded addon '%s' with error 0x%08" B_PRIx32 CT,
			(err > 0) ? NULL : addon->BrandName(),
			err));
	}
	return B_OK;
#endif
}


// PHASE 3: Device tracking implementation

device_identity
CamRoster::GetDeviceIdentity(BUSBDevice* device)
{
	device_identity id;
	id.vendor_id = device->VendorID();
	id.product_id = device->ProductID();

	const char* serial = device->SerialNumberString();
	if (serial != NULL && serial[0] != '\0')
		id.serial_number = serial;
	else
		id.serial_number = "";

	return id;
}


device_params_cache*
CamRoster::FindCachedParams(const device_identity& identity)
{
	for (int32 i = 0; i < fDeviceCache.CountItems(); i++) {
		device_params_cache* cache =
			(device_params_cache*)fDeviceCache.ItemAt(i);
		if (cache != NULL && cache->identity == identity)
			return cache;
	}
	return NULL;
}


void
CamRoster::CacheDeviceParams(CamDevice* cam)
{
	if (cam == NULL || cam->GetDevice() == NULL)
		return;

	device_identity identity = GetDeviceIdentity(cam->GetDevice());

	// Check if we already have cached params for this device
	device_params_cache* cache = FindCachedParams(identity);
	if (cache == NULL) {
		cache = new(std::nothrow) device_params_cache;
		if (cache == NULL)
			return;
		cache->identity = identity;
		fDeviceCache.AddItem(cache);
	}

	// Store current parameters
	BRect frame = cam->VideoFrame();
	cache->width = (uint32)(frame.Width() + 1);
	cache->height = (uint32)(frame.Height() + 1);
	cache->was_streaming = cam->TransferEnabled();
	cache->last_seen = system_time();

	// TODO: Store brightness, contrast, hue from sensor if available
	cache->brightness = 0.5f;
	cache->contrast = 0.5f;
	cache->hue = 0.0f;

	PRINT((CH ": cached params for device VID=%04x PID=%04x serial='%s' "
		"res=%dx%d streaming=%d" CT,
		identity.vendor_id, identity.product_id,
		identity.serial_number.String(),
		cache->width, cache->height, cache->was_streaming));
}


void
CamRoster::RestoreDeviceParams(CamDevice* cam, const device_params_cache* cache)
{
	if (cam == NULL || cache == NULL)
		return;

	PRINT((CH ": restoring params for device VID=%04x PID=%04x: "
		"res=%dx%d was_streaming=%d" CT,
		cache->identity.vendor_id, cache->identity.product_id,
		cache->width, cache->height, cache->was_streaming));

	// Restore video frame size
	if (cache->width > 0 && cache->height > 0) {
		uint32 width = cache->width;
		uint32 height = cache->height;
		cam->AcceptVideoFrame(width, height);
	}

	// Note: We don't auto-restart streaming here - that's handled by
	// the VideoProducer when it reconnects to the device
}


void
CamRoster::CleanupOldCache()
{
	// Remove cache entries older than 24 hours
	const bigtime_t kMaxCacheAge = 24LL * 60 * 60 * 1000000;	// 24 hours
	bigtime_t now = system_time();

	for (int32 i = fDeviceCache.CountItems() - 1; i >= 0; i--) {
		device_params_cache* cache =
			(device_params_cache*)fDeviceCache.ItemAt(i);
		if (cache != NULL && (now - cache->last_seen) > kMaxCacheAge) {
			PRINT((CH ": removing stale cache entry for VID=%04x PID=%04x" CT,
				cache->identity.vendor_id, cache->identity.product_id));
			fDeviceCache.RemoveItem(i);
			delete cache;
		}
	}
}



