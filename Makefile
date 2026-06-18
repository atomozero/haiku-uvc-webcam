# Makefile for UVC USB Webcam driver add-on
# Standalone build for Haiku OS

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
	addons/NW80xCamDevice.cpp

OBJECTS = $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

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

.PHONY: all clean install
