/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Distributed under the terms of the MIT License.
 *
 * UVCCamDevice audio streaming methods, split from UVCCamDevice.cpp (RF-05).
 */

#include "UVCCamDevice.h"
#include "UVCDeframer.h"
#include "UVCQuirks.h"
#include "UVCDescriptors.h"
#include "CamDebug.h"
#include "CamConfig.h"

#include <new>
#include <stdio.h>
#include <Autolock.h>
#include <stdlib.h>
#include <syslog.h>
#include <Notification.h>
#include <ParameterWeb.h>
#include <String.h>
#include <media/Buffer.h>

#undef TRACE
#define TRACE(x...) do {} while(0)
//#define TRACE(x...) printf(x)

/* Audio Transfer Methods */

status_t
UVCCamDevice::StartAudioTransfer()
{
	// Serialise concurrent starts, Start uses same lock pattern.
	BAutolock transferLock(Locker());
	if (!fHasAudio) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: No audio interface\n");
		return B_ERROR;
	}

	if (fAudioTransferRunning)
		return B_OK;

	// Apply fallback values for missing audio parameters
	// These defaults match common USB webcam microphone configurations
	if (fAudioSampleRate == 0) {
		// Try common rates; SET_CUR + GET_CUR later will correct if wrong
		fAudioSampleRate = 16000;
		syslog(LOG_WARNING, "UVCCamDevice: No sample rate from descriptors, "
			"trying %u Hz (will verify with device)\n",
			(unsigned)fAudioSampleRate);
	}
	if (fAudioChannels == 0) {
		fAudioChannels = 2;  // Stereo is common for webcam mics
		syslog(LOG_WARNING, "UVCCamDevice: Using fallback channel count: %u\n",
			(unsigned)fAudioChannels);
	}
	if (fAudioBitResolution == 0) {
		fAudioBitResolution = 16;  // 16-bit PCM is standard
		syslog(LOG_WARNING, "UVCCamDevice: Using fallback bit resolution: %u\n",
			(unsigned)fAudioBitResolution);
	}
	if (fAudioSubFrameSize == 0) {
		fAudioSubFrameSize = fAudioBitResolution / 8;
	}

	// Select audio alternate with proper bandwidth
	status_t err = _SelectAudioAlternate();
	if (err != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to select "
			"alternate: %s\n", strerror(err));
		return err;
	}

	// Set sample rate on the endpoint (required for USB Audio Class 1.0)
	if (fAudioIsoIn != NULL) {
		uint32 sampleRate = fAudioSampleRate;
		uint8 rateData[3];
		rateData[0] = sampleRate & 0xFF;
		rateData[1] = (sampleRate >> 8) & 0xFF;
		rateData[2] = (sampleRate >> 16) & 0xFF;

		uint8 endpointAddr = fAudioIsoIn->Descriptor()->endpoint_address;

		// SET_CUR request to set sampling frequency on endpoint
		// bmRequestType: 0x22 = Host-to-device, Class, Endpoint
		// bRequest: 0x01 = SET_CUR
		// wValue: 0x0100 = SAMPLING_FREQ_CONTROL << 8
		// wIndex: endpoint address
		ssize_t transferred = fDevice->ControlTransfer(
			USB_REQTYPE_CLASS | USB_REQTYPE_ENDPOINT_OUT,  // 0x22
			0x01,  // SET_CUR
			0x0100,  // SAMPLING_FREQ_CONTROL << 8
			endpointAddr,
			3,
			rateData);

		syslog(LOG_INFO, "UVCCamDevice: Set USB sample rate to %u Hz (result=%zd)\n",
			(unsigned)sampleRate, transferred);

		// Verify with GET_CUR that the device accepted the sample rate
		if (transferred == 3) {
			uint8 verifyData[3] = {0};
			ssize_t got = fDevice->ControlTransfer(
				USB_REQTYPE_CLASS | USB_REQTYPE_ENDPOINT_IN,  // 0xA2
				0x81,  // GET_CUR
				0x0100,  // SAMPLING_FREQ_CONTROL << 8
				endpointAddr,
				3,
				verifyData);

			if (got == 3) {
				uint32 readBack = verifyData[0]
					| ((uint32)verifyData[1] << 8)
					| ((uint32)verifyData[2] << 16);
				if (readBack != sampleRate) {
					syslog(LOG_WARNING,
						"UVCCamDevice: Device reports sample rate %u Hz "
						"(requested %u Hz)\n",
						(unsigned)readBack, (unsigned)sampleRate);
					fAudioSampleRate = readBack;
				}
			}
		}
	}

	// Allocate ring buffer sized for ~2 seconds of audio.
	// Scale with actual sample rate and channel count to avoid
	// underruns at high rates or wasted memory at low rates.
	// FIX-H8: 32-bit product wrapped for hostile descriptor rates; compute
	// in 64 bit and clamp so a wrap cannot under-allocate the ring.
	uint64 ringSize64 = (uint64)fAudioSampleRate * (uint64)fAudioChannels
		* 2 * 2;
	fAudioRingSize = 16384;
	if (ringSize64 >= 16384 && ringSize64 <= 262144)
		fAudioRingSize = (size_t)ringSize64;
	else if (ringSize64 > 262144)
		fAudioRingSize = 262144;
	fAudioRingBuffer = (uint8*)malloc(fAudioRingSize);
	if (!fAudioRingBuffer) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to allocate ring buffer\n");
		_SelectAudioIdleAlternate();
		return B_NO_MEMORY;
	}
	fAudioRingHead = 0;
	fAudioRingTail = 0;

	// Create semaphore for ring buffer synchronization
	fAudioRingSem = create_sem(0, "audio ring buffer");
	if (fAudioRingSem < 0) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to create semaphore\n");
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
		_SelectAudioIdleAlternate();
		return B_ERROR;
	}

	// Mark as running before starting thread
	fAudioTransferRunning = true;

	// Start audio pump thread
	fAudioPumpThread = spawn_thread(_audio_pump_thread_, "audio pump",
		B_REAL_TIME_PRIORITY, this);
	if (fAudioPumpThread < 0) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to spawn thread\n");
		fAudioTransferRunning = false;
		delete_sem(fAudioRingSem);
		fAudioRingSem = -1;
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
		_SelectAudioIdleAlternate();
		return B_ERROR;
	}

	if (resume_thread(fAudioPumpThread) != B_OK) {
		syslog(LOG_ERR, "UVCCamDevice::StartAudioTransfer: Failed to resume thread\n");
		fAudioTransferRunning = false;
		kill_thread(fAudioPumpThread);
		delete_sem(fAudioRingSem);
		fAudioRingSem = -1;
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
		_SelectAudioIdleAlternate();
		return B_ERROR;
	}

	return B_OK;
}


