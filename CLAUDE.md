# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Haiku OS media add-on (driver) for USB webcams, implementing the USB Video Class (UVC) specification. The driver supports MJPEG and YUY2 video formats with multi-resolution selection. It includes optional USB Audio Class 1.0 support for webcams with built-in microphones.

## Build Commands

```bash
# Build the media add-on
make

# Clean build artifacts
make clean

# Install to Haiku's non-packaged add-ons directory
make install
```

The build produces `aukey_webcam_v4.media_addon` which gets installed to `/boot/home/config/non-packaged/add-ons/media/`.

## Dependencies

- libturbojpeg (for MJPEG decompression)
- Haiku private headers (media, shared, drivers)

## Architecture

### Core Components

- **WebCamMediaAddOn** (`AddOn.cpp/h`): BMediaAddOn subclass that registers with Haiku's Media Kit. Entry point via `make_media_addon()`. Manages camera discovery and node instantiation.

- **CamRoster** (`CamRoster.cpp/h`): USB device roster that watches for webcam connections using BUSBRoster. Maintains list of supported device addons and active cameras.

- **CamDevice** (`CamDevice.cpp/h`): Abstract base class for all webcam devices. Handles USB communication, frame buffering, data pump thread, and sensor abstraction. Subclassed by specific device implementations.

- **VideoProducer** (`Producer.cpp/h`): BBufferProducer node that connects to Media Kit. Handles format negotiation, buffer management, and frame delivery. Uses BMediaEventLooper for timing.

- **AudioProducer** (`AudioProducer.cpp/h`): BBufferProducer node for audio from webcams with built-in microphones.

### Device Addons (in `addons/`)

- **UVCCamDevice** (`addons/uvc/`): USB Video Class implementation. Handles UVC probe/commit protocol, format negotiation, isochronous transfers, and MJPEG decompression via libturbojpeg. Main driver for modern webcams.

- **NW80xCamDevice** (`addons/NW80xCamDevice.cpp`): Legacy driver for NW80x chipset webcams.

### Frame Processing Pipeline

1. USB isochronous/bulk data → CamDevice data pump thread
2. CamDeframer extracts frames from USB packets
3. UVCCamDevice converts YUY2→RGB32 or decompresses MJPEG→RGB32
4. VideoProducer delivers BBuffers to connected consumers

### Key Data Structures

- `usbvc_probecommit`: UVC probe/commit structure for format negotiation
- `usb_webcam_support_descriptor`: Device matching table with VID/PID and sensor info
- Frame lists (`fUncompressedFrames`, `fMJPEGFrames`): Available resolutions per format

## Debugging

View driver logs:
```bash
tail -f /var/log/syslog | grep -i "UVC\|MJPEG\|webcam"
```

### Debug Levels

Set the `WEBCAM_DEBUG` environment variable to control verbosity:
- `none` or `0`: No debug output
- `error` or `1`: Errors only
- `warn` or `2`: Warnings and errors
- `info` or `3`: Informational messages (default)
- `verbose` or `4`: Verbose output
- `trace` or `5`: Full trace

Example: `export WEBCAM_DEBUG=verbose`

## Reliability Features

### USB Transfer Retry with Exponential Backoff
- Automatic retry on transient USB errors (timeout, stall, CRC)
- Exponential backoff: 100ms → 200ms → 400ms (max 1s)
- Configurable via `usb_retry_config` structure

### Hot-Plug Reconnection
- Device identification by VID/PID and serial number
- Parameter persistence across disconnects (resolution, settings)
- Automatic parameter restoration on reconnect

### Packet Loss Monitoring
- Real-time packet error rate tracking
- Automatic resolution fallback when loss exceeds 5%
- Three consecutive high-loss events trigger fallback

### Error Tracking
- USB error classification (timeout, stall, CRC, overflow, disconnect)
- Error histogram for diagnostics
- LogErrorStatistics() method for error reporting

## Key Implementation Notes

