/*
 * Copyright 2011, Gabriel Hartmann, gabriel.hartmann@gmail.com.
 * Distributed under the terms of the MIT License.
 *
 * UVCCamDevice parameter, control and XU methods, split from UVCCamDevice.cpp (RF-05).
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

void
UVCCamDevice::_AddProcessingParameter(BParameterGroup* group,
	int32 index, const usb_video_processing_unit_descriptor* descriptor)
{
	BParameterGroup* subgroup;
	uint16 wValue = 0; // Control Selector
	float minValue = 0.0;
	float maxValue = 100.0;
	if (descriptor->control_size >= 1) {
		if (descriptor->controls[0] & 1) {
			// debug_printf("\tBRIGHTNESS\n");
			fBrightness = _AddParameter(group, &subgroup, index,
				USB_VIDEO_PU_BRIGHTNESS_CONTROL, "Brightness");
		}
		if (descriptor->controls[0] & 2) {
			// debug_printf("\tCONSTRAST\n");
			fContrast = _AddParameter(group, &subgroup, index + 1,
				USB_VIDEO_PU_CONTRAST_CONTROL, "Contrast");
		}
		if (descriptor->controls[0] & 4) {
			// debug_printf("\tHUE\n");
			fHue = _AddParameter(group, &subgroup, index + 2,
				USB_VIDEO_PU_HUE_CONTROL, "Hue");
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 8) {
					fHueAuto = _AddAutoParameter(subgroup, index + 3,
						USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL);
				}
			}
		}
		if (descriptor->controls[0] & 8) {
			// debug_printf("\tSATURATION\n");
			fSaturation = _AddParameter(group, &subgroup, index + 4,
				USB_VIDEO_PU_SATURATION_CONTROL, "Saturation");
		}
		if (descriptor->controls[0] & 16) {
			// debug_printf("\tSHARPNESS\n");
			fSharpness = _AddParameter(group, &subgroup, index + 5,
				USB_VIDEO_PU_SHARPNESS_CONTROL, "Sharpness");
		}
		if (descriptor->controls[0] & 32) {
			// debug_printf("\tGamma\n");
			fGamma = _AddParameter(group, &subgroup, index + 6,
				USB_VIDEO_PU_GAMMA_CONTROL, "Gamma");
		}
		if (descriptor->controls[0] & 64) {
			// debug_printf("\tWHITE BALANCE TEMPERATURE\n");
			fWBTemp = _AddParameter(group, &subgroup, index + 7,
				USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL, "WB Temperature");
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 16) {
					fWBTempAuto = _AddAutoParameter(subgroup, index + 8,
						USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL);
				}
			}
		}
		if (descriptor->controls[0] & 128) {
			// debug_printf("\tWhite Balance Component\n");
			fWBComponent = _AddParameter(group, &subgroup, index + 9,
				USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_CONTROL, "WB Component");
			if (descriptor->control_size >= 2) {
				if (descriptor->controls[1] & 32) {
					fWBTempAuto = _AddAutoParameter(subgroup, index + 10,
						USB_VIDEO_PU_WHITE_BALANCE_COMPONENT_AUTO_CONTROL);
				}
			}
		}
	}
	if (descriptor->control_size >= 2) {
		if (descriptor->controls[1] & 1) {
			// debug_printf("\tBACKLIGHT COMPENSATION\n");
			int16 data;
			wValue = USB_VIDEO_PU_BACKLIGHT_COMPENSATION_CONTROL << 8;
			fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_MAX, wValue, fControlRequestIndex, sizeof(data), &data);
			maxValue = (float)data;
			fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_MIN, wValue, fControlRequestIndex, sizeof(data), &data);
			minValue = (float)data;
			fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data);
			fBacklightCompensation = (float)data;
			subgroup = group->MakeGroup("Backlight Compensation");
			if (maxValue - minValue == 1) { // Binary Switch
				fBinaryBacklightCompensation = true;
				subgroup->MakeDiscreteParameter(index + 11,
					B_MEDIA_RAW_VIDEO, "Backlight Compensation",
					B_ENABLE);
			} else { // Range of values
				fBinaryBacklightCompensation = false;
				subgroup->MakeContinuousParameter(index + 11,
				B_MEDIA_RAW_VIDEO, "Backlight Compensation",
				B_GAIN, "", minValue, maxValue, 1.0 / (maxValue - minValue));
			}
		}
		if (descriptor->controls[1] & 2) {
			// debug_printf("\tGAIN\n");
			fGain = _AddParameter(group, &subgroup, index + 12, USB_VIDEO_PU_GAIN_CONTROL,
				"Gain");
		}
		if (descriptor->controls[1] & 4) {
			// debug_printf("\tPOWER LINE FREQUENCY\n");
			wValue = USB_VIDEO_PU_POWER_LINE_FREQUENCY_CONTROL << 8;
			int8 data;
			if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
					USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data)
				== sizeof(data)) {
				fPowerlineFrequency = data;
			}
			subgroup = group->MakeGroup("Power Line Frequency");
			/* FIX: Use discrete parameter instead of continuous slider */
			BDiscreteParameter* plf = subgroup->MakeDiscreteParameter(index + 13,
				B_MEDIA_RAW_VIDEO, "Anti-Flicker", B_GENERIC);
			plf->AddItem(0, "Disabled");
			plf->AddItem(1, "50 Hz");
			plf->AddItem(2, "60 Hz");
		}
		// TODO Determine whether controls apply to these
		/*
		if (descriptor->controls[1] & 64)
			debug_printf("\tDigital Multiplier\n");
		if (descriptor->controls[1] & 128)
			debug_printf("\tDigital Multiplier Limit\n");
		*/
	}
	// TODO Determine whether controls apply to these
	/*
	if (descriptor->controlSize >= 3) {
		if (descriptor->controls[2] & 1)
			debug_printf("\tAnalog Video Standard\n");
		if (descriptor->controls[2] & 2)
			debug_printf("\tAnalog Video Lock Status\n");
	}
	*/

}


float
UVCCamDevice::_AddParameter(BParameterGroup* group,
	BParameterGroup** subgroup, int32 index, uint16 wValue, const char* name)
{
	float minValue = 0.0;
	float maxValue = 100.0;
	float currValue = 0.0;
	int16 data;

	wValue <<= 8;

	if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_MAX, wValue, fControlRequestIndex, sizeof(data), &data)
		== sizeof(data)) {
		maxValue = (float)data;
	}
	if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_MIN, wValue, fControlRequestIndex, sizeof(data), &data)
		== sizeof(data)) {
		minValue = (float)data;
	}
	if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data)
		== sizeof(data)) {
		currValue = (float)data;
	}

	*subgroup = group->MakeGroup(name);
	(*subgroup)->MakeContinuousParameter(index,
		B_MEDIA_RAW_VIDEO, name, B_GAIN, "", minValue, maxValue,
		1.0 / (maxValue - minValue));
	return currValue;
}


uint8
UVCCamDevice::_AddAutoParameter(BParameterGroup* subgroup, int32 index,
	uint16 wValue)
{
	uint8 data;
	wValue <<= 8;

	fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, 1, &data);
	subgroup->MakeDiscreteParameter(index, B_MEDIA_RAW_VIDEO, "Auto",
		B_ENABLE);

	return data;
}


