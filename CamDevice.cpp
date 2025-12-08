/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */


#include "CamDevice.h"
#include "CamSensor.h"
#include "CamDeframer.h"
#include "CamDebug.h"
#include "AddOn.h"

#include <OS.h>
#include <Autolock.h>
#include <syslog.h>

/* PRODUCTION BUILD: Disable all file I/O to prevent BFS corruption */
/* CRITICAL: File I/O in USB threads causes kernel panics */
#define PRODUCTION_BUILD 1

#ifdef PRODUCTION_BUILD
#undef fopen
#define fopen(path, mode) ((FILE*)NULL)
#undef fclose
#define fclose(f) do {} while(0)
#undef fflush
#define fflush(f) do {} while(0)
#endif

//#define DEBUG_WRITE_DUMP
//#define DEBUG_DISCARD_DATA
//#define DEBUG_READ_DUMP
//#define DEBUG_DISCARD_INPUT

#undef B_WEBCAM_DECLARE_SENSOR
#define B_WEBCAM_DECLARE_SENSOR(sensorclass,sensorname) \
extern "C" CamSensor *Instantiate##sensorclass(CamDevice *cam);
#include "CamInternalSensors.h"
#undef B_WEBCAM_DECLARE_SENSOR
typedef CamSensor *(*SensorInstFunc)(CamDevice *cam);
struct { const char *name; SensorInstFunc instfunc; } kSensorTable[] = {
#define B_WEBCAM_DECLARE_SENSOR(sensorclass,sensorname) \
{ #sensorname, &Instantiate##sensorclass },
#include "CamInternalSensors.h"
{ NULL, NULL },
};
#undef B_WEBCAM_DECLARE_SENSOR


// PHASE 4: Packet loss monitoring thresholds
const float CamDevice::kPacketLossThreshold = 0.05f;	// 5% packet loss triggers fallback
const bigtime_t CamDevice::kStatsWindowSize = 5000000;	// 5 second window
const uint32 CamDevice::kMinPacketsForStats = 100;		// Min packets before calculating


CamDevice::CamDevice(CamDeviceAddon &_addon, BUSBDevice* _device)
	: fInitStatus(B_NO_INIT),
	  fSensor(NULL),
	  fDeframer(NULL),
	  fDataInput(NULL),
	  fBulkIn(NULL),
	  fIsoIn(NULL),
	  fIsoMaxPacketSize(0),
	  fLastParameterChanges(0),
	  fCamDeviceAddon(_addon),
	  fDevice(_device),
	  fSupportedDeviceIndex(-1),
	  fChipIsBigEndian(false),
	  fTransferEnabled(0), // Now int32 for atomic operations
	  fPumpThread(-1),
	  fLocker("WebcamDeviceLock"),
	  fPacketSuccessCount(0),
	  fPacketErrorCount(0),
	  fLastStatsReport(0),
	  fTransferStartTime(0),
	  fConsecutiveHighLossEvents(0),
	  fFirstTransferLogged(false),
	  fDroppedFramesLogged(0),
	  fLogThrottleCounter(0),
	  fLastLogTime(0)
{
	// Initialize error histogram
	fErrorHistogram.Reset();

	// Initialize frame timing statistics (Group 4)
	fFrameTimingStats.Reset();

	// Initialize error recovery configuration (Group 5)
	fErrorRecoveryConfig.Reset();

	// Initialize double buffer structure (actual allocation deferred)
	fDoubleBuffer.initialized = false;
	// fill in the generic flavor
	_addon.WebCamAddOn()->FillDefaultFlavorInfo(&fFlavorInfo);
	// if we use id matching, cache the index to the list
	if (fCamDeviceAddon.SupportedDevices()) {
		fSupportedDeviceIndex = fCamDeviceAddon.Sniff(_device);
	}

	// Build display name with priority:
	// 1. Table entry with specific VID/PID (most reliable names)
	// 2. USB descriptor strings (may be generic)
	// 3. Table entry with class-only match (fallback)
	const char* usbManufacturer = _device->ManufacturerString();
	const char* usbProduct = _device->ProductString();

	fFlavorInfoNameStr = "";
	fFlavorInfoInfoStr = "";

	bool hasSpecificVidPid = false;
	if (fCamDeviceAddon.SupportedDevices() && fSupportedDeviceIndex >= 0) {
		// Check if this is a specific VID/PID match (not a generic class match)
		const usb_webcam_support_descriptor& desc = fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex];
		hasSpecificVidPid = (desc.desc.vendor != 0 || desc.desc.product != 0);
	}

	if (hasSpecificVidPid) {
		// Use table names for known devices (more descriptive than USB strings)
		// FIX: Append "USB Camera" so apps filtering by keywords can find it
		fFlavorInfoNameStr << fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].vendor;
		fFlavorInfoNameStr << " " << fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].product;
		fFlavorInfoNameStr << " USB Camera";
	} else if (usbProduct != NULL && usbProduct[0] != '\0') {
		// Use USB descriptor strings for unknown devices
		if (usbManufacturer != NULL && usbManufacturer[0] != '\0') {
			fFlavorInfoNameStr << usbManufacturer << " " << usbProduct;
		} else {
			fFlavorInfoNameStr << usbProduct;
		}
		fFlavorInfoNameStr << " USB Camera";
	} else if (fCamDeviceAddon.SupportedDevices() && fSupportedDeviceIndex >= 0) {
		// Fallback to table (generic class match)
		fFlavorInfoNameStr << fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].vendor;
		fFlavorInfoNameStr << " " << fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].product;
		fFlavorInfoNameStr << " USB Camera";
	} else {
		// Last resort fallback
		fFlavorInfoNameStr << "USB Webcam";
	}

	fFlavorInfoInfoStr << fFlavorInfoNameStr;
	fFlavorInfo.name = fFlavorInfoNameStr.String();
	fFlavorInfo.info = fFlavorInfoInfoStr.String();
	// Initialize fDumpFD to prevent invalid close() in destructor
	fDumpFD = -1;
#ifdef DEBUG_WRITE_DUMP
	fDumpFD = open("/boot/home/webcam.out", O_CREAT|O_RDWR, 0644);
#endif
#ifdef DEBUG_READ_DUMP
	fDumpFD = open("/boot/home/webcam.out", O_RDONLY, 0644);
#endif
	// CRITICAL FIX: Buffer must be large enough for max isochronous transfer
	// USB 2.0 High-Speed isochronous: up to 3072 bytes/packet * 8 transactions/microframe
	// With 32 packet descriptors: 32 * 3072 = 98,304 bytes minimum
	// Using 128KB (32 pages) for safety margin
	fBufferLen = 32*B_PAGE_SIZE;
	fBuffer = (uint8 *)malloc(fBufferLen);
}


