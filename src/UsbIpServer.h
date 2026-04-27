#pragma once

#include <libusb-1.0/libusb.h>

void run_server(libusb_context* usb_ctx, int port);
