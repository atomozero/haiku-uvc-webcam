/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Audio producer node for UVC webcam microphone support.
 */

#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <media/Buffer.h>
#include <media/BufferGroup.h>
#include <media/ParameterWeb.h>
#include <media/TimeSource.h>

#include <support/Autolock.h>
#include <support/Debug.h>

#include "CamDevice.h"
#include "addons/uvc/UVCCamDevice.h"
#include "AudioProducer.h"

#define TOUCH(x) ((void)(x))

// Audio buffer configuration
// Buffer size is ~10ms of audio at the connected sample rate.
// Calculated dynamically in NodeRegistered() and Connect().
#define AUDIO_BUFFER_SIZE_DEFAULT 512	// Fallback if format unknown
#define AUDIO_BUFFER_COUNT 16			// Number of buffers in group

// Define static member variable
int32 AudioProducer::fInstances = 0;


AudioProducer::AudioProducer(
		BMediaAddOn *addon, CamDevice *dev, const char *name, int32 internal_id)
	: BMediaNode(name),
	BMediaEventLooper(),
	BBufferProducer(B_MEDIA_RAW_AUDIO),
	BControllable()
{
	fInitStatus = B_NO_INIT;

	/* Only allow one instance of the node to exist at any time */
	if (atomic_add(&fInstances, 1) != 0)
		return;

	fInternalID = internal_id;
	fAddOn = addon;
	fCamDevice = dev;

	fBufferGroup = NULL;

	fThread = -1;
	fFrameSync = -1;
	fProcessingLatency = 0LL;

	fRunning = false;
	fConnected = false;
	fEnabled = false;

	fFramesSent = 0;
	fStartTime = 0;

	fMuted = false;
	fVolume = 1.0f;
	// Enable AGC by default: many webcam mics have a hardware gain that's
	// too aggressive, causing constant saturation. The AGC attenuates loud
	// input toward -6 dBFS and amplifies quiet input up to +18 dB.
	fAutoGain = true;
	fAutoGainCurrent = 1.0f;
	fLastParamChange = 0;

	// Mono microphone detection
	fMonoDominantChannel = -1;
	fMonoLastCheck = 0;

	// Group 8: Initialize audio statistics
	fAudioStats.Reset();
	fLastStatsReport = 0;

	fOutput.destination = media_destination::null;

	AddNodeKind(B_PHYSICAL_INPUT);

	fInitStatus = B_OK;
}


AudioProducer::~AudioProducer()
{
	if (fInitStatus == B_OK) {
		if (fConnected)
			Disconnect(fOutput.source, fOutput.destination);
		if (fRunning)
			HandleStop();
	}

	atomic_add(&fInstances, -1);
}


/* BMediaNode */
port_id
AudioProducer::ControlPort() const
{
	return BMediaNode::ControlPort();
}


BMediaAddOn *
AudioProducer::AddOn(int32 *internal_id) const
{
	if (internal_id)
		*internal_id = fInternalID;
	return fAddOn;
}


status_t
AudioProducer::HandleMessage(int32 /*message*/, const void* /*data*/, size_t /*size*/)
{
	return B_ERROR;
}


void
AudioProducer::Preroll()
{
	// Hardware preroll if needed
}


void
AudioProducer::SetTimeSource(BTimeSource* /*time_source*/)
{
	if (fFrameSync >= 0) release_sem(fFrameSync);
}


status_t
AudioProducer::RequestCompleted(const media_request_info &info)
{
	return BMediaNode::RequestCompleted(info);
}


/* BMediaEventLooper */