CamDevice::~CamDevice()
{
	// Ensure transfers are stopped before cleanup
	// (Unplugged() should have been called, but be safe)
	if (atomic_get(&fTransferEnabled) != 0) {
		atomic_set(&fTransferEnabled, 0);
		if (fPumpThread >= 0) {
			status_t result;
			wait_for_thread(fPumpThread, &result);
		}
	}

	// Cleanup double buffering resources
	CleanupDoubleBuffering();

	if (fDumpFD >= 0)
		close(fDumpFD);
	free(fBuffer);
	delete fDeframer;
	delete fSensor;
}


status_t
CamDevice::InitCheck()
{
	return fInitStatus;
}


bool
CamDevice::Matches(BUSBDevice* _device)
{
	return _device == fDevice;
}


BUSBDevice*
CamDevice::GetDevice()
{
	return fDevice;
}


void
CamDevice::Unplugged()
{
	// PHASE 2: Proper cleanup on device disconnect
	// Stop any running transfers first
	if (atomic_get(&fTransferEnabled) != 0) {
		// Signal the data pump thread to stop
		atomic_set(&fTransferEnabled, 0);

		// Wait for the thread to finish (with timeout)
		if (fPumpThread >= 0) {
			status_t threadResult;
			// Wait up to 2 seconds for clean shutdown
			bigtime_t deadline = system_time() + 2000000;
			while (system_time() < deadline) {
				status_t err = wait_for_thread(fPumpThread, &threadResult);
				if (err == B_OK || err == B_BAD_THREAD_ID)
					break;
				// Thread still running, wait a bit
				snooze(10000); // 10ms
			}
			fPumpThread = -1;
		}
	}

	// Stop sensor transfer if running
	if (fSensor != NULL)
		fSensor->StopTransfer();

	// Clear USB pointers (device is being removed)
	fDevice = NULL;
	fBulkIn = NULL;
	fIsoIn = NULL;
	fIsoMaxPacketSize = 0;
}


bool
CamDevice::IsPlugged()
{
	return (fDevice != NULL);
}


const char *
CamDevice::BrandName()
{
	if (fCamDeviceAddon.SupportedDevices() && (fSupportedDeviceIndex > -1))
		return fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].vendor;
	return "<unknown>";
}


const char *
CamDevice::ModelName()
{
	if (fCamDeviceAddon.SupportedDevices() && (fSupportedDeviceIndex > -1))
		return fCamDeviceAddon.SupportedDevices()[fSupportedDeviceIndex].product;
	return "<unknown>";
}


bool
CamDevice::SupportsBulk()
{
	return false;
}


bool
CamDevice::SupportsIsochronous()
{
	return false;
}


status_t
CamDevice::StartTransfer()
{
	status_t err = B_OK;
	PRINT((CH "()" CT));
	if (atomic_get(&fTransferEnabled))
		return EALREADY;
	// Use URGENT_DISPLAY_PRIORITY (120) for timely USB isochronous transfers
	// Lower priority causes scheduling delays that lead to missed USB frames
	fPumpThread = spawn_thread(_DataPumpThread, "USB Webcam Data Pump",
		B_URGENT_DISPLAY_PRIORITY, this);
	if (fPumpThread < B_OK)
		return fPumpThread;
	if (fSensor)
		err = fSensor->StartTransfer();
	if (err < B_OK)
		return err;
	atomic_set(&fTransferEnabled, 1);
	resume_thread(fPumpThread);
	PRINT((CH ": transfer enabled" CT));
	return B_OK;
}


status_t
CamDevice::StopTransfer()
{
	status_t err = B_OK;
	PRINT((CH "()" CT));
	if (!atomic_get(&fTransferEnabled))
		return EALREADY;
	if (fSensor)
		err = fSensor->StopTransfer();
	if (err < B_OK)
		return err;
	// Use atomic operation to safely signal thread to stop
	atomic_set(&fTransferEnabled, 0);

	// the thread itself might Lock(), so unlock before waiting
	// The atomic operation ensures thread-safe access to fTransferEnabled
	fLocker.Unlock();
	wait_for_thread(fPumpThread, &err);
	fLocker.Lock();

	return B_OK;
}


status_t
CamDevice::SuggestVideoFrame(uint32 &width, uint32 &height)
{
	if (Sensor()) {
		width = Sensor()->MaxWidth();
		height = Sensor()->MaxHeight();
		return B_OK;
	}
	return B_NO_INIT;
}


status_t
CamDevice::AcceptVideoFrame(uint32 &width, uint32 &height)
{
	status_t err = ENOSYS;
	if (Sensor())
		err = Sensor()->AcceptVideoFrame(width, height);
	if (err < B_OK)
		return err;
	SetVideoFrame(BRect(0, 0, width - 1, height - 1));
	return B_OK;
}


status_t
CamDevice::SetVideoFrame(BRect frame)
{
	fVideoFrame = frame;
	return B_OK;
}


status_t
CamDevice::SetScale(float scale)
{
	return B_OK;
}


status_t
CamDevice::SetVideoParams(float brightness, float contrast, float hue,
	float red, float green, float blue)
{
	return B_OK;
}


void
CamDevice::AddParameters(BParameterGroup *group, int32 &index)
{
	fFirstParameterID = index;
}


status_t
CamDevice::GetParameterValue(int32 id, bigtime_t *last_change, void *value,
	size_t *size)
{
	return B_BAD_VALUE;
}


status_t
CamDevice::SetParameterValue(int32 id, bigtime_t when, const void *value,
	size_t size)
{
	return B_BAD_VALUE;
}


size_t
CamDevice::MinRawFrameSize()
{
	return 0;
}


size_t
CamDevice::MaxRawFrameSize()
{
	return 0;
}


bool
CamDevice::ValidateStartOfFrameTag(const uint8 *tag, size_t taglen)
{
	return true;
}


bool
CamDevice::ValidateEndOfFrameTag(const uint8 *tag, size_t taglen,
	size_t datalen)
{
	return true;
}


// PHASE 4: Packet loss monitoring implementation

void
CamDevice::RecordPacketSuccess()
{
	fPacketSuccessCount++;
}


void
CamDevice::RecordPacketError()
{
	fPacketErrorCount++;
}


float
CamDevice::GetPacketLossRate() const
{
	uint32 total = fPacketSuccessCount + fPacketErrorCount;
	if (total < kMinPacketsForStats)
		return 0.0f;	// Not enough data yet

	return (float)fPacketErrorCount / (float)total;
}