status_t
UVCCamDevice::StopAudioTransfer()
{
	// Serialise with Start, same lock.
	BAutolock transferLock(Locker());
	if (!fAudioTransferRunning)
		return B_OK;

	// Signal thread to stop
	fAudioTransferRunning = false;

	// Wake thread if blocked on semaphore (don't delete yet)
	if (fAudioRingSem >= 0)
		release_sem(fAudioRingSem);

	// Wait for thread to exit. FIX-C2: honour the timeout. A pump wedged
	// in an uninterruptible kernel IsochronousTransfer still touches
	// fAudioBuffer/fAudioRingBuffer/fAudioRingSem after this point, so on
	// B_TIMED_OUT abandon the thread (leak sem + buffers like the video
	// path) and stall the device instead of freeing its working set.
	if (fAudioPumpThread >= 0) {
		status_t threadStatus;
		status_t waitErr = wait_for_thread_etc(fAudioPumpThread,
			B_RELATIVE_TIMEOUT, 5000000, &threadStatus);
		fAudioPumpThread = -1;
		if (waitErr == B_TIMED_OUT) {
			syslog(LOG_ERR, "UVCCamDevice::StopAudioTransfer: audio pump "
				"wedged — abandoning thread and marking device stalled\n");
			MarkStalled();
			// Wedged thread still uses buffers and sem, leak them.
			return B_TIMED_OUT;
		}
	}

	// Now safe to delete the semaphore
	if (fAudioRingSem >= 0) {
		delete_sem(fAudioRingSem);
		fAudioRingSem = -1;
	}

	// Free ring buffer
	if (fAudioRingBuffer) {
		free(fAudioRingBuffer);
		fAudioRingBuffer = NULL;
	}

	// Set audio interface to idle
	_SelectAudioIdleAlternate();

	return B_OK;
}


