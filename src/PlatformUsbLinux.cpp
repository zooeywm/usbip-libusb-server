#include "PlatformUsb.h"

#include "Log.h"

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <string>

namespace platform_usb {
namespace {

std::string read_sysfs_value(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::string value;
    file >> value;
    return value;
}

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

std::string sysfs_busid(libusb_device* dev) {
    const uint8_t busnum = libusb_get_bus_number(dev);
    const uint8_t devnum = libusb_get_device_address(dev);
    const std::filesystem::path base = "/sys/bus/usb/devices";

    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) {
        return topology_busid(dev);
    }

    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }

        const std::string node = entry.path().filename().string();

        if (node.rfind("usb", 0) == 0 || node.find(':') != std::string::npos) {
            continue;
        }

        const auto busnum_path = entry.path() / "busnum";
        const auto devnum_path = entry.path() / "devnum";

        if (!std::filesystem::exists(busnum_path, ec) || !std::filesystem::exists(devnum_path, ec)) {
            continue;
        }

        try {
            const int sys_bus = std::stoi(read_sysfs_value(busnum_path));
            const int sys_dev = std::stoi(read_sysfs_value(devnum_path));

            if (sys_bus == static_cast<int>(busnum) && sys_dev == static_cast<int>(devnum)) {
                return node;
            }
        } catch (const std::exception&) {
            continue;
        }
    }

    return topology_busid(dev);
}

} // namespace

const char* name() {
    return "linux";
}

std::string busid_for_device(libusb_device* dev) {
    return sysfs_busid(dev);
}

std::string device_path_for_busid(const std::string& busid, libusb_device*) {
    return "/sys/bus/usb/devices/" + busid;
}

bool detach_kernel_driver(libusb_device_handle* handle, int interface_number) {
    const int active = libusb_kernel_driver_active(handle, interface_number);
    if (active == 0) {
        return true;
    }

    if (active < 0) {
        LOGT("kernel driver active check failed: " << libusb_error_name(active));
        return true;
    }

    const int rc = libusb_detach_kernel_driver(handle, interface_number);
    if (rc != 0) {
        LOGE("libusb_detach_kernel_driver failed: " << libusb_error_name(rc));
        return false;
    }

    LOGI("kernel driver detached from interface " << interface_number);
    return true;
}

void attach_kernel_driver(libusb_device_handle* handle, int interface_number) {
    const int rc = libusb_attach_kernel_driver(handle, interface_number);
    if (rc != 0) {
        LOGT("libusb_attach_kernel_driver skipped/failed: " << libusb_error_name(rc));
    }
}

} // namespace platform_usb
