#include <arpa/inet.h>
#include <libusb-1.0/libusb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

static constexpr uint16_t USBIP_VERSION = 0x0111;
static constexpr uint16_t OP_REQ_DEVLIST = 0x8005;
static constexpr uint16_t OP_REP_DEVLIST = 0x0005;
static constexpr uint16_t OP_REQ_IMPORT = 0x8003;
static constexpr uint16_t OP_REP_IMPORT = 0x0003;

static constexpr uint32_t USBIP_CMD_SUBMIT = 0x00000001;
static constexpr uint32_t USBIP_CMD_UNLINK = 0x00000002;
static constexpr uint32_t USBIP_RET_SUBMIT = 0x00000003;
static constexpr uint32_t USBIP_RET_UNLINK = 0x00000004;

static constexpr uint32_t USBIP_DIR_OUT = 0;
static constexpr uint32_t USBIP_DIR_IN = 1;

static uint32_t get_be32(const uint8_t *p)
{
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return ntohl(v);
}

static int32_t get_be32s(const uint8_t *p)
{
    return static_cast<int32_t>(get_be32(p));
}

static bool read_exact(int fd, void *buf, size_t len)
{
    auto *p = static_cast<uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = ::recv(fd, p, len, 0);
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

static bool write_exact(int fd, const void *buf, size_t len)
{
    const auto *p = static_cast<const uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = ::send(fd, p, len, 0);
        if (n <= 0)
            return false;
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

static void put_u16(std::vector<uint8_t> &out, uint16_t v)
{
    uint16_t n = htons(v);
    auto *p = reinterpret_cast<uint8_t *>(&n);
    out.insert(out.end(), p, p + 2);
}

static void put_u32(std::vector<uint8_t> &out, uint32_t v)
{
    uint32_t n = htonl(v);
    auto *p = reinterpret_cast<uint8_t *>(&n);
    out.insert(out.end(), p, p + 4);
}

static void put_u8(std::vector<uint8_t> &out, uint8_t v)
{
    out.push_back(v);
}

static void put_fixed_string(std::vector<uint8_t> &out, const std::string &s, size_t n)
{
    std::vector<uint8_t> buf(n, 0);
    std::memcpy(buf.data(), s.data(), std::min(s.size(), n - 1));
    out.insert(out.end(), buf.begin(), buf.end());
}

static void dump_hex(const char *tag, const std::vector<uint8_t> &data, size_t max = 64)
{
    std::cout << tag << " [" << data.size() << "]:";
    for (size_t i = 0; i < data.size() && i < max; ++i) {
        std::cout << " " << std::hex << std::uppercase
                  << static_cast<int>(data[i]);
    }
    std::cout << std::dec << std::nouppercase << std::endl;
}

struct UsbInterfaceInfo
{
    uint8_t cls = 0;
    uint8_t subcls = 0;
    uint8_t proto = 0;
};

struct UsbDeviceInfo
{
    std::string path;
    std::string busid;

    uint32_t busnum = 0;
    uint32_t devnum = 0;
    uint32_t speed = 2;

    uint16_t vid = 0;
    uint16_t pid = 0;
    uint16_t bcdDevice = 0;

    uint8_t devClass = 0;
    uint8_t devSubClass = 0;
    uint8_t devProtocol = 0;
    uint8_t configValue = 1;
    uint8_t numConfigurations = 1;

    std::vector<UsbInterfaceInfo> interfaces;
};

struct UrbSubmit
{
    uint32_t seqnum = 0;
    uint32_t devid = 0;
    uint32_t direction = 0;
    uint32_t ep = 0;

    uint32_t transfer_flags = 0;
    uint32_t transfer_buffer_length = 0;
    uint32_t start_frame = 0;
    uint32_t number_of_packets = 0;
    uint32_t interval = 0;

    std::array<uint8_t, 8> setup{};
    std::vector<uint8_t> out_payload;
};

struct UsbRuntimeInfo
{
    int interfaceNumber = -1;
    int altSetting = 0;
    uint8_t interfaceClass = 0;
    uint8_t interfaceSubClass = 0;
    uint8_t interfaceProtocol = 0;
    uint8_t bulkInEp = 0;
    uint8_t bulkOutEp = 0;
};

static std::optional<UsbRuntimeInfo> find_mass_storage_runtime(
    libusb_device_handle *handle)
{
    libusb_device *dev = libusb_get_device(handle);

    libusb_config_descriptor *cfg = nullptr;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0) {
        if (libusb_get_config_descriptor(dev, 0, &cfg) != 0) {
            return std::nullopt;
        }
    }

    std::optional<UsbRuntimeInfo> fallback;
    std::optional<UsbRuntimeInfo> bot;

    for (uint8_t i = 0; i < cfg->bNumInterfaces; ++i) {
        const libusb_interface &intf = cfg->interface[i];

        for (int j = 0; j < intf.num_altsetting; ++j) {
            const libusb_interface_descriptor &alt = intf.altsetting[j];

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
                const libusb_endpoint_descriptor &ep = alt.endpoint[k];

                if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) != LIBUSB_TRANSFER_TYPE_BULK) {
                    continue;
                }

                if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN) {
                    rt.bulkInEp = ep.bEndpointAddress;
                } else {
                    rt.bulkOutEp = ep.bEndpointAddress;
                }
            }

            std::cout << "mass-storage candidate:"
                      << " if=" << static_cast<int>(rt.interfaceNumber)
                      << " alt=" << static_cast<int>(rt.altSetting)
                      << " proto=0x" << std::hex << static_cast<int>(rt.interfaceProtocol)
                      << " in=0x" << static_cast<int>(rt.bulkInEp)
                      << " out=0x" << static_cast<int>(rt.bulkOutEp)
                      << std::dec << std::endl;

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

    if (bot) {
        return bot;
    }

    return fallback;
}

