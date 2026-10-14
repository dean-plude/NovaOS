# camera: a fake UVC 1.1 camera on the xHCI controller, tools/campeer.py
# behind a QEMU usb-redir device (port 10714): 1209:000a "NovaOS Test
# Camera" with YUY2 640x480 and 320x240 and NV12 640x480, streaming on a
# bulk endpoint, each frame a test card (eight colour bars over the
# frame's number in blocks).  Plug and Play gives it NovaOS's UVC class
# driver (kernel/nt/usbvideo.c), which must log the camera's interfaces
# and modes.  camtest lists it with MFEnumDeviceSources and reads frames in
# each mode and through the source reader's converters, checking the bars
# and the frame numbers, 32-bit first; then the Camera app shows it, and
# the screenshot is the reference picture (docs/screenshots/camera.png).

TESTS = [
    Test('camera x86', r'C:\Programs\x86\camtest.exe', [r'camtest: \d+ passed, 0 failed'], timeout=600,
         boot_expect=[r'\[UVC\] 1209:000a: UVC 1\.10, control interface 0, streaming interface 1, endpoint 81, 3 mode\(s\)',
                      r'\[UVC\] 1209:000a: started as a camera']),
    Test('camera', 'camtest', [r'camtest: \d+ passed, 0 failed'], timeout=600),
    Test('camera app', 'camera --frames 150', [r'camera: showed 150 frames'], timeout=600,
         shot=r'camera: first frame'),
]
