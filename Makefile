# Makefile for UVC USB Webcam driver add-on
# Standalone build for Haiku OS
#
# Native build:
#   make              - Build for current architecture
#   make install      - Install to user add-ons directory
#   make clean        - Remove build artifacts
#   make dist-zip     - Create distributable zip with sources + binary
#
# 32-bit (x86) build:
#   On Haiku x86 (32-bit native), just run `make`. The g++ compiler
#   produces 32-bit binaries directly. On Haiku x86_64 there is no
#   multilib support, so cross-compiling to x86 is not possible from
#   a 64-bit system without a separate toolchain.

# Detect architecture
ARCH := $(shell uname -m)

# Map architecture to a short suffix used in the binary name
ifeq ($(ARCH),x86_64)
	ARCH_SUFFIX = x86_64
else ifeq ($(ARCH),BePC)
	ARCH_SUFFIX = x86
else
	ARCH_SUFFIX = $(ARCH)
endif

CC = g++
CFLAGS = -O2 -Wall -Wno-multichar -fPIC
INCLUDES = -I. -I./addons/uvc \
	-I/boot/system/develop/headers \
	-I/boot/system/develop/headers/private/media \
	-I/boot/system/develop/headers/private/shared \
	-I/boot/system/develop/headers/private/drivers

LIBS = -lbe -lmedia -ldevice -lturbojpeg
LDFLAGS = -shared -Xlinker -soname=aukey_webcam_v4.media_addon

TARGET = aukey_webcam_v4.media_addon

# Source files
SOURCES = \
	AddOn.cpp \
	Producer.cpp \
	AudioProducer.cpp \
	CamBufferedFilterInterface.cpp \
	CamBufferingDeframer.cpp \
	CamColorSpaceTransform.cpp \
	CamDebug.cpp \
	CamDeframer.cpp \
	CamDevice.cpp \
	CamFilterInterface.cpp \
	CamRoster.cpp \
	CamSensor.cpp \
	CamStreamingDeframer.cpp \
	addons/uvc/UVCCamDevice.cpp \
	addons/uvc/UVCDeframer.cpp \
	addons/uvc/UVCQuirks.cpp \
	addons/NW80xCamDevice.cpp

OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)
	@echo ""
	@echo "Built $(TARGET) for $(ARCH_SUFFIX)"

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
	rm -rf dist haiku-uvc-webcam-$(ARCH_SUFFIX).zip

install: $(TARGET)
	mkdir -p /boot/home/config/non-packaged/add-ons/media
	# Stop media servers BEFORE replacing the addon to avoid crashes
	# (the addon is mapped in memory by media_addon_server)
	-kill $$(pidof CodyCam) 2>/dev/null || true
	-kill $$(pidof BubiCam) 2>/dev/null || true
	-kill -9 $$(pidof media_addon_server) 2>/dev/null || true
	-kill -9 $$(pidof media_server) 2>/dev/null || true
	sleep 1
	cp $(TARGET) /boot/home/config/non-packaged/add-ons/media/
	@echo ""
	@echo "Driver installed. Media servers will auto-restart when needed."

# Build a distributable zip containing the compiled binary and the full
# sources. The receiver can either use the binary directly (if on the
# same arch) or run `make` from src/ to compile for their architecture
# (useful for Haiku x86 32-bit users).
dist-zip: $(TARGET)
	rm -rf dist
	mkdir -p dist/haiku-uvc-webcam-pkg/src
	cp $(TARGET) dist/haiku-uvc-webcam-pkg/$(TARGET:.media_addon=-$(ARCH_SUFFIX).media_addon)
	cp Makefile *.cpp *.h dist/haiku-uvc-webcam-pkg/src/
	cp -r addons dist/haiku-uvc-webcam-pkg/src/
	find dist/haiku-uvc-webcam-pkg/src -name '*.o' -delete
	@if [ -f README-dist.txt ]; then \
		cp README-dist.txt dist/haiku-uvc-webcam-pkg/README.txt; \
	fi
	cd dist && zip -qr ../haiku-uvc-webcam-$(ARCH_SUFFIX).zip haiku-uvc-webcam-pkg/
	@echo ""
	@echo "Created haiku-uvc-webcam-$(ARCH_SUFFIX).zip"
	@echo "Contains: $(TARGET:.media_addon=-$(ARCH_SUFFIX).media_addon) + complete sources"

.PHONY: all clean install dist-zip