static bool claim_interface(libusb_device_handle *handle, int interfaceNumber)
{
#ifdef __linux__
    if (libusb_kernel_driver_active(handle, interfaceNumber) == 1) {
        int rc = libusb_detach_kernel_driver(handle, interfaceNumber);
        if (rc != 0) {
            std::cerr << "libusb_detach_kernel_driver failed: "
                      << libusb_error_name(rc) << std::endl;
            return false;
        }

        std::cout << "kernel driver detached from interface "
                  << interfaceNumber << std::endl;
    }
#endif

    int rc = libusb_claim_interface(handle, interfaceNumber);
    if (rc != 0) {
        std::cerr << "libusb_claim_interface failed: "
                  << libusb_error_name(rc) << std::endl;
        return false;
    }

    std::cout << "claimed interface " << interfaceNumber << std::endl;
    return true;
}

static bool select_alt_setting(
    libusb_device_handle *handle,
    const UsbRuntimeInfo &rt)
{
    int rc = libusb_set_interface_alt_setting(
        handle,
        rt.interfaceNumber,
        rt.altSetting);

    if (rc != 0) {
        std::cerr << "libusb_set_interface_alt_setting failed: "
                  << libusb_error_name(rc)
                  << " if=" << rt.interfaceNumber
                  << " alt=" << rt.altSetting
                  << std::endl;
        return false;
    }

    std::cout << "selected alt setting:"
              << " if=" << rt.interfaceNumber
              << " alt=" << rt.altSetting
              << " proto=0x" << std::hex << static_cast<int>(rt.interfaceProtocol)
              << std::dec << std::endl;

    return true;
}

static bool reclaim_mass_storage_interface(
    libusb_device_handle *handle,
    UsbRuntimeInfo &rt)
{
    if (rt.interfaceNumber >= 0) {
        libusb_release_interface(handle, rt.interfaceNumber);
    }

    auto newRt = find_mass_storage_runtime(handle);
    if (!newRt) {
        std::cerr << "re-find mass storage runtime failed" << std::endl;
        return false;
    }

    rt = *newRt;

#ifdef __linux__
    if (libusb_kernel_driver_active(handle, rt.interfaceNumber) == 1) {
        int drc = libusb_detach_kernel_driver(handle, rt.interfaceNumber);
        if (drc != 0) {
            std::cerr << "detach after set-config failed: "
                      << libusb_error_name(drc) << std::endl;
        }
    }
#endif

    int rc = libusb_claim_interface(handle, rt.interfaceNumber);
    if (rc != 0) {
        std::cerr << "re-claim interface failed: "
                  << libusb_error_name(rc) << std::endl;
        return false;
    }

    if (!select_alt_setting(handle, rt)) {
        return false;
    }

    libusb_clear_halt(handle, rt.bulkInEp);
    libusb_clear_halt(handle, rt.bulkOutEp);

    std::cout << "reclaimed interface=" << rt.interfaceNumber
              << " bulkIn=0x" << std::hex << static_cast<int>(rt.bulkInEp)
              << " bulkOut=0x" << static_cast<int>(rt.bulkOutEp)
              << std::dec << std::endl;

    return true;
}

