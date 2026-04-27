#include "PlatformUsb.h"

#include "Log.h"

#include <cstdint>
#include <string>

namespace platform_usb {
namespace {

std::string fallback_busid(uint8_t busnum, uint8_t devnum) {
    return std::to_string(static_cast<unsigned>(busnum)) + "-" +
           std::to_string(static_cast<unsigned>(devnum));
}

std::string topology_busid(libusb_device* dev) {
    const uint8_t busnum = libusb_get_bus_number(dev);
    std::uint8_t ports[8] = {};
    const int count = libusb_get_port_numbers(dev, ports, sizeof(ports));

    if (count <= 0) {
        return fallback_busid(busnum, libusb_get_device_address(dev));
    }

    std::string busid = std::to_string(static_cast<unsigned>(busnum));
    busid += "-";

    for (int i = 0; i < count; ++i) {
        if (i != 0) {
            busid += ".";
        }
        busid += std::to_string(static_cast<unsigned>(ports[i]));
    }

    return busid;
}

} // namespace

const char* name() {
    return "darwin";
}

std::string busid_for_device(libusb_device* dev) {
    return topology_busid(dev);
}

std::string device_path_for_busid(const std::string& busid, libusb_device*) {
    return "/usbip-libusb/darwin/" + busid;
}

bool detach_kernel_driver(libusb_device_handle* handle, int interface_number) {
    const int active = libusb_kernel_driver_active(handle, interface_number);
    if (active == 0) {
        return true;
    }

    if (active < 0) {
        LOGT("kernel driver active check failed/unsupported on macOS: " << libusb_error_name(active));
        return true;
    }

    const int rc = libusb_detach_kernel_driver(handle, interface_number);
    if (rc != 0) {
        LOGW("libusb_detach_kernel_driver failed on macOS: " << libusb_error_name(rc));
        return true;
    }

    LOGI("kernel driver detached from interface " << interface_number);
    return true;
}

void attach_kernel_driver(libusb_device_handle*, int) {
    // macOS does not have Linux-style per-interface usb-storage reattach semantics here.
}

} // namespace platform_usb
