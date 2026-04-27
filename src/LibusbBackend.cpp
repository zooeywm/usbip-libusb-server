#include "LibusbBackend.h"

#include "Log.h"
#include "NetUtil.h"

#include <iostream>

uint32_t convert_speed(int speed) {
    switch (speed) {
    case LIBUSB_SPEED_LOW: return 1;
    case LIBUSB_SPEED_FULL: return 2;
    case LIBUSB_SPEED_HIGH: return 3;
    case LIBUSB_SPEED_SUPER: return 5;
    case LIBUSB_SPEED_SUPER_PLUS: return 6;
    default: return 0;
    }
}

std::optional<UsbDeviceInfo> find_mass_storage_device(libusb_context* ctx) {
    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        return std::nullopt;
    }

    std::optional<UsbDeviceInfo> result;

    for (ssize_t i = 0; i < count && !result; ++i) {
        libusb_device* dev = list[i];

        libusb_device_descriptor dd{};
        if (libusb_get_device_descriptor(dev, &dd) != 0) {
            continue;
        }

        libusb_config_descriptor* cfg = nullptr;
        if (libusb_get_active_config_descriptor(dev, &cfg) != 0) {
            if (libusb_get_config_descriptor(dev, 0, &cfg) != 0) {
                continue;
            }
        }

        bool isMassStorage = false;
        std::vector<UsbInterfaceInfo> ifaces;

        for (uint8_t j = 0; j < cfg->bNumInterfaces; ++j) {
            const libusb_interface& intf = cfg->interface[j];
            if (intf.num_altsetting <= 0) {
                continue;
            }

            const libusb_interface_descriptor& alt = intf.altsetting[0];

            UsbInterfaceInfo ii;
            ii.cls = alt.bInterfaceClass;
            ii.subcls = alt.bInterfaceSubClass;
            ii.proto = alt.bInterfaceProtocol;
            ifaces.push_back(ii);

            if (ii.cls == 0x08) {
                isMassStorage = true;
            }
        }

        if (!isMassStorage) {
            libusb_free_config_descriptor(cfg);
            continue;
        }

        UsbDeviceInfo info;
        info.busnum = libusb_get_bus_number(dev);
        info.devnum = libusb_get_device_address(dev);
        info.speed = convert_speed(libusb_get_device_speed(dev));

        info.vid = dd.idVendor;
        info.pid = dd.idProduct;
        info.bcdDevice = dd.bcdDevice;

        info.devClass = dd.bDeviceClass;
        info.devSubClass = dd.bDeviceSubClass;
        info.devProtocol = dd.bDeviceProtocol;
        info.numConfigurations = dd.bNumConfigurations;
        info.configValue = cfg->bConfigurationValue;
        info.interfaces = std::move(ifaces);

        info.busid = std::to_string(info.busnum) + "-" + std::to_string(info.devnum);
        info.path = "/sys/devices/usbip-libusb/" + info.busid;

        result = info;
        libusb_free_config_descriptor(cfg);
    }

    libusb_free_device_list(list, 1);
    return result;
}

libusb_device_handle* open_device_by_busid(libusb_context* ctx, const std::string& busid) {
    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        return nullptr;
    }

    libusb_device_handle* handle = nullptr;

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* dev = list[i];
        uint8_t busnum = libusb_get_bus_number(dev);
        uint8_t devnum = libusb_get_device_address(dev);

        std::string cur = std::to_string(busnum) + "-" + std::to_string(devnum);
        if (cur != busid) {
            continue;
        }

        if (libusb_open(dev, &handle) != 0) {
            handle = nullptr;
        }
        break;
    }

    libusb_free_device_list(list, 1);
    return handle;
}