size_t
UVCCamDevice::ReadAudioData(void* buffer, size_t size)
{
	if (fAudioRingBuffer == NULL || buffer == NULL || size == 0)
		return 0;

	// Block until the ring buffer has enough data for a full audio buffer.
	// This matches the multi_audio driver pattern: the loop is paced by
	// hardware data arrival (USB isochronous), not software timers.
	// The pump thread releases the semaphore after each USB packet (~128 bytes).
	// We acquire repeatedly until we have enough data or timeout.
	bigtime_t deadline = system_time() + 50000;	// 50ms max wait
	size_t available = 0;

	while (system_time() < deadline) {
		int32 head = atomic_get(&fAudioRingHead);
		int32 tail = atomic_get(&fAudioRingTail);

		if (tail < 0 || (size_t)tail >= fAudioRingSize) {
			atomic_set(&fAudioRingTail, 0);
			return 0;
		}

		if (head >= tail)
			available = head - tail;
		else
			available = fAudioRingSize - tail + head;

		if (available >= size)
			break;

		// Wait for next USB audio packet
		status_t err = acquire_sem_etc(fAudioRingSem, 1,
			B_RELATIVE_TIMEOUT, 2000);
		if (err == B_BAD_SEM_ID)
			return 0;
	}

	// Recalculate available data with fresh head/tail after the wait loop
	int32 head = atomic_get(&fAudioRingHead);
	int32 tail = atomic_get(&fAudioRingTail);
	if (tail < 0 || (size_t)tail >= fAudioRingSize
		|| head < 0 || (size_t)head >= fAudioRingSize) {
		atomic_set(&fAudioRingTail, 0);
		return 0;
	}

	if (head >= tail)
		available = head - tail;
	else
		available = fAudioRingSize - tail + head;

	if (available == 0)
		return 0;

	size_t toRead = (available < size) ? available : size;
	size_t firstChunk = fAudioRingSize - tail;

	// Re-check lifetime, Stop may have freed the ring after the wait.
	if (fAudioRingBuffer == NULL)
		return 0;

	if (firstChunk >= toRead) {
		memcpy(buffer, fAudioRingBuffer + tail, toRead);
	} else {
		memcpy(buffer, fAudioRingBuffer + tail, firstChunk);
		memcpy((uint8*)buffer + firstChunk, fAudioRingBuffer, toRead - firstChunk);
	}

	atomic_set(&fAudioRingTail, (tail + toRead) % fAudioRingSize);

	return toRead;
}


