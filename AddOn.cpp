/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */

#include <support/Autolock.h>
#include <media/MediaFormats.h>
#include <Notification.h>
#include <String.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "AddOn.h"
#include "Producer.h"
#include "AudioProducer.h"
#include "CamRoster.h"
#include "CamDebug.h"
#include "CamDevice.h"
#include "addons/uvc/UVCCamDevice.h"


WebCamMediaAddOn::WebCamMediaAddOn(image_id imid)
	: BMediaAddOn(imid),
	fInitStatus(B_NO_INIT),
	fRoster(NULL)
{
	// PHASE 10: Initialize debug level from environment
	InitWebcamDebugLevel();

	PRINT((CH "()" CT));
	fInternalIDCounter = 0;

	/* Video format */
	fMediaFormat.type = B_MEDIA_RAW_VIDEO;
	fMediaFormat.u.raw_video = media_raw_video_format::wildcard;
	fMediaFormat.u.raw_video.interlace = 1;
	fMediaFormat.u.raw_video.display.format = B_RGB32;
	/* CRITICAL: Set ALL video format fields so Media Kit properly recognizes
	 * this as a valid video format. Missing fields may cause the node to be
	 * classified incorrectly (e.g., as audio instead of video).
	 */
	fMediaFormat.u.raw_video.display.line_width = 320;
	fMediaFormat.u.raw_video.display.line_count = 240;
	fMediaFormat.u.raw_video.display.bytes_per_row = 320 * 4;  // B_RGB32 = 4 bytes/pixel
	fMediaFormat.u.raw_video.field_rate = 30.0f;
	fMediaFormat.u.raw_video.first_active = 0;
	fMediaFormat.u.raw_video.last_active = 239;  // line_count - 1
	fMediaFormat.u.raw_video.orientation = B_VIDEO_TOP_LEFT_RIGHT;
	fMediaFormat.u.raw_video.pixel_width_aspect = 1;
	fMediaFormat.u.raw_video.pixel_height_aspect = 1;
	FillDefaultFlavorInfo(&fDefaultFlavorInfo);

	/* Audio format */
	fAudioMediaFormat.type = B_MEDIA_RAW_AUDIO;
	fAudioMediaFormat.u.raw_audio = media_raw_audio_format::wildcard;
	fAudioMediaFormat.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	fAudioMediaFormat.u.raw_audio.byte_order = B_MEDIA_LITTLE_ENDIAN;
	fAudioMediaFormat.u.raw_audio.channel_count = 2;
	fAudioMediaFormat.u.raw_audio.frame_rate = 48000.0f;
	fAudioMediaFormat.u.raw_audio.buffer_size = 4096;

	/* Audio flavor info */
	fDefaultAudioFlavorInfo.name = (char *)"USB Web Camera Audio";
	fDefaultAudioFlavorInfo.info = (char *)"USB Web Camera Microphone";
	fDefaultAudioFlavorInfo.kinds = B_BUFFER_PRODUCER | B_CONTROLLABLE | B_PHYSICAL_INPUT;
	fDefaultAudioFlavorInfo.flavor_flags = 0;
	fDefaultAudioFlavorInfo.internal_id = 0;  // Set per-device
	fDefaultAudioFlavorInfo.possible_count = 0;
	fDefaultAudioFlavorInfo.in_format_count = 0;
	fDefaultAudioFlavorInfo.in_format_flags = 0;
	fDefaultAudioFlavorInfo.in_formats = NULL;
	fDefaultAudioFlavorInfo.out_format_count = 1;
	fDefaultAudioFlavorInfo.out_format_flags = 0;
	fDefaultAudioFlavorInfo.out_formats = &fAudioMediaFormat;

	fRoster = new CamRoster(this);
	fRoster->Start();
	fInitStatus = B_OK;
}


WebCamMediaAddOn::~WebCamMediaAddOn()
{
	fRoster->Stop();
	delete fRoster;
}


