/*
 * Copyright 2004-2008, François Revol, <revol@free.fr>.
 * Distributed under the terms of the MIT License.
 */

#include "CamDebug.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>


// Global debug level - default to INFO
webcam_debug_level gWebcamDebugLevel = WEBCAM_DEBUG_INFO;


void
InitWebcamDebugLevel()
{
	const char* envLevel = getenv("WEBCAM_DEBUG");
	if (envLevel == NULL) {
		gWebcamDebugLevel = WEBCAM_DEBUG_INFO;
		return;
	}

	// Parse level from environment variable
	// Accepts: none, error, warn, info, verbose, trace, or 0-5
	if (strcmp(envLevel, "none") == 0 || strcmp(envLevel, "0") == 0) {
		gWebcamDebugLevel = WEBCAM_DEBUG_NONE;
	} else if (strcmp(envLevel, "error") == 0 || strcmp(envLevel, "1") == 0) {
		gWebcamDebugLevel = WEBCAM_DEBUG_ERROR;
	} else if (strcmp(envLevel, "warn") == 0 || strcmp(envLevel, "2") == 0) {
		gWebcamDebugLevel = WEBCAM_DEBUG_WARN;
	} else if (strcmp(envLevel, "info") == 0 || strcmp(envLevel, "3") == 0) {
		gWebcamDebugLevel = WEBCAM_DEBUG_INFO;
	} else if (strcmp(envLevel, "verbose") == 0 || strcmp(envLevel, "4") == 0) {
		gWebcamDebugLevel = WEBCAM_DEBUG_VERBOSE;
	} else if (strcmp(envLevel, "trace") == 0 || strcmp(envLevel, "5") == 0) {
		gWebcamDebugLevel = WEBCAM_DEBUG_TRACE;
	} else {
		// Try to parse as integer
		int level = atoi(envLevel);
		if (level >= WEBCAM_DEBUG_NONE && level <= WEBCAM_DEBUG_TRACE) {
			gWebcamDebugLevel = (webcam_debug_level)level;
		} else {
			gWebcamDebugLevel = WEBCAM_DEBUG_INFO;
		}
	}

	syslog(LOG_INFO, "[Webcam] Debug level set to %d from environment\n",
		gWebcamDebugLevel);
}


void
SetWebcamDebugLevel(webcam_debug_level level)
{
	if (level >= WEBCAM_DEBUG_NONE && level <= WEBCAM_DEBUG_TRACE) {
		gWebcamDebugLevel = level;
		syslog(LOG_INFO, "[Webcam] Debug level changed to %d\n", level);
	}
}
