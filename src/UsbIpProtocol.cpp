#include "UsbIpProtocol.h"

#include "NetUtil.h"

#include <cstring>

void append_usb_device(std::vector<uint8_t>& out, const UsbDeviceInfo& d) {
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

std::vector<uint8_t> build_devlist_reply(const std::optional<UsbDeviceInfo>& dev) {
    std::vector<uint8_t> out;

    put_u16(out, usbip::Version);
    put_u16(out, usbip::OpRepDevlist);
    put_u32(out, 0);
    put_u32(out, dev ? 1 : 0);

    if (!dev) {
        return out;
    }

    append_usb_device(out, *dev);

    for (const auto& itf : dev->interfaces) {
        put_u8(out, itf.cls);
        put_u8(out, itf.subcls);
        put_u8(out, itf.proto);
        put_u8(out, 0);
    }

    return out;
}

std::vector<uint8_t> build_import_reply_ok(const UsbDeviceInfo& d) {
    std::vector<uint8_t> out;

    put_u16(out, usbip::Version);
    put_u16(out, usbip::OpRepImport);
    put_u32(out, 0);
    append_usb_device(out, d);

    return out;
}

std::vector<uint8_t> build_import_reply_error(uint32_t status) {
    std::vector<uint8_t> out;

    put_u16(out, usbip::Version);
    put_u16(out, usbip::OpRepImport);
    put_u32(out, status);

    return out;
}

bool parse_submit(int client_fd, const uint8_t header[48], UrbSubmit& urb) {
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

    if (urb.direction == usbip::DirOut && urb.transfer_buffer_length > 0) {
        urb.out_payload.resize(urb.transfer_buffer_length);
        if (!read_exact(client_fd, urb.out_payload.data(), urb.out_payload.size())) {
            return false;
        }
    }

    return true;
}

bool send_ret_submit(
    int client_fd,
    uint32_t seqnum,
    int32_t status,
    uint32_t actual_length,
    const std::vector<uint8_t>& in_payload)
{
    std::vector<uint8_t> out;

    put_u32(out, usbip::RetSubmit);
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
