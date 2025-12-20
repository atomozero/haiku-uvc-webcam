/*
 * Test Camera Terminal parsing with realistic USB descriptor data
 * Based on SunplusIT 5986:2113 webcam descriptor from bubicam_log
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// UVC Terminal types
#define USB_VIDEO_ITT_CAMERA    0x0201

// CT control bit positions (UVC 1.5 spec Table 4-12)
#define CT_SCANNING_MODE           0
#define CT_AUTO_EXPOSURE_MODE      1
#define CT_AUTO_EXPOSURE_PRIORITY  2
#define CT_EXPOSURE_TIME_ABS       3
#define CT_EXPOSURE_TIME_REL       4
#define CT_FOCUS_ABS               5
#define CT_FOCUS_REL               6
#define CT_IRIS_ABS                7
#define CT_IRIS_REL                8
#define CT_ZOOM_ABS                9
#define CT_ZOOM_REL               10
#define CT_PAN_TILT_ABS           11
#define CT_PAN_TILT_REL           12
#define CT_ROLL_ABS               13
#define CT_ROLL_REL               14
#define CT_RESERVED_15            15
#define CT_RESERVED_16            16
#define CT_FOCUS_AUTO             17
#define CT_PRIVACY                18
#define CT_FOCUS_SIMPLE           19
#define CT_WINDOW                 20
#define CT_REGION_OF_INTEREST     21

// Simulated USB descriptor structure (like Haiku's usb_video_camera_terminal_descriptor)
#pragma pack(push, 1)
struct usb_video_camera_terminal_descriptor {
	uint8_t length;
	uint8_t descriptor_type;
	uint8_t descriptor_subtype;
	uint8_t terminal_id;
	uint16_t terminal_type;
	uint8_t associated_terminal;
	uint8_t terminal_string;
	uint16_t objective_focal_length_min;
	uint16_t objective_focal_length_max;
	uint16_t ocular_focal_length;
	uint8_t control_size;
	uint8_t controls[3];  // Up to 3 bytes of control bitmask
};
#pragma pack(pop)

// Parse CT controls from descriptor (simulating driver logic)
uint32_t ParseCTControls(const usb_video_camera_terminal_descriptor* desc) {
	if (desc->terminal_type != USB_VIDEO_ITT_CAMERA) {
		return 0;  // Not a camera terminal
	}

	uint32_t controls = 0;

	// First byte of controls (bits 0-7)
	if (desc->control_size >= 1) {
		controls |= (desc->controls[0] & 0x01) ? (1 << CT_SCANNING_MODE) : 0;
		controls |= (desc->controls[0] & 0x02) ? (1 << CT_AUTO_EXPOSURE_MODE) : 0;
		controls |= (desc->controls[0] & 0x04) ? (1 << CT_AUTO_EXPOSURE_PRIORITY) : 0;
		controls |= (desc->controls[0] & 0x08) ? (1 << CT_EXPOSURE_TIME_ABS) : 0;
		controls |= (desc->controls[0] & 0x10) ? (1 << CT_EXPOSURE_TIME_REL) : 0;
		controls |= (desc->controls[0] & 0x20) ? (1 << CT_FOCUS_ABS) : 0;
		controls |= (desc->controls[0] & 0x40) ? (1 << CT_FOCUS_REL) : 0;
		controls |= (desc->controls[0] & 0x80) ? (1 << CT_IRIS_ABS) : 0;
	}

	// Second byte of controls (bits 8-15)
	if (desc->control_size >= 2) {
		controls |= (desc->controls[1] & 0x01) ? (1 << CT_IRIS_REL) : 0;
		controls |= (desc->controls[1] & 0x02) ? (1 << CT_ZOOM_ABS) : 0;
		controls |= (desc->controls[1] & 0x04) ? (1 << CT_ZOOM_REL) : 0;
		controls |= (desc->controls[1] & 0x08) ? (1 << CT_PAN_TILT_ABS) : 0;
		controls |= (desc->controls[1] & 0x10) ? (1 << CT_PAN_TILT_REL) : 0;
		controls |= (desc->controls[1] & 0x20) ? (1 << CT_ROLL_ABS) : 0;
		controls |= (desc->controls[1] & 0x40) ? (1 << CT_ROLL_REL) : 0;
		// bit 7 (0x80) is reserved
	}

	// Third byte of controls (bits 16-23)
	if (desc->control_size >= 3) {
		// bit 0 (0x01) is reserved
		controls |= (desc->controls[2] & 0x02) ? (1 << CT_FOCUS_AUTO) : 0;
		controls |= (desc->controls[2] & 0x04) ? (1 << CT_PRIVACY) : 0;
		controls |= (desc->controls[2] & 0x08) ? (1 << CT_FOCUS_SIMPLE) : 0;
		controls |= (desc->controls[2] & 0x10) ? (1 << CT_WINDOW) : 0;
		controls |= (desc->controls[2] & 0x20) ? (1 << CT_REGION_OF_INTEREST) : 0;
	}

	return controls;
}

void PrintCTControls(uint32_t controls) {
	printf("Camera Terminal Controls: 0x%08x\n", controls);
	if (controls & (1 << CT_SCANNING_MODE)) printf("  [x] Scanning Mode\n");
	if (controls & (1 << CT_AUTO_EXPOSURE_MODE)) printf("  [x] Auto Exposure Mode\n");
	if (controls & (1 << CT_AUTO_EXPOSURE_PRIORITY)) printf("  [x] Auto Exposure Priority\n");
	if (controls & (1 << CT_EXPOSURE_TIME_ABS)) printf("  [x] Exposure Time Absolute\n");
	if (controls & (1 << CT_EXPOSURE_TIME_REL)) printf("  [x] Exposure Time Relative\n");
	if (controls & (1 << CT_FOCUS_ABS)) printf("  [x] Focus Absolute\n");
	if (controls & (1 << CT_FOCUS_REL)) printf("  [x] Focus Relative\n");
	if (controls & (1 << CT_IRIS_ABS)) printf("  [x] Iris Absolute\n");
	if (controls & (1 << CT_IRIS_REL)) printf("  [x] Iris Relative\n");
	if (controls & (1 << CT_ZOOM_ABS)) printf("  [x] Zoom Absolute\n");
	if (controls & (1 << CT_ZOOM_REL)) printf("  [x] Zoom Relative\n");
	if (controls & (1 << CT_PAN_TILT_ABS)) printf("  [x] Pan/Tilt Absolute\n");
	if (controls & (1 << CT_PAN_TILT_REL)) printf("  [x] Pan/Tilt Relative\n");
	if (controls & (1 << CT_ROLL_ABS)) printf("  [x] Roll Absolute\n");
	if (controls & (1 << CT_ROLL_REL)) printf("  [x] Roll Relative\n");
	if (controls & (1 << CT_FOCUS_AUTO)) printf("  [x] Focus Auto\n");
	if (controls & (1 << CT_PRIVACY)) printf("  [x] Privacy\n");
	if (controls & (1 << CT_FOCUS_SIMPLE)) printf("  [x] Focus Simple\n");
	if (controls & (1 << CT_WINDOW)) printf("  [x] Window\n");
	if (controls & (1 << CT_REGION_OF_INTEREST)) printf("  [x] Region of Interest\n");
}

int main() {
	printf("========================================\n");
	printf("  Camera Terminal Parsing Test\n");
	printf("========================================\n\n");

	int passed = 0;
	int total = 0;

	// Test 1: Typical laptop webcam (Auto Exposure + Focus Auto)
	{
		total++;
		printf("Test 1: Typical laptop webcam\n");
		printf("  (SunplusIT-like, ControlSize=3)\n\n");

		usb_video_camera_terminal_descriptor desc;
		memset(&desc, 0, sizeof(desc));
		desc.length = 18;
		desc.descriptor_type = 0x24;  // CS_INTERFACE
		desc.descriptor_subtype = 0x02;  // VC_INPUT_TERMINAL
		desc.terminal_id = 1;
		desc.terminal_type = USB_VIDEO_ITT_CAMERA;
		desc.control_size = 3;

		// Typical laptop webcam controls:
		// - Auto Exposure Mode (bit 1)
		// - Exposure Time Absolute (bit 3)
		// - Focus Absolute (bit 5)
		desc.controls[0] = 0x2A;  // 0010 1010 = AE Mode + Exp Time Abs + Focus Abs
		desc.controls[1] = 0x00;
		desc.controls[2] = 0x02;  // Focus Auto

		uint32_t controls = ParseCTControls(&desc);
		PrintCTControls(controls);

		bool hasAE = (controls & (1 << CT_AUTO_EXPOSURE_MODE)) != 0;
		bool hasExpTime = (controls & (1 << CT_EXPOSURE_TIME_ABS)) != 0;
		bool hasFocus = (controls & (1 << CT_FOCUS_ABS)) != 0;
		bool hasAutoFocus = (controls & (1 << CT_FOCUS_AUTO)) != 0;

		if (hasAE && hasExpTime && hasFocus && hasAutoFocus) {
			printf("\n  Result: PASS\n");
			passed++;
		} else {
			printf("\n  Result: FAIL\n");
		}
	}

	printf("\n----------------------------------------\n\n");

	// Test 2: High-end PTZ webcam
	{
		total++;
		printf("Test 2: High-end PTZ webcam\n");
		printf("  (Zoom + Pan/Tilt + Focus)\n\n");

		usb_video_camera_terminal_descriptor desc;
		memset(&desc, 0, sizeof(desc));
		desc.length = 18;
		desc.descriptor_type = 0x24;
		desc.descriptor_subtype = 0x02;
		desc.terminal_id = 1;
		desc.terminal_type = USB_VIDEO_ITT_CAMERA;
		desc.control_size = 3;

		// PTZ webcam controls:
		desc.controls[0] = 0x2E;  // AE Mode + AE Priority + Exp Time Abs + Focus Abs
		desc.controls[1] = 0x0A;  // Zoom Abs + Pan/Tilt Abs
		desc.controls[2] = 0x02;  // Focus Auto

		uint32_t controls = ParseCTControls(&desc);
		PrintCTControls(controls);

		bool hasZoom = (controls & (1 << CT_ZOOM_ABS)) != 0;
		bool hasPanTilt = (controls & (1 << CT_PAN_TILT_ABS)) != 0;

		if (hasZoom && hasPanTilt) {
			printf("\n  Result: PASS\n");
			passed++;
		} else {
			printf("\n  Result: FAIL\n");
		}
	}

	printf("\n----------------------------------------\n\n");

	// Test 3: Basic webcam (minimal controls)
	{
		total++;
		printf("Test 3: Basic webcam\n");
		printf("  (Auto Exposure only, ControlSize=1)\n\n");

		usb_video_camera_terminal_descriptor desc;
		memset(&desc, 0, sizeof(desc));
		desc.length = 15;
		desc.descriptor_type = 0x24;
		desc.descriptor_subtype = 0x02;
		desc.terminal_id = 1;
		desc.terminal_type = USB_VIDEO_ITT_CAMERA;
		desc.control_size = 1;

		desc.controls[0] = 0x02;  // Only AE Mode

		uint32_t controls = ParseCTControls(&desc);
		PrintCTControls(controls);

		bool hasAE = (controls & (1 << CT_AUTO_EXPOSURE_MODE)) != 0;
		bool noFocus = (controls & (1 << CT_FOCUS_ABS)) == 0;
		bool noZoom = (controls & (1 << CT_ZOOM_ABS)) == 0;

		if (hasAE && noFocus && noZoom) {
			printf("\n  Result: PASS\n");
			passed++;
		} else {
			printf("\n  Result: FAIL\n");
		}
	}

	printf("\n----------------------------------------\n\n");

	// Test 4: Privacy-enabled webcam
	{
		total++;
		printf("Test 4: Privacy-enabled webcam\n");
		printf("  (With privacy shutter control)\n\n");

		usb_video_camera_terminal_descriptor desc;
		memset(&desc, 0, sizeof(desc));
		desc.length = 18;
		desc.descriptor_type = 0x24;
		desc.descriptor_subtype = 0x02;
		desc.terminal_id = 1;
		desc.terminal_type = USB_VIDEO_ITT_CAMERA;
		desc.control_size = 3;

		desc.controls[0] = 0x2A;  // AE Mode + Exp Time Abs + Focus Abs
		desc.controls[1] = 0x00;
		desc.controls[2] = 0x06;  // Focus Auto + Privacy

		uint32_t controls = ParseCTControls(&desc);
		PrintCTControls(controls);

		bool hasPrivacy = (controls & (1 << CT_PRIVACY)) != 0;

		if (hasPrivacy) {
			printf("\n  Result: PASS\n");
			passed++;
		} else {
			printf("\n  Result: FAIL\n");
		}
	}

	printf("\n----------------------------------------\n\n");

	// Test 5: Non-camera terminal (should return 0)
	{
		total++;
		printf("Test 5: Non-camera terminal\n");
		printf("  (Composite video input, type=0x0401)\n\n");

		usb_video_camera_terminal_descriptor desc;
		memset(&desc, 0, sizeof(desc));
		desc.length = 18;
		desc.descriptor_type = 0x24;
		desc.descriptor_subtype = 0x02;
		desc.terminal_id = 1;
		desc.terminal_type = 0x0401;  // Composite video input
		desc.control_size = 3;
		desc.controls[0] = 0xFF;
		desc.controls[1] = 0xFF;
		desc.controls[2] = 0xFF;

		uint32_t controls = ParseCTControls(&desc);

		if (controls == 0) {
			printf("  Controls: 0x00000000 (correctly ignored)\n");
			printf("\n  Result: PASS\n");
			passed++;
		} else {
			printf("  Controls: 0x%08x (should be 0!)\n", controls);
			printf("\n  Result: FAIL\n");
		}
	}

	printf("\n========================================\n");
	printf("  SUMMARY: %d/%d tests passed\n", passed, total);
	printf("========================================\n");

	return (passed == total) ? 0 : 1;
}
