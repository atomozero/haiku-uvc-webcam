/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */

#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <syslog.h>
#include <unistd.h>

#include <media/Buffer.h>
#include <media/BufferGroup.h>
#include <media/ParameterWeb.h>
#include <media/TimeSource.h>

#include <support/Autolock.h>
#include <support/Debug.h>

//XXX: change interface
#include <interface/Bitmap.h>

#include "CamDevice.h"
#include "CamSensor.h"

#define SINGLE_PARAMETER_GROUP 1

// FIX BUG 2: Disabilitato FORCE_320_240 per permettere risoluzioni maggiori
//#define FORCE_320_240 1
//#define FORCE_160_120 1
//#define FORCE_MAX_FRAME 1

#define TOUCH(x) ((void)(x))

/* PRODUCTION BUILD: Disable all debug output and file I/O for stability */
/* CRITICAL: File I/O in USB/media threads causes BFS corruption and kernel panics */
/* DEBUG: Temporarily disabled to trace connection issues */
// #define PRODUCTION_BUILD 1

#ifdef PRODUCTION_BUILD
#define PRINTF(a,b) do {} while(0)
#define TRACE(x...) do {} while(0)
/* Disable all file I/O to prevent BFS corruption in USB threads */
#undef fopen
#define fopen(path, mode) ((FILE*)NULL)
#undef fclose
#define fclose(f) do {} while(0)
#undef fflush
#define fflush(f) do {} while(0)
#undef fprintf
#define fprintf(...) do {} while(0)
#else
#define PRINTF(a,b) \
		do { \
			if (a < 2) { \
				printf("VideoProducer::"); \
				printf b; \
			} \
		} while (0)
#define TRACE(x...) fprintf(stderr, x)
#endif

#include "Producer.h"

// 	PERFORMANCE FIX: Changed from 5 fps (debug value) to 30 fps (production)
// 	5 fps was too slow for normal webcam usage
#define FIELD_RATE 30.0f
//#define FIELD_RATE 29.97f  // Use for NTSC compatibility if needed
//#define FIELD_RATE 5.0f    // Low framerate for debugging only

// Define static member variable
int32 VideoProducer::fInstances = 0;


VideoProducer::VideoProducer(
		BMediaAddOn *addon, CamDevice *dev, const char *name, int32 internal_id)
	: BMediaNode(name),
	BMediaEventLooper(),
	BBufferProducer(B_MEDIA_RAW_VIDEO),
	BControllable()
{
	fInitStatus = B_NO_INIT;

	/* FIX BUG 7: Removed static fInstances counter check.
	 * The previous implementation blocked node instantiation after crashes
	 * because fInstances persisted in the addon memory without being reset.
	 * The Media Kit already handles instantiation limits via flavor_info::possible_count
	 * so this check was redundant and caused "can't create more nodes" errors.
	 */
	atomic_add(&fInstances, 1);  // Keep counter for debugging/statistics only

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

	// CRITICAL FIX: Initialize timing variables to avoid garbage values
	// causing TimeSource overflow crashes in BMediaEventLooper
	fFrame = 0;
	fFrameBase = 0;
	fPerformanceTimeBase = 0;  // Will be set properly in HandleStart()
	fStartRealTime = 0;

	fOutput.destination = media_destination::null;

	// CRITICAL FIX: Initialize fConnectedFormat with default dimensions
	// so that queries for "current format" return sensible values even
	// before a connection is established. BubiCam reads this to show
	// "Current Format" in its diagnostics.
	memset(&fConnectedFormat, 0, sizeof(fConnectedFormat));
	fConnectedFormat.display.format = B_RGB32;
	fConnectedFormat.display.line_width = 320;
	fConnectedFormat.display.line_count = 240;
	fConnectedFormat.display.bytes_per_row = 320 * 4;  // FIX: Initialize bytes_per_row
	fConnectedFormat.field_rate = 30.0f;
	fConnectedFormat.interlace = 1;

	AddNodeKind(B_PHYSICAL_INPUT);

	fInitStatus = B_OK;
	return;
}


VideoProducer::~VideoProducer()
{
	/* Clean up after ourselves, in case the application didn't make us
	 * do so. */
	if (fConnected)
		Disconnect(fOutput.source, fOutput.destination);
	if (fRunning)
		HandleStop();

	/* FIX BUG 7: Always decrement counter since we always increment in constructor.
	 * Counter is now for debugging/statistics only, not for access control. */
	atomic_add(&fInstances, -1);
}


/* BMediaNode */
port_id
VideoProducer::ControlPort() const
{
	return BMediaNode::ControlPort();
}


BMediaAddOn *
VideoProducer::AddOn(int32 *internal_id) const
{
	if (internal_id)
		*internal_id = fInternalID;
	return fAddOn;
}


status_t
VideoProducer::HandleMessage(int32 /*message*/, const void* /*data*/, size_t /*size*/)
{
	return B_ERROR;
}


void
VideoProducer::Preroll()
{
	/* This hook may be called before the node is started to give the hardware
	 * a chance to start. */
}


void
VideoProducer::SetTimeSource(BTimeSource* /*time_source*/)
{
	/* Tell frame generation thread to recalculate delay value */
	/* FIX: Check semaphore is valid before releasing */
	if (fFrameSync >= 0)
		release_sem(fFrameSync);
}


status_t
VideoProducer::RequestCompleted(const media_request_info &info)
{
	return BMediaNode::RequestCompleted(info);
}


/* BMediaEventLooper */


