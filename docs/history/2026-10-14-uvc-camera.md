## Cameras: USB Video Class through Media Foundation

Roadmap step 28.3, the part QEMU can show: a USB camera's picture in a
Windows program, through the same Media Foundation calls a Windows camera
app makes.

- **UVC class driver** (`kernel/nt/usbvideo.c`, the service `usbvideo`,
  built into the kernel): Plug and Play gives it a device with a USB
  Video Class interface (`USB\Class_0E`); the USB core now keeps a
  camera's control and streaming interfaces together as one function.
  It parses the UVC 1.0 to 1.5 descriptors (uncompressed YUY2 and NV12,
  MJPEG; discrete and continuous frame intervals), negotiates with probe
  and commit, reads the bulk streaming endpoint and builds frames from
  the payload headers (frame ID, end of frame, errors), and turns on
  `KSCATEGORY_VIDEO_CAMERA`.  User mode talks to it with private device
  controls (`userland/include/novacam.h`); one handle streams at a time.
- **Media Foundation** (`mfplat.dll`, `mf.dll`, `mfreadwrite.dll`, new):
  attributes, media types, memory and 2D buffers, samples;
  `MFEnumDeviceSources` and `MFCreateDeviceSource` for video capture
  devices; the camera media source with its presentation and stream
  descriptors and type handler; `MFCreateSourceReaderFromMediaSource`
  with synchronous and asynchronous reads, `Flush`, and conversion from
  YUY2 and NV12 to RGB32 and between each other.  `MFTEnumEx` finds no
  transforms, so browsers that look for Media Foundation's codecs keep
  their own decoders.
- **Camera app** (`camera.exe`): the first camera's picture scaled to its
  window, with the camera's name, frame size and frame rate beneath.
- cfgmgr32 lists the interfaces a Windows driver turns on for a USB
  device (before, only a PCI device's or a bus driver's child's), and
  `STATUS_DEVICE_BUSY` becomes `ERROR_BUSY` as on Windows.

New devices-suite boot `camera`: `tools/campeer.py` is a fake UVC 1.1
camera (1209:000a, three modes, a test card with colour bars and the
frame number in blocks) behind QEMU's usb-redir; `camtest` checks it
through Media Foundation, 32- and 64-bit, and the Camera app shows it
(`docs/screenshots/camera.png`).

- Not yet (what the T14's webcam needs): isochronous transfers in
  `usbd.c`, a function per interface on composite devices (the webcam's
  microphone), MJPEG decoding, then a boot on the T14.  Also missing:
  camera controls, still images, the media session and source events,
  audio capture sources, file and URL source readers, and AVStream for
  vendor camera drivers.
