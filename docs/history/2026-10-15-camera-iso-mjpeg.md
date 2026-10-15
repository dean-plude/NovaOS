## Isochronous cameras and MJPEG frames

The rest of roadmap step 28.3 that QEMU can show. Most webcams, the
T14's built-in one included, stream on an isochronous endpoint and send
their larger sizes as MJPEG; NovaOS's camera path could only read a bulk
endpoint and pass MJPEG frames on undecoded.

- **Isochronous URBs** (`kernel/nt/usbd.c`): `URB_FUNCTION_ISOCH_TRANSFER`
  on an IN pipe starts the USB core's isochronous stream on it; each
  packet the controller receives goes into a ring of the pipe's, and the
  pipe's thread fills each URB's packets from it in order (lengths,
  per-packet status, `ErrorCount`). Aborting the pipe, selecting another
  alternate setting or unplugging the device stops the stream.
- **The UVC driver** (`kernel/nt/usbvideo.c`) runs an isochronous camera:
  it lists the streaming interface's alternate settings and their packet
  sizes, selects the smallest that holds the payload size the camera
  answered in its probe, reads payloads a packet at a time from URBs of
  32 packets (a frame that lost a packet is dropped), and selects
  setting 0 again to stop.
- **MJPEG in the source reader** (`userland/mfplat/mjpeg.c`): frames are
  decoded with stb_image into RGB32, NV12 or YUY2; a frame without
  Huffman tables (most cameras leave them out) gets the JPEG standard's
  typical ones, and a frame that does not decode is skipped. A size the
  camera offers only as MJPEG can now be asked for as RGB32, so the
  Camera app shows such a camera.
- **Test**: `tools/campeer.py --iso` is a camera as laptops have them
  (1209:000b: settings of 1024 and 3 x 1024 bytes a packet, MJPEG
  640x480 and 320x240 without Huffman tables, YUY2 160x120), in the new
  devices-suite boot `isocam`; `camtest --iso` reads every mode and
  checks the decoded test card, and the Camera app shows it.

Still needed for 28.3: a boot of `latest` on the T14 showing its
webcam's picture in Camera.