void
VideoProducer::NodeRegistered()
{
	syslog(LOG_INFO, "Producer: NodeRegistered called! fInitStatus=%d\n", fInitStatus);

	if (fInitStatus != B_OK) {
		syslog(LOG_ERR, "Producer: NodeRegistered - fInitStatus BAD, reporting distress\n");
		ReportError(B_NODE_IN_DISTRESS);
		return;
	}

	/* Set up the parameter web */

	//TODO: remove and put sensible stuff there
	BParameterWeb *web = new BParameterWeb();
	BParameterGroup *main = web->MakeGroup(Name());
	BParameterGroup *g;

	/*
	g = main->MakeGroup("Color");
	BDiscreteParameter *state = g->MakeDiscreteParameter(
			P_COLOR, B_MEDIA_RAW_VIDEO, "Color", "Color");
	state->AddItem(B_HOST_TO_LENDIAN_INT32(0x00ff0000), "Red");
	state->AddItem(B_HOST_TO_LENDIAN_INT32(0x0000ff00), "Green");
	state->AddItem(B_HOST_TO_LENDIAN_INT32(0x000000ff), "Blue");
	*/

	g = main->MakeGroup("Info");
	g->MakeTextParameter(P_INFO, B_MEDIA_RAW_VIDEO, "", "Info", 256);

	int32 id = P_LAST;
	if (fCamDevice) {
#ifndef SINGLE_PARAMETER_GROUP
		main = web->MakeGroup("Device");
#endif
		fCamDevice->AddParameters(main, id);
		if (fCamDevice->Sensor()) {
#ifndef SINGLE_PARAMETER_GROUP
			main = web->MakeGroup("Sensor");
#endif
			fCamDevice->Sensor()->AddParameters(main, id);
		}
	}

	fColor = B_HOST_TO_LENDIAN_INT32(0x00ff0000);
	fLastColorChange = system_time();

	/* After this call, the BControllable owns the BParameterWeb object and
	 * will delete it for you */
	SetParameterWeb(web);

	fOutput.node = Node();
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.destination = media_destination::null;
	strcpy(fOutput.name, Name());

	/* Tailor these for the output of your device */
	fOutput.format.type = B_MEDIA_RAW_VIDEO;
	fOutput.format.u.raw_video = media_raw_video_format::wildcard;
	fOutput.format.u.raw_video.interlace = 1;
	fOutput.format.u.raw_video.display.format = B_RGB32;
	fOutput.format.u.raw_video.field_rate = FIELD_RATE; // XXX: mmu

	/* CRITICAL FIX: Populate video dimensions from device
	 * Without this, fOutput.format has 0x0 dimensions from the wildcard,
	 * which the Media Kit returns when enumerating node capabilities.
	 * This caused the "Video format not available (0x0)" diagnostic error.
	 */
	if (fCamDevice) {
		uint32 width = 0, height = 0;
		if (fCamDevice->SuggestVideoFrame(width, height) == B_OK) {
			fOutput.format.u.raw_video.display.line_width = width;
			fOutput.format.u.raw_video.display.line_count = height;
			// FIX: Set bytes_per_row to avoid "bytes_per_row=0" warning
			// B_RGB32 format uses 4 bytes per pixel
			fOutput.format.u.raw_video.display.bytes_per_row = width * 4;
			// Also update fConnectedFormat so "Current Format" queries work
			fConnectedFormat.display.line_width = width;
			fConnectedFormat.display.line_count = height;
			fConnectedFormat.display.bytes_per_row = width * 4;
			syslog(LOG_INFO, "Producer: NodeRegistered - video dimensions set to %ux%u, bpr=%u\n",
				width, height, width * 4);
		} else {
			// Fallback to a common resolution if SuggestVideoFrame fails
			fOutput.format.u.raw_video.display.line_width = 320;
			fOutput.format.u.raw_video.display.line_count = 240;
			fOutput.format.u.raw_video.display.bytes_per_row = 320 * 4;
			fConnectedFormat.display.line_width = 320;
			fConnectedFormat.display.line_count = 240;
			fConnectedFormat.display.bytes_per_row = 320 * 4;
			syslog(LOG_WARNING, "Producer: NodeRegistered - SuggestVideoFrame failed, using 320x240\n");
		}
	}

	/* Start the BMediaEventLooper control loop */
	syslog(LOG_INFO, "Producer: NodeRegistered - calling Run()\n");
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
	syslog(LOG_INFO, "Producer: NodeRegistered COMPLETE - Run() returned\n");
}


void
VideoProducer::Start(bigtime_t performance_time)
{
	syslog(LOG_INFO, "Producer: Start(%lld) called\n", performance_time);
	BMediaEventLooper::Start(performance_time);
}


void
VideoProducer::Stop(bigtime_t performance_time, bool immediate)
{
	PRINTF(1, ("VideoProducer::Stop(%" B_PRIdBIGTIME ", %s)\n",
		performance_time, immediate ? "immediate" : "not immediate"));
	BMediaEventLooper::Stop(performance_time, immediate);
}


void
VideoProducer::Seek(bigtime_t media_time, bigtime_t performance_time)
{
	// CRITICAL FIX: Clamp performance_time to prevent TimeSource overflow
	bigtime_t now = system_time();
	bigtime_t maxSafe = now + 10000000LL;

	if (performance_time > maxSafe || performance_time < 0) {
	fprintf(stderr, "VideoProducer::Seek: CLAMPING performance_time from %" B_PRIdBIGTIME " to %" B_PRIdBIGTIME "\n",
			performance_time, now);
		performance_time = now;
	}

	BMediaEventLooper::Seek(media_time, performance_time);
}


void
VideoProducer::TimeWarp(bigtime_t at_real_time, bigtime_t to_performance_time)
{
	// CRITICAL FIX: Clamp times to prevent TimeSource overflow
	bigtime_t now = system_time();
	bigtime_t maxSafe = now + 10000000LL;

	if (at_real_time > maxSafe || at_real_time < 0) {
	fprintf(stderr, "VideoProducer::TimeWarp: CLAMPING at_real_time from %" B_PRIdBIGTIME " to %" B_PRIdBIGTIME "\n",
			at_real_time, now);
		at_real_time = now;
	}

	if (to_performance_time > maxSafe || to_performance_time < 0) {
	fprintf(stderr, "VideoProducer::TimeWarp: CLAMPING to_performance_time from %" B_PRIdBIGTIME " to %" B_PRIdBIGTIME "\n",
			to_performance_time, now);
		to_performance_time = now;
	}

	BMediaEventLooper::TimeWarp(at_real_time, to_performance_time);
}


status_t
VideoProducer::AddTimer(bigtime_t at_performance_time, int32 cookie)
{
	return BMediaEventLooper::AddTimer(at_performance_time, cookie);
}


void
VideoProducer::SetRunMode(run_mode mode)
{
	// Allow mode changes - we're using our own TimeSource now so it should be safe
	fprintf(stderr, "VideoProducer::SetRunMode: Setting mode to %d\n", mode);
	BMediaEventLooper::SetRunMode(mode);
}