void
AudioProducer::NodeRegistered()
{
	if (fInitStatus != B_OK) {
		ReportError(B_NODE_IN_DISTRESS);
		return;
	}

	// Set up parameter web for audio controls
	BParameterWeb* web = new BParameterWeb();
	BParameterGroup* main = web->MakeGroup(Name());

	// Mute control
	BParameterGroup* g = main->MakeGroup("Mute");
	g->MakeDiscreteParameter(P_MUTE, B_MEDIA_RAW_AUDIO, "Mute", B_MUTE);

	// Volume / gain control (0.0 - 4.0x)
	// Range extended above 1.0x to boost quiet webcam microphones up to +12dB
	g = main->MakeGroup("Volume");
	g->MakeContinuousParameter(P_VOLUME, B_MEDIA_RAW_AUDIO, "Volume",
		B_GAIN, "", 0.0f, 4.0f, 0.01f);

	// Automatic gain control toggle
	g = main->MakeGroup("Auto Gain");
	g->MakeDiscreteParameter(P_AUTO_GAIN, B_MEDIA_RAW_AUDIO, "Auto Gain",
		B_ENABLE);

	SetParameterWeb(web);

	// Setup output
	fOutput.node = Node();
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.destination = media_destination::null;
	strlcpy(fOutput.name, Name(), sizeof(fOutput.name));

	// Configure audio format based on device capabilities
	fOutput.format.type = B_MEDIA_RAW_AUDIO;
	fOutput.format.u.raw_audio = media_raw_audio_format::wildcard;
	fOutput.format.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	fOutput.format.u.raw_audio.byte_order = B_MEDIA_LITTLE_ENDIAN;

	// Get audio parameters from device
	UVCCamDevice* uvcDev = dynamic_cast<UVCCamDevice*>(fCamDevice);
	if (uvcDev != NULL && uvcDev->HasAudio()) {
		uint8 channels = uvcDev->AudioChannels();
		uint32 sampleRate = uvcDev->AudioSampleRate();

		fOutput.format.u.raw_audio.channel_count = (channels > 0) ? channels : 2;
		fOutput.format.u.raw_audio.frame_rate = (sampleRate > 0)
			? (float)sampleRate : 48000.0f;

		syslog(LOG_INFO, "AudioProducer: Using %d ch, %.0f Hz from device\n",
			(int)fOutput.format.u.raw_audio.channel_count,
			fOutput.format.u.raw_audio.frame_rate);
	} else {
		// Default: stereo, 16-bit, 48000 Hz
		fOutput.format.u.raw_audio.channel_count = 2;
		fOutput.format.u.raw_audio.frame_rate = 48000.0f;
		syslog(LOG_WARNING, "AudioProducer: No device info, using defaults\n");
	}
	// ~10ms of audio data per buffer, scaled to sample rate and channels
	{
		uint32 rate = (uint32)fOutput.format.u.raw_audio.frame_rate;
		uint32 channels = fOutput.format.u.raw_audio.channel_count;
		uint32 bufSize = (rate * channels * 2) / 100;  // 10ms
		if (bufSize < 256) bufSize = 256;
		if (bufSize > 4096) bufSize = 4096;
		// Align to frame size (channels * 2 bytes)
		bufSize -= bufSize % (channels * 2);
		fOutput.format.u.raw_audio.buffer_size = bufSize;
	}

	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
}


void
AudioProducer::Start(bigtime_t performance_time)
{
	BMediaEventLooper::Start(performance_time);
}


void
AudioProducer::Stop(bigtime_t performance_time, bool immediate)
{
	BMediaEventLooper::Stop(performance_time, immediate);
}


void
AudioProducer::Seek(bigtime_t media_time, bigtime_t performance_time)
{
	BMediaEventLooper::Seek(media_time, performance_time);
}


void
AudioProducer::TimeWarp(bigtime_t at_real_time, bigtime_t to_performance_time)
{
	BMediaEventLooper::TimeWarp(at_real_time, to_performance_time);
}


status_t
AudioProducer::AddTimer(bigtime_t at_performance_time, int32 cookie)
{
	return BMediaEventLooper::AddTimer(at_performance_time, cookie);
}


void
AudioProducer::SetRunMode(run_mode mode)
{
	BMediaEventLooper::SetRunMode(mode);
}


void
AudioProducer::HandleEvent(const media_timed_event *event,
		bigtime_t lateness, bool realTimeEvent)
{
	TOUCH(lateness);
	TOUCH(realTimeEvent);

	switch (event->type) {
		case BTimedEventQueue::B_START:
			HandleStart(event->event_time);
			break;
		case BTimedEventQueue::B_STOP:
			HandleStop();
			break;
		case BTimedEventQueue::B_WARP:
			HandleTimeWarp(event->bigdata);
			break;
		case BTimedEventQueue::B_SEEK:
			HandleSeek(event->bigdata);
			break;
		default:
			break;
	}
}


void
AudioProducer::CleanUpEvent(const media_timed_event *event)
{
	BMediaEventLooper::CleanUpEvent(event);
}


bigtime_t
AudioProducer::OfflineTime()
{
	return BMediaEventLooper::OfflineTime();
}


status_t
AudioProducer::DeleteHook(BMediaNode *node)
{
	return BMediaEventLooper::DeleteHook(node);
}