void
UVCCamDevice::AddParameters(BParameterGroup* group, int32& index)
{
	fFirstParameterID = index;
	CamDevice::AddParameters(group, index);

	// ── Stream Configuration ─────────────────────────────────
	BString streamLabel;
	streamLabel.SetToFormat("Stream (%s)", fIsMJPEG ? "MJPEG" : "YUY2");
	BParameterGroup* streamGroup = group->MakeGroup(streamLabel.String());

	// Resolution selector
	BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;
	if (frameList->CountItems() > 0) {
		fResolutionParameterID = index + 14;

		BDiscreteParameter* resParam = streamGroup->MakeDiscreteParameter(
			fResolutionParameterID, B_MEDIA_RAW_VIDEO, "Resolution",
			B_RESOLUTION);

		for (int32 i = 0; i < frameList->CountItems(); i++) {
			const usb_video_frame_descriptor* desc =
				(const usb_video_frame_descriptor*)frameList->ItemAt(i);
			if (desc == NULL)
				continue;
			BString label;
			label.SetToFormat("%ux%u", desc->width, desc->height);
			resParam->AddItem(i, label.String());
		}

		if (fSelectedResolutionIndex >= frameList->CountItems())
			fSelectedResolutionIndex = 0;

		// Frame rate selector
		const usb_video_frame_descriptor* curFrame =
			(const usb_video_frame_descriptor*)frameList->ItemAt(
				fSelectedResolutionIndex);
		if (curFrame != NULL && curFrame->frame_interval_type > 0) {
			fFrameRateParameterID = index + 16;
			fNumFrameIntervals = curFrame->frame_interval_type;
			if (fNumFrameIntervals > kMaxFrameIntervals)
				fNumFrameIntervals = kMaxFrameIntervals;

			for (uint8 k = 0; k < fNumFrameIntervals; k++)
				fCurrentFrameIntervals[k] =
					curFrame->discrete_frame_intervals[k];

			BDiscreteParameter* fpsParam =
				streamGroup->MakeDiscreteParameter(fFrameRateParameterID,
					B_MEDIA_RAW_VIDEO, "Frame Rate", B_GENERIC);

			for (uint8 k = 0; k < fNumFrameIntervals; k++) {
				if (fCurrentFrameIntervals[k] == 0)
					continue;
				float fps = 10000000.0f / fCurrentFrameIntervals[k];
				BString label;
				label.SetToFormat("%.0f fps", fps);
				fpsParam->AddItem(k, label.String());
			}

			for (uint8 k = 0; k < fNumFrameIntervals; k++) {
				if (fCurrentFrameIntervals[k]
					== curFrame->default_frame_interval) {
					fSelectedFrameIntervalIndex = k;
					break;
				}
			}
			fSelectedFrameInterval =
				fCurrentFrameIntervals[fSelectedFrameIntervalIndex];
		}
	}

	// ── Image Adjustments ────────────────────────────────────
	const BUSBConfiguration* config;
	const BUSBInterface* interface;
	uint8 buffer[1024];
	usb_descriptor* generic = (usb_descriptor*)buffer;

	for (uint32 i = 0; i < fDevice->CountConfigurations(); i++) {
		config = fDevice->ConfigurationAt(i);
		if (config == NULL)
			continue;
		fDevice->SetConfiguration(config);
		for (uint32 j = 0; j < config->CountInterfaces(); j++) {
			interface = config->InterfaceAt(j);
			if (interface == NULL)
				continue;
			if (interface->Class() != USB_VIDEO_DEVICE_CLASS
				|| interface->Subclass()
					!= USB_VIDEO_INTERFACE_VIDEOCONTROL_SUBCLASS)
				continue;
			for (uint32 k = 0; interface->OtherDescriptorAt(k, generic,
				sizeof(buffer)) == B_OK; k++) {
				if (generic->generic.descriptor_type
					!= (USB_REQTYPE_CLASS | USB_DESCRIPTOR_INTERFACE))
					continue;
				if (((const usbvc_class_descriptor*)generic)
					->descriptorSubtype
					== USB_VIDEO_VC_PROCESSING_UNIT) {
					BParameterGroup* imageGroup =
						group->MakeGroup("Image");
					_AddProcessingParameter(imageGroup, index,
						(const usb_video_processing_unit_descriptor*)
							generic);
				}
			}
		}
	}

	// PU uses index+0..13, stream uses +14/+16. Keep them apart
	// from CT/XU which allocate with index++ below.
	index += 20;

	// ── Camera Controls ──────────────────────────────────────
	_AddCameraTerminalControls(group, index);

	// ── Extension Unit Controls ──────────────────────────────
	if (fHasExtensionUnits) {
		BParameterGroup* xuGroup = group->MakeGroup("Vendor Features");

		for (int32 i = 0; i < fExtensionUnits.CountItems(); i++) {
			extension_unit_info* xu =
				(extension_unit_info*)fExtensionUnits.ItemAt(i);
			if (xu == NULL)
				continue;

			// Sonix: LED control
			if (xu->vendor == XU_VENDOR_SONIX
				&& (xu->capabilities & XU_CAP_LED_CONTROL)) {
				fXULedParameterID = ++index;
				BDiscreteParameter* ledParam =
					xuGroup->MakeDiscreteParameter(fXULedParameterID,
						B_MEDIA_RAW_VIDEO, "Camera LED", B_ENABLE);
				ledParam->AddItem(0, "Off");
				ledParam->AddItem(1, "On");
			}

			// Probe XU selectors to discover available controls.
			// Skip the entire unit if the first selector fails,
			// to avoid hanging on unresponsive Extension Units.
			bool xuResponds = false;
			for (uint8 sel = 1; sel <= xu->num_controls; sel++) {
				uint8 info = 0;
				if (_XUGetInfo(xu->unit_id, sel, &info) != B_OK) {
					if (!xuResponds)
						break;	// first probe failed, skip this XU
					continue;
				}
				xuResponds = true;
				if ((info & 0x01) == 0)
					continue;

				// Determine the control's real length first. Probing GET_CUR
				// with a guessed (too-small) length makes UVC 1.1+ devices
				// return the full control payload anyway, which overruns the
				// transfer and halts the control endpoint with an xHCI
				// "Babble detected" error (observed on Logitech C920).
				uint16 ctrlLen = 0;
				if (_XUGetLen(xu->unit_id, sel, &ctrlLen) != B_OK
					|| ctrlLen == 0) {
					// No reliable length: skip the GET_CUR probe rather than
					// risk a babble. GET_INFO already told us it's readable.
					syslog(LOG_INFO, "UVCCamDevice: XU[%d] sel=%d info=0x%02x "
						"%s (length unknown, GET_CUR skipped)\n",
						xu->unit_id, sel, info,
						(info & 0x02) ? "(rw)" : "(ro)");
					continue;
				}

				// Read GET_CUR with the exact negotiated length.
				uint8 probe[64];
				memset(probe, 0, sizeof(probe));
				if (ctrlLen > sizeof(probe))
					ctrlLen = sizeof(probe);
				if (_XUGetCur(xu->unit_id, sel, probe, ctrlLen) != B_OK)
					continue;

				syslog(LOG_INFO, "UVCCamDevice: XU[%d] sel=%d info=0x%02x "
					"len=%u cur=0x%02x %s\n",
					xu->unit_id, sel, info, ctrlLen, probe[0],
					(info & 0x02) ? "(rw)" : "(ro)");
			}
		}
	}
}


