#!/usr/bin/env python3
"""The host end of a QEMU virtio-serial port, for the vioser self-test.

    tools/drivertest/serialpeer.py PORT

QEMU's port (-device virtserialport) is backed by a socket chardev that
connects to this program:

    -chardev socket,id=vs,host=127.0.0.1,port=10720
    -device virtio-serial-pci -device virtserialport,chardev=vs,name=org.novaos.test.0

It listens on 127.0.0.1:PORT, prints "listening", takes QEMU's
connection and sends back every byte the guest writes XORed with 0x5A,
so the guest (userland/programs/viosertest.c) can tell its bytes went to
the host and came back changed.  It prints how many bytes it echoed.
"""
import socket, sys

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', int(sys.argv[1])))
srv.listen(1)
print('listening', flush=True)
while True:
    conn, _ = srv.accept()
    total = 0
    while True:
        data = conn.recv(65536)
        if not data:
            break
        conn.sendall(bytes(b ^ 0x5A for b in data))
        total += len(data)
        print(f'echoed {total} bytes', flush=True)
    conn.close()
