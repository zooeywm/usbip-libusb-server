#include "Log.h"
#include "UsbIpServer.h"

#include <libusb-1.0/libusb.h>

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    int port = 3240;
    if (argc >= 2) {
        port = std::stoi(argv[1]);
    }

    if (argc >= 3) {
        g_log_level = std::atoi(argv[2]);
    }

    libusb_context* usb_ctx = nullptr;
    if (libusb_init(&usb_ctx) != 0) {
        std::cerr << "libusb_init failed" << std::endl;
        return 1;
    }

    run_server(usb_ctx, port);

    libusb_exit(usb_ctx);
    return 0;
}