static void bot_reset_recovery(
    libusb_device_handle *handle,
    const UsbRuntimeInfo &rt)
{
    std::cout << "BOT reset recovery" << std::endl;

    int rc = libusb_control_transfer(
        handle,
        0x21, // class, interface, host-to-device
        0xff, // Bulk-Only Mass Storage Reset
        0,
        static_cast<uint16_t>(rt.interfaceNumber),
        nullptr,
        0,
        5000);

    std::cout << "BOT reset rc=" << rc << std::endl;

    libusb_clear_halt(handle, rt.bulkInEp);
    libusb_clear_halt(handle, rt.bulkOutEp);
}

static int handle_bulk_submit(
    libusb_device_handle *handle,
    const UsbRuntimeInfo &rt,
    const UrbSubmit &urb,
    std::vector<uint8_t> &response,
    int &transferred)
{
    transferred = 0;
    uint8_t endpoint = 0;

    if (urb.direction == USBIP_DIR_IN) {
        endpoint = rt.bulkInEp;
        response.resize(urb.transfer_buffer_length);
    } else {
        endpoint = rt.bulkOutEp;
    }

    uint8_t *data = nullptr;
    int length = static_cast<int>(urb.transfer_buffer_length);

    if (urb.direction == USBIP_DIR_IN) {
        data = response.data();
    } else {
        data = const_cast<uint8_t *>(urb.out_payload.data());
    }

    int rc = libusb_bulk_transfer(
        handle,
        endpoint,
        data,
        length,
        &transferred,
        15000);

    if (rc < 0) {
        std::cerr << "bulk failed:"
                  << " ep=0x" << std::hex << static_cast<int>(endpoint)
                  << " rc=" << std::dec << rc
                  << " " << libusb_error_name(rc)
                  << std::endl;

        if (rc == LIBUSB_ERROR_PIPE || rc == LIBUSB_ERROR_TIMEOUT) {
            std::cerr << "bulk stalled/timeout, do BOT reset recovery"
                      << std::endl;

            bot_reset_recovery(handle, rt);
            response.clear();
            return rc;
        }

        libusb_clear_halt(handle, rt.bulkInEp);
        libusb_clear_halt(handle, rt.bulkOutEp);

        response.clear();
        return rc;
    }

    if (urb.direction == USBIP_DIR_OUT) {
        dump_hex("bulk OUT payload", urb.out_payload);
    }

    std::cout << "bulk:"
              << " ep=0x" << std::hex << static_cast<int>(endpoint)
              << " dir=" << std::dec << urb.direction
              << " len=" << length
              << " transferred=" << transferred
              << " rc=" << rc
              << std::endl;

    if (rc < 0) {
        response.clear();
        return rc;
    }

    if (urb.direction == USBIP_DIR_IN) {
        response.resize(transferred);
    }

    return 0;
}

