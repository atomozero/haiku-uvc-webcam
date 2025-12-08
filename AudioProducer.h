/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Audio producer node for UVC webcam microphone support.
 */
#ifndef _AUDIO_PRODUCER_H
#define _AUDIO_PRODUCER_H

#include <kernel/OS.h>
#include <media/BufferProducer.h>
#include <media/Controllable.h>
#include <media/MediaDefs.h>
#include <media/MediaEventLooper.h>
#include <media/MediaNode.h>
#include <support/Locker.h>

class CamDevice;


// =============================================================================
// Audio Statistics (Group 8: Audio Optimization)
// =============================================================================

struct audio_timing_stats {
	// Buffer timing
	bigtime_t	last_buffer_time;
	bigtime_t	buffer_interval_sum;
	bigtime_t	buffer_interval_min;
	bigtime_t	buffer_interval_max;
	uint32		buffer_count;

	// Buffer delivery
	uint32		buffers_sent;
	uint32		buffers_dropped;
	uint32		underruns;		// No data available
	uint32		overruns;		// Too much data

	// Audio levels (RMS)
	float		peak_level;		// Maximum absolute sample (0.0 - 1.0)
	float		rms_level;		// Root mean square level
	double		rms_sum;		// Running sum for RMS calculation
	uint64		samples_processed;

	void Reset() {
		last_buffer_time = 0;
		buffer_interval_sum = 0;
		buffer_interval_min = INT64_MAX;
		buffer_interval_max = 0;
		buffer_count = 0;
		buffers_sent = 0;
		buffers_dropped = 0;
		underruns = 0;
		overruns = 0;
		peak_level = 0.0f;
		rms_level = 0.0f;
		rms_sum = 0.0;
		samples_processed = 0;
	}

	void RecordBuffer(bigtime_t now) {
		if (last_buffer_time > 0) {
			bigtime_t interval = now - last_buffer_time;
			buffer_interval_sum += interval;
			if (interval < buffer_interval_min)
				buffer_interval_min = interval;
			if (interval > buffer_interval_max)
				buffer_interval_max = interval;
		}
		last_buffer_time = now;
		buffer_count++;
	}

	void RecordSamples(const int16* samples, size_t count) {
		for (size_t i = 0; i < count; i++) {
			float normalized = samples[i] / 32768.0f;
			float absVal = normalized < 0 ? -normalized : normalized;

			if (absVal > peak_level)
				peak_level = absVal;

			rms_sum += (double)(normalized * normalized);
		}
		samples_processed += count;

		// Update RMS periodically (every 1000 samples)
		if (samples_processed > 0 && samples_processed % 1000 == 0) {
			rms_level = (float)sqrt(rms_sum / samples_processed);
		}
	}

	float GetAverageBufferRate() const {
		if (buffer_count < 2 || buffer_interval_sum == 0)
			return 0.0f;
		bigtime_t avgInterval = buffer_interval_sum / (buffer_count - 1);
		return 1000000.0f / avgInterval;
	}

	float GetDropRate() const {
		uint32 total = buffers_sent + buffers_dropped;
		if (total == 0)
			return 0.0f;
		return 100.0f * buffers_dropped / total;
	}
};


