# usbip-libusb-server

A prototype USB/IP server implemented in userspace with libusb.

Current status:
- Linux USB Mass Storage / BOT path is the validated target.
- Linux-specific device identity and kernel-driver detach/reattach logic is isolated in `PlatformUsbLinux.cpp`.
- macOS scaffolding is isolated in `PlatformUsbDarwin.cpp`; it is a portability layer, not a guarantee that macOS can claim every USB storage device without additional entitlement / unmount handling.

Build:

```bash
cmake -S . -B build -G Ninja
cmake --build build
sudo ./build/usbip-libusb-server 3240
```

Client:

```bash
sudo modprobe vhci-hcd
usbip list -r <server-ip>
sudo usbip attach -r <server-ip> -b <busid>
```
