/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */

#include <support/Autolock.h>
#include <media/MediaFormats.h>

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

	int32 videoCount = fRoster->CountCameras();

	// Count cameras with audio support
	int32 audioCount = 0;
	fRoster->Lock();
	for (int32 i = 0; i < videoCount; i++) {
		CamDevice* cam = fRoster->CameraAt(i);
		if (cam != NULL) {
			UVCCamDevice* uvcCam = dynamic_cast<UVCCamDevice*>(cam);
			if (uvcCam != NULL && uvcCam->HasAudio())
				audioCount++;
		}
	}
	fRoster->Unlock();

	PRINT((CH ": %d video + %d audio flavors" CT, videoCount, audioCount));
	return videoCount + audioCount;
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

	int32 videoCount = fRoster->CountCameras();
	PRINT((CH ": %d cameras" CT, videoCount));

	if (n < 0)
		return B_BAD_INDEX;

	fRoster->Lock();

	// First: video flavors (indices 0 to videoCount-1)
	if (n < videoCount) {
		CamDevice* cam = fRoster->CameraAt(n);
		*out_info = &fDefaultFlavorInfo;
		if (cam && cam->FlavorInfo())
			*out_info = cam->FlavorInfo();
		fRoster->Unlock();
		PRINT((CH ": returning VIDEO flavor for %d, internal_id %d" CT, n, (*out_info)->internal_id));
		return B_OK;
	}

	// Then: audio flavors (indices videoCount onwards)
	int32 audioIndex = n - videoCount;
	int32 audioFound = 0;

	for (int32 i = 0; i < videoCount; i++) {
		CamDevice* cam = fRoster->CameraAt(i);
		if (cam) {
			UVCCamDevice* uvcCam = dynamic_cast<UVCCamDevice*>(cam);
			if (uvcCam && uvcCam->HasAudio()) {
				if (audioFound == audioIndex) {
					// Found the audio flavor we're looking for
					// Use audio flavor with modified internal_id to distinguish from video
					// Audio internal_id = video_internal_id | 0x80000000
					fDefaultAudioFlavorInfo.internal_id =
						cam->FlavorInfo()->internal_id | 0x80000000;

					// Build name with device name
					static char audioName[256];
					snprintf(audioName, sizeof(audioName), "%s Audio",
						cam->FlavorInfo()->name);
					fDefaultAudioFlavorInfo.name = audioName;

					*out_info = &fDefaultAudioFlavorInfo;
					fRoster->Unlock();
					return B_OK;
				}
				audioFound++;
			}
		}
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

	// Check if this is an audio flavor request (high bit set)
	bool isAudioFlavor = (info->internal_id & 0x80000000) != 0;
	int32 videoInternalId = info->internal_id & 0x7FFFFFFF;

	fRoster->Lock();
	uint32 camCount = fRoster->CountCameras();

	for (uint32 i = 0; i < camCount; i++) {
		CamDevice *c = fRoster->CameraAt(i);
		PRINT((CH ": cam[%d]: %d, %s" CT, i, c->FlavorInfo()->internal_id, c->BrandName()));

		if (c && (c->FlavorInfo()->internal_id == videoInternalId)) {
			cam = c;
			break;
		}
	}
	fRoster->Unlock();

	if (cam == NULL)
		return NULL;

	if (isAudioFlavor) {
		// Instantiate AudioProducer for audio flavor
		UVCCamDevice* uvcCam = dynamic_cast<UVCCamDevice*>(cam);
		if (uvcCam == NULL || !uvcCam->HasAudio())
			return NULL;

		char audioName[256];
		snprintf(audioName, sizeof(audioName), "%s Audio", cam->FlavorInfo()->name);

		AudioProducer *audioNode = new AudioProducer(this, cam, audioName, info->internal_id);
		if (audioNode != NULL && audioNode->InitCheck() < B_OK) {
			delete audioNode;
			audioNode = NULL;
		}
		return audioNode;
	} else {
		// Instantiate VideoProducer for video flavor
		VideoProducer *videoNode = new VideoProducer(this, cam, cam->FlavorInfo()->name,
			cam->FlavorInfo()->internal_id);
		if (videoNode != NULL && videoNode->InitCheck() < B_OK) {
			delete videoNode;
			videoNode = NULL;
		}
		return videoNode;
	}
}


status_t
WebCamMediaAddOn::CameraAdded(CamDevice* device)
{
	PRINT((CH "()" CT));
	NotifyFlavorChange();
	return B_OK;
}


status_t
WebCamMediaAddOn::CameraRemoved(CamDevice* device)
{
	PRINT((CH "()" CT));
	NotifyFlavorChange();
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