status_t
UVCCamDevice::_SelectAudioAlternate()
{
	if (fDevice == NULL || fAudioStreamingIndex == 0) {
		syslog(LOG_ERR, "UVCCamDevice: _SelectAudioAlternate: No device or "
			"streaming index\n");
		return B_ERROR;
	}

	const BUSBConfiguration* config = fDevice->ActiveConfiguration();
	if (config == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: _SelectAudioAlternate: No active config\n");
		return B_ERROR;
	}

	const BUSBInterface* streaming = config->InterfaceAt(fAudioStreamingIndex);
	if (streaming == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: _SelectAudioAlternate: Interface %u "
			"not found\n", (unsigned)fAudioStreamingIndex);
		return B_BAD_INDEX;
	}

	// Find best alternate (highest bandwidth with valid isochronous endpoint)
	uint32 bestBandwidth = 0;
	uint32 alternateIndex = 0;
	uint32 endpointIndex = 0;
	uint32 alternatesChecked = 0;
	uint32 endpointsChecked = 0;

	syslog(LOG_INFO, "UVCCamDevice: Scanning %u audio alternates\n",
		(unsigned)streaming->CountAlternates());

	for (uint32 i = 1; i < streaming->CountAlternates(); i++) {
		const BUSBInterface* alternate = streaming->AlternateAt(i);
		if (alternate == NULL)
			continue;

		alternatesChecked++;

		for (uint32 j = 0; j < alternate->CountEndpoints(); j++) {
			const BUSBEndpoint* endpoint = alternate->EndpointAt(j);
			if (endpoint == NULL)
				continue;

			endpointsChecked++;

			// Must be isochronous input endpoint
			if (!endpoint->IsIsochronous() || !endpoint->IsInput())
				continue;

			// Validate endpoint descriptor
			const usb_endpoint_descriptor* desc = endpoint->Descriptor();
			if (desc == NULL) {
				syslog(LOG_WARNING, "UVCCamDevice: Audio endpoint %u.%u has "
					"no descriptor\n", (unsigned)i, (unsigned)j);
				continue;
			}

			uint32 maxPacketSize = desc->max_packet_size & 0x7FF;

			// Sanity check: packet size should be reasonable for audio
			// Minimum: 1 sample * 2 bytes * 1 channel = 2 bytes
			// Maximum: 48kHz * 2ch * 2bytes / 1000 * 2 = 384 bytes (with margin)
			if (maxPacketSize < 2 || maxPacketSize > 1024) {
				syslog(LOG_WARNING, "UVCCamDevice: Audio endpoint %u.%u has "
					"unusual packet size: %u\n",
					(unsigned)i, (unsigned)j, (unsigned)maxPacketSize);
			}

			if (maxPacketSize > bestBandwidth) {
				bestBandwidth = maxPacketSize;
				endpointIndex = j;
				alternateIndex = i;
			}
		}
	}

	syslog(LOG_INFO, "UVCCamDevice: Checked %u alternates, %u endpoints\n",
		(unsigned)alternatesChecked, (unsigned)endpointsChecked);

	if (bestBandwidth == 0 || alternateIndex == 0) {
		syslog(LOG_ERR, "UVCCamDevice: No suitable audio alternate found\n");
		return B_ERROR;
	}

	syslog(LOG_INFO, "UVCCamDevice: Selected audio alternate %u, endpoint %u, "
		"maxPacket %u\n",
		(unsigned)alternateIndex, (unsigned)endpointIndex,
		(unsigned)bestBandwidth);

	// Same Haiku bug workaround as video - must use SetAlternate() despite the bug
	// because ControlTransfer doesn't work for SET_INTERFACE in Haiku.
	if (fCurrentAudioAlternate != alternateIndex) {
		syslog(LOG_INFO, "UVCCamDevice: Audio changing alternate from %u to %u\n",
			(unsigned)fCurrentAudioAlternate, (unsigned)alternateIndex);

		// Call SetAlternate - may crash on unpatched Haiku
		status_t setAltResult = ((BUSBInterface*)streaming)->SetAlternate(alternateIndex);
		if (setAltResult != B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Audio SetAlternate(%u) failed: %s\n",
				(unsigned)alternateIndex, strerror(setAltResult));
			return setAltResult;
		}

		syslog(LOG_INFO, "UVCCamDevice: Audio SetAlternate(%u) successful\n",
			(unsigned)alternateIndex);
		fCurrentAudioAlternate = alternateIndex;

		// Re-fetch interface reference to avoid corrupted pointers
		streaming = config->InterfaceAt(fAudioStreamingIndex);
		if (streaming == NULL) {
			syslog(LOG_ERR, "UVCCamDevice: Audio interface lost after SetAlternate\n");
			return B_BAD_INDEX;
		}
	}

	// Get endpoint from selected alternate
	const BUSBInterface* selectedAlt = streaming->AlternateAt(alternateIndex);
	if (selectedAlt == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: Selected alternate %u not found\n",
			(unsigned)alternateIndex);
		return B_ERROR;
	}

	fAudioIsoIn = selectedAlt->EndpointAt(endpointIndex);
	if (fAudioIsoIn == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: Audio endpoint %u not found in "
			"alternate %u\n", (unsigned)endpointIndex, (unsigned)alternateIndex);
		return B_ERROR;
	}

	fAudioMaxPacketSize = bestBandwidth;

	// Allocate audio buffer for isochronous transfers
	// Free previous buffer if re-entering after a restart
	// FIX-H8: MaxPacketSize comes from the descriptor; 0 would malloc(0)
	// and a huge value would overflow the 32-bit product.
	const uint32 kAudioPackets = 16;
	if (fAudioMaxPacketSize == 0)
		return B_BAD_VALUE;
	uint64 audioLen64 = (uint64)fAudioMaxPacketSize * kAudioPackets;
	if (audioLen64 == 0 || audioLen64 > 4u * 1024 * 1024)
		return B_BAD_VALUE;
	free(fAudioBuffer);
	fAudioBufferLen = (size_t)audioLen64;
	fAudioBuffer = (uint8*)malloc(fAudioBufferLen);
	if (fAudioBuffer == NULL) {
		syslog(LOG_ERR, "UVCCamDevice: Failed to allocate %u bytes for "
			"audio buffer\n", (unsigned)fAudioBufferLen);
		fAudioIsoIn = NULL;
		return B_NO_MEMORY;
	}

	syslog(LOG_INFO, "UVCCamDevice: Audio ready: %u Hz, %u ch, buffer %u bytes\n",
		(unsigned)fAudioSampleRate, (unsigned)fAudioChannels,
		(unsigned)fAudioBufferLen);

	return B_OK;
}


