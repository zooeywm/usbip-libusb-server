#pragma once

#include <libusb-1.0/libusb.h>

#include <string>

namespace platform_usb {

const char* name();

std::string busid_for_device(libusb_device* dev);
std::string device_path_for_busid(const std::string& busid, libusb_device* dev);

bool detach_kernel_driver(libusb_device_handle* handle, int interface_number);
void attach_kernel_driver(libusb_device_handle* handle, int interface_number);

} // namespace platform_usb
