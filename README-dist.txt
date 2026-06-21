Haiku UVC Webcam Driver
=======================

USB Video Class (UVC) webcam driver for Haiku OS, with support for many
common webcam models. Provides video capture and (when available) audio
capture through the standard Haiku Media Kit.

Contents
--------

aukey_webcam_v4-<arch>.media_addon
    Precompiled media addon for the specified architecture
    (x86_64 = Haiku 64-bit, x86 = Haiku 32-bit).

src/
    Complete sources. Use these to build the driver for a different
    architecture (typically Haiku x86 32-bit).

Using the precompiled binary
----------------------------

If your Haiku architecture matches the binary's, simply copy it into
your user add-ons directory:

    cp aukey_webcam_v4-x86_64.media_addon \
       /boot/home/config/non-packaged/add-ons/media/aukey_webcam_v4.media_addon

Then restart the media servers (or reboot):

    kill $(pidof media_addon_server)
    kill $(pidof media_server)

Open a webcam application (CodyCam, BubiCam, etc.) and select your
webcam from the device menu.

Building from source
--------------------

On Haiku x86 (32-bit) or any architecture different from the bundled
binary, build from the included sources:

    cd src
    make
    make install

Dependencies:
    pkgman install libjpeg_turbo_devel

The resulting addon will be installed into
/boot/home/config/non-packaged/add-ons/media/

Supported devices
-----------------

The driver supports the USB Video Class 1.0/1.1 standard and works with
many common webcams. Specifically tested or listed devices include:

  - Chicony CNF8111 (04f2:b119)
  - Chicony HD UVC Webcam (04f2:b40a)
  - AUKEY PC-LM1E (1bcf:0001)
  - Microdia Motion Eye (0c45:6409)
  - and many others

Both YUY2 (uncompressed) and MJPEG (compressed) video formats are
supported, plus UAC audio when present.

Diagnostics
-----------

If the webcam is not detected, verify the device shows up via:

    listusb | grep <VID>

Check the system log for messages from the driver:

    tail -f /var/log/syslog | grep -E "UVC|webcam|CamRoster"

Bug reports
-----------

https://github.com/atomozero/haiku-uvc-webcam/issues