status_t
WebCamMediaAddOn::InitCheck(const char **out_failure_text)
{
	if (fInitStatus < B_OK) {
		*out_failure_text = "No cameras attached";
		return fInitStatus;
	}

	return B_OK;
}


int32
WebCamMediaAddOn::CountFlavors()
{
	PRINT((CH "()" CT));
	if (!fRoster) {
		return B_NO_INIT;
	}
	if (fInitStatus < B_OK) {
		return fInitStatus;
	}

	int32 cameraCount = fRoster->CountCameras();
	int32 videoFlavors = 0;
	int32 audioFlavors = 0;

	fRoster->Lock();
	for (int32 i = 0; i < cameraCount; i++) {
		CamDevice* cam = fRoster->CameraAt(i);
		if (cam == NULL)
			continue;
		// P3 fase B: each VideoStreaming interface gets its own flavor
		// (depth/IR/secondary color on RealSense, SN9C292 etc.).
		UVCCamDevice* uvcCam = dynamic_cast<UVCCamDevice*>(cam);
		int32 streams = (uvcCam != NULL) ? uvcCam->NumStreams() : 1;
		if (streams < 1)
			streams = 1;
		videoFlavors += streams;
		if (uvcCam != NULL && uvcCam->HasAudio())
			audioFlavors++;
	}
	fRoster->Unlock();

	PRINT((CH ": %d video + %d audio flavors" CT, videoFlavors, audioFlavors));
	return videoFlavors + audioFlavors;
}


/*
 * The pointer to the flavor received only needs to be valid between
 * successive calls to BMediaAddOn::GetFlavorAt().
 * Flavor layout: first all video flavors, then audio flavors for cameras with mics
 */
status_t
WebCamMediaAddOn::GetFlavorAt(int32 n, const flavor_info **out_info)
{
	PRINT((CH "(%d, ) roster %p is %" B_PRIx32 CT, n, fRoster, fInitStatus));

	if (!fRoster)
		return B_NO_INIT;
	if (fInitStatus < B_OK)
		return fInitStatus;

	if (n < 0)
		return B_BAD_INDEX;

	fRoster->Lock();
	int32 cameraCount = fRoster->CountCameras();

	// P3 fase B internal_id encoding (32 bits):
	//   bit 31      audio (1) / video (0)
	//   bits 24-30  stream index for video flavors (0..127)
	//   bits 0-23   camera internal id (FlavorInfo->internal_id)
	// Stream 0 keeps the camera's original internal_id unchanged so the
	// existing InstantiateNodeFor lookup stays backward compatible.

	// First: walk video flavors (one per VS interface per camera).
	int32 videoWalker = 0;
	for (int32 i = 0; i < cameraCount; i++) {
		CamDevice* cam = fRoster->CameraAt(i);
		if (cam == NULL || cam->FlavorInfo() == NULL)
			continue;
		UVCCamDevice* uvcCam = dynamic_cast<UVCCamDevice*>(cam);
		int32 streams = (uvcCam != NULL) ? uvcCam->NumStreams() : 1;
		if (streams < 1)
			streams = 1;
		if (n < videoWalker + streams) {
			int32 streamIdx = n - videoWalker;
			if (streamIdx == 0) {
				// Primary stream: hand back the unmodified per-camera flavor
				// (backward compatible with anything that cached the id).
				*out_info = cam->FlavorInfo();
			} else {
				// Secondary stream: synthesise into the scratch buffer.
				// flavor_info has a private operator= so we memcpy by hand.
				memcpy(&fStreamFlavorInfo, cam->FlavorInfo(),
					sizeof(fStreamFlavorInfo));
				fStreamFlavorInfo.internal_id =
					(cam->FlavorInfo()->internal_id & 0x00FFFFFF)
					| (streamIdx << 24);
				const char* baseName = cam->FlavorInfo()->name
					? cam->FlavorInfo()->name : "USB Web Camera";
				BString label;
				if (uvcCam != NULL) {
					uvcCam->GetStreamName(streamIdx, &label);
					snprintf(fStreamFlavorName,
						sizeof(fStreamFlavorName),
						"%s — %s", baseName, label.String());
				} else {
					snprintf(fStreamFlavorName,
						sizeof(fStreamFlavorName),
						"%s — Stream %d", baseName, (int)streamIdx);
				}
				fStreamFlavorInfo.name = fStreamFlavorName;
				*out_info = &fStreamFlavorInfo;
			}
			fRoster->Unlock();
			PRINT((CH ": returning VIDEO flavor %d (cam=%d stream=%d "
				"internal_id=0x%x)" CT, n, (int)i, (int)streamIdx,
				(*out_info)->internal_id));
			return B_OK;
		}
		videoWalker += streams;
	}

	// Then: audio flavors (one per audio-capable camera).
	int32 audioIndex = n - videoWalker;
	if (audioIndex < 0) {
		fRoster->Unlock();
		return B_BAD_INDEX;
	}
	int32 audioFound = 0;
	for (int32 i = 0; i < cameraCount; i++) {
		CamDevice* cam = fRoster->CameraAt(i);
		if (cam == NULL || cam->FlavorInfo() == NULL)
			continue;
		UVCCamDevice* uvcCam = dynamic_cast<UVCCamDevice*>(cam);
		if (uvcCam == NULL || !uvcCam->HasAudio())
			continue;
		if (audioFound == audioIndex) {
			fDefaultAudioFlavorInfo.internal_id =
				cam->FlavorInfo()->internal_id | 0x80000000;
			snprintf(fAudioFlavorName, sizeof(fAudioFlavorName),
				"%s Audio", cam->FlavorInfo()->name);
			fDefaultAudioFlavorInfo.name = fAudioFlavorName;
			*out_info = &fDefaultAudioFlavorInfo;
			fRoster->Unlock();
			return B_OK;
		}
		audioFound++;
	}

	fRoster->Unlock();
	return B_BAD_INDEX;
}