bool
CamDevice::ShouldReduceResolution()
{
	// Check if we have enough data
	uint32 total = fPacketSuccessCount + fPacketErrorCount;
	if (total < kMinPacketsForStats)
		return false;

	// Check if loss rate exceeds threshold
	float lossRate = GetPacketLossRate();
	if (lossRate > kPacketLossThreshold) {
		fConsecutiveHighLossEvents++;

		// Only trigger fallback after sustained high loss (3+ consecutive checks)
		if (fConsecutiveHighLossEvents >= 3) {
			syslog(LOG_WARNING, "CamDevice: High packet loss detected (%.1f%%), "
				"suggesting resolution reduction\n", lossRate * 100.0f);
			return true;
		}
	} else {
		// Reset counter if loss is acceptable
		fConsecutiveHighLossEvents = 0;
	}

	return false;
}


status_t
CamDevice::ReduceResolution()
{
	// Base implementation - subclasses should override
	// Returns B_OK if resolution was reduced, B_ERROR if already at minimum
	syslog(LOG_INFO, "CamDevice: ReduceResolution called (base implementation)\n");
	return B_ERROR;
}


void
CamDevice::ResetPacketStatistics()
{
	fPacketSuccessCount = 0;
	fPacketErrorCount = 0;
	fConsecutiveHighLossEvents = 0;
	fLastStatsReport = system_time();
}


// PHASE 8: Error histogram implementation

void
CamDevice::RecordTransferResult(usb_error_type error)
{
	fErrorHistogram.total_transfers++;
	fErrorHistogram.RecordError(error);

	if (error != USB_ERROR_NONE)
		RecordPacketError();
	else
		RecordPacketSuccess();
}


const usb_error_histogram&
CamDevice::GetErrorHistogram() const
{
	return fErrorHistogram;
}


void
CamDevice::ResetErrorHistogram()
{
	fErrorHistogram.Reset();
}


void
CamDevice::LogErrorStatistics()
{
	if (fErrorHistogram.total_transfers == 0)
		return;

	float totalErrorRate = fErrorHistogram.GetTotalErrorRate() * 100.0f;

	syslog(LOG_INFO, "CamDevice: Error Statistics (since %lld us ago):\n",
		system_time() - fErrorHistogram.last_reset);
	syslog(LOG_INFO, "  Total transfers: %u, Error rate: %.2f%%\n",
		fErrorHistogram.total_transfers, totalErrorRate);

	static const char* errorNames[] = {
		"None", "Timeout", "Stall", "CRC", "Overflow", "Disconnected", "Unknown"
	};

	for (int i = 1; i < USB_ERROR_TYPE_COUNT; i++) {
		if (fErrorHistogram.counts[i] > 0) {
			syslog(LOG_INFO, "  %s errors: %u (%.2f%%)\n",
				errorNames[i], fErrorHistogram.counts[i],
				fErrorHistogram.GetErrorRate((usb_error_type)i) * 100.0f);
		}
	}
}


// High-bandwidth auto-detection callbacks
// Base implementation does nothing - UVCCamDevice overrides these

void
CamDevice::OnConsecutiveTransferFailures(uint32 count)
{
	// Base implementation: just log
	if (count >= 50) {
		syslog(LOG_WARNING, "CamDevice: %u consecutive transfer failures\n", count);
	}
}


void
CamDevice::OnTransferSuccess()
{
	// Base implementation: nothing to do
}


status_t
CamDevice::WaitFrame(bigtime_t timeout)
{
	// CRITICAL FIX: Was infinite recursion! Should call fDeframer->WaitFrame()
	if (fDeframer)
		return fDeframer->WaitFrame(timeout);
	return EINVAL;
}


status_t
CamDevice::GetFrameBitmap(BBitmap **bm, bigtime_t *stamp)
{
	return EINVAL;
}


status_t
CamDevice::FillFrameBuffer(BBuffer *buffer, bigtime_t *stamp)
{
	return EINVAL;
}


bool
CamDevice::Lock()
{
	return fLocker.Lock();
}


status_t
CamDevice::PowerOnSensor(bool on)
{
	return B_OK;
}


ssize_t
CamDevice::WriteReg(uint16 address, uint8 *data, size_t count)
{
	return ENOSYS;
}


ssize_t
CamDevice::WriteReg8(uint16 address, uint8 data)
{
	return WriteReg(address, &data, sizeof(uint8));
}


ssize_t
CamDevice::WriteReg16(uint16 address, uint16 data)
{
	if (fChipIsBigEndian)
		data = B_HOST_TO_BENDIAN_INT16(data);
	else
		data = B_HOST_TO_LENDIAN_INT16(data);
	return WriteReg(address, (uint8 *)&data, sizeof(uint16));
}


ssize_t
CamDevice::ReadReg(uint16 address, uint8 *data, size_t count, bool cached)
{
	return ENOSYS;
}


ssize_t
CamDevice::OrReg8(uint16 address, uint8 data, uint8 mask)
{
	uint8 value;
	if (ReadReg(address, &value, 1, true) < 1)
		return EIO;
	value &= mask;
	value |= data;
	return WriteReg8(address, value);
}


ssize_t
CamDevice::AndReg8(uint16 address, uint8 data)
{
	uint8 value;
	if (ReadReg(address, &value, 1, true) < 1)
		return EIO;
	value &= data;
	return WriteReg8(address, value);
}


/*
status_t
CamDevice::GetStatusIIC()
{
	return ENOSYS;
}
*/

/*status_t
CamDevice::WaitReadyIIC()
{
	return ENOSYS;
}
*/

ssize_t
CamDevice::WriteIIC(uint8 address, uint8 *data, size_t count)
{
	return ENOSYS;
}


ssize_t
CamDevice::WriteIIC8(uint8 address, uint8 data)
{
	return WriteIIC(address, &data, 1);
}


ssize_t
CamDevice::WriteIIC16(uint8 address, uint16 data)
{
	if (Sensor() && Sensor()->IsBigEndian())
		data = B_HOST_TO_BENDIAN_INT16(data);
	else
		data = B_HOST_TO_LENDIAN_INT16(data);
	return WriteIIC(address, (uint8 *)&data, 2);
}


ssize_t
CamDevice::ReadIIC(uint8 address, uint8 *data)
{
	//TODO: make it mode generic
	return ENOSYS;
}


ssize_t
CamDevice::ReadIIC8(uint8 address, uint8 *data)
{
	return ReadIIC(address, data);
}


ssize_t
CamDevice::ReadIIC16(uint8 address, uint16 *data)
{
	return ENOSYS;
}