status_t
UVCCamDevice::GetParameterValue(int32 id, bigtime_t* last_change, void* value,
	size_t* size)
{
	// No per-poll log here, the Media Kit polls often.
	// Caller provides the buffer, check it before writing.
	if (last_change == NULL || value == NULL || size == NULL)
		return B_BAD_VALUE;
	// All cases below write 4 bytes, reject small buffers.
	if (*size < sizeof(float))
		return B_BAD_VALUE;
	float* currValue;
	int* currValueInt;
	int16 data;
	uint16 wValue = 0;
	switch (id - fFirstParameterID) {
		case 0:
			// debug_printf("\tBrightness:\n");
			// debug_printf("\tValue = %f\n",fBrightness);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fBrightness;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 1:
			// debug_printf("\tContrast:\n");
			// debug_printf("\tValue = %f\n",fContrast);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fContrast;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 2:
			// debug_printf("\tHue:\n");
			// debug_printf("\tValue = %f\n",fHue);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fHue;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 4:
			// debug_printf("\tSaturation:\n");
			// debug_printf("\tValue = %f\n",fSaturation);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fSaturation;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 5:
			// debug_printf("\tSharpness:\n");
			// debug_printf("\tValue = %f\n",fSharpness);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fSharpness;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 6:
			// Gamma
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fGamma;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 7:
			// debug_printf("\tWB Temperature:\n");
			*size = sizeof(float);
			currValue = (float*)value;
			wValue = USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL << 8;
			if (fDevice->ControlTransfer(USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
				USB_VIDEO_RC_GET_CUR, wValue, fControlRequestIndex, sizeof(data), &data)
				== sizeof(data)) {
				fWBTemp = (float)data;
			}
			// debug_printf("\tValue = %f\n",fWBTemp);
			*currValue = fWBTemp;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 8:
			// debug_printf("\tWB Temperature Auto:\n");
			// debug_printf("\tValue = %d\n",fWBTempAuto);
			*size = sizeof(int);
			currValueInt = ((int*)value);
			*currValueInt = fWBTempAuto;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 11:
			if (!fBinaryBacklightCompensation) {
				// debug_printf("\tBacklight Compensation:\n");
				// debug_printf("\tValue = %f\n",fBacklightCompensation);
				*size = sizeof(float);
				currValue = (float*)value;
				*currValue = fBacklightCompensation;
				*last_change = fLastParameterChanges;
			} else {
				// debug_printf("\tBacklight Compensation:\n");
				// debug_printf("\tValue = %d\n",fBacklightCompensationBinary);
				*size = sizeof(int);
				currValueInt = (int*)value;
				*currValueInt = fBacklightCompensationBinary;
				*last_change = fLastParameterChanges;
			}
			return B_OK;
		case 12:
			// debug_printf("\tGain:\n");
			// debug_printf("\tValue = %f\n",fGain);
			*size = sizeof(float);
			currValue = (float*)value;
			*currValue = fGain;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 13:
			/* FIX: Return int for discrete parameter */
			*size = sizeof(int);
			currValueInt = (int*)value;
			*currValueInt = fPowerlineFrequency;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 14:
			/* Resolution selector (Task 2) */
			*size = sizeof(int);
			currValueInt = (int*)value;
			*currValueInt = fSelectedResolutionIndex;
			*last_change = fLastParameterChanges;
			return B_OK;
		case 16:
			/* Frame rate selector (P2 Feature) */
			*size = sizeof(int);
			currValueInt = (int*)value;
			*currValueInt = fSelectedFrameIntervalIndex;
			*last_change = fLastParameterChanges;
			return B_OK;

	}

	/* Handle Camera Terminal controls by dynamic ID */
	if (id == fAutoExposureModeID && fAutoExposureModeID >= 0) {
		*size = sizeof(int);
		currValueInt = (int*)value;
		*currValueInt = (int)fAutoExposureMode;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fExposureTimeID && fExposureTimeID >= 0) {
		*size = sizeof(float);
		currValue = (float*)value;
		// Convert from 100μs units to milliseconds
		*currValue = fExposureTimeAbs / 10.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fAutoFocusID && fAutoFocusID >= 0) {
		*size = sizeof(int);
		currValueInt = (int*)value;
		*currValueInt = fAutoFocus ? 1 : 0;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fFocusAbsoluteID && fFocusAbsoluteID >= 0) {
		*size = sizeof(float);
		currValue = (float*)value;
		*currValue = (float)fFocusAbsolute;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fZoomAbsoluteID && fZoomAbsoluteID >= 0) {
		*size = sizeof(float);
		currValue = (float*)value;
		// Convert from internal units (100 = 1x) to display (1.0 = 1x)
		*currValue = fZoomAbsolute / 100.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == fPanTiltID && fPanTiltID >= 0) {
		// Pan control - convert from arc-seconds to degrees
		*size = sizeof(float);
		currValue = (float*)value;
		*currValue = fPanAbsolute / 3600.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}
	if (id == (fPanTiltID + 1) && fPanTiltID >= 0) {
		// Tilt control - convert from arc-seconds to degrees
		*size = sizeof(float);
		currValue = (float*)value;
		*currValue = fTiltAbsolute / 3600.0f;
		*last_change = fLastParameterChanges;
		return B_OK;
	}

	// Relative controls always return 0 (stopped)
	if ((id == fExposureRelID && fExposureRelID >= 0)
		|| (id == fFocusRelID && fFocusRelID >= 0)
		|| (id == fZoomRelID && fZoomRelID >= 0)
		|| (id == fPanRelID && fPanRelID >= 0)
		|| (id == fTiltRelID && fTiltRelID >= 0)) {
		*size = sizeof(int32);
		*((int32*)value) = 0;
		*last_change = fLastParameterChanges;
		return B_OK;
	}

	// Extension Unit controls
	if (id == fXULedParameterID && fXULedParameterID > 0) {
		*size = sizeof(int32);
		*((int32*)value) = fXULedState ? 1 : 0;
		*last_change = fLastParameterChanges;
		return B_OK;
	}

	return B_BAD_VALUE;
}


