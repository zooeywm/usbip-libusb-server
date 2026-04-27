#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct UsbInterfaceInfo {
    uint8_t cls = 0;
    uint8_t subcls = 0;
    uint8_t proto = 0;
};

struct UsbDeviceInfo {
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

struct UrbSubmit {
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

namespace usbip {

static constexpr uint16_t Version = 0x0111;
static constexpr uint16_t OpReqDevlist = 0x8005;
static constexpr uint16_t OpRepDevlist = 0x0005;
static constexpr uint16_t OpReqImport = 0x8003;
static constexpr uint16_t OpRepImport = 0x0003;

static constexpr uint32_t CmdSubmit = 0x00000001;
static constexpr uint32_t CmdUnlink = 0x00000002;
static constexpr uint32_t RetSubmit = 0x00000003;
static constexpr uint32_t RetUnlink = 0x00000004;

static constexpr uint32_t DirOut = 0;
static constexpr uint32_t DirIn = 1;

} // namespace usbip

void append_usb_device(std::vector<uint8_t>& out, const UsbDeviceInfo& d);
std::vector<uint8_t> build_devlist_reply(const std::vector<UsbDeviceInfo>& devices);
std::vector<uint8_t> build_import_reply_ok(const UsbDeviceInfo& d);
std::vector<uint8_t> build_import_reply_error(uint32_t status);

bool parse_submit(int client_fd, const uint8_t header[48], UrbSubmit& urb);
bool send_ret_submit(
    int client_fd,
    uint32_t seqnum,
    int32_t status,
    uint32_t actual_length,
    const std::vector<uint8_t>& in_payload);

bool send_ret_unlink(int client_fd, uint32_t seqnum, int32_t status);