void
VideoProducer::HandleEvent(const media_timed_event *event,
		bigtime_t lateness, bool realTimeEvent)
{
	TOUCH(lateness); TOUCH(realTimeEvent);

	syslog(LOG_INFO, "Producer: HandleEvent type=%d lateness=%lld realTime=%d\n",
		event->type, lateness, realTimeEvent);

	switch(event->type) {
		case BTimedEventQueue::B_START:
			syslog(LOG_INFO, "Producer: HandleEvent - B_START event received!\n");
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
		case BTimedEventQueue::B_HANDLE_BUFFER:
		case BTimedEventQueue::B_DATA_STATUS:
		case BTimedEventQueue::B_PARAMETER:
		default:
			PRINTF(-1, ("HandleEvent: Unhandled event -- %" B_PRIx32 "\n",
				event->type));
			break;
	}
}


void
VideoProducer::CleanUpEvent(const media_timed_event *event)
{
	BMediaEventLooper::CleanUpEvent(event);
}


bigtime_t
VideoProducer::OfflineTime()
{
	return BMediaEventLooper::OfflineTime();
}


status_t
VideoProducer::DeleteHook(BMediaNode * node)
{
	return BMediaEventLooper::DeleteHook(node);
}


/* BBufferProducer */


status_t
VideoProducer::FormatSuggestionRequested(
		media_type type, int32 quality, media_format *format)
{
	if (type != B_MEDIA_RAW_VIDEO)
		return B_MEDIA_BAD_FORMAT;

	TOUCH(quality);

	PRINTF(1, ("FormatSuggestionRequested() %" B_PRIu32 "x%" B_PRIu32 "\n", \
			format->u.raw_video.display.line_width, \
			format->u.raw_video.display.line_count));

	*format = fOutput.format;
	uint32 width, height;
	if (fCamDevice && fCamDevice->SuggestVideoFrame(width, height) == B_OK) {
		format->u.raw_video.display.line_width = width;
		format->u.raw_video.display.line_count = height;
	}
	format->u.raw_video.field_rate = FIELD_RATE;
	return B_OK;
}


status_t
VideoProducer::FormatProposal(const media_source &output, media_format *format)
{
	status_t err;

	fprintf(stderr, "\n=== FormatProposal START ===\n");
	fprintf(stderr, "Output source: port=%d id=%d\n", output.port, output.id);

	if (!format) {
	fprintf(stderr, "ERROR: format is NULL\n");
	fprintf(stderr, "=== FormatProposal END (B_BAD_VALUE) ===\n\n");
		return B_BAD_VALUE;
	}

	fprintf(stderr, "Consumer requests:\n");
	fprintf(stderr, "  Format type: %d\n", format->type);
	fprintf(stderr, "  Color space: 0x%08x\n", format->u.raw_video.display.format);
	fprintf(stderr, "  Width: %u\n", format->u.raw_video.display.line_width);
	fprintf(stderr, "  Height: %u\n", format->u.raw_video.display.line_count);
	fprintf(stderr, "  Field rate: %.2f fps\n", format->u.raw_video.field_rate);

	if (output != fOutput.source) {
	fprintf(stderr, "ERROR: Bad source - expected port=%d id=%d\n",
				fOutput.source.port, fOutput.source.id);
	fprintf(stderr, "=== FormatProposal END (B_MEDIA_BAD_SOURCE) ===\n\n");
		return B_MEDIA_BAD_SOURCE;
	}

	PRINTF(1, ("FormatProposal() %" B_PRIu32 "x%" B_PRIu32 "\n", \
			format->u.raw_video.display.line_width, \
			format->u.raw_video.display.line_count));

	/* FIX: More flexible format negotiation
	 * Instead of using strict format_is_compatible() which fails on resolution mismatch,
	 * we check only the essential format properties (type and colorspace) and let
	 * AcceptVideoFrame() handle resolution validation. This allows consumers to request
	 * any supported resolution without knowing the driver's default resolution in advance.
	 */
	uint32 width = format->u.raw_video.display.line_width;
	uint32 height = format->u.raw_video.display.line_count;

	// Check basic format compatibility (type and colorspace only)
	bool basicCompatible = true;
	if (format->type != B_MEDIA_RAW_VIDEO && format->type != B_MEDIA_UNKNOWN_TYPE)
		basicCompatible = false;
	if (format->u.raw_video.display.format != 0 &&
		format->u.raw_video.display.format != B_RGB32 &&
		format->u.raw_video.display.format != B_RGB32_BIG)
		basicCompatible = false;

	err = basicCompatible ? B_OK : B_MEDIA_BAD_FORMAT;

	fprintf(stderr, "Format compatibility check: %s\n",
			err == B_OK ? "OK" : "BAD_FORMAT");

	// Copy our output format as base, then adjust resolution
	*format = fOutput.format;

	if (err == B_OK && fCamDevice) {
	fprintf(stderr, "Calling AcceptVideoFrame(%u, %u)\n", width, height);
		err = fCamDevice->AcceptVideoFrame(width, height);
	fprintf(stderr, "AcceptVideoFrame result: %s\n", strerror(err));
		if (err >= B_OK) {
			format->u.raw_video.display.line_width = width;
			format->u.raw_video.display.line_count = height;
			format->u.raw_video.display.bytes_per_row = width * 4;

			/* FIX: Update fOutput.format to match the accepted resolution.
			 * Without this, PrepareToConnect's format_is_compatible() check
			 * fails because fOutput.format contains the initial resolution
			 * from NodeRegistered, not the consumer-requested resolution
			 * that was just accepted by AcceptVideoFrame().
			 */
			fOutput.format.u.raw_video.display.line_width = width;
			fOutput.format.u.raw_video.display.line_count = height;
			fOutput.format.u.raw_video.display.bytes_per_row = width * 4;
			fprintf(stderr, "Updated fOutput.format to %ux%u\n", width, height);
		}
	}

	PRINTF(1, ("FormatProposal: %" B_PRIu32 "x%" B_PRIu32 "\n", \
			format->u.raw_video.display.line_width, \
			format->u.raw_video.display.line_count));

	fprintf(stderr, "Producer responds:\n");
	fprintf(stderr, "  Color space: 0x%08x (B_RGB32)\n", format->u.raw_video.display.format);
	fprintf(stderr, "  Width: %u\n", format->u.raw_video.display.line_width);
	fprintf(stderr, "  Height: %u\n", format->u.raw_video.display.line_count);
	fprintf(stderr, "  Result: %s (%d)\n", strerror(err), err);
	fprintf(stderr, "=== FormatProposal END ===\n\n");

	return err;

}