status_t
UVCCamDevice::_SelectAudioIdleAlternate()
{
	if (fDevice == NULL || fAudioStreamingIndex == 0)
		return B_ERROR;

	syslog(LOG_INFO, "UVCCamDevice: Audio switching to idle (alternate 0)\n");

	// Switch to alternate 0 for consistency with _SelectAudioAlternate()
	if (fCurrentAudioAlternate != 0) {
		const BUSBConfiguration* config = fDevice->ActiveConfiguration();
		if (config != NULL) {
			const BUSBInterface* streaming = config->InterfaceAt(fAudioStreamingIndex);
			if (streaming != NULL) {
				status_t result = ((BUSBInterface*)streaming)->SetAlternate(0);
				if (result == B_OK) {
					syslog(LOG_INFO, "UVCCamDevice: Audio SetAlternate(0) successful\n");
					fCurrentAudioAlternate = 0;
				} else {
					syslog(LOG_WARNING, "UVCCamDevice: Audio SetAlternate(0) failed: %s\n",
						strerror(result));
				}
			}
		}
	}

	// Invalidate endpoint references
	fAudioIsoIn = NULL;
	fAudioMaxPacketSize = 0;

	if (fAudioBuffer != NULL) {
		free(fAudioBuffer);
		fAudioBuffer = NULL;
		fAudioBufferLen = 0;
	}

	return B_OK;
}


int32
UVCCamDevice::_audio_pump_thread_(void* data)
{
	return ((UVCCamDevice*)data)->AudioPumpThread();
}