status_t
UVCCamDevice::SetParameterValue(int32 id, bigtime_t when, const void* value,
	size_t size)
{
	syslog(LOG_INFO, "UVCCamDevice::SetParameterValue(%" B_PRId32 ")\n", id - fFirstParameterID);
	switch (id - fFirstParameterID) {
		case 0:
			// debug_printf("\tBrightness:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fBrightness = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_BRIGHTNESS_CONTROL, (int16)fBrightness);
		case 1:
			// debug_printf("\tContrast:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fContrast = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_CONTRAST_CONTROL, (int16)fContrast);
		case 2:
			// debug_printf("\tHue:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fHue = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_HUE_CONTROL, (int16)fHue);
		case 4:
			// debug_printf("\tSaturation:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fSaturation = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_SATURATION_CONTROL, (int16)fSaturation);
		case 5:
			// debug_printf("\tSharpness:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fSharpness = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_SHARPNESS_CONTROL, (int16)fSharpness);
		case 6:
			// Gamma
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fGamma = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_GAMMA_CONTROL, (int16)fGamma);
		case 7:
			if (fWBTempAuto)
				return B_OK;
			// debug_printf("\tWB Temperature:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fWBTemp = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_CONTROL,
				(int16)fWBTemp);
		case 8:
			// debug_printf("\tWB Temperature Auto:\n");
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;
			fWBTempAuto = *((int*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(
				USB_VIDEO_PU_WHITE_BALANCE_TEMPERATURE_AUTO_CONTROL, (int8)fWBTempAuto);
		case 11:
			if (!fBinaryBacklightCompensation) {
				// debug_printf("\tBacklight Compensation:\n");
				if (!value || (size != sizeof(float)))
					return B_BAD_VALUE;
				fBacklightCompensation = *((float*)value);
			} else {
				// debug_printf("\tBacklight Compensation:\n");
				if (!value || (size != sizeof(int)))
					return B_BAD_VALUE;
				fBacklightCompensationBinary = *((int*)value);
			}
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_BACKLIGHT_COMPENSATION_CONTROL,
				(int16)fBacklightCompensationBinary);
		case 12:
			// debug_printf("\tGain:\n");
			if (!value || (size != sizeof(float)))
				return B_BAD_VALUE;
			fGain = *((float*)value);
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_GAIN_CONTROL, (int16)fGain);
		case 13:
			/* FIX: Accept int for discrete parameter */
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;
			fPowerlineFrequency = *((int*)value);
			/* Clamp to valid range 0-2 */
			if (fPowerlineFrequency < 0) fPowerlineFrequency = 0;
			if (fPowerlineFrequency > 2) fPowerlineFrequency = 2;
			fLastParameterChanges = when;
			return _SetParameterValue(USB_VIDEO_PU_POWER_LINE_FREQUENCY_CONTROL,
				(int8)fPowerlineFrequency);
		case 14:
		{
			/* Resolution selector (Task 2 & 3) */
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;

			int32 newIndex = *((int*)value);
			BList* frameList = fIsMJPEG ? &fMJPEGFrames : &fUncompressedFrames;

			/* Validate index */
			if (newIndex < 0 || newIndex >= frameList->CountItems()) {
				syslog(LOG_INFO, "UVCCamDevice: Invalid resolution index %d (max %d)\n",
					(int)newIndex, (int)frameList->CountItems() - 1);
				return B_BAD_VALUE;
			}

			/* Only change if different */
			if (newIndex != fSelectedResolutionIndex) {
				const usb_video_frame_descriptor* frameDesc =
					(const usb_video_frame_descriptor*)frameList->ItemAt(newIndex);
				if (frameDesc != NULL) {
					syslog(LOG_INFO, "UVCCamDevice: Resolution changed to %ux%u (index %d)\n",
						frameDesc->width, frameDesc->height, (int)newIndex);
					syslog(LOG_INFO, "UVCCamDevice: Resolution changed to %ux%u (index %d, frame_index %u)\n",
						frameDesc->width, frameDesc->height, (int)newIndex, frameDesc->frame_index);

					fSelectedResolutionIndex = newIndex;

					/* Task 3: Update frame indices for UVC format negotiation */
					if (fIsMJPEG) {
						fMJPEGFrameIndex = frameDesc->frame_index;
						if (fDeframer)
							((UVCDeframer*)fDeframer)->SetExpectedFrameSize(0);
					} else {
						fUncompressedFrameIndex = frameDesc->frame_index;
						if (fDeframer) {
							size_t frameBytes = _UncompressedFrameSize(
								fUncompressedPixelFormat,
								frameDesc->width, frameDesc->height);
							((UVCDeframer*)fDeframer)->SetExpectedFrameSize(
								frameBytes);
						}
					}

					/* P2 Feature: Update available frame intervals for new resolution */
					if (frameDesc->frame_interval_type > 0) {
						fNumFrameIntervals = frameDesc->frame_interval_type;
						if (fNumFrameIntervals > kMaxFrameIntervals)
							fNumFrameIntervals = kMaxFrameIntervals;

						for (uint8 k = 0; k < fNumFrameIntervals; k++) {
							fCurrentFrameIntervals[k] = frameDesc->discrete_frame_intervals[k];
						}

						/* Reset to default frame interval for this resolution */
						fSelectedFrameIntervalIndex = 0;
						for (uint8 k = 0; k < fNumFrameIntervals; k++) {
							if (fCurrentFrameIntervals[k] == frameDesc->default_frame_interval) {
								fSelectedFrameIntervalIndex = k;
								break;
							}
						}
						fSelectedFrameInterval = fCurrentFrameIntervals[fSelectedFrameIntervalIndex];

						syslog(LOG_INFO, "UVCCamDevice: Frame intervals updated for new resolution: %d options, default=%.1f fps\n",
							(int)fNumFrameIntervals, 10000000.0f / fSelectedFrameInterval);
					} else {
						/* Continuous interval - use default */
						fNumFrameIntervals = 0;
						fSelectedFrameInterval = frameDesc->default_frame_interval;
					}

					/* FIX BUG 12: Do not call AcceptVideoFrame(), it looks up
					 * by resolution and could match a DIFFERENT frame than
					 * the selected one (e.g. same resolution, other fps).
					 * That overwrote fMJPEGFrameIndex with the wrong value.
					 * Update fVideoFrame directly instead.
					 */
					uint32 newWidth = frameDesc->width;
					uint32 newHeight = frameDesc->height;
					SetVideoFrame(BRect(0, 0, newWidth - 1, newHeight - 1));
					syslog(LOG_INFO, "UVCCamDevice: VideoFrame updated to %ux%u (frame_index=%u)\n",
						newWidth, newHeight, frameDesc->frame_index);

					/* Always flush deframer to discard any frames from old resolution.
					 * This is important even when transfer is not running, as there may
					 * be stale frames in the queue from before the resolution change.
					 */
					if (fDeframer) {
						fDeframer->Flush();
						syslog(LOG_INFO, "UVCCamDevice: Deframer flushed for resolution change\n");
					}

					/* Always mark resolution transition start time. Frames with wrong
					 * dimensions will be skipped during the transition period.
					 * This handles the case where the transfer starts after resolution change.
					 */
					fResolutionTransitionStart = system_time();

					/* If transfer is running, we need to renegotiate */
					if (TransferEnabled()) {
						syslog(LOG_INFO, "UVCCamDevice: Transfer running, stopping to change resolution\n");
						/* Stop pump thread but skip idle alternate switch (we're about
						 * to start a new transfer with a different alternate anyway) */
						CamDevice::StopTransfer();

						/* Brief delay for camera to process format change */
						snooze(20000);  // 20ms

						status_t err = StartTransfer();
						if (err != B_OK) {
							syslog(LOG_ERR, "UVCCamDevice: Failed to restart transfer with new resolution: %s\n",
								strerror(err));
							return err;
						}
						syslog(LOG_INFO, "UVCCamDevice: Transfer restarted with new resolution\n");
					}

					fLastParameterChanges = when;
				}
			}
			return B_OK;
		}
		case 16:
		{
			/* Frame rate selector (P2 Feature) */
			if (!value || (size != sizeof(int)))
				return B_BAD_VALUE;

			int32 newIndex = *((int*)value);

			/* Validate index */
			if (newIndex < 0 || newIndex >= fNumFrameIntervals) {
				syslog(LOG_INFO, "UVCCamDevice: Invalid frame rate index %d (max %d)\n",
					(int)newIndex, (int)fNumFrameIntervals - 1);
				return B_BAD_VALUE;
			}

			/* Only change if different */
			if (newIndex != fSelectedFrameIntervalIndex) {
				fSelectedFrameIntervalIndex = newIndex;
				fSelectedFrameInterval = fCurrentFrameIntervals[newIndex];

				float fps = 10000000.0f / fSelectedFrameInterval;
				syslog(LOG_INFO, "UVCCamDevice: Frame rate changed to %.1f fps (interval %u)\n",
					fps, fSelectedFrameInterval);
				syslog(LOG_INFO, "UVCCamDevice: Frame rate changed to %.1f fps (interval %u)\n",
					fps, fSelectedFrameInterval);

				/* If transfer is running, renegotiate format */
				if (TransferEnabled()) {
					syslog(LOG_INFO, "UVCCamDevice: Transfer running, stopping to change frame rate\n");
					CamDevice::StopTransfer();

					snooze(20000);  // 20ms delay

					status_t err = StartTransfer();
					if (err != B_OK) {
						syslog(LOG_ERR, "UVCCamDevice: Failed to restart transfer with new frame rate: %s\n",
							strerror(err));
						return err;
					}
					syslog(LOG_INFO, "UVCCamDevice: Transfer restarted with new frame rate\n");
				}

				fLastParameterChanges = when;
			}
			return B_OK;
		}

	}

	/* Handle Camera Terminal controls by dynamic ID */
	if (id == fAutoExposureModeID && fAutoExposureModeID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		uint8 mode = (uint8)*((int*)value);
		if (mode == 1 || mode == 2 || mode == 4 || mode == 8) {
			status_t err = _SetCTControlValue(USB_VIDEO_CT_AE_MODE_CONTROL, &mode, 1);
			if (err == B_OK) {
				fAutoExposureMode = mode;
				fLastParameterChanges = when;
				syslog(LOG_INFO, "UVCCamDevice: Auto Exposure Mode set to %d\n", mode);
			}
			return err;
		}
		return B_BAD_VALUE;
	}
	if (id == fExposureTimeID && fExposureTimeID >= 0) {
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		float msValue = *((float*)value);
		// Convert from milliseconds to 100μs units
		uint32 expTime = (uint32)(msValue * 10.0f);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL,
			&expTime, 4);
		if (err == B_OK) {
			fExposureTimeAbs = expTime;
			fLastParameterChanges = when;
			syslog(LOG_INFO, "UVCCamDevice: Exposure Time set to %.1f ms (%u units)\n",
				msValue, expTime);
		}
		return err;
	}
	if (id == fAutoFocusID && fAutoFocusID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		uint8 autoFocus = (*((int*)value) != 0) ? 1 : 0;
		status_t err = _SetCTControlValue(USB_VIDEO_CT_FOCUS_AUTO_CONTROL, &autoFocus, 1);
		if (err == B_OK) {
			fAutoFocus = (autoFocus != 0);
			fLastParameterChanges = when;
			syslog(LOG_INFO, "UVCCamDevice: Auto Focus set to %s\n", fAutoFocus ? "On" : "Off");
		}
		return err;
	}
	if (id == fFocusAbsoluteID && fFocusAbsoluteID >= 0) {
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		uint16 focusVal = (uint16)*((float*)value);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_FOCUS_ABSOLUTE_CONTROL, &focusVal, 2);
		if (err == B_OK) {
			fFocusAbsolute = focusVal;
			fLastParameterChanges = when;
			syslog(LOG_INFO, "UVCCamDevice: Focus set to %u\n", focusVal);
		}
		return err;
	}
	if (id == fZoomAbsoluteID && fZoomAbsoluteID >= 0) {
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		// Convert from display (1.0 = 1x) to internal units (100 = 1x)
		uint16 zoomVal = (uint16)(*((float*)value) * 100.0f);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_ZOOM_ABSOLUTE_CONTROL, &zoomVal, 2);
		if (err == B_OK) {
			fZoomAbsolute = zoomVal;
			fLastParameterChanges = when;
			syslog(LOG_INFO, "UVCCamDevice: Zoom set to %.1fx (%u)\n", zoomVal / 100.0f, zoomVal);
		}
		return err;
	}
	if (id == fPanTiltID && fPanTiltID >= 0) {
		// Pan control - convert from degrees to arc-seconds and write compound control
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		struct {
			int32 pan;
			int32 tilt;
		} panTilt;
		panTilt.pan = (int32)(*((float*)value) * 3600.0f);
		panTilt.tilt = fTiltAbsolute;  // Keep current tilt
		status_t err = _SetCTControlValue(USB_VIDEO_CT_PANTILT_ABSOLUTE_CONTROL, &panTilt, 8);
		if (err == B_OK) {
			fPanAbsolute = panTilt.pan;
			fLastParameterChanges = when;
			syslog(LOG_INFO, "UVCCamDevice: Pan set to %.1f°\n", panTilt.pan / 3600.0f);
		}
		return err;
	}
	if (id == (fPanTiltID + 1) && fPanTiltID >= 0) {
		// Tilt control - convert from degrees to arc-seconds and write compound control
		if (!value || (size != sizeof(float)))
			return B_BAD_VALUE;
		struct {
			int32 pan;
			int32 tilt;
		} panTilt;
		panTilt.pan = fPanAbsolute;  // Keep current pan
		panTilt.tilt = (int32)(*((float*)value) * 3600.0f);
		status_t err = _SetCTControlValue(USB_VIDEO_CT_PANTILT_ABSOLUTE_CONTROL, &panTilt, 8);
		if (err == B_OK) {
			fTiltAbsolute = panTilt.tilt;
			fLastParameterChanges = when;
			syslog(LOG_INFO, "UVCCamDevice: Tilt set to %.1f°\n", panTilt.tilt / 3600.0f);
		}
		return err;
	}

	// Relative controls - send directional command to camera
	if (id == fExposureRelID && fExposureRelID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		int8 dir = (int8)*((int*)value);
		return _SetCTControlValue(
			USB_VIDEO_CT_EXPOSURE_TIME_RELATIVE_CONTROL, &dir, 1);
	}
	if (id == fFocusRelID && fFocusRelID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		int8 data[2];
		data[0] = (int8)*((int*)value);	// direction
		data[1] = (data[0] != 0) ? 1 : 0;	// speed (1=default)
		return _SetCTControlValue(
			USB_VIDEO_CT_FOCUS_RELATIVE_CONTROL, data, 2);
	}
	if (id == fZoomRelID && fZoomRelID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		int8 data[3];
		data[0] = (int8)*((int*)value);	// optical zoom direction
		data[1] = 0;						// digital zoom (unused)
		data[2] = (data[0] != 0) ? 1 : 0;	// speed
		return _SetCTControlValue(
			USB_VIDEO_CT_ZOOM_RELATIVE_CONTROL, data, 3);
	}
	if (id == fPanRelID && fPanRelID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		int8 data[4];
		data[0] = (int8)*((int*)value);	// pan direction
		data[1] = (data[0] != 0) ? 1 : 0;	// pan speed
		data[2] = 0;						// tilt direction (unchanged)
		data[3] = 0;						// tilt speed
		return _SetCTControlValue(
			USB_VIDEO_CT_PANTILT_RELATIVE_CONTROL, data, 4);
	}
	if (id == fTiltRelID && fTiltRelID >= 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		int8 data[4];
		data[0] = 0;						// pan direction (unchanged)
		data[1] = 0;						// pan speed
		data[2] = (int8)*((int*)value);	// tilt direction
		data[3] = (data[2] != 0) ? 1 : 0;	// tilt speed
		return _SetCTControlValue(
			USB_VIDEO_CT_PANTILT_RELATIVE_CONTROL, data, 4);
	}

	// Extension Unit controls
	if (id == fXULedParameterID && fXULedParameterID > 0) {
		if (!value || (size != sizeof(int)))
			return B_BAD_VALUE;
		int32 ledOn = *((int*)value);
		fXULedState = (ledOn != 0);
		syslog(LOG_INFO, "UVCCamDevice: LED %s\n", fXULedState ? "ON" : "OFF");
		fLastParameterChanges = when;
		return B_OK;
	}

	return B_BAD_VALUE;
}


