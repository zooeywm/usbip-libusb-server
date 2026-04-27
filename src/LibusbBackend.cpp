#include "DebugStats.h"
#include "LibusbBackend.h"
#include "Log.h"
#include "NetUtil.h"

#include <filesystem>
#include <fstream>
#include <iostream>

uint32_t convert_speed(int speed) {
    switch (speed) {
    case LIBUSB_SPEED_LOW:
        return 1;
    case LIBUSB_SPEED_FULL:
        return 2;
    case LIBUSB_SPEED_HIGH:
        return 3;
    case LIBUSB_SPEED_SUPER:
        return 5;
    case LIBUSB_SPEED_SUPER_PLUS:
        return 6;
    default:
        return 0;
    }
}

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

std::string find_linux_sysfs_busid(uint8_t busnum, uint8_t devnum) {
#ifdef __linux__
    const std::filesystem::path base = "/sys/bus/usb/devices";

    std::error_code ec;
    if (!std::filesystem::exists(base, ec)) {
        return fallback_busid(busnum, devnum);
    }

    for (const auto& entry : std::filesystem::directory_iterator(base, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }

        const std::string name = entry.path().filename().string();

        // Skip root hubs and interface nodes. We only want device nodes such as 6-1.3.
        if (name.rfind("usb", 0) == 0 || name.find(':') != std::string::npos) {
            continue;
        }

        const auto busnumPath = entry.path() / "busnum";
        const auto devnumPath = entry.path() / "devnum";

        if (!std::filesystem::exists(busnumPath, ec) || !std::filesystem::exists(devnumPath, ec)) {
            continue;
        }

        try {
            int sysBus = std::stoi(read_sysfs_value(busnumPath));
            int sysDev = std::stoi(read_sysfs_value(devnumPath));

            if (sysBus == static_cast<int>(busnum) && sysDev == static_cast<int>(devnum)) {
                return name;
            }
        } catch (const std::exception&) {
            continue;
        }
    }
#endif

    return fallback_busid(busnum, devnum);
}

bool append_mass_storage_device_info(libusb_device* dev, UsbDeviceInfo& info) {
    libusb_device_descriptor dd{};
    if (libusb_get_device_descriptor(dev, &dd) != 0) {
        return false;
    }

    libusb_config_descriptor* cfg = nullptr;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0) {
        if (libusb_get_config_descriptor(dev, 0, &cfg) != 0) {
            return false;
        }
    }

    bool isMassStorage = false;
    std::vector<UsbInterfaceInfo> ifaces;

    for (uint8_t j = 0; j < cfg->bNumInterfaces; ++j) {
        const libusb_interface& intf = cfg->interface[j];
        if (intf.num_altsetting <= 0) {
            continue;
        }

        // Prefer reporting the first alternate setting here; runtime selection still
        // chooses BOT protocol 0x50 later when the device is imported.
        const libusb_interface_descriptor& alt = intf.altsetting[0];

        UsbInterfaceInfo ii;
        ii.cls = alt.bInterfaceClass;
        ii.subcls = alt.bInterfaceSubClass;
        ii.proto = alt.bInterfaceProtocol;
        ifaces.push_back(ii);

        for (int k = 0; k < intf.num_altsetting; ++k) {
            if (intf.altsetting[k].bInterfaceClass == 0x08) {
                isMassStorage = true;
            }
        }
    }

    if (!isMassStorage) {
        libusb_free_config_descriptor(cfg);
        return false;
    }

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

    info.busid = find_linux_sysfs_busid(static_cast<uint8_t>(info.busnum),
                                        static_cast<uint8_t>(info.devnum));
#ifdef __linux__
    info.path = "/sys/bus/usb/devices/" + info.busid;
#else
    info.path = "/usbip-libusb/" + info.busid;
#endif

    libusb_free_config_descriptor(cfg);
    return true;
}

} // namespace

std::vector<UsbDeviceInfo> find_mass_storage_devices(libusb_context* ctx) {
    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        return {};
    }

    std::vector<UsbDeviceInfo> devices;

    for (ssize_t i = 0; i < count; ++i) {
        UsbDeviceInfo info;
        if (append_mass_storage_device_info(list[i], info)) {
            devices.push_back(std::move(info));
        }
    }

    libusb_free_device_list(list, 1);
    return devices;
}