class AudioProducer :
	public virtual BMediaEventLooper,
	public virtual BBufferProducer,
	public virtual BControllable {
public:
						AudioProducer(BMediaAddOn *addon, CamDevice *dev,
								const char *name, int32 internal_id);
virtual					~AudioProducer();

virtual	status_t		InitCheck() const { return fInitStatus; }


/* BMediaNode */
public:
virtual port_id		ControlPort() const;
virtual	BMediaAddOn	*AddOn(int32 * internal_id) const;
virtual	status_t 	HandleMessage(int32 message, const void *data,
							size_t size);
protected:
virtual	void 		Preroll();
virtual void		SetTimeSource(BTimeSource * time_source);
virtual status_t	RequestCompleted(const media_request_info & info);

/* BMediaEventLooper */
protected:
virtual	void 		NodeRegistered();
virtual void		Start(bigtime_t performance_time);
virtual void		Stop(bigtime_t performance_time, bool immediate);
virtual void		Seek(bigtime_t media_time, bigtime_t performance_time);
virtual void		TimeWarp(bigtime_t at_real_time,
							bigtime_t to_performance_time);
virtual status_t	AddTimer(bigtime_t at_performance_time, int32 cookie);
virtual void		SetRunMode(run_mode mode);
virtual void		HandleEvent(const media_timed_event *event,
							bigtime_t lateness, bool realTimeEvent = false);
virtual void		CleanUpEvent(const media_timed_event *event);
virtual bigtime_t	OfflineTime();
virtual status_t	DeleteHook(BMediaNode * node);

/* BBufferProducer */
protected:
virtual	status_t	FormatSuggestionRequested(media_type type, int32 quality,
							media_format * format);
virtual	status_t 	FormatProposal(const media_source &output,
							media_format *format);
virtual	status_t	FormatChangeRequested(const media_source &source,
							const media_destination &destination,
							media_format *io_format, int32 *_deprecated_);
virtual	status_t 	GetNextOutput(int32 * cookie, media_output * out_output);
virtual	status_t	DisposeOutputCookie(int32 cookie);
virtual	status_t	SetBufferGroup(const media_source &for_source,
							BBufferGroup * group);
virtual	status_t	GetLatency(bigtime_t * out_latency);
virtual	status_t	PrepareToConnect(const media_source &what,
							const media_destination &where,
							media_format *format,
							media_source *out_source, char *out_name);
virtual	void		Connect(status_t error, const media_source &source,
							const media_destination &destination,
							const media_format & format, char *io_name);
virtual	void 		Disconnect(const media_source & what,
							const media_destination & where);
virtual	void 		LateNoticeReceived(const media_source & what,
							bigtime_t how_much, bigtime_t performance_time);
virtual	void 		EnableOutput(const media_source & what, bool enabled,
							int32 * _deprecated_);
virtual	status_t	SetPlayRate(int32 numer,int32 denom);
virtual	void 		AdditionalBufferRequested(const media_source & source,
							media_buffer_id prev_buffer, bigtime_t prev_time,
							const media_seek_tag * prev_tag);
virtual	void		LatencyChanged(const media_source & source,
							const media_destination & destination,
							bigtime_t new_latency, uint32 flags);

/* BControllable */
protected:
virtual status_t	GetParameterValue(int32 id, bigtime_t *last_change,
							void *value, size_t *size);
virtual void		SetParameterValue(int32 id, bigtime_t when,
							const void *value, size_t size);
virtual status_t	StartControlPanel(BMessenger *out_messenger);

/* state */
private:
		void				HandleStart(bigtime_t performance_time);
		void				HandleStop();
		void				HandleTimeWarp(bigtime_t performance_time);
		void				HandleSeek(bigtime_t performance_time);

static	int32				fInstances;

		status_t			fInitStatus;

		int32				fInternalID;
		BMediaAddOn			*fAddOn;
		CamDevice			*fCamDevice;

		BLocker				fLock;
		BBufferGroup		*fBufferGroup;

		thread_id			fThread;
		sem_id				fFrameSync;
static	int32				_audio_generator_(void *data);
		int32				AudioGenerator();

		// Audio timing
		uint64				fFramesSent;
		bigtime_t			fStartTime;
		bigtime_t			fProcessingLatency;

		// Output and format
		media_output		fOutput;
		media_raw_audio_format	fConnectedFormat;

		// State flags
		bool				fRunning;
		bool				fConnected;
		bool				fEnabled;

		// Audio parameters
		enum {
			P_MUTE,
			P_VOLUME,
			P_LAST
		};
		bool				fMuted;
		float				fVolume;
		bigtime_t			fLastParamChange;

		// Audio buffer ring
		uint8*				fAudioRingBuffer;
		size_t				fRingBufferSize;
		volatile size_t		fRingBufferHead;
		volatile size_t		fRingBufferTail;
		sem_id				fRingBufferSem;

		// Group 8: Audio statistics
		audio_timing_stats	fAudioStats;
		bigtime_t			fLastStatsReport;

public:
		// Group 8: Statistics API
		const audio_timing_stats&	GetAudioStats() const { return fAudioStats; }
		void				ResetAudioStats();
		void				LogAudioStats();
};

#endif /* _AUDIO_PRODUCER_H */
