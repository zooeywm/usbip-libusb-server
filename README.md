# usbip-libusb-server

A minimal C++ USB/IP server prototype backed by libusb.

Current target:

- Linux development host
- Linux official usbip client / vhci-hcd
- USB Mass Storage / BOT flash drive

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

## Run

```bash
sudo ./build/usbip-libusb-server 3240 1
```

Log level:

```text
0 = error only
1 = info, default
2 = trace, per-URB / per-bulk logs
```

## Client test

```bash
sudo modprobe vhci-hcd
usbip list -r 127.0.0.1
sudo usbip attach -r 127.0.0.1 -b <busid>
lsblk
```

Detach:

```bash
sudo usbip port
sudo usbip detach -p <port>
```

## Notes

This is an MVP/prototype. It intentionally focuses on Mass Storage BOT and synchronous libusb transfers.