std::optional<UsbRuntimeInfo> find_mass_storage_runtime(libusb_device_handle* handle) {
    libusb_device* dev = libusb_get_device(handle);

    libusb_config_descriptor* cfg = nullptr;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0) {
        if (libusb_get_config_descriptor(dev, 0, &cfg) != 0) {
            return std::nullopt;
        }
    }

    std::optional<UsbRuntimeInfo> fallback;
    std::optional<UsbRuntimeInfo> bot;

    for (uint8_t i = 0; i < cfg->bNumInterfaces; ++i) {
        const libusb_interface& intf = cfg->interface[i];

        for (int j = 0; j < intf.num_altsetting; ++j) {
            const libusb_interface_descriptor& alt = intf.altsetting[j];

            if (alt.bInterfaceClass != 0x08) {
                continue;
            }

            UsbRuntimeInfo rt;
            rt.interfaceNumber = alt.bInterfaceNumber;
            rt.altSetting = alt.bAlternateSetting;
            rt.interfaceClass = alt.bInterfaceClass;
            rt.interfaceSubClass = alt.bInterfaceSubClass;
            rt.interfaceProtocol = alt.bInterfaceProtocol;

            for (uint8_t k = 0; k < alt.bNumEndpoints; ++k) {
                const libusb_endpoint_descriptor& ep = alt.endpoint[k];

                if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) {
                    continue;
                }

                if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
                    rt.bulkInEp = ep.bEndpointAddress;
                } else {
                    rt.bulkOutEp = ep.bEndpointAddress;
                }
            }

            LOGI("mass-storage candidate:"
                 << " if=" << static_cast<int>(rt.interfaceNumber)
                 << " alt=" << static_cast<int>(rt.altSetting)
                 << " proto=0x" << std::hex << static_cast<int>(rt.interfaceProtocol)
                 << " in=0x" << static_cast<int>(rt.bulkInEp)
                 << " out=0x" << static_cast<int>(rt.bulkOutEp)
                 << std::dec);

            if (rt.bulkInEp == 0 || rt.bulkOutEp == 0) {
                continue;
            }

            if (!fallback) {
                fallback = rt;
            }

            if (rt.interfaceSubClass == 0x06 && rt.interfaceProtocol == 0x50) {
                bot = rt;
            }
        }
    }

    libusb_free_config_descriptor(cfg);
    return bot ? bot : fallback;
}

bool claim_interface(libusb_device_handle* handle, int interfaceNumber) {
#ifdef __linux__
    if (libusb_kernel_driver_active(handle, interfaceNumber) == 1) {
        int rc = libusb_detach_kernel_driver(handle, interfaceNumber);
        if (rc != 0) {
            LOGE("libusb_detach_kernel_driver failed: " << libusb_error_name(rc));
            return false;
        }
        LOGI("kernel driver detached from interface " << interfaceNumber);
    }
#endif

    int rc = libusb_claim_interface(handle, interfaceNumber);
    if (rc != 0) {
        LOGE("libusb_claim_interface failed: " << libusb_error_name(rc));
        return false;
    }

    LOGI("claimed interface " << interfaceNumber);
    return true;
}

bool select_alt_setting(libusb_device_handle* handle, const UsbRuntimeInfo& rt) {
    int rc = libusb_set_interface_alt_setting(handle, rt.interfaceNumber, rt.altSetting);
    if (rc != 0) {
        LOGE("libusb_set_interface_alt_setting failed: "
             << libusb_error_name(rc)
             << " if=" << rt.interfaceNumber
             << " alt=" << rt.altSetting);
        return false;
    }

    LOGI("selected alt setting:"
         << " if=" << rt.interfaceNumber
         << " alt=" << rt.altSetting
         << " proto=0x" << std::hex << static_cast<int>(rt.interfaceProtocol)
         << std::dec);
    return true;
}

bool reclaim_mass_storage_interface(libusb_device_handle* handle, UsbRuntimeInfo& rt) {
    if (rt.interfaceNumber >= 0) {
        libusb_release_interface(handle, rt.interfaceNumber);
    }

    auto newRt = find_mass_storage_runtime(handle);
    if (!newRt) {
        LOGE("re-find mass storage runtime failed");
        return false;
    }

    rt = *newRt;

#ifdef __linux__
    if (libusb_kernel_driver_active(handle, rt.interfaceNumber) == 1) {
        int drc = libusb_detach_kernel_driver(handle, rt.interfaceNumber);
        if (drc != 0) {
            LOGE("detach after set-config failed: " << libusb_error_name(drc));
        }
    }
#endif

    int rc = libusb_claim_interface(handle, rt.interfaceNumber);
    if (rc != 0) {
        LOGE("re-claim interface failed: " << libusb_error_name(rc));
        return false;
    }

    if (!select_alt_setting(handle, rt)) {
        return false;
    }

    libusb_clear_halt(handle, rt.bulkInEp);
    libusb_clear_halt(handle, rt.bulkOutEp);

    LOGI("reclaimed interface=" << rt.interfaceNumber
         << " bulkIn=0x" << std::hex << static_cast<int>(rt.bulkInEp)
         << " bulkOut=0x" << static_cast<int>(rt.bulkOutEp)
         << std::dec);

    return true;
}

void bot_reset_recovery(libusb_device_handle* handle, const UsbRuntimeInfo& rt) {
    LOGI("BOT reset recovery");

    int rc = libusb_control_transfer(
        handle,
        0x21,
        0xff,
        0,
        static_cast<uint16_t>(rt.interfaceNumber),
        nullptr,
        0,
        5000);

    LOGI("BOT reset rc=" << rc);

    libusb_clear_halt(handle, rt.bulkInEp);
    libusb_clear_halt(handle, rt.bulkOutEp);
}