status_t
VideoProducer::FormatChangeRequested(const media_source &source,
		const media_destination &destination, media_format *io_format,
		int32 *_deprecated_)
{
	TOUCH(destination); TOUCH(io_format); TOUCH(_deprecated_);
	if (source != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	return B_ERROR;
}


status_t
VideoProducer::GetNextOutput(int32 *cookie, media_output *out_output)
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
VideoProducer::DisposeOutputCookie(int32 cookie)
{
	TOUCH(cookie);

	return B_OK;
}


status_t
VideoProducer::SetBufferGroup(const media_source &for_source,
		BBufferGroup *group)
{
	TOUCH(for_source); TOUCH(group);

	return B_ERROR;
}


status_t
VideoProducer::VideoClippingChanged(const media_source &for_source,
		int16 num_shorts, int16 *clip_data,
		const media_video_display_info &display, int32 *_deprecated_)
{
	TOUCH(for_source); TOUCH(num_shorts); TOUCH(clip_data);
	TOUCH(display); TOUCH(_deprecated_);

	return B_ERROR;
}


status_t
VideoProducer::GetLatency(bigtime_t *out_latency)
{
	*out_latency = EventLatency() + SchedulingLatency();
	return B_OK;
}


status_t
VideoProducer::PrepareToConnect(const media_source &source,
		const media_destination &destination, media_format *format,
		media_source *out_source, char *out_name)
{
	status_t err;

	fprintf(stderr, "\n=== PrepareToConnect START ===\n");
	fprintf(stderr, "Source: port=%d id=%d\n", source.port, source.id);
	fprintf(stderr, "Destination: port=%d id=%d\n", destination.port, destination.id);
	fprintf(stderr, "Format requested: 0x%08x %ux%u\n",
			format->u.raw_video.display.format,
			format->u.raw_video.display.line_width,
			format->u.raw_video.display.line_count);

	PRINTF(1, ("PrepareToConnect() %" B_PRIu32 "x%" B_PRIu32 "\n", \
			format->u.raw_video.display.line_width, \
			format->u.raw_video.display.line_count));

	if (fConnected) {
		PRINTF(0, ("PrepareToConnect: Already connected\n"));
	fprintf(stderr, "ERROR: Already connected\n");
	fprintf(stderr, "=== PrepareToConnect END (EALREADY) ===\n\n");
		return EALREADY;
	}

	if (source != fOutput.source) {
	fprintf(stderr, "ERROR: Bad source\n");
	fprintf(stderr, "=== PrepareToConnect END (B_MEDIA_BAD_SOURCE) ===\n\n");
		return B_MEDIA_BAD_SOURCE;
	}

	if (fOutput.destination != media_destination::null) {
	fprintf(stderr, "ERROR: Already connected to another destination\n");
	fprintf(stderr, "=== PrepareToConnect END (B_MEDIA_ALREADY_CONNECTED) ===\n\n");
		return B_MEDIA_ALREADY_CONNECTED;
	}

	/* The format parameter comes in with the suggested format, and may be
	 * specialized as desired by the node */
	if (!format_is_compatible(*format, fOutput.format)) {
	fprintf(stderr, "ERROR: Format not compatible\n");
		*format = fOutput.format;
	fprintf(stderr, "=== PrepareToConnect END (B_MEDIA_BAD_FORMAT) ===\n\n");
		return B_MEDIA_BAD_FORMAT;
	}
	fprintf(stderr, "Format is compatible\n");

#ifdef FORCE_320_240
	{
		format->u.raw_video.display.line_width = 320;
		format->u.raw_video.display.line_count = 240;
	}
#endif
#ifdef FORCE_160_120
	{
		format->u.raw_video.display.line_width = 160;
		format->u.raw_video.display.line_count = 120;
	}
#endif
#ifdef FORCE_MAX_FRAME
	{
		format->u.raw_video.display.line_width = 0;
		format->u.raw_video.display.line_count = 0;
	}
#endif
	if (fCamDevice) {
	fprintf(stderr, "Calling AcceptVideoFrame(%u, %u)\n",
				format->u.raw_video.display.line_width,
				format->u.raw_video.display.line_count);
		err = fCamDevice->AcceptVideoFrame(
			format->u.raw_video.display.line_width,
			format->u.raw_video.display.line_count);
	fprintf(stderr, "AcceptVideoFrame result: %s (%d)\n", strerror(err), err);
		if (err < B_OK) {
	fprintf(stderr, "ERROR: AcceptVideoFrame failed\n");
	fprintf(stderr, "=== PrepareToConnect END (AcceptVideoFrame error) ===\n\n");
			return err;
		}
	}

	if (format->u.raw_video.field_rate == 0)
		format->u.raw_video.field_rate = FIELD_RATE;

	// CRITICAL FIX: Ensure bytes_per_row is set (needed for buffer allocation)
	if (format->u.raw_video.display.bytes_per_row == 0) {
		// Calculate based on colorspace and width
		uint32 bytesPerPixel = 4;  // Default to B_RGB32
		if (format->u.raw_video.display.format == B_YCbCr422)
			bytesPerPixel = 2;
		format->u.raw_video.display.bytes_per_row =
			format->u.raw_video.display.line_width * bytesPerPixel;
	}

	// CRITICAL FIX: Save the negotiated format in fOutput.format
	// The Media Kit's Connect() callback may receive a zeroed format, so we need
	// to store it here and use it in Connect() as a fallback.
	fOutput.format = *format;

	*out_source = fOutput.source;
	/* FIX: Use strlcpy to prevent buffer overflow */
	strlcpy(out_name, fOutput.name, B_MEDIA_NAME_LENGTH);

	fOutput.destination = destination;

	fprintf(stderr, "PrepareToConnect successful:\n");
	fprintf(stderr, "  Final format: %ux%u @ %.2f fps, bytes_per_row=%u\n",
			format->u.raw_video.display.line_width,
			format->u.raw_video.display.line_count,
			format->u.raw_video.field_rate,
			format->u.raw_video.display.bytes_per_row);
	fprintf(stderr, "  Saved to fOutput.format for Connect() fallback\n");
	fprintf(stderr, "=== PrepareToConnect END (B_OK) ===\n\n");

	return B_OK;
}


void
VideoProducer::Connect(status_t error, const media_source &source,
		const media_destination &destination, const media_format &format,
		char *io_name)
{
	syslog(LOG_INFO, "Producer: Connect called! error=%d format=%ux%u\n",
		error, format.u.raw_video.display.line_width, format.u.raw_video.display.line_count);
	fprintf(stderr, "\n=== Connect START ===\n");
	fprintf(stderr, "Error status: %s (%d)\n", strerror(error), error);
	fprintf(stderr, "Source: port=%d id=%d\n", source.port, source.id);
	fprintf(stderr, "Destination: port=%d id=%d\n", destination.port, destination.id);
	fprintf(stderr, "Format: 0x%08x %ux%u @ %.2f fps\n",
				format.u.raw_video.display.format,
				format.u.raw_video.display.line_width,
				format.u.raw_video.display.line_count,
				format.u.raw_video.field_rate);

	PRINTF(1, ("Connect() %" B_PRIu32 "x%" B_PRIu32 "\n", \
			format.u.raw_video.display.line_width, \
			format.u.raw_video.display.line_count));

	if (fConnected) {
		PRINTF(0, ("Connect: Already connected\n"));
	fprintf(stderr, "ERROR: Already connected\n");
	fprintf(stderr, "=== Connect END (already connected) ===\n\n");
		return;
	}

	if (!fCamDevice) {
		PRINTF(0, ("Connect: No camera device\n"));
	fprintf(stderr, "ERROR: No camera device\n");
	fprintf(stderr, "=== Connect END (no device) ===\n\n");
		return;
	}

	BAutolock lock(fCamDevice->Locker());
	if (!fCamDevice->IsPlugged()) {
		PRINTF(0, ("Connect: Device unplugged\n"));
	fprintf(stderr, "ERROR: Device unplugged\n");
	fprintf(stderr, "=== Connect END (unplugged) ===\n\n");
		return;
	}

	if (source != fOutput.source || error < B_OK
		|| !const_cast<media_format *>(&format)->Matches(&fOutput.format)) {
		PRINTF(1, ("Connect: Connect error\n"));
	fprintf(stderr, "ERROR: Source mismatch or error status or format mismatch\n");
	fprintf(stderr, "  source match: %s\n", source == fOutput.source ? "YES" : "NO");
	fprintf(stderr, "  error < B_OK: %s\n", error < B_OK ? "YES" : "NO");
	fprintf(stderr, "=== Connect END (validation failed) ===\n\n");
		return;
	}

	fOutput.destination = destination;
	/* FIX: Use strlcpy to prevent buffer overflow */
	strlcpy(io_name, fOutput.name, B_MEDIA_NAME_LENGTH);

	if (fOutput.format.u.raw_video.field_rate != 0.0f) {
		// Initialize timing base - will be reset properly in HandleStart()
		fPerformanceTimeBase = 0;
		fStartRealTime = system_time();
		fFrameBase = fFrame;
	}

	// CRITICAL FIX: Use fOutput.format (saved in PrepareToConnect) when the
	// incoming format has invalid dimensions. The Media Kit sometimes passes
	// a zeroed format to Connect() even after successful PrepareToConnect().
	if (format.u.raw_video.display.line_width == 0 ||
		format.u.raw_video.display.line_count == 0) {
		fprintf(stderr, "WARNING: Connect received zeroed format, using fOutput.format fallback\n");
		fprintf(stderr, "  fOutput.format: %ux%u @ %.2f fps\n",
			fOutput.format.u.raw_video.display.line_width,
			fOutput.format.u.raw_video.display.line_count,
			fOutput.format.u.raw_video.field_rate);
		fConnectedFormat = fOutput.format.u.raw_video;
	} else {
		fConnectedFormat = format.u.raw_video;
	}

	/* get the latency */
	bigtime_t latency = 0;
	media_node_id tsID = 0;
	FindLatencyFor(fOutput.destination, &latency, &tsID);
	#define NODE_LATENCY 1000
	SetEventLatency(latency + NODE_LATENCY);

	uint32 *buffer, *p, f = 3;
	p = buffer = (uint32 *)malloc(4 * fConnectedFormat.display.line_count *
			fConnectedFormat.display.line_width);
	if (!buffer) {
		PRINTF(0, ("Connect: Out of memory\n"));
		return;
	}
	bigtime_t now = system_time();
	for (uint32 y=0;y<fConnectedFormat.display.line_count;y++)
		for (uint32 x=0;x<fConnectedFormat.display.line_width;x++)
			*(p++) = ((((x+y)^0^x)+f) & 0xff) * (0x01010101 & fColor);
	fProcessingLatency = system_time() - now;
	free(buffer);

	/* Create the buffer group */
	size_t bufferSize = 4 * fConnectedFormat.display.line_width *
			fConnectedFormat.display.line_count;
	fprintf(stderr, "Creating buffer group: size=%zu count=8\n", bufferSize);
	fBufferGroup = new BBufferGroup(bufferSize, 8);
	if (fBufferGroup->InitCheck() < B_OK) {
	fprintf(stderr, "ERROR: BufferGroup InitCheck failed: %s\n",
					strerror(fBufferGroup->InitCheck()));
		delete fBufferGroup;
		fBufferGroup = NULL;
	fprintf(stderr, "=== Connect END (buffer group failed) ===\n\n");
		return;
	}
	fprintf(stderr, "BufferGroup created successfully\n");

	fConnected = true;
	fEnabled = true;

	syslog(LOG_INFO, "Producer: Connect SUCCESS! fConnected=true fEnabled=true bufferGroup=%p\n", fBufferGroup);
	fprintf(stderr, "Connection established successfully!\n");
	fprintf(stderr, "  fConnected: %s\n", fConnected ? "TRUE" : "FALSE");
	fprintf(stderr, "  fEnabled: %s\n", fEnabled ? "TRUE" : "FALSE");
	fprintf(stderr, "  Buffer group: %p\n", fBufferGroup);
	fprintf(stderr, "=== Connect END (SUCCESS) ===\n\n");

	/* Tell frame generation thread to recalculate delay value */
	release_sem(fFrameSync);
}

void
VideoProducer::Disconnect(const media_source &source,
		const media_destination &destination)
{
	PRINTF(1, ("Disconnect()\n"));

	if (!fConnected) {
		PRINTF(0, ("Disconnect: Not connected\n"));
		return;
	}

	if ((source != fOutput.source) || (destination != fOutput.destination)) {
		PRINTF(0, ("Disconnect: Bad source and/or destination\n"));
		return;
	}

#if 1
	/* Some dumb apps don't stop nodes before disconnecting... */
	if (fRunning)
		HandleStop();
#endif

	fEnabled = false;
	fOutput.destination = media_destination::null;

	fLock.Lock();
		delete fBufferGroup;
		fBufferGroup = NULL;
	fLock.Unlock();

	fConnected = false;
}


void
VideoProducer::LateNoticeReceived(const media_source &source,
		bigtime_t how_much, bigtime_t performance_time)
{
	TOUCH(source); TOUCH(how_much); TOUCH(performance_time);
}


void
VideoProducer::EnableOutput(const media_source &source, bool enabled,
		int32 *_deprecated_)
{
	TOUCH(_deprecated_);

	if (source != fOutput.source)
		return;

	fEnabled = enabled;
}


status_t
VideoProducer::SetPlayRate(int32 numer, int32 denom)
{
	TOUCH(numer); TOUCH(denom);

	return B_ERROR;
}


void
VideoProducer::AdditionalBufferRequested(const media_source &source,
		media_buffer_id prev_buffer, bigtime_t prev_time,
		const media_seek_tag *prev_tag)
{
	TOUCH(source); TOUCH(prev_buffer); TOUCH(prev_time); TOUCH(prev_tag);
}


void
VideoProducer::LatencyChanged(const media_source &source,
		const media_destination &destination, bigtime_t new_latency,
		uint32 flags)
{
	TOUCH(source); TOUCH(destination); TOUCH(new_latency); TOUCH(flags);
}


/* BControllable */


status_t
VideoProducer::GetParameterValue(
	int32 id, bigtime_t *last_change, void *value, size_t *size)
{
	status_t err;

	switch (id) {
		case P_COLOR:
			//return B_BAD_VALUE;

			*last_change = fLastColorChange;
			*size = sizeof(uint32);
			*((uint32 *)value) = fColor;
			return B_OK;
		case P_INFO:
			if (*size < (size_t)(fInfoString.Length() + 1))
				return EINVAL;
			*last_change = fLastColorChange;
			*size = fInfoString.Length() + 1;
			memcpy(value, fInfoString.String(), *size);
			return B_OK;
	}

	if (fCamDevice) {
		BAutolock lock(fCamDevice->Locker());
		err = fCamDevice->GetParameterValue(id, last_change, value, size);
		if (err >= B_OK)
			return err;
		if (fCamDevice->Sensor()) {
			err = fCamDevice->Sensor()->GetParameterValue(id, last_change, value, size);
			if (err >= B_OK)
				return err;
		}
	}

	return B_BAD_VALUE;
}


void
VideoProducer::SetParameterValue(
	int32 id, bigtime_t when, const void *value, size_t size)
{
	status_t err = B_OK;

	switch (id) {
		case P_COLOR:
			if (!value || (size != sizeof(uint32)))
				return;

			if (*(uint32 *)value == fColor)
				return;

			fColor = *(uint32 *)value;
			fLastColorChange = when;
			break;
		case P_INFO:
			// forbidden
			return;
		default:
			if (fCamDevice == NULL)
				return;

			BAutolock lock(fCamDevice->Locker());
			err = fCamDevice->SetParameterValue(id, when, value, size);
			if ((err < B_OK) && (fCamDevice->Sensor())) {
				err = fCamDevice->Sensor()->SetParameterValue(id, when, value, size);
			}

			/* FIX BUG 10: Aggiorna fOutput.format quando cambia la risoluzione.
			 * Senza questo fix, SetParameterValue aggiornava solo lo stato interno
			 * del device (fVideoFrame) ma non fOutput.format del Producer.
			 * Questo causava il fallimento di FormatProposal() perché
			 * format_is_compatible() confrontava la nuova risoluzione richiesta
			 * con la vecchia fOutput.format ancora impostata alla risoluzione iniziale.
			 */
			if (err >= B_OK) {
				BRect frame = fCamDevice->VideoFrame();
				uint32 newWidth = (uint32)(frame.Width() + 1);
				uint32 newHeight = (uint32)(frame.Height() + 1);

				/* Aggiorna solo se le dimensioni sono effettivamente cambiate */
				if (fOutput.format.u.raw_video.display.line_width != newWidth ||
				    fOutput.format.u.raw_video.display.line_count != newHeight) {

					fOutput.format.u.raw_video.display.line_width = newWidth;
					fOutput.format.u.raw_video.display.line_count = newHeight;
					fConnectedFormat.display.line_width = newWidth;
					fConnectedFormat.display.line_count = newHeight;

					syslog(LOG_INFO, "Producer: fOutput.format updated to %ux%u\n",
						newWidth, newHeight);

					/* FIX: Recreate buffer group for new resolution.
					 * The old buffer group has buffers sized for the old resolution.
					 * We need new buffers sized for the new resolution.
					 */
					if (fConnected && fBufferGroup != NULL) {
						size_t newBufferSize = 4 * newWidth * newHeight;
						syslog(LOG_INFO, "Producer: Recreating buffer group for new size %zu bytes\n",
							newBufferSize);

						/* Delete old buffer group */
						delete fBufferGroup;
						fBufferGroup = NULL;

						/* Create new buffer group with proper size */
						fBufferGroup = new BBufferGroup(newBufferSize, 8);
						if (fBufferGroup->InitCheck() < B_OK) {
							syslog(LOG_ERR, "Producer: Failed to recreate buffer group: %s\n",
								strerror(fBufferGroup->InitCheck()));
							delete fBufferGroup;
							fBufferGroup = NULL;
						} else {
							syslog(LOG_INFO, "Producer: Buffer group recreated successfully\n");
						}
					}
				}
			}
	}

	if (err >= B_OK)
		BroadcastNewParameterValue(when, id, (void *)value, size);
}


status_t
VideoProducer::StartControlPanel(BMessenger *out_messenger)
{
	return BControllable::StartControlPanel(out_messenger);
}


/* VideoProducer */


void
VideoProducer::HandleStart(bigtime_t performance_time)
{
	/* Start producing frames, even if the output hasn't been connected yet. */
	syslog(LOG_INFO, "Producer: HandleStart called! perf_time=%lld running=%d connected=%d enabled=%d device=%p\n",
		performance_time, fRunning, fConnected, fEnabled, fCamDevice);

	if (fRunning) {
		syslog(LOG_INFO, "Producer: HandleStart - already running, return\n");
		return;
	}

	if (!fCamDevice) {
		syslog(LOG_ERR, "Producer: HandleStart - NO CAMERA DEVICE!\n");
		return;
	}

	fFrame = 0;
	fFrameBase = 0;
	// Store the performance time when we start - this is used as base for buffer timestamps
	// Use the passed performance_time which comes from the media kit
	fPerformanceTimeBase = performance_time;
	fStartRealTime = system_time();  // Track real time at start for proper offset calculation

	fFrameSync = create_sem(0, "frame synchronization");
	if (fFrameSync < B_OK) {
		syslog(LOG_ERR, "Producer: HandleStart - create_sem failed: %d\n", fFrameSync);
		return;
	}
	syslog(LOG_INFO, "Producer: HandleStart - sem created: %d\n", fFrameSync);

	// CRITICAL: Set fRunning BEFORE spawning thread to avoid race condition
	// The thread checks fRunning in its loop condition
	fRunning = true;

	fThread = spawn_thread(_frame_generator_, "frame generator", B_NORMAL_PRIORITY, this);
	if (fThread < B_OK) {
		syslog(LOG_ERR, "Producer: HandleStart - spawn_thread failed: %d\n", fThread);
		fRunning = false;
		delete_sem(fFrameSync);
		return;
	}
	syslog(LOG_INFO, "Producer: HandleStart - thread spawned: %d\n", fThread);

	if (resume_thread(fThread) < B_OK) {
		syslog(LOG_ERR, "Producer: HandleStart - resume_thread failed\n");
		fRunning = false;
		kill_thread(fThread);
		delete_sem(fFrameSync);
		return;
	}
	syslog(LOG_INFO, "Producer: HandleStart - thread resumed\n");

	{
		BAutolock lock(fCamDevice->Locker());
		fCamDevice->StartTransfer();
	}
	syslog(LOG_INFO, "Producer: HandleStart COMPLETE! fRunning=true\n");
}


void
VideoProducer::HandleStop(void)
{
	syslog(LOG_INFO, "Producer: HandleStop called, fRunning=%d\n", fRunning);

	if (!fRunning) {
		syslog(LOG_INFO, "Producer: HandleStop - not running, return\n");
		return;
	}

	// CRITICAL: Set fRunning=false BEFORE deleting sem
	// The FrameGenerator thread checks this flag in its loop
	fRunning = false;

	// Now delete the semaphore - thread will see fRunning=false and exit
	delete_sem(fFrameSync);
	fFrameSync = -1;  // Mark as invalid

	// Wait for thread with 5 second timeout
	status_t threadStatus;
	status_t waitResult = wait_for_thread_etc(fThread, B_RELATIVE_TIMEOUT,
		5000000, &threadStatus); // 5 seconds
	if (waitResult == B_TIMED_OUT) {
		syslog(LOG_WARNING, "Producer: HandleStop - thread timeout, killing\n");
		kill_thread(fThread);
	} else if (waitResult != B_OK) {
		syslog(LOG_ERR, "Producer: HandleStop - wait failed: %s\n", strerror(waitResult));
	} else {
		syslog(LOG_INFO, "Producer: HandleStop - thread exited cleanly\n");
	}

	if (fCamDevice) {
		BAutolock lock(fCamDevice->Locker());
		fCamDevice->StopTransfer();
	}

	syslog(LOG_INFO, "Producer: HandleStop COMPLETE\n");
}


void
VideoProducer::HandleTimeWarp(bigtime_t performance_time)
{
	// Reset timing base to the new performance time
	fPerformanceTimeBase = performance_time;
	fStartRealTime = system_time();
	fFrameBase = fFrame;

	/* Tell frame generation thread to recalculate delay value */
	release_sem(fFrameSync);
}


void
VideoProducer::HandleSeek(bigtime_t performance_time)
{
	// Reset timing base to the new performance time
	fPerformanceTimeBase = performance_time;
	fStartRealTime = system_time();
	fFrameBase = fFrame;

	/* Tell frame generation thread to recalculate delay value */
	release_sem(fFrameSync);
}


void
VideoProducer::_UpdateStats()
{
	float fps = (fStats[0].frames - fStats[1].frames) * 1000000LL
				/ (double)(fStats[0].stamp - fStats[1].stamp);
	float rfps = (fStats[0].actual - fStats[1].actual) * 1000000LL
				/ (double)(fStats[0].stamp - fStats[1].stamp);
	fInfoString = "FPS: ";
	fInfoString << fps << " virt, "
		<< rfps << " real, missed: " << fStats[0].missed;
	memcpy(&fStats[1], &fStats[0], sizeof(fStats[0]));
	fLastColorChange = system_time();
	BroadcastNewParameterValue(fLastColorChange, P_INFO,
		(void *)fInfoString.String(), fInfoString.Length()+1);
}


/* The following functions form the thread that generates frames. You should
 * replace this with the code that interfaces to your hardware. */
int32
VideoProducer::FrameGenerator()
{
	syslog(LOG_INFO, "Producer: FrameGenerator STARTED! connected=%d enabled=%d\n", fConnected, fEnabled);

	bigtime_t wait_until = system_time();
	int frameLog = 0;  // Log first 10 frames (reset each time thread starts)

	while (fRunning) {
		status_t err = acquire_sem_etc(fFrameSync, 1, B_ABSOLUTE_TIMEOUT,
				wait_until);

		// Handle semaphore errors gracefully
		if (err == B_BAD_SEM_ID) {
			// Semaphore was deleted - check if we should stop
			if (!fRunning) {
				syslog(LOG_INFO, "Producer: FrameGenerator - sem deleted, stopping (fRunning=false)\n");
				break;
			}
			// Otherwise keep trying - might be a transient issue
			syslog(LOG_WARNING, "Producer: FrameGenerator - sem deleted but fRunning=true, retrying\n");
			snooze(10000);  // Wait 10ms before retry
			continue;
		}
		if (err == B_INTERRUPTED) {
			continue;  // Retry on signal
		}
		if ((err != B_OK) && (err != B_TIMED_OUT)) {
			syslog(LOG_WARNING, "Producer: FrameGenerator - sem error %d, continuing\n", err);
			snooze(10000);
			continue;  // Don't exit on transient errors
		}

		fFrame++;

		BTimeSource* timeSource = TimeSource();
		if (!timeSource) {
			syslog(LOG_ERR, "Producer: FrameGenerator - NO TIME SOURCE! Frame %u, exiting\n", fFrame);
			break;
		}

		bigtime_t frameDuration = (bigtime_t)(1000000 / fConnectedFormat.field_rate);
		// For real camera: don't drop "late" frames - process all available frames
		// The camera produces frames at its own rate, we should display them all
		wait_until = system_time() + frameDuration;

		// Only skip if semaphore was explicitly released (timing change signal)
		if (err == B_OK) {
			if (frameLog < 10) {
				syslog(LOG_INFO, "Producer: Frame %u: sem acquired (timing change), skip\n", fFrame);
				frameLog++;
			}
			continue;
		}

		if (!fRunning || !fEnabled) {
			if (frameLog < 10) {
				syslog(LOG_INFO, "Producer: Frame %u: not running/enabled (%d/%d)\n", fFrame, fRunning, fEnabled);
				frameLog++;
			}
			continue;
		}

		BAutolock _(fLock);

		if (!fBufferGroup) {
			if (frameLog < 10) {
				syslog(LOG_WARNING, "Producer: Frame %u: NO BUFFER GROUP!\n", fFrame);
				frameLog++;
			}
			continue;
		}

		/* Fetch a buffer from the buffer group */
		BBuffer *buffer = fBufferGroup->RequestBuffer(
						4 * fConnectedFormat.display.line_width *
						fConnectedFormat.display.line_count, 0LL);
		if (!buffer) {
			if (frameLog < 10) {
				syslog(LOG_WARNING, "Producer: Frame %u: RequestBuffer failed\n", fFrame);
				frameLog++;
			}
			continue;
		}

		/* Fill out the details about this buffer. */
		media_header *h = buffer->Header();
		h->type = B_MEDIA_RAW_VIDEO;
		h->time_source = TimeSource()->ID();
		h->size_used = 4 * fConnectedFormat.display.line_width *
						fConnectedFormat.display.line_count;
		/* For a buffer originating from a device, you might want to calculate
		 * this based on the PerformanceTimeFor the time your buffer arrived at
		 * the hardware (plus any applicable adjustments). */
		/*
		h->start_time = fPerformanceTimeBase +
						(bigtime_t)
							((fFrame - fFrameBase) *
							(1000000 / fConnectedFormat.field_rate));
		*/
		h->file_pos = 0;
		h->orig_size = 0;
		h->data_offset = 0;
		h->u.raw_video.field_gamma = 1.0;
		h->u.raw_video.field_sequence = fFrame;
		h->u.raw_video.field_number = 0;
		h->u.raw_video.pulldown_number = 0;
		h->u.raw_video.first_active_line = 1;
		h->u.raw_video.line_count = fConnectedFormat.display.line_count;

		// This is where we fill the video buffer.

		//NO! must be called without lock!
		//BAutolock lock(fCamDevice->Locker());

		bigtime_t now = system_time();
		bigtime_t stamp = 0;
		if (fCamDevice) {
			err = fCamDevice->FillFrameBuffer(buffer, &stamp);
			if (err < B_OK) {
				if (frameLog < 10) {
					syslog(LOG_WARNING, "Producer: FillFrameBuffer FAILED #%d: %s\n", frameLog, strerror(err));
					frameLog++;
				}
				fStats[0].missed++;
				buffer->Recycle();
				continue;
			}
			if (frameLog < 10) {
				syslog(LOG_INFO, "Producer: FillFrameBuffer OK #%d\n", frameLog);
				frameLog++;
			}
		} else {
			buffer->Recycle();
			continue;
		}
#ifdef UseGetFrameBitmap
		BBitmap *bm;
		err = fCamDevice->GetFrameBitmap(&bm, &stamp);
		if (err >= B_OK) {
			;//XXX handle error
			fStats[0].missed++;
		}
#endif
		fStats[0].frames = fFrame;
		fStats[0].actual++;;
		fStats[0].stamp = system_time();

		//PRINTF(1, ("FrameGenerator: stamp %lld vs %lld\n", stamp, h->start_time));
		// FIX: Use current performance time for live video
		// CodyCam drops frames when start_time=0, interpreting it as "too late"
		// Instead, calculate the proper performance time from the TimeSource
		{
			BTimeSource* ts = TimeSource();
			if (ts != NULL) {
				// Get current performance time - this tells the consumer
				// "display this frame at this performance time"
				h->start_time = ts->PerformanceTimeFor(system_time());
			} else {
				// Fallback: use fPerformanceTimeBase + elapsed time
				bigtime_t elapsed = system_time() - fStartRealTime;
				h->start_time = fPerformanceTimeBase + elapsed;
			}
		}
		fProcessingLatency = system_time() - now;
		fProcessingLatency /= 10;

		/* Send the buffer on down to the consumer */
		status_t sendErr = SendBuffer(buffer, fOutput.source, fOutput.destination);
		if (sendErr < B_OK) {
			if (frameLog < 10) {
				syslog(LOG_WARNING, "Producer: Frame %u: SendBuffer FAILED: %s\n", fFrame, strerror(sendErr));
				frameLog++;
			}
			buffer->Recycle();
		} else {
			if (frameLog < 10) {
				syslog(LOG_INFO, "Producer: Frame %u: SendBuffer OK!\n", fFrame);
				frameLog++;
			}
		}

		_UpdateStats();
	}

	PRINTF(1, ("FrameGenerator: thread existed.\n"));
	return B_OK;
}


int32
VideoProducer::_frame_generator_(void *data)
{
	return ((VideoProducer *)data)->FrameGenerator();
}