status_t
CamDevice::SetIICBitsMode(size_t bits)
{
	return ENOSYS;
}


status_t
CamDevice::ProbeSensor()
{
	const usb_webcam_support_descriptor *devs;
	const usb_webcam_support_descriptor *dev = NULL;
	status_t err;
	int32 i;

	PRINT((CH ": probing sensors..." CT));
	if (fCamDeviceAddon.SupportedDevices() == NULL)
		return B_ERROR;
	devs = fCamDeviceAddon.SupportedDevices();
	for (i = 0; devs[i].vendor; i++) {
		if (GetDevice()->VendorID() != devs[i].desc.vendor)
			continue;
		if (GetDevice()->ProductID() != devs[i].desc.product)
			continue;
		dev = &devs[i];
		break;
	}
	if (!dev)
		return ENODEV;
	if (!dev->sensors) // no usable sensor
		return ENOENT;
	BString sensors(dev->sensors);
	for (i = 0; i > -1 && i < sensors.Length(); ) {
		BString name;
		sensors.CopyInto(name, i, sensors.FindFirst(',', i) - i);
		PRINT((CH ": probing sensor '%s'..." CT, name.String()));

		fSensor = CreateSensor(name.String());
		if (fSensor) {
			err = fSensor->Probe();
			if (err >= B_OK)
				return B_OK;

			PRINT((CH ": sensor '%s' Probe: %s" CT, name.String(),
				strerror(err)));

			delete fSensor;
			fSensor = NULL;
		}

		i = sensors.FindFirst(',', i+1);
		if (i > - 1)
			i++;
	}
	return ENOENT;
}


CamSensor *
CamDevice::CreateSensor(const char *name)
{
	for (int32 i = 0; kSensorTable[i].name; i++) {
		if (!strcmp(kSensorTable[i].name, name))
			return kSensorTable[i].instfunc(this);
	}
	PRINT((CH ": sensor '%s' not found" CT, name));
	return NULL;
}


void
CamDevice::SetDataInput(BDataIO *input)
{
	fDataInput = input;
}