int handle_control_submit(
    libusb_device_handle* handle,
    UsbRuntimeInfo& rt,
    const UrbSubmit& urb,
    std::vector<uint8_t>& response)
{
    uint8_t bmRequestType = urb.setup[0];
    uint8_t bRequest = urb.setup[1];

    uint16_t wValue = static_cast<uint16_t>(urb.setup[2] | (urb.setup[3] << 8));
    uint16_t wIndex = static_cast<uint16_t>(urb.setup[4] | (urb.setup[5] << 8));
    uint16_t wLength = static_cast<uint16_t>(urb.setup[6] | (urb.setup[7] << 8));

    if (bmRequestType == 0x00 && bRequest == 0x31) {
        LOGT("usbip/vhci reset-like request ignored locally"
             << " value=0x" << std::hex << wValue
             << " index=0x" << wIndex
             << std::dec);
        return 0;
    }

    if (bmRequestType == 0x00 && bRequest == 0x09) {
        LOGI("SET_CONFIGURATION value=" << wValue);

        int rc = libusb_set_configuration(handle, static_cast<int>(wValue));
        if (rc != 0 && rc != LIBUSB_ERROR_BUSY) {
            LOGE("libusb_set_configuration failed: " << libusb_error_name(rc));
            return rc;
        }

        if (!reclaim_mass_storage_interface(handle, rt)) {
            return LIBUSB_ERROR_BUSY;
        }

        return 0;
    }

    std::vector<uint8_t> buffer;
    if ((bmRequestType & 0x80) != 0) {
        buffer.resize(wLength);
    } else {
        buffer = urb.out_payload;
    }

    int rc = libusb_control_transfer(
        handle,
        bmRequestType,
        bRequest,
        wValue,
        wIndex,
        buffer.data(),
        static_cast<uint16_t>(buffer.size()),
        5000);

    LOGT("control:"
         << " bm=0x" << std::hex << static_cast<int>(bmRequestType)
         << " req=0x" << static_cast<int>(bRequest)
         << " value=0x" << wValue
         << " index=0x" << wIndex
         << " len=" << std::dec << wLength
         << " rc=" << rc);

    if (rc < 0) {
        return rc;
    }

    if ((bmRequestType & 0x80) != 0) {
        response.assign(buffer.begin(), buffer.begin() + rc);
    }

    return 0;
}

int handle_bulk_submit(
    libusb_device_handle* handle,
    const UsbRuntimeInfo& rt,
    const UrbSubmit& urb,
    std::vector<uint8_t>& response,
    int& transferred)
{
    transferred = 0;
    uint8_t endpoint = 0;

    if (urb.direction == usbip::DirIn) {
        endpoint = rt.bulkInEp;
        response.resize(urb.transfer_buffer_length);
    } else {
        endpoint = rt.bulkOutEp;
    }

    uint8_t* data = nullptr;
    int length = static_cast<int>(urb.transfer_buffer_length);

    if (urb.direction == usbip::DirIn) {
        data = response.data();
    } else {
        data = const_cast<uint8_t*>(urb.out_payload.data());
    }

    int rc = libusb_bulk_transfer(handle, endpoint, data, length, &transferred, 15000);

    if (rc < 0) {
        LOGE("bulk failed:"
             << " ep=0x" << std::hex << static_cast<int>(endpoint)
             << " rc=" << std::dec << rc
             << " " << libusb_error_name(rc));

        if (rc == LIBUSB_ERROR_PIPE || rc == LIBUSB_ERROR_TIMEOUT) {
            LOGI("bulk stalled/timeout, do BOT reset recovery");
            bot_reset_recovery(handle, rt);
            response.clear();
            return rc;
        }

        libusb_clear_halt(handle, rt.bulkInEp);
        libusb_clear_halt(handle, rt.bulkOutEp);
        response.clear();
        return rc;
    }

    if (urb.direction == usbip::DirOut && g_log_level >= LOG_TRACE) {
        dump_hex("bulk OUT payload", urb.out_payload);
    }

    LOGT("bulk:"
         << " ep=0x" << std::hex << static_cast<int>(endpoint)
         << " dir=" << std::dec << urb.direction
         << " len=" << length
         << " transferred=" << transferred
         << " rc=" << rc);

    if (urb.direction == usbip::DirIn) {
        response.resize(transferred);
    }

    return 0;
}