BMediaNode *
WebCamMediaAddOn::InstantiateNodeFor(
		const flavor_info *info, BMessage* /*_config*/, status_t* /*_out_error*/)
{
	PRINT((CH "()" CT));
	CamDevice *cam = NULL;

	if (fInitStatus < B_OK) {
		return NULL;
	}

	// P3 fase B internal_id layout (mirror of GetFlavorAt):
	//   bit 31     audio (1) / video (0)
	//   bits 24-30 stream index (0..127) for video; ignored for audio
	//   bits 0-23  camera internal id
	const bool isAudioFlavor = (info->internal_id & 0x80000000) != 0;
	const int32 streamIdx = (info->internal_id >> 24) & 0x7F;
	const int32 cameraId = info->internal_id & 0x00FFFFFF;

	fRoster->Lock();
	uint32 camCount = fRoster->CountCameras();

	for (uint32 i = 0; i < camCount; i++) {
		CamDevice *c = fRoster->CameraAt(i);
		PRINT((CH ": cam[%d]: %d, %s" CT, i, c->FlavorInfo()->internal_id, c->BrandName()));

		if (c && ((c->FlavorInfo()->internal_id & 0x00FFFFFF) == cameraId)) {
			cam = c;
			break;
		}
	}

	// P3 fase B: switch the camera to the requested VS interface BEFORE the
	// VideoProducer is created. SelectStream refuses while streaming, but at
	// this point the producer hasn't started a transfer yet so the switch
	// is safe. The audio path is per-camera, not per-stream, so we leave it
	// alone.
	if (cam != NULL && !isAudioFlavor) {
		UVCCamDevice* uvc = dynamic_cast<UVCCamDevice*>(cam);
		if (uvc != NULL && streamIdx != uvc->ActiveStreamIndex()) {
			status_t ss = uvc->SelectStream(streamIdx);
			if (ss != B_OK) {
				syslog(LOG_WARNING, "WebCamMediaAddOn: SelectStream(%d) "
					"returned %s — falling back to active stream %d\n",
					(int)streamIdx, strerror(ss),
					(int)uvc->ActiveStreamIndex());
			}
		}
	}

	// Keep the roster locked across node creation. DeviceRemoved() unlinks a
	// camera from the list under this same lock before tearing it down, so as
	// long as we hold the lock the `cam` pointer we resolved above cannot be
	// deleted out from under us (use-after-free otherwise).
	BMediaNode* result = NULL;
	if (cam != NULL) {
		if (isAudioFlavor) {
			AudioProducer *audioNode = new AudioProducer(this, cam,
				cam->FlavorInfo()->name, cam->FlavorInfo()->internal_id);
			if (audioNode != NULL && audioNode->InitCheck() < B_OK) {
				delete audioNode;
				audioNode = NULL;
			}
			// Register the audio node so the CamDevice can stop it on unplug.
			if (audioNode != NULL)
				cam->SetAudioNode(audioNode);
			result = audioNode;
		} else {
			// Instantiate VideoProducer for video flavor
			VideoProducer *videoNode = new VideoProducer(this, cam, cam->FlavorInfo()->name,
				cam->FlavorInfo()->internal_id);
			if (videoNode != NULL && videoNode->InitCheck() < B_OK) {
				delete videoNode;
				videoNode = NULL;
			}
			if (videoNode != NULL)
				cam->SetVideoNode(videoNode);
			result = videoNode;
		}
	}

	fRoster->Unlock();
	return result;
}


