/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */
#ifndef _CAM_DEBUG_H
#define _CAM_DEBUG_H

#include <Debug.h>
#include <syslog.h>

/* Debug logging control - comment out to disable verbose logging */
//#define DEBUG_LOGGING 1
//#define DEBUG_FILE_LOGGING 1

/* allow overriding ANSI color */
#ifndef CD_COL
#define CD_COL "34"
#endif

#define CH "\033[" CD_COL "mWebcam::%s::%s"
#define CT "\033[0m\n", __BASE_FILE__, __FUNCTION__


// PHASE 10: Debug level system
enum webcam_debug_level {
	WEBCAM_DEBUG_NONE = 0,		// No debug output
	WEBCAM_DEBUG_ERROR = 1,		// Errors only
	WEBCAM_DEBUG_WARN = 2,		// Warnings and errors
	WEBCAM_DEBUG_INFO = 3,		// Informational messages
	WEBCAM_DEBUG_VERBOSE = 4,	// Verbose output
	WEBCAM_DEBUG_TRACE = 5		// Full trace (very verbose)
};


// Global debug level (set via environment variable or API)
// Default: WEBCAM_DEBUG_INFO
extern webcam_debug_level gWebcamDebugLevel;


// Initialize debug level from environment variable WEBCAM_DEBUG
// Call this at addon initialization
void InitWebcamDebugLevel();


// Set debug level programmatically
void SetWebcamDebugLevel(webcam_debug_level level);


// Debug macros with level checking
#define WEBCAM_LOG(level, format, ...) \
	do { \
		if (gWebcamDebugLevel >= (level)) { \
			syslog(LOG_INFO, "[Webcam] " format, ##__VA_ARGS__); \
		} \
	} while (0)

#define WEBCAM_ERROR(format, ...) WEBCAM_LOG(WEBCAM_DEBUG_ERROR, "ERROR: " format, ##__VA_ARGS__)
#define WEBCAM_WARN(format, ...)  WEBCAM_LOG(WEBCAM_DEBUG_WARN, "WARN: " format, ##__VA_ARGS__)
#define WEBCAM_INFO(format, ...)  WEBCAM_LOG(WEBCAM_DEBUG_INFO, format, ##__VA_ARGS__)
#define WEBCAM_VERBOSE(format, ...) WEBCAM_LOG(WEBCAM_DEBUG_VERBOSE, format, ##__VA_ARGS__)
#define WEBCAM_TRACE(format, ...) WEBCAM_LOG(WEBCAM_DEBUG_TRACE, "TRACE: %s: " format, __FUNCTION__, ##__VA_ARGS__)


/* Conditional debug macros (legacy) */
#ifdef DEBUG_LOGGING
#define DEBUG_PRINT(x) fprintf x
#define DEBUG_FPRINTF(...) fprintf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x) do {} while(0)
#define DEBUG_FPRINTF(...) do {} while(0)
#endif

#ifdef DEBUG_FILE_LOGGING
#define DEBUG_LOG_FILE_OPEN(file, path, mode) file = fopen(path, mode)
#define DEBUG_LOG_FILE_CLOSE(file) if(file) { fclose(file); file = NULL; }
#else
#define DEBUG_LOG_FILE_OPEN(file, path, mode) file = NULL
#define DEBUG_LOG_FILE_CLOSE(file) do {} while(0)
#endif

#endif