status_t
UVCCamDevice::_SetParameterValue(uint16 wValue, int16 setValue)
{
	// Fail closed when unplugged, device goes NULL.
	if (fDevice == NULL)
		return B_DEV_NOT_READY;
	return (fDevice->ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR, wValue << 8, fControlRequestIndex,
		sizeof(setValue), &setValue)) == sizeof(setValue);
}


status_t
UVCCamDevice::_SetParameterValue(uint16 wValue, int8 setValue)
{
	// Fail closed when unplugged, device goes NULL.
	if (fDevice == NULL)
		return B_DEV_NOT_READY;
	return (fDevice->ControlTransfer(USB_REQTYPE_CLASS
		| USB_REQTYPE_INTERFACE_OUT, USB_VIDEO_RC_SET_CUR, wValue << 8, fControlRequestIndex,
		sizeof(setValue), &setValue)) == sizeof(setValue);
}


// =============================================================================
// Feature 2: Camera Control Methods
// =============================================================================


status_t
UVCCamDevice::_ProbeControlRange(uint16 selector, camera_control_info* info)
{
	if (info == NULL || fProcessingUnitID == 0) {
		return B_BAD_VALUE;
	}
	// FIX: Unplugged() clears fDevice on another thread; five ControlTransfers
	// below dereferenced it unchecked.
	if (fDevice == NULL)
		return B_NO_INIT;

	ssize_t result;
	int16 value;

	// GET_MIN
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_MIN,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->min_value = value;
	} else {
		info->min_value = 0;
	}

	// GET_MAX
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_MAX,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->max_value = value;
	} else {
		info->max_value = 100;
	}

	// GET_DEF
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_DEF,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->default_value = value;
	} else {
		info->default_value = (info->min_value + info->max_value) / 2;
	}

	// GET_RES (resolution/step)
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_RES,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->resolution = (uint16)value;
	} else {
		info->resolution = 1;
	}

	// GET_CUR
	result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_CUR,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);
	if (result >= 0) {
		info->current_value = value;
	} else {
		info->current_value = info->default_value;
	}

	return B_OK;
}


status_t
UVCCamDevice::_InitializeProcessingControls()
{
	if (fControlsInitialized) {
		return B_OK;
	}

	// Note: fProcessingUnitID should be set during _ParseVideoControl
	// For now we skip if it's not set
	if (fProcessingUnitID == 0) {
		syslog(LOG_INFO, "UVCCamDevice: No Processing Unit found, skipping controls init\n");
		fControlsInitialized = true;
		return B_OK;
	}

	syslog(LOG_INFO, "UVCCamDevice: Initializing processing controls for unit %u\n",
		fProcessingUnitID);

	fControlsInitialized = true;
	return B_OK;
}


void
UVCCamDevice::_AddProcessingControls(BParameterGroup* group, int32& index)
{
	(void)group;  // Will be used when adding parameters
	(void)index;

	if (!fControlsInitialized) {
		_InitializeProcessingControls();
	}

	// Controls are added via the existing _AddProcessingParameter mechanism
	// This method is a placeholder for future expansion
}


status_t
UVCCamDevice::_GetControlValue(uint16 selector, int16* value)
{
	if (value == NULL || fProcessingUnitID == 0) {
		return B_BAD_VALUE;
	}
	// Fail closed when unplugged, device goes NULL.
	if (fDevice == NULL)
		return B_DEV_NOT_READY;

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_CUR,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(*value), value);

	return (result >= 0) ? B_OK : (status_t)result;
}


status_t
UVCCamDevice::_SetControlValue(uint16 selector, int16 value)
{
	if (fProcessingUnitID == 0) {
		return B_BAD_VALUE;
	}
	// Fail closed when unplugged, device goes NULL.
	if (fDevice == NULL)
		return B_DEV_NOT_READY;

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_SET_CUR,
		selector << 8,
		(fProcessingUnitID << 8) | fControlIndex,
		sizeof(value), &value);

	return (result >= 0) ? B_OK : (status_t)result;
}


// =============================================================================
// Camera Terminal (CT) Control Methods
// =============================================================================

status_t
UVCCamDevice::_GetCTControlValue(uint16 selector, void* value, size_t size)
{
	if (value == NULL || !fHasCameraTerminal || fCameraTerminalID == 0) {
		return B_BAD_VALUE;
	}
	// Fail closed when unplugged, device goes NULL.
	if (fDevice == NULL)
		return B_DEV_NOT_READY;

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_GET_CUR,
		selector << 8,
		(fCameraTerminalID << 8) | fControlIndex,
		size, value);

	return (result >= 0) ? B_OK : (status_t)result;
}


status_t
UVCCamDevice::_SetCTControlValue(uint16 selector, const void* value, size_t size)
{
	if (value == NULL || !fHasCameraTerminal || fCameraTerminalID == 0) {
		return B_BAD_VALUE;
	}
	// Fail closed when unplugged, device goes NULL.
	if (fDevice == NULL)
		return B_DEV_NOT_READY;

	ssize_t result = fDevice->ControlTransfer(
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		USB_VIDEO_RC_SET_CUR,
		selector << 8,
		(fCameraTerminalID << 8) | fControlIndex,
		size, (void*)value);

	return (result >= 0) ? B_OK : (status_t)result;
}


