# isocam: a fake UVC 1.1 camera that streams as most laptop cameras do,
# tools/campeer.py --iso behind a QEMU usb-redir device (port 10716):
# 1209:000b "NovaOS Isochronous Camera", whose streaming interface has no
# endpoint in its first setting and an isochronous one (1024 or 3 x 1024
# bytes a packet, every millisecond) in each of two more, with MJPEG
# 640x480 and 320x240 frames (baseline JPEG images without Huffman
# tables) and YUY2 160x120.  NovaOS's UVC driver (kernel/nt/usbvideo.c)
# selects the setting the payload needs and reads the stream through
# isochronous URBs (kernel/nt/usbd.c); camtest --iso reads every mode,
# the MJPEG frames as they come and decoded by the source reader
# (userland/mfplat/mjpeg.c) into RGB32, NV12 and YUY2, checking the test
# card's bars and frame numbers; then the Camera app shows the camera's
# first mode, MJPEG 640x480, decoded.

TESTS = [
    Test('isochronous camera x86', r'C:\Programs\x86\camtest.exe --iso', [r'camtest: \d+ passed, 0 failed'], timeout=600,
         boot_expect=[r'\[UVC\] 1209:000b: UVC 1\.10, control interface 0, streaming interface 1, endpoint 81, 3 mode\(s\)',
                      r'\[UVC\] 1209:000b: isochronous, 2 setting\(s\), up to 3072 bytes a packet',
                      r'\[UVC\] 1209:000b: started as a camera']),
    Test('isochronous camera', 'camtest --iso', [r'camtest: \d+ passed, 0 failed'], timeout=600),
    Test('isochronous camera app', 'camera --frames 90', [r'camera: showed 90 frames'], timeout=600,
         shot=r'camera: first frame'),
]