int32
UVCCamDevice::AudioPumpThread()
{
	if (fAudioIsoIn == NULL || fAudioBuffer == NULL || fAudioRingBuffer == NULL)
		return B_ERROR;

	// Audio USB class sends 1 packet per 1ms USB frame.
	// With EHCI, not all packet slots get filled. Using fewer packets
	// per transfer means more frequent transfers and better data capture.
	// 4 packets = 4ms per transfer - minimizes empty slots.
	const uint32 kPacketsPerTransfer = 4;
	usb_iso_packet_descriptor packetDescs[kPacketsPerTransfer];

	// Use the endpoint's maxPacketSize for USB slot allocation.
	// The actual payload per packet varies (e.g., 128 bytes for 32kHz stereo)
	// but the kernel allocates fixed-size slots based on the transfer buffer.
	// Using maxPacketSize ensures no data gets truncated.
	uint32 bytesPerPacket = fAudioMaxPacketSize;
	if (bytesPerPacket == 0) {
		bytesPerPacket = (fAudioSampleRate * fAudioChannels * 2) / 1000;
		if (bytesPerPacket == 0)
			bytesPerPacket = 192;
	}
	syslog(LOG_INFO, "UVCCamDevice: Audio pump: maxPkt=%u, slotSize=%u, "
		"rate=%u, ch=%u\n",
		(unsigned)fAudioMaxPacketSize, (unsigned)bytesPerPacket,
		(unsigned)fAudioSampleRate, (unsigned)fAudioChannels);

	// Retry configuration (similar to video transfer retry logic)
	const uint32 kMaxRetries = 3;
	const bigtime_t kInitialBackoff = 1000;		// 1ms
	const bigtime_t kMaxBackoff = 10000;		// 10ms

	uint32 consecutiveErrors = 0;
	bigtime_t currentBackoff = kInitialBackoff;

	// Statistics for logging
	uint32 transferCount = 0;
	uint32 errorCount = 0;
	bigtime_t lastLogTime = system_time();

	while (fAudioTransferRunning) {
		// Verify endpoint is still valid (device may have been unplugged)
		if (fAudioIsoIn == NULL)
			break;

		// Initialize packet descriptors
		for (uint32 i = 0; i < kPacketsPerTransfer; i++) {
			packetDescs[i].request_length = bytesPerPacket;
			packetDescs[i].actual_length = 0;
			packetDescs[i].status = B_OK;
		}

		// Perform isochronous transfer with retry logic
		ssize_t transferred = -1;
		uint32 retryCount = 0;

		while (retryCount < kMaxRetries && fAudioTransferRunning) {
			// Snapshot endpoint pointer to avoid TOCTOU race with hot-unplug
			const BUSBEndpoint* isoIn = fAudioIsoIn;
			if (isoIn == NULL) {
				syslog(LOG_WARNING,
					"UVCCamDevice: Audio endpoint lost during transfer\n");
				fAudioTransferRunning = false;
				break;
			}

			transferred = isoIn->IsochronousTransfer(fAudioBuffer,
				bytesPerPacket * kPacketsPerTransfer, packetDescs,
				kPacketsPerTransfer);

			if (transferred >= 0)
				break;

			// Transient error - retry with backoff
			retryCount++;
			if (retryCount < kMaxRetries) {
				snooze(currentBackoff);
				currentBackoff = min_c(currentBackoff * 2, kMaxBackoff);
			}
		}

		transferCount++;

		if (transferred < 0) {
			errorCount++;
			consecutiveErrors++;

			if (consecutiveErrors == 10) {
				syslog(LOG_WARNING,
					"UVCCamDevice: Audio transfer errors: %u consecutive\n",
					(unsigned)consecutiveErrors);
			}

			// After 50 consecutive failures, attempt endpoint recovery
			// by stopping and restarting the alternate setting
			if (consecutiveErrors == 50) {
				syslog(LOG_WARNING,
					"UVCCamDevice: Audio: 50 failures, attempting recovery\n");
				snooze(50000);
				consecutiveErrors = 0;
			}

			snooze(currentBackoff);
			if (currentBackoff < 10000)
				currentBackoff *= 2;
			continue;
		}

		// Success - reset error tracking
		if (consecutiveErrors > 0) {
			consecutiveErrors = 0;
			currentBackoff = kInitialBackoff;
		}

		// Periodic statistics logging (every 30 seconds)
		bigtime_t now = system_time();
		if (now - lastLogTime > 30000000) {
			if (errorCount > 0) {
				syslog(LOG_INFO,
					"UVCCamDevice: Audio stats: %u transfers, %u errors (%.1f%%)\n",
					(unsigned)transferCount, (unsigned)errorCount,
					100.0f * errorCount / transferCount);
			}
			lastLogTime = now;
			transferCount = 0;
			errorCount = 0;
		}

		// Copy received audio data to ring buffer using atomic operations
		for (uint32 i = 0; i < kPacketsPerTransfer; i++) {
			if (packetDescs[i].status != B_OK || packetDescs[i].actual_length == 0)
				continue;

			uint8* packetData = fAudioBuffer + (i * bytesPerPacket);
			size_t packetLen = packetDescs[i].actual_length;

			// Log first few packets to diagnose distortion
			static int32 sAudioPktLog = 0;
			if (++sAudioPktLog <= 10) {
				syslog(LOG_INFO, "AUDIO pkt[%u]: actual=%zu slot=%u first8=[%02x %02x %02x %02x %02x %02x %02x %02x]\n",
					i, packetLen, (unsigned)bytesPerPacket,
					packetData[0], packetData[1], packetData[2], packetData[3],
					packetData[4], packetData[5], packetData[6], packetData[7]);
			}

			// Validate packet length against buffer size
			if (packetLen > bytesPerPacket)
				packetLen = bytesPerPacket;

			// Ensure packet length is aligned to sample frame boundary
			// (channels * 2 bytes per sample). Unaligned data causes distortion.
			uint32 frameSize = fAudioChannels * 2;
			if (frameSize > 0)
				packetLen -= packetLen % frameSize;

			// Calculate space in ring buffer
			int32 head = atomic_get(&fAudioRingHead);
			int32 tail = atomic_get(&fAudioRingTail);

			// Validate pointers are within bounds
			if (head < 0 || (size_t)head >= fAudioRingSize
				|| tail < 0 || (size_t)tail >= fAudioRingSize) {
				atomic_set(&fAudioRingHead, 0);
				atomic_set(&fAudioRingTail, 0);
				continue;
			}

			ssize_t space;
			if (head >= tail)
				space = (ssize_t)fAudioRingSize - (head - tail) - 1;
			else
				space = tail - head - 1;

			if (space < 0)
				space = 0;

			if ((size_t)space < packetLen) {
				static int32 sOverflowLog = 0;
				if (++sOverflowLog <= 5 || (sOverflowLog % 100) == 0)
					syslog(LOG_WARNING,
						"UVCCamDevice: Audio ring buffer overflow #%d "
						"(need %zu, have %zu)\n",
						(int)sOverflowLog, packetLen, space);
				continue;
			}

			// Copy to ring buffer (handle wraparound)
			size_t firstChunk = fAudioRingSize - head;
			if (firstChunk >= packetLen) {
				memcpy(fAudioRingBuffer + head, packetData, packetLen);
			} else {
				memcpy(fAudioRingBuffer + head, packetData, firstChunk);
				memcpy(fAudioRingBuffer, packetData + firstChunk,
					packetLen - firstChunk);
			}

			atomic_set(&fAudioRingHead, (head + packetLen) % fAudioRingSize);

			// Signal consumer that data is available
			release_sem_etc(fAudioRingSem, 1, B_DO_NOT_RESCHEDULE);
		}
	}

	return B_OK;
}