status_t
CamDevice::DataPumpThread()
{
	// PRODUCTION: Removed file I/O logging to prevent BFS corruption
	// File operations in USB threads can cause kernel panics

	if (SupportsBulk()) {
		fprintf(stderr, "DataPumpThread: Using BULK transfer mode (buffer size: "
			"%" B_PRIuSIZE " bytes)\n", fBufferLen);
		PRINT((CH ": using Bulk" CT));

		// Configure retry for bulk transfers: fewer retries, shorter delays
		// for real-time streaming
		usb_retry_config bulkRetryConfig = {
			2,			// max_retries: 2 (quick recovery for streaming)
			50000,		// initial_delay: 50ms
			200000,		// max_delay: 200ms
			2.0f		// backoff_multiplier
		};

		while (atomic_get(&fTransferEnabled)) {
			ssize_t len = -1;
			BAutolock lock(fLocker);
			if (!lock.IsLocked())
				break;
			if (!fBulkIn)
				break;
#ifndef DEBUG_DISCARD_INPUT
			// Use retry wrapper for more robust bulk transfers
			len = BulkTransferWithRetry(fBulkIn, fBuffer, fBufferLen,
				bulkRetryConfig);
#endif

			//PRINT((CH ": got %ld bytes" CT, len));
#ifdef DEBUG_WRITE_DUMP
			write(fDumpFD, fBuffer, len);
#endif
#ifdef DEBUG_READ_DUMP
			if ((len = read(fDumpFD, fBuffer, fBufferLen)) < fBufferLen)
				lseek(fDumpFD, 0LL, SEEK_SET);
#endif

			if (len <= 0) {
				PRINT((CH ": BulkIn: %s" CT, strerror(len)));
				// Check if device disconnected
				if (ClassifyUSBError(len) == USB_ERROR_DISCONNECTED)
					break;
				// For other errors, continue trying
				continue;
			}

#ifndef DEBUG_DISCARD_DATA
			if (fDataInput) {
				fDataInput->Write(fBuffer, len);
			} else {
				// Data dropped: no consumer connected
				PRINT((CH ": dropping %zd bytes (no consumer)" CT, len));
			}
#endif
			//snooze(2000);
		}
	}
#ifdef SUPPORT_ISO
	else if (SupportsIsochronous()) {
		// DYNAMIC BUFFER ALLOCATION - Inspired by Linux UVC driver approach
		// MIT License implementation - graceful degradation on allocation failure
		//
		// Strategy: Start with buffer size from _SelectBestAlternate (conservative)
		// If transfers succeed, we could grow. If they fail, we shrink further.

		const int kMaxPacketDescriptors = 32;  // Array size (max possible)
		const int kMinPacketDescriptors = 2;   // Minimum viable
		usb_iso_packet_descriptor packetDescriptors[kMaxPacketDescriptors];

		// Use native MaxPacketSize from device
		uint32 packetSize = (fIsoMaxPacketSize > 0) ? fIsoMaxPacketSize : 256;

		// Calculate packet count based on EXISTING buffer (set by _SelectBestAlternate)
		// Don't reallocate - use what we have (conservative for EHCI)
		int numPacketDescriptors = fBufferLen / packetSize;
		if (numPacketDescriptors > kMaxPacketDescriptors)
			numPacketDescriptors = kMaxPacketDescriptors;
		if (numPacketDescriptors < kMinPacketDescriptors)
			numPacketDescriptors = kMinPacketDescriptors;

		// Log transfer setup to syslog
		syslog(LOG_INFO, "ISO Transfer: buffer=%zu, packets=%d, slotSize=%u\n",
			fBufferLen, numPacketDescriptors, packetSize);

		// Initialize packet descriptors
		for (int i = 0; i < kMaxPacketDescriptors; i++)
			packetDescriptors[i].request_length = packetSize;

		// Statistics tracking
		fPacketSuccessCount = 0;
		fPacketErrorCount = 0;
		fLastStatsReport = system_time();
		fTransferStartTime = system_time();

		int consecutiveFailures = 0;
		int32 transferAttempts = 0;

		while (atomic_get(&fTransferEnabled)) {
			ssize_t len = -1;

			// Minimize lock scope: only lock during USB transfer
			// Processing happens outside the lock
			{
				BAutolock lock(fLocker);
				if (!lock.IsLocked())
					break;
				if (fIsoIn == NULL)
					break;

				// OPTIMIZATION: Removed memset() from hot path
				// Pre-filling buffer is unnecessary because:
				// 1. We track actual_length for each packet
				// 2. Only valid data is written to the deframer
				// 3. Invalid packets are skipped entirely
				// The deframer handles incomplete frames with its own validation

				// Isochronous transfer
				len = fIsoIn->IsochronousTransfer(fBuffer, fBufferLen,
					packetDescriptors, numPacketDescriptors);
			}
			// Lock released here - processing happens unlocked

			// Throttled logging: first 5, then based on time/count threshold
			transferAttempts++;
			bigtime_t now = system_time();
			bool shouldLog = (transferAttempts <= 5)
				|| (transferAttempts % kLogThrottleInterval == 0)
				|| (now - fLastLogTime > kLogTimeInterval);

			if (shouldLog) {
				fLastLogTime = now;
				syslog(LOG_INFO, "ISO xfer #%d: len=%zd, pkt[0]=%d/%u\n",
					(int)transferAttempts, len,
					packetDescriptors[0].actual_length,
					packetDescriptors[0].request_length);
			}

			// Handle transfer errors
			if (len < 0) {
				consecutiveFailures++;
				OnConsecutiveTransferFailures(consecutiveFailures);

				// If too many consecutive failures, log and consider brief pause
				if (consecutiveFailures == 10) {
					syslog(LOG_WARNING, "USB: 10 consecutive transfer failures (err=%zd)\n", len);
				}
				if (consecutiveFailures >= 50) {
					// Brief pause to let USB subsystem recover
					syslog(LOG_WARNING, "USB: 50+ failures, pausing 10ms for recovery\n");
					snooze(10000);
					consecutiveFailures = 0;  // Reset counter after recovery attempt
				}
				// Don't return or continue - fall through to process any completed packets!
			} else {
				// Success - notify subclass to reset failure tracking
				if (consecutiveFailures > 0) {
					OnTransferSuccess();
				}
				consecutiveFailures = 0;
			}

			//PRINT((CH ": got %d bytes" CT, len));
#ifdef DEBUG_WRITE_DUMP
			write(fDumpFD, fBuffer, len);
#endif
#ifdef DEBUG_READ_DUMP
			if ((len = read(fDumpFD, fBuffer, fBufferLen)) < fBufferLen)
				lseek(fDumpFD, 0LL, SEEK_SET);
#endif

			// REMOVED: Don't skip on len <= 0
			// Even when transfer returns error, individual packets may have completed.
			// Process packet descriptors regardless of overall transfer status.

#ifndef DEBUG_DISCARD_DATA
			if (fDataInput) {
				// PHASE 4 FIX: Use fixed packet offsets (not sequential)
				// EHCI places data at packet_index * request_length

				// DEBUG: Log first transfer results
				// DISABLED: File I/O in hot path causes performance issues
				#if 0
				static bool firstResultLogged = false;
				if (!firstResultLogged) {
					FILE* resultLog = fopen("/boot/home/Desktop/iso_transfer_debug.log", "a");
// 					fprintf(stderr, "DEBUG: First transfer results (first 5 packets):\n");
					if (resultLog) {
	fprintf(resultLog, "\n=== FIRST TRANSFER RESULTS ===\n");
	fprintf(resultLog, "Transfer returned len=%zd\n", len);
					}
					for (int i = 0; i < 5 && i < numPacketDescriptors; i++) {
	fprintf(stderr, "  packet[%d]: request=%u actual=%d status=0x%x\n",
							i,
							packetDescriptors[i].request_length,
							packetDescriptors[i].actual_length,
							packetDescriptors[i].status);
						if (resultLog) {
	fprintf(resultLog, "  packet[%d]: request=%u actual=%d status=0x%x\n",
								i,
								packetDescriptors[i].request_length,
								packetDescriptors[i].actual_length,
								packetDescriptors[i].status);
						}
					}
					if (resultLog) {
	fprintf(resultLog, "==============================\n");
						fclose(resultLog);
					}
					firstResultLogged = true;
				}
				#endif

				// CRITICAL FIX: EHCI kernel calculates slot size as DataLength/packet_count
				// We must ensure: fBufferLen = slotSize * numPacketDescriptors
				// So: slotSize = fBufferLen / numPacketDescriptors
				// This MUST match request_length if buffer was properly sized
				size_t slotSize = fBufferLen / numPacketDescriptors;

				// Verify alignment and log packet info (first few times)
				static int logCount = 0;
				if (logCount < 5) {
					logCount++;
					size_t reqLen = packetDescriptors[0].request_length;
					syslog(LOG_INFO, "ISO #%d: bufLen=%zu, slots=%d, slotSize=%zu, reqLen=%zu\n",
						logCount, fBufferLen, numPacketDescriptors, slotSize, reqLen);

					// Log first few packet actual_lengths
					syslog(LOG_INFO, "  actual_len: [%d, %d, %d, %d, %d, %d, %d, %d]\n",
						packetDescriptors[0].actual_length,
						numPacketDescriptors > 1 ? packetDescriptors[1].actual_length : -1,
						numPacketDescriptors > 2 ? packetDescriptors[2].actual_length : -1,
						numPacketDescriptors > 3 ? packetDescriptors[3].actual_length : -1,
						numPacketDescriptors > 4 ? packetDescriptors[4].actual_length : -1,
						numPacketDescriptors > 5 ? packetDescriptors[5].actual_length : -1,
						numPacketDescriptors > 6 ? packetDescriptors[6].actual_length : -1,
						numPacketDescriptors > 7 ? packetDescriptors[7].actual_length : -1);
				}

				for (int i = 0; i < numPacketDescriptors; i++) {
					// Calculate offset matching kernel's layout (i * DataLength/packet_count)
					size_t packetOffset = i * slotSize;

					// PHASE 3: Check if this packet succeeded
					if (packetDescriptors[i].status != B_OK) {
						fPacketErrorCount++;
						// Skip failed packets - data is invalid
						continue;
					}

					// Direct access to struct member (no copy)
					int actual_length = packetDescriptors[i].actual_length;

					// Bounds check
					if (actual_length > 0 && packetOffset + actual_length <= fBufferLen) {
						fDataInput->Write(&fBuffer[packetOffset], actual_length);
						fPacketSuccessCount++;
					}
				}

				// Periodic statistics reporting (every 30 seconds)
				bigtime_t now = system_time();
				if (now - fLastStatsReport > 30000000) {  // 30 seconds
					uint32 totalPackets = fPacketSuccessCount + fPacketErrorCount;
					float lossPercent = totalPackets > 0
						? (100.0f * fPacketErrorCount / totalPackets)
						: 0.0f;

					// Calculate throughput
					bigtime_t elapsed = now - fTransferStartTime;
					float elapsedSec = elapsed / 1000000.0f;
					float packetsPerSec = elapsedSec > 0 ? totalPackets / elapsedSec : 0;

					syslog(LOG_INFO, "USB Stats: success=%u errors=%u loss=%.1f%% rate=%.0f pkt/s\n",
						fPacketSuccessCount, fPacketErrorCount, lossPercent, packetsPerSec);

					// Warn if packet loss is high (>5% is concerning for video)
					if (lossPercent > 5.0f) {
						syslog(LOG_WARNING, "USB: HIGH packet loss (%.1f%%) - may cause grey frames\n",
							lossPercent);
					}

					fLastStatsReport = now;
				}
			} else {
				// Data dropped: no consumer connected (isochronous mode)
				if (fDroppedFramesLogged < 5) {
					syslog(LOG_WARNING, "Dropping isochronous data (no consumer)\n");
					fDroppedFramesLogged++;
				}
			}
#endif
			//snooze(2000);
		}
	}
#endif
	else {
		PRINT((CH ": No supported transport." CT));
		return B_UNSUPPORTED;
	}
	return B_OK;
}


