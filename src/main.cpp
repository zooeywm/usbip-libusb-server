#include <arpa/inet.h>
#include <libusb-1.0/libusb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// #include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

static constexpr uint16_t USBIP_VERSION = 0x0111;
static constexpr uint16_t OP_REQ_DEVLIST = 0x8005;
static constexpr uint16_t OP_REP_DEVLIST = 0x0005;

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

static void put_fixed_string(std::vector<uint8_t> &out, const std::string &s,
                             size_t n)
{
    std::vector<uint8_t> buf(n, 0);
    std::memcpy(buf.data(), s.data(), std::min(s.size(), n - 1));
    out.insert(out.end(), buf.begin(), buf.end());
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

static std::optional<UsbDeviceInfo>
find_mass_storage_device(libusb_context *ctx)
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

static std::vector<uint8_t>
build_devlist_reply(const std::optional<UsbDeviceInfo> &dev)
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

    for (const auto &itf : d.interfaces) {
        put_u8(out, itf.cls);
        put_u8(out, itf.subcls);
        put_u8(out, itf.proto);
        put_u8(out, 0);
    }

    return out;
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

    if (version != USBIP_VERSION || code != OP_REQ_DEVLIST) {
        std::cerr << "unsupported request" << std::endl;
        return;
    }

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