std::optional<UsbDeviceInfo> find_mass_storage_device_by_busid(libusb_context* ctx,
                                                               const std::string& busid) {
    auto devices = find_mass_storage_devices(ctx);
    for (auto& dev : devices) {
        if (dev.busid == busid) {
            return dev;
        }
    }

    return std::nullopt;
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
        std::string cur = find_linux_sysfs_busid(busnum, devnum);

        if (cur != busid) {
            continue;
        }

        int rc = libusb_open(dev, &handle);
        if (rc != 0) {
            LOGE("libusb_open failed: busid=" << busid << " rc=" << rc << " "
                                              << libusb_error_name(rc));
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

            LOGI("mass-storage candidate:" << " if=" << static_cast<int>(rt.interfaceNumber)
                                           << " alt=" << static_cast<int>(rt.altSetting)
                                           << " proto=0x" << std::hex
                                           << static_cast<int>(rt.interfaceProtocol) << " in=0x"
                                           << static_cast<int>(rt.bulkInEp) << " out=0x"
                                           << static_cast<int>(rt.bulkOutEp) << std::dec);

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
             << libusb_error_name(rc) << " if=" << rt.interfaceNumber << " alt=" << rt.altSetting);
        return false;
    }

    LOGI("selected alt setting:" << " if=" << rt.interfaceNumber << " alt=" << rt.altSetting
                                 << " proto=0x" << std::hex
                                 << static_cast<int>(rt.interfaceProtocol) << std::dec);
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

    LOGI("reclaimed interface=" << rt.interfaceNumber << " bulkIn=0x" << std::hex
                                << static_cast<int>(rt.bulkInEp) << " bulkOut=0x"
                                << static_cast<int>(rt.bulkOutEp) << std::dec);

    return true;
}

void bot_reset_recovery(libusb_device_handle* handle, const UsbRuntimeInfo& rt,
                        const char* reason) {
    auto n = ++g_stats.botRecovery;

    LOGW("BOT reset recovery count=" << n << " reason=" << reason);

    int rc = libusb_control_transfer(handle, 0x21, 0xff, 0,
                                     static_cast<uint16_t>(rt.interfaceNumber), nullptr, 0, 5000);

    LOGW("BOT reset rc=" << rc);

    libusb_clear_halt(handle, rt.bulkInEp);
    libusb_clear_halt(handle, rt.bulkOutEp);

    if ((n % 20) == 0) {
        dump_stats("bot-recovery");
    }
}