int32
CamDevice::_DataPumpThread(void *_this)
{
	CamDevice *dev = (CamDevice *)_this;
	return dev->DataPumpThread();
}


void
CamDevice::DumpRegs()
{
}


status_t
CamDevice::SendCommand(uint8 dir, uint8 request, uint16 value,
	uint16 index, uint16 length, void* data)
{
	ssize_t ret;
	if (!GetDevice())
		return ENODEV;
	if (length > GetDevice()->MaxEndpoint0PacketSize())
		return EINVAL;
	ret = GetDevice()->ControlTransfer(
		USB_REQTYPE_VENDOR | USB_REQTYPE_INTERFACE_OUT | dir,
		request, value, index, length, data);
	return ret;
}


CamDeviceAddon::CamDeviceAddon(WebCamMediaAddOn* webcam)
	: fWebCamAddOn(webcam),
	  fSupportedDevices(NULL)
{
}


CamDeviceAddon::~CamDeviceAddon()
{
}


const char *
CamDeviceAddon::BrandName()
{
	return "<unknown>";
}


status_t
CamDeviceAddon::Sniff(BUSBDevice *device)
{
	PRINT((CH ": Sniffing for %s" CT, BrandName()));
// 	syslog(LOG_ERR, "CamDeviceAddon::Sniff: Checking device VID:0x%04x PID:0x%04x Class:0x%02x Subclass:0x%02x\n",
// 		device ? device->VendorID() : 0, device ? device->ProductID() : 0,
// 		device ? device->Class() : 0, device ? device->Subclass() : 0);

	if (!fSupportedDevices) {
// 		syslog(LOG_ERR, "CamDeviceAddon::Sniff: No supported devices table!\n");
		return ENODEV;
	}
	if (!device) {
// 		syslog(LOG_ERR, "CamDeviceAddon::Sniff: NULL device!\n");
		return EINVAL;
	}

	bool supported = false;
	for (uint32 i = 0; !supported && fSupportedDevices[i].vendor; i++) {
// 		syslog(LOG_ERR, "  Checking entry %d: VID:0x%04x PID:0x%04x Class:0x%02x Subclass:0x%02x Brand:%s\n",
// 			i, fSupportedDevices[i].desc.vendor, fSupportedDevices[i].desc.product,
// 			fSupportedDevices[i].desc.dev_class, fSupportedDevices[i].desc.dev_subclass,
// 			fSupportedDevices[i].vendor);

		if ((fSupportedDevices[i].desc.vendor != 0
			&& device->VendorID() != fSupportedDevices[i].desc.vendor)
			|| (fSupportedDevices[i].desc.product != 0
			&& device->ProductID() != fSupportedDevices[i].desc.product)) {
// 			syslog(LOG_ERR, "    Skipped: VID/PID mismatch\n");
			continue;
		}

		if ((fSupportedDevices[i].desc.dev_class == 0
			|| device->Class() == fSupportedDevices[i].desc.dev_class)
			&& (fSupportedDevices[i].desc.dev_subclass == 0
			|| device->Subclass() == fSupportedDevices[i].desc.dev_subclass)
			&& (fSupportedDevices[i].desc.dev_protocol == 0
			|| device->Protocol() == fSupportedDevices[i].desc.dev_protocol)) {
// 			syslog(LOG_ERR, "    MATCHED device-level class/subclass!\n");
			supported = true;
		}

#ifdef __HAIKU__
		// we have to check all interfaces for matching class/subclass/protocol
// 		syslog(LOG_ERR, "    Checking %d configurations...\n", device->CountConfigurations());
		for (uint32 j = 0; !supported && j < device->CountConfigurations(); j++) {
			const BUSBConfiguration* cfg = device->ConfigurationAt(j);
// 			syslog(LOG_ERR, "      Config %d: %d interfaces\n", j, cfg ? cfg->CountInterfaces() : 0);
			for (uint32 k = 0; !supported && k < cfg->CountInterfaces(); k++) {
				const BUSBInterface* intf = cfg->InterfaceAt(k);
// 				syslog(LOG_ERR, "        Interface %d: %d alternates\n", k, intf ? intf->CountAlternates() : 0);
				for (uint32 l = 0; !supported && l < intf->CountAlternates(); l++) {
					const BUSBInterface* alt = intf->AlternateAt(l);
// 					syslog(LOG_ERR, "          Alternate %d: Class:0x%02x Subclass:0x%02x Protocol:0x%02x\n",
// 						l, alt ? alt->Class() : 0, alt ? alt->Subclass() : 0, alt ? alt->Protocol() : 0);
					if ((fSupportedDevices[i].desc.dev_class == 0
						|| alt->Class() == fSupportedDevices[i].desc.dev_class)
						&& (fSupportedDevices[i].desc.dev_subclass == 0
						|| alt->Subclass() == fSupportedDevices[i].desc.dev_subclass)
						&& (fSupportedDevices[i].desc.dev_protocol == 0
						|| alt->Protocol() == fSupportedDevices[i].desc.dev_protocol)) {
// 						syslog(LOG_ERR, "          MATCHED interface-level class/subclass!\n");
						supported = true;
					}
				}
			}
		}
#endif

		if (supported) {
// 			syslog(LOG_ERR, "  Device SUPPORTED as entry %d\n", i);
			return i;
		}
	}

// 	syslog(LOG_ERR, "CamDeviceAddon::Sniff: Device NOT SUPPORTED\n");
	return ENODEV;
}


CamDevice *
CamDeviceAddon::Instantiate(CamRoster &roster, BUSBDevice *from)
{
	return NULL;
}