status_t
WebCamMediaAddOn::CameraAdded(CamDevice* device)
{
	PRINT((CH "()" CT));
	NotifyFlavorChange();

	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup("USB Webcam");
	notification.SetTitle("Webcam connected");
	BString message;
	message.SetToFormat("%s %s", device->BrandName(), device->ModelName());
	notification.SetContent(message);
	notification.Send();

	return B_OK;
}


status_t
WebCamMediaAddOn::CameraRemoved(CamDevice* device)
{
	PRINT((CH "()" CT));
	NotifyFlavorChange();

	BNotification notification(B_INFORMATION_NOTIFICATION);
	notification.SetGroup("USB Webcam");
	notification.SetTitle("Webcam disconnected");
	BString message;
	message.SetToFormat("%s %s", device->BrandName(), device->ModelName());
	notification.SetContent(message);
	notification.Send();

	return B_OK;
}


void
WebCamMediaAddOn::FillDefaultFlavorInfo(flavor_info* info)
{
	info->name = (char *)"USB Web Camera";
	info->info = (char *)"USB Web Camera";
	info->kinds = B_BUFFER_PRODUCER | B_CONTROLLABLE | B_PHYSICAL_INPUT;
	info->flavor_flags = 0;//B_FLAVOR_IS_GLOBAL;
	info->internal_id = atomic_add((int32*)&fInternalIDCounter, 1);
	/* FIX BUG 8: Set possible_count = 0 (unlimited) to bypass Media Kit's
	 * stale in_use_count tracking. The Media Kit increments in_use_count when
	 * a node is created but sometimes fails to decrement it on destruction,
	 * especially after crashes. With possible_count = 0, the Media Kit won't
	 * enforce any limit. The physical device limitation is handled by the
	 * USB subsystem itself (only one app can claim the interface at a time).
	 */
	info->possible_count = 0;
	info->in_format_count = 0;
	info->in_format_flags = 0;
	info->in_formats = NULL;
	info->out_format_count = 1;
	info->out_format_flags = 0;
	info->out_formats = &fMediaFormat;
}


BMediaAddOn *
make_media_addon(image_id id)
{
	return new WebCamMediaAddOn(id);
}
