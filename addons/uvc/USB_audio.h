/*
 * Copyright 2024 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * USB Audio Class 1.0 definitions for UVC webcam microphone support.
 */
#ifndef _USB_AUDIO_H
#define _USB_AUDIO_H

#include <SupportDefs.h>

/* USB Audio Device Class code */
#define USB_AUDIO_DEVICE_CLASS				0x01

/* USB Audio Interface Subclass codes */
#define USB_AUDIO_INTERFACE_AUDIOCONTROL	0x01
#define USB_AUDIO_INTERFACE_AUDIOSTREAMING	0x02

/* USB Audio Class 1.0 Audio Control Interface descriptor subtypes */
#define USB_AUDIO_AC_HEADER					0x01
#define USB_AUDIO_AC_INPUT_TERMINAL			0x02
#define USB_AUDIO_AC_OUTPUT_TERMINAL		0x03
#define USB_AUDIO_AC_FEATURE_UNIT			0x06

/* USB Audio Class 1.0 Audio Streaming Interface descriptor subtypes */
#define USB_AUDIO_AS_GENERAL				0x01
#define USB_AUDIO_AS_FORMAT_TYPE			0x02

/* USB Audio Terminal Types */
#define USB_AUDIO_TERMINAL_USB_STREAMING	0x0101
#define USB_AUDIO_TERMINAL_MICROPHONE		0x0201

/* Audio Data Format Type I */
#define USB_AUDIO_FORMAT_TYPE_I				0x01
#define USB_AUDIO_FORMAT_PCM				0x0001

/* Feature Unit Control Selectors */
#define USB_AUDIO_FU_MUTE_CONTROL			0x01
#define USB_AUDIO_FU_VOLUME_CONTROL			0x02

/* Audio Class-specific Request Codes */
#define USB_AUDIO_RC_SET_CUR				0x01
#define USB_AUDIO_RC_GET_CUR				0x81
#define USB_AUDIO_RC_GET_MIN				0x82
#define USB_AUDIO_RC_GET_MAX				0x83
#define USB_AUDIO_RC_GET_RES				0x84

/* Endpoint Control Selectors */
#define USB_AUDIO_EP_SAMPLING_FREQ_CONTROL	0x01

/* Generic Audio Class Descriptor header */
typedef struct usb_audio_class_descriptor {
	uint8	length;
	uint8	descriptorType;
	uint8	descriptorSubtype;
} _PACKED usb_audio_class_descriptor;

/* AC Interface Header Descriptor */
typedef struct usb_audio_ac_header_descriptor {
	uint8	length;
	uint8	descriptorType;
	uint8	descriptorSubtype;
	uint16	bcdADC;
	uint16	totalLength;
	uint8	inCollection;
	uint8	interfaceNumbers[0];
} _PACKED usb_audio_ac_header_descriptor;

/* Input Terminal Descriptor */
typedef struct usb_audio_input_terminal_descriptor {
	uint8	length;
	uint8	descriptorType;
	uint8	descriptorSubtype;
	uint8	terminalID;
	uint16	terminalType;
	uint8	associatedTerminal;
	uint8	numChannels;
	uint16	channelConfig;
	uint8	channelNames;
	uint8	terminal;
} _PACKED usb_audio_input_terminal_descriptor;

/* Output Terminal Descriptor */
typedef struct usb_audio_output_terminal_descriptor {
	uint8	length;
	uint8	descriptorType;
	uint8	descriptorSubtype;
	uint8	terminalID;
	uint16	terminalType;
	uint8	associatedTerminal;
	uint8	sourceID;
	uint8	terminal;
} _PACKED usb_audio_output_terminal_descriptor;

/* Feature Unit Descriptor (variable length) */
typedef struct usb_audio_feature_unit_descriptor {
	uint8	length;
	uint8	descriptorType;
	uint8	descriptorSubtype;
	uint8	unitID;
	uint8	sourceID;
	uint8	controlSize;
	uint8	controls[0];
} _PACKED usb_audio_feature_unit_descriptor;

/* AS Interface General Descriptor */
typedef struct usb_audio_as_general_descriptor {
	uint8	length;
	uint8	descriptorType;
	uint8	descriptorSubtype;
	uint8	terminalLink;
	uint8	delay;
	uint16	formatTag;
} _PACKED usb_audio_as_general_descriptor;

/* Type I Format Descriptor */
typedef struct usb_audio_format_type_i_descriptor {
	uint8	length;
	uint8	descriptorType;
	uint8	descriptorSubtype;
	uint8	formatType;
	uint8	numChannels;
	uint8	subFrameSize;
	uint8	bitResolution;
	uint8	sampleFreqType;
	uint8	sampleFrequencies[0];
} _PACKED usb_audio_format_type_i_descriptor;

/* Helper to get sample frequency from 3-byte packed format */
static inline uint32
usb_audio_get_sample_rate(const uint8* data)
{
	return (uint32)data[0] | ((uint32)data[1] << 8) | ((uint32)data[2] << 16);
}

#endif /* _USB_AUDIO_H */