/* BBufferProducer */

status_t
AudioProducer::FormatSuggestionRequested(
		media_type type, int32 quality, media_format *format)
{
	if (type != B_MEDIA_RAW_AUDIO)
		return B_MEDIA_BAD_FORMAT;

	TOUCH(quality);

	*format = fOutput.format;
	return B_OK;
}


status_t
AudioProducer::FormatProposal(const media_source &output, media_format *format)
{
	if (output != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	// Accept compatible formats
	if (format->type != B_MEDIA_RAW_AUDIO)
		return B_MEDIA_BAD_FORMAT;

	*format = fOutput.format;
	return B_OK;
}


status_t
AudioProducer::FormatChangeRequested(const media_source &source,
		const media_destination &destination, media_format *io_format,
		int32 *_deprecated_)
{
	TOUCH(destination);
	TOUCH(_deprecated_);

	if (source != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	// For audio input from USB microphone, format is fixed by hardware
	// We can only accept compatible formats
	if (io_format == NULL)
		return B_BAD_VALUE;

	if (io_format->type != B_MEDIA_RAW_AUDIO)
		return B_MEDIA_BAD_FORMAT;

	// Check if requested format is compatible with our output
	media_raw_audio_format &requested = io_format->u.raw_audio;
	media_raw_audio_format &supported = fOutput.format.u.raw_audio;

	// Sample format must match (we only support 16-bit)
	if (requested.format != media_raw_audio_format::wildcard.format
		&& requested.format != supported.format)
		return B_MEDIA_BAD_FORMAT;

	// Byte order must match
	if (requested.byte_order != media_raw_audio_format::wildcard.byte_order
		&& requested.byte_order != supported.byte_order)
		return B_MEDIA_BAD_FORMAT;

	// Channel count must match (hardware limitation)
	if (requested.channel_count != media_raw_audio_format::wildcard.channel_count
		&& requested.channel_count != supported.channel_count)
		return B_MEDIA_BAD_FORMAT;

	// Frame rate must match (hardware limitation)
	if (requested.frame_rate != media_raw_audio_format::wildcard.frame_rate
		&& requested.frame_rate != supported.frame_rate)
		return B_MEDIA_BAD_FORMAT;

	// Format is compatible - return our exact format
	*io_format = fOutput.format;
	return B_OK;
}


status_t
AudioProducer::GetNextOutput(int32 *cookie, media_output *out_output)
{
	if (!out_output)
		return B_BAD_VALUE;

	if ((*cookie) != 0)
		return B_BAD_INDEX;

	*out_output = fOutput;
	(*cookie)++;
	return B_OK;
}


status_t
AudioProducer::DisposeOutputCookie(int32 cookie)
{
	TOUCH(cookie);
	return B_OK;
}


status_t
AudioProducer::SetBufferGroup(const media_source &for_source,
		BBufferGroup *group)
{
	if (for_source != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	// Changing buffer group while running is not supported
	if (fRunning)
		return B_NOT_ALLOWED;

	BAutolock lock(fLock);

	if (group != NULL) {
		// Use provided buffer group
		// Verify it meets our requirements
		int32 bufferCount;
		if (group->CountBuffers(&bufferCount) != B_OK)
			return B_ERROR;

		if (bufferCount < 2) {
			syslog(LOG_WARNING,
				"AudioProducer: Buffer group has too few buffers (%d)\n",
				(int)bufferCount);
			return B_BAD_VALUE;
		}

		// Delete our own buffer group if we have one
		delete fBufferGroup;
		fBufferGroup = group;
	} else {
		// NULL means use our own buffer group
		// Recreate default buffer group if needed
		if (fBufferGroup == NULL && fConnected) {
			size_t bufSize = fConnectedFormat.buffer_size;
			if (bufSize == 0)
				bufSize = AUDIO_BUFFER_SIZE_DEFAULT;
			fBufferGroup = new BBufferGroup(bufSize, AUDIO_BUFFER_COUNT);
			if (fBufferGroup->InitCheck() != B_OK) {
				delete fBufferGroup;
				fBufferGroup = NULL;
				return B_NO_MEMORY;
			}
		}
	}

	return B_OK;
}


status_t
AudioProducer::GetLatency(bigtime_t *out_latency)
{
	*out_latency = EventLatency() + SchedulingLatency();
	return B_OK;
}


status_t
AudioProducer::PrepareToConnect(const media_source &source,
		const media_destination &destination, media_format *format,
		media_source *out_source, char *out_name)
{
	if (fConnected)
		return EALREADY;

	if (source != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	if (fOutput.destination != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;

	if (!format_is_compatible(*format, fOutput.format)) {
		*format = fOutput.format;
		return B_MEDIA_BAD_FORMAT;
	}

	*out_source = fOutput.source;
	strlcpy(out_name, fOutput.name, B_MEDIA_NAME_LENGTH);

	fOutput.destination = destination;

	return B_OK;
}


void
AudioProducer::Connect(status_t error, const media_source &source,
		const media_destination &destination, const media_format &format,
		char *io_name)
{
	if (fConnected)
		return;

	if (source != fOutput.source || error < B_OK
		|| !const_cast<media_format *>(&format)->Matches(&fOutput.format))
		return;

	fOutput.destination = destination;
	strlcpy(io_name, fOutput.name, B_MEDIA_NAME_LENGTH);

	fConnectedFormat = format.u.raw_audio;

	// Validate and fix format fields that may come as wildcards (0)
	if ((fConnectedFormat.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK) == 0)
		fConnectedFormat.format = media_raw_audio_format::B_AUDIO_SHORT;
	if (fConnectedFormat.channel_count == 0)
		fConnectedFormat.channel_count = 2;
	if (fConnectedFormat.frame_rate <= 0)
		fConnectedFormat.frame_rate = 48000.0f;
	if (fConnectedFormat.buffer_size == 0)
		fConnectedFormat.buffer_size = AUDIO_BUFFER_SIZE_DEFAULT;
	if (fConnectedFormat.byte_order == 0)
		fConnectedFormat.byte_order = B_MEDIA_LITTLE_ENDIAN;

	syslog(LOG_INFO, "AudioProducer: Connect format: %u ch, %.0f Hz, "
		"bufSize=%u, sampleSize=%u\n",
		(unsigned)fConnectedFormat.channel_count,
		fConnectedFormat.frame_rate,
		(unsigned)fConnectedFormat.buffer_size,
		(unsigned)(fConnectedFormat.format
			& media_raw_audio_format::B_AUDIO_SIZE_MASK));

	// Get latency
	bigtime_t latency = 0;
	media_node_id tsID = 0;
	FindLatencyFor(fOutput.destination, &latency, &tsID);
	SetEventLatency(latency + 1000);

	// Calculate buffer duration
	size_t sampleSize = fConnectedFormat.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK;
	size_t channelCount = fConnectedFormat.channel_count;
	size_t frameSize = sampleSize * channelCount;
	size_t framesPerBuffer = fConnectedFormat.buffer_size / frameSize;
	if (framesPerBuffer == 0)
		framesPerBuffer = 1;
	fProcessingLatency = (bigtime_t)(framesPerBuffer * 1000000LL
		/ (bigtime_t)fConnectedFormat.frame_rate);

	// Create buffer group
	syslog(LOG_INFO, "AudioProducer: Creating BufferGroup size=%u count=%d\n",
		(unsigned)fConnectedFormat.buffer_size, AUDIO_BUFFER_COUNT);
	fBufferGroup = new BBufferGroup(fConnectedFormat.buffer_size,
		AUDIO_BUFFER_COUNT);
	if (fBufferGroup->InitCheck() < B_OK) {
		syslog(LOG_ERR, "AudioProducer: BufferGroup InitCheck failed "
			"(size=%u, err=%s)\n",
			(unsigned)fConnectedFormat.buffer_size,
			strerror(fBufferGroup->InitCheck()));
		delete fBufferGroup;
		fBufferGroup = NULL;
		return;
	}

	fConnected = true;
	fEnabled = true;

	if (fFrameSync >= 0) release_sem(fFrameSync);
}


void
AudioProducer::Disconnect(const media_source &source,
		const media_destination &destination)
{
	if (!fConnected)
		return;

	if ((source != fOutput.source) || (destination != fOutput.destination))
		return;

	if (fRunning)
		HandleStop();

	fEnabled = false;
	fOutput.destination = media_destination::null;

	fLock.Lock();
	delete fBufferGroup;
	fBufferGroup = NULL;
	fLock.Unlock();

	fConnected = false;
}


void
AudioProducer::LateNoticeReceived(const media_source &source,
		bigtime_t how_much, bigtime_t performance_time)
{
	TOUCH(performance_time);

	if (source != fOutput.source)
		return;

	// For live audio input, we can't really catch up like a file source
	// The best we can do is adjust our timing and log the issue
	if (how_much > 10000) {
		// More than 10ms late - worth noting
		syslog(LOG_DEBUG, "AudioProducer: Late by %lld us at %lld\n",
			(long long)how_much, (long long)performance_time);
	}

	// Try to reduce latency by speeding up buffer delivery
	// Signal the generator thread to recalculate timing
	if (fFrameSync >= 0) release_sem(fFrameSync);
}


void
AudioProducer::EnableOutput(const media_source &source, bool enabled,
		int32 *_deprecated_)
{
	TOUCH(_deprecated_);

	if (source != fOutput.source)
		return;

	fEnabled = enabled;
}


status_t
AudioProducer::SetPlayRate(int32 numer, int32 denom)
{
	TOUCH(numer);
	TOUCH(denom);
	return B_ERROR;
}


void
AudioProducer::AdditionalBufferRequested(const media_source &source,
		media_buffer_id prev_buffer, bigtime_t prev_time,
		const media_seek_tag *prev_tag)
{
	TOUCH(source);
	TOUCH(prev_buffer);
	TOUCH(prev_time);
	TOUCH(prev_tag);
}


void
AudioProducer::LatencyChanged(const media_source &source,
		const media_destination &destination, bigtime_t new_latency,
		uint32 flags)
{
	TOUCH(source);
	TOUCH(destination);
	TOUCH(new_latency);
	TOUCH(flags);
}


/* BControllable */

status_t
AudioProducer::GetParameterValue(
	int32 id, bigtime_t *last_change, void *value, size_t *size)
{
	switch (id) {
		case P_MUTE:
			*last_change = fLastParamChange;
			*size = sizeof(int32);
			*((int32 *)value) = fMuted ? 1 : 0;
			return B_OK;
		case P_VOLUME:
			*last_change = fLastParamChange;
			*size = sizeof(float);
			*((float *)value) = fVolume;
			return B_OK;
		case P_AUTO_GAIN:
			*last_change = fLastParamChange;
			*size = sizeof(int32);
			*((int32 *)value) = fAutoGain ? 1 : 0;
			return B_OK;
	}
	return B_BAD_VALUE;
}


void
AudioProducer::SetParameterValue(
	int32 id, bigtime_t when, const void *value, size_t size)
{
	switch (id) {
		case P_MUTE:
			if (!value || size != sizeof(int32))
				return;
			fMuted = (*((int32 *)value) != 0);
			fLastParamChange = when;
			BroadcastNewParameterValue(when, id, (void *)value, size);
			break;
		case P_VOLUME:
			if (!value || size != sizeof(float))
				return;
			fVolume = *((float *)value);
			if (fVolume < 0.0f) fVolume = 0.0f;
			if (fVolume > 4.0f) fVolume = 4.0f;
			fLastParamChange = when;
			BroadcastNewParameterValue(when, id, (void *)value, size);
			break;
		case P_AUTO_GAIN:
			if (!value || size != sizeof(int32))
				return;
			fAutoGain = (*((int32 *)value) != 0);
			fAutoGainCurrent = 1.0f;	// reset when toggled
			fLastParamChange = when;
			BroadcastNewParameterValue(when, id, (void *)value, size);
			break;
	}
}


status_t
AudioProducer::StartControlPanel(BMessenger *out_messenger)
{
	return BControllable::StartControlPanel(out_messenger);
}


/* AudioProducer */

void
AudioProducer::HandleStart(bigtime_t performance_time)
{
	if (fRunning)
		return;

	if (!fCamDevice) {
		syslog(LOG_ERR, "AudioProducer: HandleStart - no device!\n");
		return;
	}

	fFramesSent = 0;
	fStartTime = system_time();

	fFrameSync = create_sem(0, "audio frame sync");
	if (fFrameSync < B_OK) {
		syslog(LOG_ERR, "AudioProducer: HandleStart - create_sem failed\n");
		return;
	}

	// Start USB audio transfer on device
	UVCCamDevice* uvcDev = dynamic_cast<UVCCamDevice*>(fCamDevice);
	if (uvcDev && uvcDev->HasAudio()) {
		status_t err = uvcDev->StartAudioTransfer();
		if (err != B_OK) {
			syslog(LOG_ERR, "AudioProducer: Failed to start audio transfer: %s\n",
				strerror(err));
			delete_sem(fFrameSync);
			return;
		}

		// Re-read sample rate after StartAudioTransfer - the device may
		// report a different rate than what was parsed from descriptors.
		// Without this, audio plays at wrong speed (too fast or too slow).
		uint32 actualRate = uvcDev->AudioSampleRate();
		if (actualRate > 0
			&& actualRate != (uint32)fOutput.format.u.raw_audio.frame_rate) {
			syslog(LOG_WARNING, "AudioProducer: Sample rate changed %u -> %u "
				"after USB negotiation\n",
				(unsigned)fOutput.format.u.raw_audio.frame_rate,
				(unsigned)actualRate);
			fOutput.format.u.raw_audio.frame_rate = (float)actualRate;
			// The generator paces and timestamps buffers from
			// fConnectedFormat.frame_rate (frozen in Connect() from the
			// descriptor-parsed rate). Update it too, otherwise the audio is
			// paced at the wrong rate and drifts, eventually over/underrunning
			// the ring buffer. Done here, before the generator thread is
			// spawned below, so there is no race with it.
			fConnectedFormat.frame_rate = (float)actualRate;
		}
	} else {
		syslog(LOG_WARNING, "AudioProducer: Device has no audio support\n");
	}

	fRunning = true;

	fThread = spawn_thread(_audio_generator_, "audio generator", B_REAL_TIME_PRIORITY, this);
	if (fThread < B_OK) {
		syslog(LOG_ERR, "AudioProducer: HandleStart - spawn_thread failed\n");
		fRunning = false;
		if (uvcDev)
			uvcDev->StopAudioTransfer();
		delete_sem(fFrameSync);
		return;
	}

	if (resume_thread(fThread) < B_OK) {
		syslog(LOG_ERR, "AudioProducer: HandleStart - resume_thread failed\n");
		fRunning = false;
		kill_thread(fThread);
		if (uvcDev)
			uvcDev->StopAudioTransfer();
		delete_sem(fFrameSync);
		return;
	}

}


void
AudioProducer::HandleStop(void)
{
	if (!fRunning)
		return;

	fRunning = false;

	delete_sem(fFrameSync);
	fFrameSync = -1;

	status_t threadStatus;
	wait_for_thread_etc(fThread, B_RELATIVE_TIMEOUT, 5000000, &threadStatus);

	// Stop USB audio transfer on device
	if (fCamDevice != NULL) {
		UVCCamDevice* uvcDev = dynamic_cast<UVCCamDevice*>(fCamDevice);
		if (uvcDev != NULL && uvcDev->HasAudio())
			uvcDev->StopAudioTransfer();
	}
}


void
AudioProducer::HandleTimeWarp(bigtime_t performance_time)
{
	TOUCH(performance_time);
	fStartTime = system_time();
	fFramesSent = 0;
	if (fFrameSync >= 0) release_sem(fFrameSync);
}


void
AudioProducer::HandleSeek(bigtime_t performance_time)
{
	TOUCH(performance_time);
	fStartTime = system_time();
	fFramesSent = 0;
	if (fFrameSync >= 0) release_sem(fFrameSync);
}


int32
AudioProducer::_audio_generator_(void *data)
{
	return ((AudioProducer *)data)->AudioGenerator();
}


int32
AudioProducer::AudioGenerator()
{
	size_t sampleSize = fConnectedFormat.format
		& media_raw_audio_format::B_AUDIO_SIZE_MASK;
	if (sampleSize == 0)
		sampleSize = 2;
	size_t channelCount = fConnectedFormat.channel_count;
	if (channelCount == 0)
		channelCount = 1;
	size_t frameSize = sampleSize * channelCount;
	size_t framesPerBuffer = fConnectedFormat.buffer_size / frameSize;
	if (framesPerBuffer == 0)
		framesPerBuffer = 1;
	float frameRate = fConnectedFormat.frame_rate;
	if (frameRate <= 0)
		frameRate = 48000.0f;
	bigtime_t bufferDuration = (bigtime_t)(framesPerBuffer * 1000000LL
		/ (bigtime_t)frameRate);

	while (fRunning) {
		// For live audio capture, timing is driven by USB data arrival.
		// ReadAudioData blocks until data is available, so no timer needed.
		if (!fEnabled) {
			snooze(bufferDuration);
			continue;
		}

		BAutolock _(fLock);

		if (!fBufferGroup)
			continue;

		BBuffer *buffer = fBufferGroup->RequestBuffer(fConnectedFormat.buffer_size,
			bufferDuration);
		if (!buffer) {
			fAudioStats.buffers_dropped++;
			snooze(bufferDuration);
			continue;
		}

		// Group 8: Record buffer timing
		fAudioStats.RecordBuffer(system_time());

		// Fill buffer with audio data from device ring buffer
		int16 *audioData = (int16 *)buffer->Data();
		size_t bytesToFill = fConnectedFormat.buffer_size;

		// Read audio data from device
		UVCCamDevice* uvcDev = dynamic_cast<UVCCamDevice*>(fCamDevice);
		size_t bytesRead = 0;
		if (uvcDev != NULL && uvcDev->HasAudio()) {
			bytesRead = uvcDev->ReadAudioData(audioData, bytesToFill);
		}

		// Only log when status changes: first read, transitions to/from underrun
		static int sLastBytesRead = -1;
		bool wasUnderrun = (sLastBytesRead == 0);
		bool isUnderrun = (bytesRead == 0);
		if (sLastBytesRead < 0 || wasUnderrun != isUnderrun) {
			syslog(LOG_INFO, "AudioProducer: read %zu/%zu bytes%s\n",
				bytesRead, bytesToFill,
				isUnderrun ? " (UNDERRUN)" : "");
			sLastBytesRead = (int)bytesRead;
		}

		// Fill remaining with silence if not enough data
		if (bytesRead < bytesToFill) {
			memset((uint8*)audioData + bytesRead, 0, bytesToFill - bytesRead);
			if (bytesRead == 0)
				fAudioStats.underruns++;
		}

		// Mono microphone fix: many webcams have a single microphone but
		// report stereo format. Detect which channel carries the signal
		// and duplicate it to both channels. Check every 5 seconds to
		// avoid per-buffer overhead and prevent state oscillation.
		// Align bytesRead to audio frame boundary
		size_t frameBytes = fConnectedFormat.channel_count * sizeof(int16);
		if (frameBytes > 0)
			bytesRead -= bytesRead % frameBytes;
		if (fConnectedFormat.channel_count == 2 && bytesRead >= 8) {
			bigtime_t now = system_time();
			if (now - fMonoLastCheck > 5000000) {
				fMonoLastCheck = now;
				size_t samplePairs = bytesRead / 4;
				int32 sumL = 0, sumR = 0;
				size_t checkCount = (samplePairs < 256) ? samplePairs : 256;
				for (size_t i = 0; i < checkCount; i++) {
					sumL += abs(audioData[i * 2]);
					sumR += abs(audioData[i * 2 + 1]);
				}
				int32 prev = fMonoDominantChannel;
				if (sumR == 0 || (sumL > 0 && sumL > sumR * 10)) {
					fMonoDominantChannel = 0;
				} else if (sumL == 0 || (sumR > 0 && sumR > sumL * 10)) {
					fMonoDominantChannel = 1;
				} else {
					fMonoDominantChannel = -1;
				}
				if (fMonoDominantChannel != prev) {
					syslog(LOG_INFO, "AudioProducer: Mono detect: %s "
						"(L=%d R=%d)\n",
						fMonoDominantChannel == 0 ? "LEFT dominant" :
						fMonoDominantChannel == 1 ? "RIGHT dominant" :
						"balanced",
						(int)sumL, (int)sumR);
				}
			}
			if (fMonoDominantChannel >= 0) {
				size_t samplePairs = bytesRead / 4;
				if (fMonoDominantChannel == 0) {
					for (size_t i = 0; i < samplePairs; i++)
						audioData[i * 2 + 1] = audioData[i * 2];
				} else {
					for (size_t i = 0; i < samplePairs; i++)
						audioData[i * 2] = audioData[i * 2 + 1];
				}
			}
		}

		// Group 8: Record audio levels
		size_t sampleCount = bytesRead / sizeof(int16);
		if (sampleCount > 0)
			fAudioStats.RecordSamples(audioData, sampleCount);

		// Apply volume / gain and mute
		// Volume range is 0.0 - 4.0. Below 1.0 attenuates, above 1.0 amplifies
		// quiet microphones. Saturate to int16 range to prevent wrap-around clipping.
		size_t samplesToProcess = bytesToFill / sizeof(int16);
		if (fMuted) {
			memset(audioData, 0, fConnectedFormat.buffer_size);
		} else {
			float gain = fVolume;

			// Auto gain: measure peak, smoothly adjust gain toward a target
			// level. Skip below noise floor to avoid amplifying silence.
			if (fAutoGain && samplesToProcess > 0) {
				int32 peak = 0;
				for (size_t i = 0; i < samplesToProcess; i++) {
					int32 v = audioData[i];
					if (v < 0) v = -v;
					if (v > peak) peak = v;
				}

				const int32 kNoiseFloor = 200;	// ~-44 dBFS
				const int32 kTarget = 16384;	// ~-6 dBFS (50% of int16)
				const float kMaxGain = 8.0f;	// +18 dB ceiling
				const float kAttack = 0.05f;	// fast when over-target
				const float kRelease = 0.005f;	// slow when under-target

				if (peak > kNoiseFloor) {
					float desired = (float)kTarget / (float)peak;
					if (desired > kMaxGain) desired = kMaxGain;
					float blend = (desired < fAutoGainCurrent) ? kAttack : kRelease;
					fAutoGainCurrent += (desired - fAutoGainCurrent) * blend;
					if (fAutoGainCurrent < 0.1f) fAutoGainCurrent = 0.1f;
					if (fAutoGainCurrent > kMaxGain) fAutoGainCurrent = kMaxGain;
				}
				gain *= fAutoGainCurrent;
			}

			if (gain != 1.0f) {
				for (size_t i = 0; i < samplesToProcess; i++) {
					int32 sample = (int32)(audioData[i] * gain);
					if (sample > 32767) sample = 32767;
					else if (sample < -32768) sample = -32768;
					audioData[i] = (int16)sample;
				}
			}
		}

		// Pace the producer to wall clock. The buffer represents
		// framesPerBuffer of audio (= bufferDuration of wall time). If we
		// produced it faster than wall time (e.g. during a burst of queued
		// USB data), snooze so it arrives at the right moment. Otherwise the
		// recorder sees timestamps run ahead of the time source and discards
		// the "future" buffers.
		bigtime_t framesElapsed = (bigtime_t)((double)fFramesSent
			* 1000000.0 / (double)frameRate);
		bigtime_t targetTime = fStartTime + framesElapsed;
		bigtime_t now = system_time();
		if (targetTime > now + 1000)
			snooze(targetTime - now);

		// Set buffer header
		media_header *h = buffer->Header();
		h->type = B_MEDIA_RAW_AUDIO;
		h->size_used = bytesToFill;
		h->time_source = TimeSource()->ID();

		BTimeSource* ts = TimeSource();
		if (ts != NULL)
			h->start_time = ts->PerformanceTimeFor(targetTime);
		else
			h->start_time = targetTime;

		if (SendBuffer(buffer, fOutput.source, fOutput.destination) != B_OK) {
			syslog(LOG_WARNING, "AudioProducer: SendBuffer failed\n");
			buffer->Recycle();
			fAudioStats.buffers_dropped++;
		} else {
			fAudioStats.buffers_sent++;
		}

		fFramesSent += framesPerBuffer;

		// Group 8: Periodic statistics report (every 30 seconds)
		bigtime_t nowStats = system_time();
		if (nowStats - fLastStatsReport > 30000000) {
			LogAudioStats();
			fLastStatsReport = nowStats;
		}
	}

	return 0;
}


// =============================================================================
// Group 8: Audio Statistics Implementation
// =============================================================================

void
AudioProducer::ResetAudioStats()
{
	fAudioStats.Reset();
	fLastStatsReport = system_time();
}


void
AudioProducer::LogAudioStats()
{
	if (fAudioStats.buffer_count == 0)
		return;

	float bufferRate = fAudioStats.GetAverageBufferRate();
	float dropRate = fAudioStats.GetDropRate();

	syslog(LOG_INFO, "AudioProducer stats: sent=%u dropped=%u (%.1f%%) underruns=%u overruns=%u\n",
		fAudioStats.buffers_sent, fAudioStats.buffers_dropped, dropRate,
		fAudioStats.underruns, fAudioStats.overruns);

	syslog(LOG_INFO, "AudioProducer timing: rate=%.1f buf/s interval min=%lld max=%lld us\n",
		bufferRate, fAudioStats.buffer_interval_min, fAudioStats.buffer_interval_max);

	syslog(LOG_INFO, "AudioProducer levels: peak=%.3f rms=%.3f samples=%llu\n",
		fAudioStats.peak_level, fAudioStats.rms_level,
		(unsigned long long)fAudioStats.samples_processed);
}
