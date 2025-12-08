/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */
#ifndef _CAM_ROSTER_H
#define _CAM_ROSTER_H

#include <image.h>
#include <List.h>
#include <Locker.h>
#include <String.h>

#include "CamDevice.h"

class WebCamMediaAddOn;
class CamDeviceAddon;


// PHASE 3: Device identification for reconnection support
struct device_identity {
	uint16		vendor_id;
	uint16		product_id;
	BString		serial_number;

	bool operator==(const device_identity& other) const {
		return vendor_id == other.vendor_id
			&& product_id == other.product_id
			&& serial_number == other.serial_number;
	}
};

// Cached device parameters for restoration on reconnect
struct device_params_cache {
	device_identity	identity;
	uint32			width;
	uint32			height;
	float			brightness;
	float			contrast;
	float			hue;
	bool			was_streaming;
	bigtime_t		last_seen;		// Timestamp when device was disconnected
};

class CamRoster : public BUSBRoster {
public:
						CamRoster(WebCamMediaAddOn* _addon);
	virtual				~CamRoster();
	virtual status_t	DeviceAdded(BUSBDevice* _device);
	virtual void		DeviceRemoved(BUSBDevice* _device);

			uint32		CountCameras();
			bool		Lock();
			void		Unlock();
	// those must be called with Lock()
			CamDevice*	CameraAt(int32 index);



private:
			status_t	LoadInternalAddons();
			status_t	LoadExternalAddons();

	// PHASE 3: Device tracking for reconnection
			device_identity	GetDeviceIdentity(BUSBDevice* device);
			device_params_cache* FindCachedParams(
								const device_identity& identity);
			void		CacheDeviceParams(CamDevice* cam);
			void		RestoreDeviceParams(CamDevice* cam,
								const device_params_cache* cache);
			void		CleanupOldCache();

	BLocker				fLocker;
	WebCamMediaAddOn*	fAddon;
	BList				fCamerasAddons;
	BList				fCameras;
	BList				fDeviceCache;	// List of device_params_cache*
};

#endif