void
CamDeviceAddon::SetSupportedDevices(const usb_webcam_support_descriptor *devs)
{
	fSupportedDevices = devs;
}


// USB Transfer Retry Implementation
// Following Haiku coding style guidelines


usb_error_type
CamDevice::ClassifyUSBError(ssize_t error)
{
	if (error >= B_OK)
		return USB_ERROR_NONE;

	switch (error) {
		case B_TIMED_OUT:
		case B_DEV_TIMEOUT:
			return USB_ERROR_TIMEOUT;

		case B_DEV_STALLED:
			return USB_ERROR_STALL;

		case B_DEV_CRC_ERROR:
		case B_DEV_DATA_OVERRUN:
			return USB_ERROR_CRC;

		case B_DEV_FIFO_OVERRUN:
		case B_DEV_DATA_UNDERRUN:
			return USB_ERROR_OVERFLOW;

		case B_DEV_NOT_READY:
		case B_DEV_NO_MEDIA:
		case B_DEV_UNREADABLE:
			return USB_ERROR_DISCONNECTED;

		default:
			return USB_ERROR_UNKNOWN;
	}
}


bigtime_t
CamDevice::CalculateBackoffDelay(uint32 attempt, const usb_retry_config& config)
{
	// Exponential backoff: delay = initial * (multiplier ^ attempt)
	// Capped at max_delay
	bigtime_t delay = config.initial_delay;

	for (uint32 i = 0; i < attempt; i++) {
		delay = (bigtime_t)(delay * config.backoff_multiplier);
		if (delay > config.max_delay) {
			delay = config.max_delay;
			break;
		}
	}

	return delay;
}


ssize_t
CamDevice::ControlTransferWithRetry(uint8 requestType, uint8 request,
	uint16 value, uint16 index, uint16 length, void* data,
	const usb_retry_config& config)
{
	if (fDevice == NULL)
		return B_DEV_NOT_READY;

	ssize_t result = B_ERROR;
	uint32 attempt = 0;

	while (attempt <= config.max_retries) {
		result = fDevice->ControlTransfer(requestType, request, value, index,
			length, data);

		if (result >= B_OK)
			return result;

		// Classify error and decide if retryable
		usb_error_type errorType = ClassifyUSBError(result);

		// Don't retry on disconnect or certain fatal errors
		if (errorType == USB_ERROR_DISCONNECTED) {
			syslog(LOG_ERR, "USB: Device disconnected, aborting transfer\n");
			return result;
		}

		if (attempt < config.max_retries) {
			bigtime_t delay = CalculateBackoffDelay(attempt, config);
			syslog(LOG_WARNING, "USB: ControlTransfer failed (%s), retry %u/%u "
				"after %lld ms\n", strerror(result), attempt + 1,
				config.max_retries, delay / 1000);
			snooze(delay);
		}

		attempt++;
	}

	syslog(LOG_ERR, "USB: ControlTransfer failed after %u retries: %s\n",
		config.max_retries, strerror(result));
	return result;
}


ssize_t
CamDevice::BulkTransferWithRetry(const BUSBEndpoint* endpoint, void* data,
	size_t length, const usb_retry_config& config)
{
	if (endpoint == NULL)
		return B_BAD_VALUE;

	ssize_t result = B_ERROR;
	uint32 attempt = 0;

	while (attempt <= config.max_retries) {
		result = endpoint->BulkTransfer(data, length);

		if (result >= B_OK)
			return result;

		// Classify error and decide if retryable
		usb_error_type errorType = ClassifyUSBError(result);

		// Don't retry on disconnect
		if (errorType == USB_ERROR_DISCONNECTED) {
			syslog(LOG_ERR, "USB: Device disconnected, aborting bulk transfer\n");
			return result;
		}

		// For stall errors, we might need to clear the endpoint
		if (errorType == USB_ERROR_STALL) {
			syslog(LOG_WARNING, "USB: Endpoint stalled, attempting clear\n");
			// Note: BUSBEndpoint doesn't expose ClearStall directly
			// The retry might help if the driver auto-clears
		}

		if (attempt < config.max_retries) {
			bigtime_t delay = CalculateBackoffDelay(attempt, config);
			syslog(LOG_WARNING, "USB: BulkTransfer failed (%s), retry %u/%u "
				"after %lld ms\n", strerror(result), attempt + 1,
				config.max_retries, delay / 1000);
			snooze(delay);
		}

		attempt++;
	}

	syslog(LOG_ERR, "USB: BulkTransfer failed after %u retries: %s\n",
		config.max_retries, strerror(result));
	return result;
}


// =============================================================================
// Double Buffering Implementation
// =============================================================================
//
// Double buffering allows concurrent USB reception and data processing:
// - One buffer receives data from USB
// - The other buffer is processed by the deframer
// This reduces latency and prevents data loss during processing.


status_t
CamDevice::InitDoubleBuffering(size_t bufferSize)
{
	if (fDoubleBuffer.initialized) {
		// Already initialized, check if size matches
		if (fDoubleBuffer.bufferSize == bufferSize)
			return B_OK;
		// Size changed, cleanup and reinitialize
		CleanupDoubleBuffering();
	}

	// Allocate both buffers
	fDoubleBuffer.buffers[0] = (uint8*)malloc(bufferSize);
	if (fDoubleBuffer.buffers[0] == NULL) {
		syslog(LOG_ERR, "CamDevice: Failed to allocate double buffer 0\n");
		return B_NO_MEMORY;
	}

	fDoubleBuffer.buffers[1] = (uint8*)malloc(bufferSize);
	if (fDoubleBuffer.buffers[1] == NULL) {
		free(fDoubleBuffer.buffers[0]);
		fDoubleBuffer.buffers[0] = NULL;
		syslog(LOG_ERR, "CamDevice: Failed to allocate double buffer 1\n");
		return B_NO_MEMORY;
	}

	// Create synchronization semaphore
	fDoubleBuffer.bufferReady = create_sem(0, "usb_buffer_ready");
	if (fDoubleBuffer.bufferReady < B_OK) {
		free(fDoubleBuffer.buffers[0]);
		free(fDoubleBuffer.buffers[1]);
		fDoubleBuffer.buffers[0] = NULL;
		fDoubleBuffer.buffers[1] = NULL;
		syslog(LOG_ERR, "CamDevice: Failed to create buffer semaphore\n");
		return fDoubleBuffer.bufferReady;
	}

	fDoubleBuffer.bufferSize = bufferSize;
	fDoubleBuffer.activeBuffer = 0;
	fDoubleBuffer.readyBuffer = -1;
	fDoubleBuffer.initialized = true;

	syslog(LOG_INFO, "CamDevice: Double buffering initialized (%zu bytes x 2)\n",
		bufferSize);

	return B_OK;
}