int handle_control_submit(libusb_device_handle* handle, UsbRuntimeInfo& rt, const UrbSubmit& urb,
                          std::vector<uint8_t>& response) {
    uint8_t bmRequestType = urb.setup[0];
    uint8_t bRequest = urb.setup[1];

    uint16_t wValue = static_cast<uint16_t>(urb.setup[2] | (urb.setup[3] << 8));
    uint16_t wIndex = static_cast<uint16_t>(urb.setup[4] | (urb.setup[5] << 8));
    uint16_t wLength = static_cast<uint16_t>(urb.setup[6] | (urb.setup[7] << 8));

    if (bmRequestType == 0x00 && bRequest == 0x31) {
        auto n = ++g_stats.req031;

        if ((n % 50) == 0) {
            LOGI("0x31 reset-like request count=" << n);
            dump_stats("0x31");
        }

        return 0;
    }

    if (bmRequestType == 0x00 && bRequest == 0x09) {
        LOGI("SET_CONFIGURATION value=" << wValue);

        int rc = libusb_set_configuration(handle, static_cast<int>(wValue));
        if (rc != 0 && rc != LIBUSB_ERROR_BUSY) {
            ++g_stats.controlError;
            LOGE("libusb_set_configuration failed: " << libusb_error_name(rc));
            return rc;
        }

        if (!reclaim_mass_storage_interface(handle, rt)) {
            ++g_stats.controlError;
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

    int rc = libusb_control_transfer(handle, bmRequestType, bRequest, wValue, wIndex, buffer.data(),
                                     static_cast<uint16_t>(buffer.size()), 5000);

    LOGT("control:" << " bm=0x" << std::hex << static_cast<int>(bmRequestType) << " req=0x"
                    << static_cast<int>(bRequest) << " value=0x" << wValue << " index=0x" << wIndex
                    << " len=" << std::dec << wLength << " rc=" << rc);

    if (rc < 0) {
        ++g_stats.controlError;
        if (rc == LIBUSB_ERROR_NO_DEVICE) {
            LOGI("control device gone:" << " bm=0x" << std::hex << static_cast<int>(bmRequestType)
                                        << " req=0x" << static_cast<int>(bRequest) << " value=0x"
                                        << wValue << " index=0x" << wIndex << std::dec
                                        << " rc=" << rc << " " << libusb_error_name(rc));
        } else {
            LOGE("control failed:" << " bm=0x" << std::hex << static_cast<int>(bmRequestType)
                                   << " req=0x" << static_cast<int>(bRequest) << " value=0x"
                                   << wValue << " index=0x" << wIndex << std::dec << " rc=" << rc
                                   << " " << libusb_error_name(rc));
        }
        return rc;
    }

    if ((bmRequestType & 0x80) != 0) {
        response.assign(buffer.begin(), buffer.begin() + rc);
    }

    return 0;
}

int handle_bulk_submit(libusb_device_handle* handle, const UsbRuntimeInfo& rt, const UrbSubmit& urb,
                       std::vector<uint8_t>& response, int& transferred) {
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
        /*
         * 可恢复 PIPE：不要先 LOGE。
         * 先 clear halt + retry，retry 成功就静默返回。
         */
        if (rc == LIBUSB_ERROR_PIPE) {
            LOGT("bulk endpoint stalled, clear halt and retry once:"
                 << " ep=0x" << std::hex << static_cast<int>(endpoint) << " dir=" << std::dec
                 << urb.direction << " len=" << length);

            libusb_clear_halt(handle, endpoint);

            transferred = 0;
            rc = libusb_bulk_transfer(handle, endpoint, data, length, &transferred, 15000);

            if (rc == 0) {
                if (transferred != length) {
                    LOGT("bulk retry short packet:" << " requested=" << length
                                                    << " transferred=" << transferred);
                }

                if (urb.direction == usbip::DirIn) {
                    response.resize(transferred);
                }

                return 0;
            }

            ++g_stats.bulkError;

            LOGE("bulk retry failed:" << " ep=0x" << std::hex << static_cast<int>(endpoint)
                                      << " rc=" << std::dec << rc << " " << libusb_error_name(rc)
                                      << " dir=" << urb.direction << " len=" << length
                                      << " transferred=" << transferred);

            response.clear();
            return rc;
        }

        /*
         * NO_DEVICE：热拔，不算程序错误。
         */
        if (rc == LIBUSB_ERROR_NO_DEVICE) {
            ++g_stats.bulkError;

            LOGI("bulk device gone:" << " ep=0x" << std::hex << static_cast<int>(endpoint)
                                     << " dir=" << std::dec << urb.direction << " len=" << length);

            response.clear();
            return rc;
        }

        /*
         * TIMEOUT：才做 BOT reset recovery。
         */
        if (rc == LIBUSB_ERROR_TIMEOUT) {
            ++g_stats.bulkError;

            LOGW("bulk timeout:" << " ep=0x" << std::hex << static_cast<int>(endpoint)
                                 << " dir=" << std::dec << urb.direction << " len=" << length);

            bot_reset_recovery(handle, rt, libusb_error_name(rc));
            response.clear();
            return rc;
        }

        /*
         * 其他才是真 ERROR。
         */
        ++g_stats.bulkError;

        LOGE("bulk failed:" << " ep=0x" << std::hex << static_cast<int>(endpoint)
                            << " rc=" << std::dec << rc << " " << libusb_error_name(rc)
                            << " dir=" << urb.direction << " len=" << length);

        response.clear();
        return rc;
    }

    if (urb.direction == usbip::DirOut && ::logx::g_log_level >= ::logx::Trace) {
        dump_hex("bulk OUT payload", urb.out_payload);
    }

    LOGT("bulk:" << " ep=0x" << std::hex << static_cast<int>(endpoint) << " dir=" << std::dec
                 << urb.direction << " len=" << length << " transferred=" << transferred
                 << " rc=" << rc);

    if (urb.direction == usbip::DirIn) {
        response.resize(transferred);
    }

    return 0;
}