void
UVCCamDevice::_AddCameraTerminalControls(BParameterGroup* group, int32& index)
{
	if (!fHasCameraTerminal || fCameraTerminalControls == 0)
		return;

	BParameterGroup* ctGroup = group->MakeGroup("Camera Controls");

	// Auto Exposure Mode (selector 0x02)
	if (fCameraTerminalControls & (1 << 1)) {
		BDiscreteParameter* aeMode = ctGroup->MakeDiscreteParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Auto Exposure",
			B_ENABLE);
		if (aeMode) {
			aeMode->AddItem(1, "Manual");
			aeMode->AddItem(2, "Auto");
			aeMode->AddItem(4, "Shutter Priority");
			aeMode->AddItem(8, "Aperture Priority");
			fAutoExposureModeID = index - 1;

			// Read current value from camera
			uint8 mode = 2;
			if (_GetCTControlValue(USB_VIDEO_CT_AE_MODE_CONTROL, &mode, 1) == B_OK) {
				fAutoExposureMode = mode;
			}
		}
	}

	// Exposure Time Absolute (selector 0x04) - value in 100μs units
	if (fCameraTerminalControls & (1 << 3)) {
		BContinuousParameter* exposure = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Exposure Time",
			B_GAIN,
			"ms",
			0.1f,      // min: 0.01ms (100μs)
			1000.0f,   // max: 100ms
			0.1f);     // step
		if (exposure) {
			fExposureTimeID = index - 1;

			// Read current value from camera (4 bytes, 100μs units)
			uint32 expTime = 333;
			if (_GetCTControlValue(USB_VIDEO_CT_EXPOSURE_TIME_ABSOLUTE_CONTROL,
					&expTime, 4) == B_OK) {
				fExposureTimeAbs = expTime;
			}
		}
	}

	// Focus Auto (selector 0x08) - checkbox
	if (fCameraTerminalControls & (1 << 17)) {
		BDiscreteParameter* focusAuto = ctGroup->MakeDiscreteParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Auto Focus",
			B_ENABLE);
		if (focusAuto) {
			focusAuto->AddItem(0, "Off");
			focusAuto->AddItem(1, "On");
			fAutoFocusID = index - 1;

			// Read current value from camera
			uint8 autoFocus = 1;
			if (_GetCTControlValue(USB_VIDEO_CT_FOCUS_AUTO_CONTROL, &autoFocus, 1) == B_OK) {
				fAutoFocus = (autoFocus != 0);
			}
		}
	}

	// Focus Absolute (selector 0x06) - slider, 2 bytes
	if (fCameraTerminalControls & (1 << 5)) {
		BContinuousParameter* focus = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Focus",
			B_GAIN,
			"",
			0.0f,      // min
			255.0f,    // max (typical range, actual may vary)
			1.0f);     // step
		if (focus) {
			fFocusAbsoluteID = index - 1;

			// Read current value from camera (2 bytes)
			uint16 focusVal = 0;
			if (_GetCTControlValue(USB_VIDEO_CT_FOCUS_ABSOLUTE_CONTROL,
					&focusVal, 2) == B_OK) {
				fFocusAbsolute = focusVal;
			}
		}
	}

	// Zoom Absolute (selector 0x0B) - slider, 2 bytes
	if (fCameraTerminalControls & (1 << 9)) {
		BContinuousParameter* zoom = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Zoom",
			B_GAIN,
			"x",
			1.0f,      // min: 1x
			10.0f,     // max: 10x (typical range)
			0.1f);     // step
		if (zoom) {
			fZoomAbsoluteID = index - 1;

			// Read current value from camera (2 bytes)
			uint16 zoomVal = 100;
			if (_GetCTControlValue(USB_VIDEO_CT_ZOOM_ABSOLUTE_CONTROL,
					&zoomVal, 2) == B_OK) {
				fZoomAbsolute = zoomVal;
			}
		}
	}

	// Pan/Tilt Absolute (selector 0x0D) - compound control, 8 bytes (pan + tilt)
	// Pan and Tilt values are in arc-seconds (1/3600 of a degree)
	if (fCameraTerminalControls & (1 << 11)) {
		// Pan control (first 4 bytes)
		BContinuousParameter* pan = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Pan",
			B_GAIN,
			"°",
			-180.0f,   // min: -180 degrees
			180.0f,    // max: +180 degrees
			1.0f);     // step: 1 degree
		if (pan) {
			fPanTiltID = index - 1;  // Store first ID for the compound control
		}

		// Tilt control (last 4 bytes)
		BContinuousParameter* tilt = ctGroup->MakeContinuousParameter(
			index++,
			B_MEDIA_RAW_VIDEO,
			"Tilt",
			B_GAIN,
			"°",
			-180.0f,   // min: -180 degrees
			180.0f,    // max: +180 degrees
			1.0f);     // step: 1 degree

		// Read current values from camera (8 bytes total)
		if (pan || tilt) {
			struct {
				int32 pan;
				int32 tilt;
			} panTilt = { 0, 0 };
			if (_GetCTControlValue(USB_VIDEO_CT_PANTILT_ABSOLUTE_CONTROL,
					&panTilt, 8) == B_OK) {
				fPanAbsolute = panTilt.pan;
				fTiltAbsolute = panTilt.tilt;
			}
		}
	}

	// ── Relative Controls ────────────────────────────────────
	// Relative controls send directional commands: -1 (decrease),
	// 0 (stop), +1 (increase). Exposed as discrete parameters.

	// Exposure Time Relative (bit 4)
	if (fCameraTerminalControls & (1 << 4)) {
		fExposureRelID = index++;
		BDiscreteParameter* expRel = ctGroup->MakeDiscreteParameter(
			fExposureRelID, B_MEDIA_RAW_VIDEO, "Exposure +/-", B_GENERIC);
		if (expRel) {
			expRel->AddItem(-1, "Decrease");
			expRel->AddItem(0, "Stop");
			expRel->AddItem(1, "Increase");
		}
	}

	// Focus Relative (bit 6) - 2 bytes: direction (int8) + speed (uint8)
	if (fCameraTerminalControls & (1 << 6)) {
		fFocusRelID = index++;
		BDiscreteParameter* focusRel = ctGroup->MakeDiscreteParameter(
			fFocusRelID, B_MEDIA_RAW_VIDEO, "Focus +/-", B_GENERIC);
		if (focusRel) {
			focusRel->AddItem(-1, "Near");
			focusRel->AddItem(0, "Stop");
			focusRel->AddItem(1, "Far");
		}
	}

	// Zoom Relative (bit 10) - 3 bytes: direction (int8), digital (int8), speed (uint8)
	if (fCameraTerminalControls & (1 << 10)) {
		fZoomRelID = index++;
		BDiscreteParameter* zoomRel = ctGroup->MakeDiscreteParameter(
			fZoomRelID, B_MEDIA_RAW_VIDEO, "Zoom +/-", B_GENERIC);
		if (zoomRel) {
			zoomRel->AddItem(-1, "Wide");
			zoomRel->AddItem(0, "Stop");
			zoomRel->AddItem(1, "Tele");
		}
	}

	// Pan/Tilt Relative (bit 12) - 4 bytes: panDir (int8), panSpeed (uint8),
	//                                        tiltDir (int8), tiltSpeed (uint8)
	if (fCameraTerminalControls & (1 << 12)) {
		fPanRelID = index++;
		BDiscreteParameter* panRel = ctGroup->MakeDiscreteParameter(
			fPanRelID, B_MEDIA_RAW_VIDEO, "Pan +/-", B_GENERIC);
		if (panRel) {
			panRel->AddItem(-1, "Left");
			panRel->AddItem(0, "Stop");
			panRel->AddItem(1, "Right");
		}

		fTiltRelID = index++;
		BDiscreteParameter* tiltRel = ctGroup->MakeDiscreteParameter(
			fTiltRelID, B_MEDIA_RAW_VIDEO, "Tilt +/-", B_GENERIC);
		if (tiltRel) {
			tiltRel->AddItem(-1, "Down");
			tiltRel->AddItem(0, "Stop");
			tiltRel->AddItem(1, "Up");
		}
	}

	syslog(LOG_INFO, "UVCCamDevice: Camera Terminal controls added "
		"(bitmap=0x%08x)\n", fCameraTerminalControls);
}


// =============================================================================
// Extension Unit Methods (XU) - Vendor-Specific Features
// =============================================================================


