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
LDFLAGS = -shared -Xlinker -soname=uvc_webcam.media_addon

TARGET = uvc_webcam.media_addon

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
	cp $(TARGET) /boot/home/config/non-packaged/add-ons/media/

.PHONY: all clean install