void
CamDevice::CleanupDoubleBuffering()
{
	if (!fDoubleBuffer.initialized)
		return;

	if (fDoubleBuffer.bufferReady >= B_OK) {
		delete_sem(fDoubleBuffer.bufferReady);
		fDoubleBuffer.bufferReady = -1;
	}

	free(fDoubleBuffer.buffers[0]);
	free(fDoubleBuffer.buffers[1]);
	fDoubleBuffer.buffers[0] = NULL;
	fDoubleBuffer.buffers[1] = NULL;
	fDoubleBuffer.bufferSize = 0;
	fDoubleBuffer.initialized = false;
}


uint8*
CamDevice::GetActiveBuffer()
{
	if (!fDoubleBuffer.initialized)
		return fBuffer;  // Fallback to single buffer

	return fDoubleBuffer.buffers[fDoubleBuffer.activeBuffer];
}


uint8*
CamDevice::GetReadyBuffer()
{
	if (!fDoubleBuffer.initialized || fDoubleBuffer.readyBuffer < 0)
		return NULL;

	return fDoubleBuffer.buffers[fDoubleBuffer.readyBuffer];
}


void
CamDevice::SwapBuffers()
{
	if (!fDoubleBuffer.initialized)
		return;

	// Mark current active buffer as ready for processing
	fDoubleBuffer.readyBuffer = fDoubleBuffer.activeBuffer;

	// Switch to the other buffer for receiving
	fDoubleBuffer.activeBuffer = 1 - fDoubleBuffer.activeBuffer;

	// Signal that a buffer is ready
	release_sem(fDoubleBuffer.bufferReady);
}


// =============================================================================
// Group 4: Frame Timing Statistics
// =============================================================================

void
CamDevice::RecordFrameTiming(bigtime_t processingTime)
{
	fFrameTimingStats.RecordFrame(processingTime);
}


const frame_timing_stats&
CamDevice::GetFrameTimingStats() const
{
	return fFrameTimingStats;
}


void
CamDevice::ResetFrameTimingStats()
{
	fFrameTimingStats.Reset();
}


void
CamDevice::SetExpectedFrameRate(float fps)
{
	if (fps > 0.0f) {
		fFrameTimingStats.expected_interval = (bigtime_t)(1000000.0f / fps);
	}
}


bigtime_t
CamDevice::GetAdaptiveTimeout() const
{
	return fFrameTimingStats.adaptive_timeout;
}


void
CamDevice::LogFrameTimingStats()
{
	const frame_timing_stats& stats = fFrameTimingStats;

	if (stats.frame_count < 2) {
		syslog(LOG_INFO, "CamDevice: Frame timing - insufficient data (%u frames)\n",
			stats.frame_count);
		return;
	}

	syslog(LOG_INFO, "CamDevice: Frame timing stats:\n");
	syslog(LOG_INFO, "  Frames: %u\n", stats.frame_count);
	syslog(LOG_INFO, "  Avg FPS: %.2f\n", stats.GetAverageFPS());
	syslog(LOG_INFO, "  Interval: min=%lld avg=%lld max=%lld us\n",
		stats.frame_interval_min,
		stats.GetAverageInterval(),
		stats.frame_interval_max);
	syslog(LOG_INFO, "  Avg jitter: %lld us\n", stats.GetAverageJitter());
	syslog(LOG_INFO, "  Processing: avg=%lld max=%lld us\n",
		stats.GetAverageProcessingTime(),
		stats.processing_time_max);
	syslog(LOG_INFO, "  Adaptive timeout: %lld us\n", stats.adaptive_timeout);
}


// =============================================================================
// Group 5: Error Recovery
// =============================================================================

error_recovery_action
CamDevice::EvaluateErrorRecovery(usb_error_type error)
{
	// Track consecutive errors
	if (error != USB_ERROR_NONE) {
		fErrorRecoveryConfig.consecutive_errors++;
	} else {
		fErrorRecoveryConfig.consecutive_errors = 0;
	}

	// Get base recommendation for this error type
	error_recovery_action action = error_recovery_config::GetRecommendedAction(error);

	// Escalate if error rate is high
	float errorRate = fErrorHistogram.GetTotalErrorRate();
	if (errorRate > fErrorRecoveryConfig.error_rate_action) {
		// High error rate - escalate to more aggressive recovery
		if (action == RECOVERY_RETRY)
			action = RECOVERY_RESTART_TRANSFER;
		else if (action == RECOVERY_RESET_ENDPOINT)
			action = RECOVERY_RESTART_TRANSFER;
	}

	// Escalate if too many consecutive errors
	if (fErrorRecoveryConfig.consecutive_errors >= fErrorRecoveryConfig.consecutive_errors_max) {
		if (action < RECOVERY_RESTART_TRANSFER && action != RECOVERY_FATAL)
			action = RECOVERY_RESTART_TRANSFER;
	}

	return action;
}


bool
CamDevice::ShouldTriggerRecovery() const
{
	// Check error rate threshold
	float errorRate = fErrorHistogram.GetTotalErrorRate();
	if (errorRate >= fErrorRecoveryConfig.error_rate_action)
		return true;

	// Check consecutive errors threshold
	if (fErrorRecoveryConfig.consecutive_errors >= fErrorRecoveryConfig.consecutive_errors_max)
		return true;

	return false;
}


void
CamDevice::ResetErrorRecoveryState()
{
	fErrorRecoveryConfig.Reset();
}


const error_recovery_config&
CamDevice::GetErrorRecoveryConfig() const
{
	return fErrorRecoveryConfig;
}


void
CamDevice::LogRecoveryRecommendation(usb_error_type error)
{
	error_recovery_action action = EvaluateErrorRecovery(error);

	static const char* errorNames[] = {
		"None", "Timeout", "Stall", "CRC", "Overflow", "Disconnected", "Unknown"
	};

	const char* errorName = (error >= 0 && error < USB_ERROR_TYPE_COUNT)
		? errorNames[error] : "Invalid";

	syslog(LOG_INFO, "CamDevice: Error '%s' -> Recommended action: %s\n",
		errorName, error_recovery_config::GetActionName(action));

	// Log additional context if recovery is needed
	if (action != RECOVERY_NONE && action != RECOVERY_RETRY) {
		float errorRate = fErrorHistogram.GetTotalErrorRate() * 100.0f;
		syslog(LOG_INFO, "  Error rate: %.2f%%, Consecutive errors: %u\n",
			errorRate, fErrorRecoveryConfig.consecutive_errors);
	}
}