void
UVCCamDevice::_ParseExtensionUnit(
	const usb_video_extension_unit_descriptor* descriptor)
{
	// Descriptor comes from the USB kit, check before use.
	if (descriptor == NULL || descriptor->length < 8)
		return;
	// Bounds-safe extraction (UVCDescriptors). The XU descriptor's variable
	// arrays and its Extension()/ControlSize() offset math are driven by two
	// untrusted count bytes; validate first and read via the checked view so a
	// malformed descriptor can't walk the parser off the end of the buffer.
	const UVCExtensionUnitCheck xuChk = UVCCheckExtensionUnitDescriptor(
		(const uint8*)descriptor, descriptor->length);
	if (!xuChk.valid) {
		syslog(LOG_WARNING, "UVCCamDevice: skipping malformed VC_EXTENSION_UNIT "
			"descriptor (bLength=%u)\n", descriptor->length);
		return;
	}

	// Create extension unit info structure
	// FIX: nothrow + AddItem check so OOM cannot NULL-deref or leak.
	extension_unit_info* xu = new (std::nothrow) extension_unit_info;
	if (xu == NULL)
		return;
	memset(xu, 0, sizeof(extension_unit_info));

	// Copy basic info (all from the validated, bounds-checked view)
	xu->unit_id = xuChk.unitID;
	memcpy(xu->guid, xuChk.guid, 16);
	xu->num_controls = xuChk.numControls;
	xu->num_input_pins = xuChk.numInputPins;

	// Copy source IDs (already clamped to what fits, up to 8)
	uint8 pinCount = (xuChk.sourceIdCount < 8) ? xuChk.sourceIdCount : 8;
	for (uint8 i = 0; i < pinCount; i++) {
		xu->source_ids[i] = xuChk.sourceIds[i];
	}

	// Get description string from device (iExtension is 0 when out of bounds).
	const char* desc = xuChk.iExtension != 0
		? fDevice->DecodeStringDescriptor(xuChk.iExtension) : NULL;
	if (desc != NULL) {
		strncpy(xu->description, desc, sizeof(xu->description) - 1);
		xu->description[sizeof(xu->description) - 1] = '\0';
	}

	// Identify vendor from GUID
	xu->vendor = _IdentifyXUVendor(xu->guid);
	xu->vendor_name = _GetXUVendorName(xu->vendor);
	xu->capabilities = _GetXUCapabilities(xu->vendor);

	// Store the extension unit
	if (!fExtensionUnits.AddItem(xu)) {
		delete xu;
		return;
	}
	fHasExtensionUnits = true;

	// Log the extension unit
	syslog(LOG_INFO, "UVCCamDevice: XU unit_id=%d vendor=%s controls=%d guid=%02x%02x%02x%02x\n",
		xu->unit_id, xu->vendor_name, xu->num_controls,
		xu->guid[0], xu->guid[1], xu->guid[2], xu->guid[3]);
	printf("VC_EXTENSION_UNIT:\tid=%d, vendor=%s\n", xu->unit_id, xu->vendor_name);
	printf("\tGUID: ");
	for (int i = 0; i < 16; i++) {
		printf("%02x", xu->guid[i]);
		if (i == 3 || i == 5 || i == 7 || i == 9)
			printf("-");
	}
	printf("\n\t#ctrls=%d, #pins=%d\n", xu->num_controls, xu->num_input_pins);
	if (xu->description[0] != '\0')
		printf("\tDesc: %s\n", xu->description);

	// Log capabilities if known vendor
	if (xu->capabilities != XU_CAP_NONE) {
		printf("\tCapabilities:");
		if (xu->capabilities & XU_CAP_LED_CONTROL)
			printf(" LED");
		if (xu->capabilities & XU_CAP_FACE_DETECTION)
			printf(" FaceDetect");
		if (xu->capabilities & XU_CAP_HDR)
			printf(" HDR");
		if (xu->capabilities & XU_CAP_NOISE_REDUCTION)
			printf(" NoiseReduction");
		if (xu->capabilities & XU_CAP_H264_ENCODING)
			printf(" H264");
		if (xu->capabilities & XU_CAP_PTZ_CONTROL)
			printf(" PTZ");
		printf("\n");
	}
}


extension_unit_vendor
UVCCamDevice::_IdentifyXUVendor(const uint8* guid)
{
	if (memcmp(guid, kMicrosoftH264XUGUID, 16) == 0)
		return XU_VENDOR_MICROSOFT;
	if (memcmp(guid, kSonixXUGUID, 16) == 0
		|| memcmp(guid, kSonixSysHWGUID, 16) == 0
		|| memcmp(guid, kSonixUsrHWGUID, 16) == 0)
		return XU_VENDOR_SONIX;
	if (memcmp(guid, kLogitechXUGUID, 16) == 0)
		return XU_VENDOR_LOGITECH;
	if (memcmp(guid, kRealtekXUGUID, 16) == 0)
		return XU_VENDOR_REALTEK;
	return XU_VENDOR_UNKNOWN;
}


uint32
UVCCamDevice::_GetXUCapabilities(extension_unit_vendor vendor)
{
	switch (vendor) {
		case XU_VENDOR_MICROSOFT:
			return XU_CAP_H264_ENCODING;
		case XU_VENDOR_SONIX:
			return XU_CAP_LED_CONTROL | XU_CAP_FACE_DETECTION;
		case XU_VENDOR_LOGITECH:
			return XU_CAP_LED_CONTROL | XU_CAP_PTZ_CONTROL | XU_CAP_H264_ENCODING;
		case XU_VENDOR_REALTEK:
			return XU_CAP_HDR | XU_CAP_NOISE_REDUCTION;
		default:
			return XU_CAP_NONE;
	}
}


const char*
UVCCamDevice::_GetXUVendorName(extension_unit_vendor vendor)
{
	switch (vendor) {
		case XU_VENDOR_MICROSOFT:
			return "Microsoft";
		case XU_VENDOR_SONIX:
			return "Sonix";
		case XU_VENDOR_LOGITECH:
			return "Logitech";
		case XU_VENDOR_REALTEK:
			return "Realtek";
		default:
			return "Unknown";
	}
}


void
UVCCamDevice::_LogExtensionUnits()
{
	if (!fHasExtensionUnits || fExtensionUnits.CountItems() == 0) {
		syslog(LOG_INFO, "UVCCamDevice: No Extension Units detected\n");
		return;
	}

	syslog(LOG_INFO, "UVCCamDevice: %d Extension Unit(s) detected:\n",
		fExtensionUnits.CountItems());

	for (int32 i = 0; i < fExtensionUnits.CountItems(); i++) {
		extension_unit_info* xu = (extension_unit_info*)fExtensionUnits.ItemAt(i);
		printf("  [%d] ID=%d Vendor=%s Controls=%d",
			i + 1, xu->unit_id, xu->vendor_name, xu->num_controls);
		if (xu->description[0] != '\0')
			printf(" (%s)", xu->description);
		printf("\n");
	}
}


// =============================================================================
// Extension Unit Control Transfer Primitives
// =============================================================================


status_t
UVCCamDevice::_XUSetCur(uint8 unitId, uint8 selector,
	const uint8* data, uint16 length)
{
	if (fDevice == NULL || data == NULL || length == 0)
		return B_BAD_VALUE;

	ssize_t ret = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
		USB_VIDEO_RC_SET_CUR,
		(uint16)selector << 8,
		(uint16)unitId << 8 | fControlIndex,
		length,
		(void*)data);

	// FIX-M7: ControlTransfer returns bytes transferred. A short transfer
	// (the C920 XU babble case) previously passed as success with a
	// half-written control.
	return (ret != length) ? B_IO_ERROR : B_OK;
}


status_t
UVCCamDevice::_XUGetCur(uint8 unitId, uint8 selector,
	uint8* data, uint16 length)
{
	if (fDevice == NULL || data == NULL || length == 0)
		return B_BAD_VALUE;

	ssize_t ret = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_CUR,
		(uint16)selector << 8,
		(uint16)unitId << 8 | fControlIndex,
		length,
		data);

	return (ret != length) ? B_IO_ERROR : B_OK;
}


status_t
UVCCamDevice::_XUGetMin(uint8 unitId, uint8 selector,
	uint8* data, uint16 length)
{
	if (fDevice == NULL || data == NULL || length == 0)
		return B_BAD_VALUE;

	ssize_t ret = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_MIN,
		(uint16)selector << 8,
		(uint16)unitId << 8 | fControlIndex,
		length,
		data);

	return (ret != length) ? B_IO_ERROR : B_OK;
}


status_t
UVCCamDevice::_XUGetMax(uint8 unitId, uint8 selector,
	uint8* data, uint16 length)
{
	if (fDevice == NULL || data == NULL || length == 0)
		return B_BAD_VALUE;

	ssize_t ret = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_MAX,
		(uint16)selector << 8,
		(uint16)unitId << 8 | fControlIndex,
		length,
		data);

	return (ret != length) ? B_IO_ERROR : B_OK;
}


status_t
UVCCamDevice::_XUGetInfo(uint8 unitId, uint8 selector, uint8* info)
{
	if (fDevice == NULL || info == NULL)
		return B_BAD_VALUE;

	ssize_t ret = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_INFO,
		(uint16)selector << 8,
		(uint16)unitId << 8 | fControlIndex,
		1,
		info);

	return (ret != 1) ? B_IO_ERROR : B_OK;
}


// GET_LEN returns the byte length (little-endian uint16) of the control's
// payload. UVC 1.1+ devices (e.g. Logitech C920) require GET_CUR/GET_MIN/...
// to use exactly this length: requesting fewer bytes makes the device return
// the full control anyway, overrunning the host transfer and triggering an
// xHCI "Babble detected" / halted control endpoint.
status_t
UVCCamDevice::_XUGetLen(uint8 unitId, uint8 selector, uint16* length)
{
	if (fDevice == NULL || length == NULL)
		return B_BAD_VALUE;

	uint8 lenData[2] = { 0, 0 };
	ssize_t ret = fDevice->ControlTransfer(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_IN,
		USB_VIDEO_RC_GET_LEN,
		(uint16)selector << 8,
		(uint16)unitId << 8 | fControlIndex,
		sizeof(lenData),
		lenData);

	if (ret < 0)
		return B_ERROR;
	// FIX-M7: a 1-byte short read previously passed as success and left
	// lenData[1] stale from the zero-init (wrong length, then babble).
	if (ret != 2)
		return B_IO_ERROR;

	*length = (uint16)lenData[0] | ((uint16)lenData[1] << 8);
	return B_OK;
}


extension_unit_info*
UVCCamDevice::_FindXU(extension_unit_vendor vendor)
{
	for (int32 i = 0; i < fExtensionUnits.CountItems(); i++) {
		extension_unit_info* xu = (extension_unit_info*)fExtensionUnits.ItemAt(i);
		if (xu != NULL && xu->vendor == vendor)
			return xu;
	}
	return NULL;
}


