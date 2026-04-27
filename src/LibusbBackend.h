#pragma once

#include "UsbIpProtocol.h"

#include <libusb-1.0/libusb.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct UsbRuntimeInfo {
    int interfaceNumber = -1;
    int altSetting = 0;
    uint8_t interfaceClass = 0;
    uint8_t interfaceSubClass = 0;
    uint8_t interfaceProtocol = 0;
    uint8_t bulkInEp = 0;
    uint8_t bulkOutEp = 0;
};

uint32_t convert_speed(int speed);

std::vector<UsbDeviceInfo> find_mass_storage_devices(libusb_context* ctx);
std::optional<UsbDeviceInfo> find_mass_storage_device_by_busid(libusb_context* ctx, const std::string& busid);
libusb_device_handle* open_device_by_busid(libusb_context* ctx, const std::string& busid);

std::optional<UsbRuntimeInfo> find_mass_storage_runtime(libusb_device_handle* handle);
bool claim_interface(libusb_device_handle* handle, int interfaceNumber);
bool select_alt_setting(libusb_device_handle* handle, const UsbRuntimeInfo& rt);
bool reclaim_mass_storage_interface(libusb_device_handle* handle, UsbRuntimeInfo& rt);
void bot_reset_recovery(libusb_device_handle* handle, const UsbRuntimeInfo& rt, const char* reason);

int handle_control_submit(
    libusb_device_handle* handle,
    UsbRuntimeInfo& rt,
    const UrbSubmit& urb,
    std::vector<uint8_t>& response);

int handle_bulk_submit(
    libusb_device_handle* handle,
    const UsbRuntimeInfo& rt,
    const UrbSubmit& urb,
    std::vector<uint8_t>& response,
    int& transferred);