- USB descriptor parsing may fail on Haiku; the driver includes hardcoded resolution fallbacks for known devices (AUKEY PC-LM1E)
- High-bandwidth USB endpoints (3 transactions/microframe) are not used due to Haiku EHCI driver limitations
- Resolution changes require stream restart for proper buffer reallocation
- Uses atomic operations for thread-safe transfer state (`fTransferEnabled`)
- Camera disconnect safety: `SetCamDevice(NULL)` is called before device destruction to prevent use-after-free in the VideoProducer event loop
- Microdia 0c45:6409 (Sonix): YUY2-only camera with known horizontal tearing issue due to USB stream byte misalignment. The camera responds to Sonix XU ASIC read commands (unit 4, GUID 7033f028) but blocks format register writes. No MJPEG support in firmware. The chip ID reads as 0x01 (unknown Sonix variant, not SN9C291/292). On Linux this device is NOT supported by any specific driver.
- UVC Extension Units: Detected and parsed for Sonix, Microsoft, Logitech, Realtek vendors. Sonix XU GUID matching implemented for LED and face detection capabilities.
- Audio ring buffer uses semaphore-based synchronization: producer (AudioPumpThread) signals `release_sem()` after writing, consumer (ReadAudioData) waits with `acquire_sem_etc()` with timeout. Handles `B_BAD_SEM_ID` for clean shutdown.
- Stride quirk for Microdia 0c45:6409 only activates when source data has actual row padding (`srcSize > expectedSize`), preventing incorrect stride application on cameras that send standard row-aligned YUY2.
- MJPEG deframer uses fixed buffer (`fFixedBuffer`) for both YUY2 and MJPEG frame assembly. Frame completion on FID toggle reads from fixed buffer, not BMallocIO.

## Still Image Capture

The driver supports UVC still image capture via `TriggerStillCapture()`:
- Method 2 (Host Software Triggered): Sends VS_STILL_IMAGE_TRIGGER_CONTROL SET_CUR
- Parses VS_STILL_IMAGE_FRAME descriptors for available resolutions
- Returns raw frame data (MJPEG or YUY2 depending on active format)
- Methods 1 and 3 (hardware button) are detected but not host-triggerable

## Haiku USB Bug Workaround

The driver includes a workaround for a bug in Haiku's `BUSBInterface::SetAlternate()`. A patch file is provided in `patches/` directory for upstream contribution.

## Known Haiku USB Issues

- EHCI isochronous transfers may cause kernel panic ("General Protection Exception" in `EHCI::FinishIsochronousTransfers`) after prolonged streaming. This is a race condition in the Haiku EHCI driver, not in this media addon.
- EHCI "host system error" may occur during sustained high-bandwidth isochronous transfers, causing the USB controller to stop responding.

## Performance Optimizations

### USB Transfer Optimizations (Group 1)

The driver implements several performance optimizations for USB transfers:

1. **Removed memset() from hot path**: The USB receive buffer is no longer pre-filled with zeros on each transfer. This eliminates ~128KB of memory writes per transfer (~4778x speedup measured).

2. **Throttled logging**: Debug logs are throttled to reduce syslog overhead:
   - First 5 transfers logged for debugging
   - Then every 1000 transfers OR every 5 seconds
   - Reduces log volume by ~99.8%

3. **Minimized lock scope**: The BLocker is now held only during the USB transfer itself, not during data processing. This improves concurrency.

4. **Double buffering infrastructure**: The `usb_double_buffer` structure provides:
   - Two alternating buffers for USB reception
   - Semaphore-based synchronization
   - Automatic cleanup on destruction

### Memory Management Optimizations (Group 2)

1. **Frame pool recycling**: Frames are recycled instead of allocated/freed:
   - Pool capacity: 12 frames
   - 99.9% reuse rate in multi-threaded scenarios
   - `RecycleFrame()` returns frames to pool for reuse