status_t
UVCCamDevice::_SonixAsicRead(uint16 addr, uint8* value)
{
	if (value == NULL)
		return B_BAD_VALUE;
	extension_unit_info* xu = _FindXU(XU_VENDOR_SONIX);
	if (xu == NULL)
		return B_NOT_SUPPORTED;

	uint8 data[4];
	data[0] = addr & 0xFF;
	data[1] = (addr >> 8) & 0xFF;
	data[2] = 0x00;
	data[3] = 0xFF;		// dummy write flag

	status_t err = _XUSetCur(xu->unit_id, 0x01, data, 4);
	if (err != B_OK)
		return err;

	snooze(5000);

	err = _XUGetCur(xu->unit_id, 0x01, data, 4);
	if (err != B_OK)
		return err;

	*value = data[2];
	return B_OK;
}


status_t
UVCCamDevice::_SonixAsicWrite(uint16 addr, uint8 value)
{
	extension_unit_info* xu = _FindXU(XU_VENDOR_SONIX);
	if (xu == NULL)
		return B_NOT_SUPPORTED;

	uint8 data[4];
	data[0] = addr & 0xFF;
	data[1] = (addr >> 8) & 0xFF;
	data[2] = value;
	data[3] = 0x00;		// write flag

	return _XUSetCur(xu->unit_id, 0x01, data, 4);
}


// =============================================================================
// Still Image Capture Methods
// =============================================================================


void
UVCCamDevice::_ParseStillImageFrame(
	const usb_video_still_image_frame_descriptor* descriptor, size_t len)
{
	// FIX-H2: num_image_size_patterns / compressions are device-controlled.
	// Clamp both to what bLength (and the readable bytes) can hold instead
	// of reading up to 16 patterns + compressions out of bounds.
	if (len < 6 || descriptor->length < 6)
		return;
	size_t avail = len < descriptor->length ? len : descriptor->length;
	// Store still image endpoint
	fStillImageInfo.endpoint_address = descriptor->endpoint_address;

	uint8 advertisedSizes = descriptor->num_image_size_patterns;
	size_t maxSizes = (avail - 6) / 4;
	if ((size_t)advertisedSizes > maxSizes)
		advertisedSizes = (uint8)maxSizes;
	// Store still image sizes
	fStillImageInfo.num_sizes = (advertisedSizes < 16)
		? advertisedSizes : 16;
	for (uint8 i = 0; i < fStillImageInfo.num_sizes; i++) {
		fStillImageInfo.sizes[i].width = descriptor->_pattern_size[i].width;
		fStillImageInfo.sizes[i].height = descriptor->_pattern_size[i].height;
	}

	// Compression section starts after the size patterns actually present.
	size_t compOff = (size_t)6 + (size_t)advertisedSizes * 4;
	uint8 advertisedComps = 0;
	if (compOff < avail)
		advertisedComps = ((const uint8*)descriptor)[compOff];
	size_t maxComps = (compOff + 1 <= avail) ? avail - compOff - 1 : 0;
	if ((size_t)advertisedComps > maxComps)
		advertisedComps = (uint8)maxComps;
	// Store compression patterns
	fStillImageInfo.num_compressions = (advertisedComps < 8)
		? advertisedComps : 8;
	const uint8* rawBytes = (const uint8*)descriptor;
	for (uint8 i = 0; i < fStillImageInfo.num_compressions; i++) {
		fStillImageInfo.compressions[i] = rawBytes[compOff + 1 + i];
	}

	// Mark still capture as available
	fHasStillCapture = true;

	// Log still image info
	printf("VS_STILL_IMAGE_FRAME:\t#imageSizes=%d, #compressions=%d, ept=0x%x\n",
		fStillImageInfo.num_sizes, fStillImageInfo.num_compressions,
		fStillImageInfo.endpoint_address);

	for (uint8 i = 0; i < fStillImageInfo.num_sizes; i++) {
		printf("\tstill size %d: %dx%d\n", i,
			fStillImageInfo.sizes[i].width, fStillImageInfo.sizes[i].height);
	}
}


void
UVCCamDevice::_LogStillImageCapabilities()
{
	if (!fHasStillCapture && fStillCaptureMethod == STILL_CAPTURE_NONE) {
		syslog(LOG_INFO, "UVCCamDevice: Still image capture not supported\n");
		return;
	}

	syslog(LOG_INFO, "UVCCamDevice: Still Image Capture Capabilities:\n");
	printf("  Capture Method: %s\n", _GetStillCaptureMethodName(fStillCaptureMethod));

	if (fTriggerSupport) {
		printf("  Hardware Trigger: Yes (%s)\n",
			fTriggerUsage ? "general purpose" : "fixed to still capture");
	}

	if (fHasStillCapture && fStillImageInfo.num_sizes > 0) {
		printf("  Endpoint: 0x%02x\n", fStillImageInfo.endpoint_address);
		printf("  Available Still Resolutions:\n");
		for (uint8 i = 0; i < fStillImageInfo.num_sizes; i++) {
			printf("    [%d] %dx%d\n", i,
				fStillImageInfo.sizes[i].width, fStillImageInfo.sizes[i].height);
		}
	}
}


const char*
UVCCamDevice::_GetStillCaptureMethodName(still_capture_method method)
{
	switch (method) {
		case STILL_CAPTURE_NONE:
			return "None";
		case STILL_CAPTURE_METHOD_1:
			return "Method 1 (Dedicated Button)";
		case STILL_CAPTURE_METHOD_2:
			return "Method 2 (Host Software Triggered)";
		case STILL_CAPTURE_METHOD_3:
			return "Method 3 (Dedicated Pipe + Button)";
		default:
			return "Unknown";
	}
}


status_t
UVCCamDevice::TriggerStillCapture(uint8* buffer, size_t bufferSize,
	size_t* bytesWritten, uint32 width, uint32 height)
{
	if (!fHasStillCapture || fStillCaptureMethod == STILL_CAPTURE_NONE)
		return B_NOT_SUPPORTED;

	if (buffer == NULL || bytesWritten == NULL)
		return B_BAD_VALUE;

	*bytesWritten = 0;

	// Method 2: Host software triggered via VS_STILL_IMAGE_TRIGGER_CONTROL
	if (fStillCaptureMethod == STILL_CAPTURE_METHOD_2) {
		// Step 1: Configure still probe/commit with desired resolution
		// If no resolution specified, use current stream resolution
		if (width == 0 || height == 0) {
			BRect frame = VideoFrame();
			width = (uint32)(frame.Width() + 1);
			height = (uint32)(frame.Height() + 1);
		}

		syslog(LOG_INFO, "UVCCamDevice: Triggering still capture %ux%u\n",
			width, height);

		// Device can go NULL on unplug, fail closed.
		if (fDevice == NULL)
			return B_DEV_NOT_READY;

		// Step 2: Send trigger command
		// UVC spec: VS_STILL_IMAGE_TRIGGER_CONTROL SET_CUR with value 0x01
		uint8 trigger = 0x01;
		status_t err = fDevice->ControlTransfer(
			USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT,
			0x01,	// SET_CUR
			(0x05 << 8),	// VS_STILL_IMAGE_TRIGGER_CONTROL
			fStreamingIndex,
			1,
			&trigger);

		if (err < B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Still trigger failed: %s\n",
				strerror(err));
			return err;
		}

		// Step 3: Wait for the still frame via the deframer
		// The camera will send a still frame with the same FID/EOF markers
		if (fDeframer == NULL)
			return B_NO_INIT;

		err = fDeframer->WaitFrame(5000000);	// 5 second timeout for still
		if (err < B_OK) {
			syslog(LOG_ERR, "UVCCamDevice: Still capture timeout: %s\n",
				strerror(err));
			return err;
		}

		CamFrame* f;
		bigtime_t stamp;
		err = fDeframer->GetFrame(&f, &stamp);
		if (err < B_OK)
			return err;

		size_t frameSize = f->BufferLength();
		if (frameSize > bufferSize) {
			syslog(LOG_WARNING, "UVCCamDevice: Still frame too large: %zu > %zu\n",
				frameSize, bufferSize);
			frameSize = bufferSize;
		}

		memcpy(buffer, f->Buffer(), frameSize);
		*bytesWritten = frameSize;

		if (fDeframer != NULL)
			fDeframer->RecycleFrame(f);
		else
			delete f;

		syslog(LOG_INFO, "UVCCamDevice: Still capture OK: %zu bytes\n",
			*bytesWritten);
		return B_OK;
	}

	// Method 1 and 3 use hardware button - not host-triggerable
	syslog(LOG_INFO, "UVCCamDevice: Still capture method %d not host-triggerable\n",
		fStillCaptureMethod);
	return B_NOT_SUPPORTED;
}


