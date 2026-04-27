#pragma once

#include "LibusbBackend.h"

void urb_loop(int client_fd, libusb_device_handle* handle, UsbRuntimeInfo& rt);