2. **Conditional pre-fill**: Buffer pre-fill only for invalid frames:
   - Valid frames: no pre-fill (MJPEG/YUY2 overwrites entire buffer)
   - Invalid frames: memset with background color
   - 13.6x speedup measured (90% valid frame assumption)

3. **Pool statistics**: Tracks hits/misses for monitoring:
   - Logged on destruction for diagnostics
   - Helps identify memory pressure issues

### Video Conversion Optimizations (Group 3)

1. **YUV-RGB lookup tables**: Pre-computed conversion tables eliminate per-pixel multiplications:
   - 5 tables: Y, U→B, U→G, V→R, V→G contributions
   - Memory footprint: ~5KB (int32 tables to avoid overflow)
   - Tables initialized once, shared across all device instances

2. **Optimized clamp function**: Branchless clamp for RGB value clamping:
   - Eliminates conditional branches in inner loop
   - ~1.2-1.4x speedup measured for YUY2→RGB32 conversion

3. **Conversion accuracy**: Output matches original per-pixel algorithm:
   - 0 differences with original implementation
   - Proper handling of edge cases (black, white, saturated colors)

### Timing and Latency Optimizations (Group 4)

1. **Frame timing statistics**: Comprehensive tracking of frame delivery:
   - Frame interval min/max/average tracking
   - Processing time statistics
   - Jitter calculation (deviation from expected interval)

2. **Adaptive timeout**: Dynamically adjusted timeout based on actual performance:
   - Exponential moving average of observed intervals
   - Automatic clamping (10ms - 500ms range)
   - Accounts for processing time overhead

3. **Logging**: `LogFrameTimingStats()` for diagnostics:
   - Average FPS calculation
   - Interval distribution
   - Jitter and processing time analysis

### Running Tests

```bash
cd tests

# USB Performance Tests (Group 1)
g++ -O2 -o test_usb_performance test_usb_performance.cpp -lbe
./test_usb_performance

# Memory Management Tests (Group 2)
g++ -O2 -o test_memory_management test_memory_management.cpp -lbe
./test_memory_management

# Video Conversion Tests (Group 3)
g++ -O2 -o test_video_conversion test_video_conversion.cpp -lbe
./test_video_conversion

# Timing and Latency Tests (Group 4)
g++ -O2 -o test_timing_latency test_timing_latency.cpp -lbe
./test_timing_latency

# Error Handling Tests (Group 5)
g++ -O2 -o test_error_handling test_error_handling.cpp -lbe
./test_error_handling

# Deframer Tests (Group 6)
g++ -O2 -o test_deframer test_deframer.cpp -lbe
./test_deframer

# Architecture Tests (Group 7)
g++ -O2 -I.. -o test_architecture test_architecture.cpp -lbe
./test_architecture

# Audio Tests (Group 8)
g++ -O2 -o test_audio test_audio.cpp -lbe
./test_audio
```

### Error Handling Optimizations (Group 5)

1. **Error recovery actions**: Each error type maps to a recommended recovery action:
   - NONE: No action needed
   - RETRY: Retry the operation
   - RESET_ENDPOINT: Reset the USB endpoint
   - REDUCE_BANDWIDTH: Lower resolution/fps
   - RESTART_TRANSFER: Stop and restart stream
   - DEVICE_RESET: Full device reset
   - FATAL: Unrecoverable error

2. **Automatic escalation**: Recovery actions escalate based on:
   - Error rate thresholds (warning at 5%, action at 10%)
   - Consecutive error count (max 20 before escalation)
   - Evaluation window for calculating rates

3. **Recovery configuration**: `error_recovery_config` structure provides:
   - Configurable thresholds
   - Recovery attempt tracking
   - `EvaluateErrorRecovery()` for action recommendation
   - `ShouldTriggerRecovery()` for threshold checking

### Deframer Optimizations (Group 6)

1. **Frame statistics tracking**: `deframer_stats` structure provides:
   - Frames completed/incomplete count
   - FID (Frame ID) change tracking
   - Queue overflow monitoring
   - Expected frame size validation

