#include "TransferManager.h"

#include "Log.h"
#include "NetUtil.h"
#include "UsbIpProtocol.h"

#include <cstdint>
#include <vector>

void urb_loop(int client_fd, libusb_device_handle* handle, UsbRuntimeInfo& rt) {
    LOGI("enter URB loop");

    while (true) {
        uint8_t header[48];

        if (!read_exact(client_fd, header, sizeof(header))) {
            LOGI("URB connection closed");
            return;
        }

        uint32_t command = get_be32(header + 0);

        if (command == usbip::CmdSubmit) {
            UrbSubmit urb;
            if (!parse_submit(client_fd, header, urb)) {
                LOGE("failed to parse submit");
                return;
            }

            LOGT("SUBMIT:"
                 << " seq=" << urb.seqnum
                 << " dir=" << urb.direction
                 << " ep=" << urb.ep
                 << " len=" << urb.transfer_buffer_length);

            std::vector<uint8_t> response;

            if (urb.ep == 0) {
                int rc = handle_control_submit(handle, rt, urb, response);

                if (rc < 0) {
                    send_ret_submit(client_fd, urb.seqnum, rc, 0, {});
                } else {
                    send_ret_submit(
                        client_fd,
                        urb.seqnum,
                        0,
                        static_cast<uint32_t>(response.size()),
                        response);
                }
            } else {
                int transferred = 0;
                int rc = handle_bulk_submit(handle, rt, urb, response, transferred);

                if (rc < 0) {
                    int usbip_status = rc;

                    if (rc == LIBUSB_ERROR_IO) {
                        usbip_status = -32; // EPIPE-like recovery path for vhci.
                    } else if (rc == LIBUSB_ERROR_TIMEOUT) {
                        usbip_status = -110;
                    } else if (rc == LIBUSB_ERROR_NO_DEVICE) {
                        usbip_status = -19;
                    }

                    send_ret_submit(client_fd, urb.seqnum, usbip_status, 0, {});
                } else {
                    uint32_t actual = 0;
                    if (urb.direction == usbip::DirIn) {
                        actual = static_cast<uint32_t>(response.size());
                    } else {
                        actual = static_cast<uint32_t>(transferred);
                    }

                    send_ret_submit(client_fd, urb.seqnum, 0, actual, response);
                }
            }

            continue;
        }

        if (command == usbip::CmdUnlink) {
            LOGE("UNLINK not implemented yet");
            return;
        }

        LOGE("unknown URB command=0x" << std::hex << command << std::dec);
        return;
    }
}