static uint32_t convert_speed(int speed)
{
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

static bool send_ret_submit(
    int client_fd,
    uint32_t seqnum,
    int32_t status,
    uint32_t actual_length,
    const std::vector<uint8_t> &in_payload)
{
    std::vector<uint8_t> out;

    put_u32(out, USBIP_RET_SUBMIT);
    put_u32(out, seqnum);
    put_u32(out, 0);
    put_u32(out, 0);
    put_u32(out, 0);

    put_u32(out, static_cast<uint32_t>(status));
    put_u32(out, actual_length);
    put_u32(out, 0);
    put_u32(out, 0xffffffff);
    put_u32(out, 0);

    for (int i = 0; i < 8; ++i) {
        put_u8(out, 0);
    }

    out.insert(out.end(), in_payload.begin(), in_payload.end());
    return write_exact(client_fd, out.data(), out.size());
}

static bool parse_submit(
    int client_fd,
    const uint8_t header[48],
    UrbSubmit &urb)
{
    urb.seqnum = get_be32(header + 4);
    urb.devid = get_be32(header + 8);
    urb.direction = get_be32(header + 12);
    urb.ep = get_be32(header + 16);

    urb.transfer_flags = get_be32(header + 20);
    urb.transfer_buffer_length = get_be32(header + 24);
    urb.start_frame = get_be32(header + 28);
    urb.number_of_packets = get_be32(header + 32);
    urb.interval = get_be32(header + 36);

    std::memcpy(urb.setup.data(), header + 40, 8);

    if (urb.direction == USBIP_DIR_OUT && urb.transfer_buffer_length > 0) {
        urb.out_payload.resize(urb.transfer_buffer_length);
        if (!read_exact(client_fd, urb.out_payload.data(), urb.out_payload.size())) {
            return false;
        }
    }

    return true;
}

static int handle_control_submit(
    libusb_device_handle *handle,
    UsbRuntimeInfo &rt,
    const UrbSubmit &urb,
    std::vector<uint8_t> &response)
{
    uint8_t bmRequestType = urb.setup[0];
    uint8_t bRequest = urb.setup[1];

    uint16_t wValue = static_cast<uint16_t>(urb.setup[2] | (urb.setup[3] << 8));
    uint16_t wIndex = static_cast<uint16_t>(urb.setup[4] | (urb.setup[5] << 8));
    uint16_t wLength = static_cast<uint16_t>(urb.setup[6] | (urb.setup[7] << 8));

    if (bmRequestType == 0x00 && bRequest == 0x31) {
        std::cout << "usbip/vhci reset-like request ignored locally"
                  << " value=0x" << std::hex << wValue
                  << " index=0x" << wIndex
                  << std::dec << std::endl;

        return 0;
    }

    std::vector<uint8_t> buffer;

    if ((bmRequestType & 0x80) != 0) {
        buffer.resize(wLength);
    } else {
        buffer = urb.out_payload;
    }

    if (bmRequestType == 0x00 && bRequest == 0x09) {
        std::cout << "SET_CONFIGURATION value=" << wValue << std::endl;

        int rc = libusb_set_configuration(handle, static_cast<int>(wValue));
        if (rc != 0 && rc != LIBUSB_ERROR_BUSY) {
            std::cerr << "libusb_set_configuration failed: "
                      << libusb_error_name(rc) << std::endl;
            return rc;
        }

        if (!reclaim_mass_storage_interface(handle, rt)) {
            return LIBUSB_ERROR_BUSY;
        }

        return 0;
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

    std::cout << "control:"
              << " bm=0x" << std::hex << static_cast<int>(bmRequestType)
              << " req=0x" << static_cast<int>(bRequest)
              << " value=0x" << wValue
              << " index=0x" << wIndex
              << " len=" << std::dec << wLength
              << " rc=" << rc
              << std::endl;

    if (rc < 0) {
        return rc;
    }

    if ((bmRequestType & 0x80) != 0) {
        response.assign(buffer.begin(), buffer.begin() + rc);
    }

    return 0;
}

static libusb_device_handle *open_device_by_busid(
    libusb_context *ctx,
    const std::string &busid)
{
    libusb_device **list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0)
        return nullptr;

    libusb_device_handle *handle = nullptr;

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device *dev = list[i];

        uint8_t busnum = libusb_get_bus_number(dev);
        uint8_t devnum = libusb_get_device_address(dev);

        std::string cur = std::to_string(busnum) + "-" + std::to_string(devnum);
        if (cur != busid)
            continue;

        if (libusb_open(dev, &handle) != 0) {
            handle = nullptr;
        }
        break;
    }

    libusb_free_device_list(list, 1);
    return handle;
}