2. **Completion rate metrics**: Real-time frame quality monitoring:
   - `GetCompletionRate()`: Percentage of complete frames
   - `GetIncompleteRate()`: Percentage of incomplete frames
   - Sum always equals 100% (or 100% completion when no data)

3. **UVC protocol handling**:
   - FID bit toggle detection for frame boundaries
   - EOF (End of Frame) marker processing
   - UVC error bit detection in header
   - Header validation (length, PTS/SCR flags)

4. **YUY2 frame padding**: Incomplete frames are padded with black pixels:
   - Pattern: Y=0, U=128, V=128 (black in YUY2)
   - Prevents stair-step artifacts from incomplete data
   - Maintains correct row alignment in RGB conversion

5. **Statistics API**:
   - `GetStats()`: Returns current statistics snapshot
   - `ResetStats()`: Clears all counters for fresh measurement

### Architecture Optimizations (Group 7)

1. **RAII utility classes** (`CamUtils.h`):
   - `ScopedLock`: Automatic lock/unlock for BLocker
   - `ScopedSemaphore`: Automatic acquire/release with timeout support
   - `ScopedBuffer<T>`: Automatic memory allocation and cleanup
   - `AtomicFlag`: Thread-safe boolean with TestAndSet
   - `AtomicCounter`: Thread-safe counter with Increment/Decrement
   - `RingBufferIndex`: Lock-free circular buffer index management

2. **Result<T> type**: Success/error wrapper similar to std::expected:
   - Encapsulates value or error status
   - `IsOK()`, `Value()`, `ValueOr()` methods
   - Error construction via `Result<T>::Error(status_t)`

3. **Central configuration** (`CamConfig.h`):
   - All magic numbers consolidated in one place
   - USB transfer constants (buffer sizes, timeouts)
   - Frame pool configuration
   - Error handling thresholds
   - UVC protocol constants

4. **Resolution helpers**:
   - `CamConfig::Resolution` struct with width/height
   - Pre-defined resolutions (VGA, 720p, 1080p)
   - `YUY2Size()` and `RGB32Size()` methods

5. **Timing utilities**:
   - `MicrosecondsToFPS()` / `FPSToMicroseconds()`
   - Unit conversion helpers
   - `ClampByte()` for optimized RGB conversion

### Audio Optimizations (Group 8)

1. **Audio timing statistics** (`audio_timing_stats` structure):
   - Buffer delivery timing (interval min/max/average)
   - Buffer rate calculation (buffers per second)
   - Underrun/overrun tracking

2. **Audio level metering**:
   - Peak level detection (0.0 - 1.0 normalized)
   - RMS level calculation for average volume
   - Running statistics across all samples processed

3. **Buffer quality metrics**:
   - `buffers_sent` / `buffers_dropped` counters
   - `GetDropRate()`: Percentage of failed buffer deliveries
   - Underrun detection when no audio data available

4. **Statistics API**:
   - `GetAudioStats()`: Returns current audio statistics
   - `ResetAudioStats()`: Clears all counters
   - `LogAudioStats()`: Periodic logging (every 30s)

5. **Diagnostic output**: Logs include:
   - Buffer send/drop counts and rates
   - Timing interval distribution
   - Peak and RMS audio levels

6. **Ring buffer synchronization**:
   - Producer (AudioPumpThread) signals semaphore after each write
   - Consumer (ReadAudioData) uses `acquire_sem_etc()` with 10ms timeout
   - Handles `B_BAD_SEM_ID` for clean shutdown when audio stops
   - Space calculation: `fAudioRingSize - (head - tail) - 1` (guard byte prevents empty/full confusion)

7. **Safety**:
   - All `release_sem(fFrameSync)` calls guarded with `fFrameSync >= 0` check
   - `strlcpy()` used for all media node name strings (no buffer overflows)
   - Audio frame alignment uses dynamic `channel_count * sizeof(int16)` not hardcoded 4
