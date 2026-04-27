#include "UsbIpServer.h"

#include "LibusbBackend.h"
#include "Log.h"
#include "NetUtil.h"
#include "TransferManager.h"
#include "UsbIpProtocol.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>

namespace {

struct OpHeader {
    uint16_t version;
    uint16_t code;
    uint32_t status;
} __attribute__((packed));

void handle_client(int client_fd, libusb_context* usb_ctx) {
    OpHeader h{};
    if (!read_exact(client_fd, &h, sizeof(h))) {
        return;
    }

    uint16_t version = ntohs(h.version);
    uint16_t code = ntohs(h.code);

    LOGI("USB/IP request: version=0x" << std::hex << version
         << " code=0x" << code << std::dec);

    if (version != usbip::Version) {
        LOGE("unsupported version");
        return;
    }

    if (code == usbip::OpReqDevlist) {
        auto dev = find_mass_storage_device(usb_ctx);

        if (dev) {
            LOGI("export device: busid=" << dev->busid
                 << " vid=0x" << std::hex << dev->vid
                 << " pid=0x" << dev->pid
                 << std::dec);
        } else {
            LOGI("no mass storage device found");
        }

        auto reply = build_devlist_reply(dev);
        write_exact(client_fd, reply.data(), reply.size());
        return;
    }

    if (code == usbip::OpReqImport) {
        char busid_raw[32]{};
        if (!read_exact(client_fd, busid_raw, sizeof(busid_raw))) {
            LOGE("failed to read import busid");
            return;
        }

        std::string req_busid = trim_c_string(busid_raw, sizeof(busid_raw));
        LOGI("import request busid=" << req_busid);

        auto dev = find_mass_storage_device(usb_ctx);
        if (!dev || dev->busid != req_busid) {
            LOGE("requested device not found");
            auto reply = build_import_reply_error(1);
            write_exact(client_fd, reply.data(), reply.size());
            return;
        }

        auto reply = build_import_reply_ok(*dev);
        if (!write_exact(client_fd, reply.data(), reply.size())) {
            LOGE("failed to send import reply");
            return;
        }

        LOGI("import ok, keep connection for URB traffic");

        libusb_device_handle* handle = open_device_by_busid(usb_ctx, dev->busid);
        if (!handle) {
            LOGE("failed to open libusb device");
            return;
        }

        LOGI("libusb open ok");

        auto rt = find_mass_storage_runtime(handle);
        if (!rt) {
            LOGE("mass storage bulk endpoints not found");
            libusb_close(handle);
            return;
        }

        LOGI("mass storage interface=" << rt->interfaceNumber
             << " bulkIn=0x" << std::hex << static_cast<int>(rt->bulkInEp)
             << " bulkOut=0x" << static_cast<int>(rt->bulkOutEp)
             << std::dec);

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

    LOGE("unsupported request code=0x" << std::hex << code << std::dec);
}

} // namespace

void run_server(libusb_context* usb_ctx, int port) {
    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return;
    }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        ::close(server_fd);
        return;
    }

    if (::listen(server_fd, 16) < 0) {
        perror("listen");
        ::close(server_fd);
        return;
    }

    LOGI("usbip-libusb-server listening on port " << port);

    while (true) {
        int client_fd = ::accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        handle_client(client_fd, usb_ctx);
        ::close(client_fd);
    }

    ::close(server_fd);
}