static std::optional<UsbDeviceInfo> find_mass_storage_device(libusb_context *ctx)
{
    libusb_device **list = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0)
        return std::nullopt;

    std::optional<UsbDeviceInfo> result;

    for (ssize_t i = 0; i < count && !result; ++i) {
        libusb_device *dev = list[i];

        libusb_device_descriptor dd{};
        if (libusb_get_device_descriptor(dev, &dd) != 0)
            continue;

        libusb_config_descriptor *cfg = nullptr;
        if (libusb_get_active_config_descriptor(dev, &cfg) != 0) {
            if (libusb_get_config_descriptor(dev, 0, &cfg) != 0) {
                continue;
            }
        }

        bool isMassStorage = false;
        std::vector<UsbInterfaceInfo> ifaces;

        for (uint8_t j = 0; j < cfg->bNumInterfaces; ++j) {
            const libusb_interface &intf = cfg->interface[j];
            if (intf.num_altsetting <= 0)
                continue;

            const libusb_interface_descriptor &alt = intf.altsetting[0];

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

static void append_usb_device(std::vector<uint8_t> &out, const UsbDeviceInfo &d)
{
    put_fixed_string(out, d.path, 256);
    put_fixed_string(out, d.busid, 32);

    put_u32(out, d.busnum);
    put_u32(out, d.devnum);
    put_u32(out, d.speed);

    put_u16(out, d.vid);
    put_u16(out, d.pid);
    put_u16(out, d.bcdDevice);

    put_u8(out, d.devClass);
    put_u8(out, d.devSubClass);
    put_u8(out, d.devProtocol);
    put_u8(out, d.configValue);
    put_u8(out, d.numConfigurations);
    put_u8(out, static_cast<uint8_t>(d.interfaces.size()));
}

static std::vector<uint8_t> build_devlist_reply(const std::optional<UsbDeviceInfo> &dev)
{
    std::vector<uint8_t> out;

    put_u16(out, USBIP_VERSION);
    put_u16(out, OP_REP_DEVLIST);
    put_u32(out, 0);

    put_u32(out, dev ? 1 : 0);

    if (!dev) {
        return out;
    }

    const UsbDeviceInfo &d = *dev;

    append_usb_device(out, d);

    for (const auto &itf : d.interfaces) {
        put_u8(out, itf.cls);
        put_u8(out, itf.subcls);
        put_u8(out, itf.proto);
        put_u8(out, 0);
    }

    return out;
}

static std::vector<uint8_t> build_import_reply_ok(const UsbDeviceInfo &d)
{
    std::vector<uint8_t> out;

    put_u16(out, USBIP_VERSION);
    put_u16(out, OP_REP_IMPORT);
    put_u32(out, 0);

    append_usb_device(out, d);

    return out;
}

static std::vector<uint8_t> build_import_reply_error(uint32_t status)
{
    std::vector<uint8_t> out;

    put_u16(out, USBIP_VERSION);
    put_u16(out, OP_REP_IMPORT);
    put_u32(out, status);

    return out;
}

static std::string trim_c_string(const char *data, size_t len)
{
    size_t n = 0;
    while (n < len && data[n] != '\0') {
        ++n;
    }
    return std::string(data, n);
}

static void urb_loop(
    int client_fd,
    libusb_device_handle *handle,
    UsbRuntimeInfo &rt)
{
    std::cout << "enter URB loop" << std::endl;

    while (true) {
        uint8_t header[48];

        if (!read_exact(client_fd, header, sizeof(header))) {
            std::cout << "URB connection closed" << std::endl;
            return;
        }

        uint32_t command = get_be32(header + 0);

        if (command == USBIP_CMD_SUBMIT) {
            UrbSubmit urb;
            if (!parse_submit(client_fd, header, urb)) {
                std::cerr << "failed to parse submit" << std::endl;
                return;
            }

            std::cout << "SUBMIT:"
                      << " seq=" << urb.seqnum
                      << " dir=" << urb.direction
                      << " ep=" << urb.ep
                      << " len=" << urb.transfer_buffer_length
                      << std::endl;

            std::vector<uint8_t> response;

            if (urb.ep == 0) {
                int rc = handle_control_submit(handle, rt, urb, response);

                if (rc < 0) {
                    send_ret_submit(client_fd, urb.seqnum, rc, 0, {});
                } else {
                    send_ret_submit(client_fd, urb.seqnum, 0,
                                    static_cast<uint32_t>(response.size()),
                                    response);
                }
            } else {
                int transferred = 0;
                int rc = handle_bulk_submit(handle, rt, urb, response, transferred);

                if (rc < 0) {
                    int usbip_status = rc;

                    if (rc == LIBUSB_ERROR_IO) {
                        usbip_status = -32;
                    } else if (rc == LIBUSB_ERROR_TIMEOUT) {
                        usbip_status = -110;
                    } else if (rc == LIBUSB_ERROR_NO_DEVICE) {
                        usbip_status = -19;
                    }

                    send_ret_submit(client_fd, urb.seqnum, usbip_status, 0, {});
                } else {
                    uint32_t actual = 0;

                    if (urb.direction == USBIP_DIR_IN) {
                        actual = static_cast<uint32_t>(response.size());
                    } else {
                        actual = static_cast<uint32_t>(transferred);
                    }

                    send_ret_submit(client_fd, urb.seqnum, 0, actual, response);
                }
            }

            continue;
        }

        if (command == USBIP_CMD_UNLINK) {
            std::cerr << "UNLINK not implemented yet" << std::endl;
            return;
        }

        std::cerr << "unknown URB command=0x"
                  << std::hex << command
                  << std::dec << std::endl;
        return;
    }
}

static void handle_client(int client_fd, libusb_context *usb_ctx)
{
    struct OpHeader
    {
        uint16_t version;
        uint16_t code;
        uint32_t status;
    } __attribute__((packed));

    OpHeader h{};
    if (!read_exact(client_fd, &h, sizeof(h))) {
        return;
    }

    uint16_t version = ntohs(h.version);
    uint16_t code = ntohs(h.code);

    std::cout << "USB/IP request: version=0x" << std::hex << version << " code=0x"
              << code << std::dec << std::endl;

    if (version != USBIP_VERSION) {
        std::cerr << "unsupported version" << std::endl;
        return;
    }

    if (code == OP_REQ_DEVLIST) {
        auto dev = find_mass_storage_device(usb_ctx);

        if (dev) {
            std::cout << "export device: busid=" << dev->busid
                      << " vid=0x" << std::hex << dev->vid
                      << " pid=0x" << dev->pid
                      << std::dec << std::endl;
        } else {
            std::cout << "no mass storage device found" << std::endl;
        }

        auto reply = build_devlist_reply(dev);
        write_exact(client_fd, reply.data(), reply.size());
        return;
    }

    if (code == OP_REQ_IMPORT) {
        char busid_raw[32]{};
        if (!read_exact(client_fd, busid_raw, sizeof(busid_raw))) {
            std::cerr << "failed to read import busid" << std::endl;
            return;
        }

        std::string req_busid = trim_c_string(busid_raw, sizeof(busid_raw));
        std::cout << "import request busid=" << req_busid << std::endl;

        auto dev = find_mass_storage_device(usb_ctx);

        if (!dev || dev->busid != req_busid) {
            std::cerr << "requested device not found" << std::endl;
            auto reply = build_import_reply_error(1);
            write_exact(client_fd, reply.data(), reply.size());
            return;
        }

        auto reply = build_import_reply_ok(*dev);
        if (!write_exact(client_fd, reply.data(), reply.size())) {
            std::cerr << "failed to send import reply" << std::endl;
            return;
        }

        std::cout << "import ok, keep connection for URB traffic" << std::endl;
        libusb_device_handle *handle = open_device_by_busid(usb_ctx, dev->busid);
        if (!handle) {
            std::cerr << "failed to open libusb device" << std::endl;
            return;
        }

        std::cout << "libusb open ok" << std::endl;

        auto rt = find_mass_storage_runtime(handle);
        if (!rt) {
            std::cerr << "mass storage bulk endpoints not found" << std::endl;
            libusb_close(handle);
            return;
        }

        std::cout << "mass storage interface=" << rt->interfaceNumber
                  << " bulkIn=0x" << std::hex << static_cast<int>(rt->bulkInEp)
                  << " bulkOut=0x" << static_cast<int>(rt->bulkOutEp)
                  << std::dec << std::endl;

        if (!claim_interface(handle, rt->interfaceNumber)) {
            libusb_close(handle);
            return;
        }

        if (!select_alt_setting(handle, *rt)) {
            libusb_release_interface(handle, rt->interfaceNumber);
            libusb_close(handle);
            return;
        }

        libusb_clear_halt(handle, rt->bulkInEp);
        libusb_clear_halt(handle, rt->bulkOutEp);

        urb_loop(client_fd, handle, *rt);

        libusb_release_interface(handle, rt->interfaceNumber);

#ifdef __linux__
        libusb_attach_kernel_driver(handle, rt->interfaceNumber);
#endif

        libusb_close(handle);
        return;
    }

    std::cerr << "unsupported request code=0x"
              << std::hex << code
              << std::dec << std::endl;

    auto dev = find_mass_storage_device(usb_ctx);
    if (dev) {
        std::cout << "export device: busid=" << dev->busid << " vid=0x" << std::hex
                  << dev->vid << " pid=0x" << dev->pid << std::dec << std::endl;
    } else {
        std::cout << "no mass storage device found" << std::endl;
    }

    auto reply = build_devlist_reply(dev);
    write_exact(client_fd, reply.data(), reply.size());
}

int main(int argc, char **argv)
{
    int port = 3240;
    if (argc >= 2) {
        port = std::stoi(argv[1]);
    }

    libusb_context *usb_ctx = nullptr;
    if (libusb_init(&usb_ctx) != 0) {
        std::cerr << "libusb_init failed" << std::endl;
        return 1;
    }

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (::listen(server_fd, 16) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "usbip-libusb-server listening on port " << port << std::endl;

    while (true) {
        int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        handle_client(client_fd, usb_ctx);
        ::close(client_fd);
    }

    libusb_exit(usb_ctx);
    return 0;
}
