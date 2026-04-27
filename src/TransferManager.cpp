#include "DebugStats.h"
#include "Log.h"
#include "NetUtil.h"
#include "TransferManager.h"
#include "UsbIpProtocol.h"

#include <cstdint>
#include <vector>

namespace {

void log_and_count_ret_error(const UrbSubmit& urb, int32_t usbip_status) {
    record_ret_error(usbip_status);

    if (usbip_status == -19) {
        LOGI("RET_SUBMIT device gone:" << " seq=" << urb.seqnum << " status=" << usbip_status
                                       << " ep=" << urb.ep << " dir=" << urb.direction
                                       << " len=" << urb.transfer_buffer_length);
    } else {
        LOGW("RET_SUBMIT error:" << " seq=" << urb.seqnum << " status=" << usbip_status
                                 << " ep=" << urb.ep << " dir=" << urb.direction
                                 << " len=" << urb.transfer_buffer_length);
    }

    if ((g_stats.retError.load() % 20) == 0) {
        dump_stats("ret-error");
    }
}

int32_t libusb_to_usbip_status(int rc) {
    if (rc == LIBUSB_ERROR_IO) {
        return -32; // EPIPE-like recovery path for vhci.
    }

    if (rc == LIBUSB_ERROR_PIPE) {
        return -32;
    }

    if (rc == LIBUSB_ERROR_TIMEOUT) {
        return -110;
    }

    if (rc == LIBUSB_ERROR_NO_DEVICE) {
        return -19;
    }

    return rc;
}

} // namespace

void urb_loop(int client_fd, libusb_device_handle* handle, UsbRuntimeInfo& rt) {
    LOGI("enter URB loop");

    while (true) {
        uint8_t header[48];

        if (!read_exact(client_fd, header, sizeof(header))) {
            LOGI("URB connection closed");
            dump_stats("connection-closed");
            return;
        }

        uint32_t command = get_be32(header + 0);

        if (command == usbip::CmdSubmit) {
            UrbSubmit urb;
            if (!parse_submit(client_fd, header, urb)) {
                LOGE("failed to parse submit");
                dump_stats("parse-submit-failed");
                return;
            }

            LOGT("SUBMIT:" << " seq=" << urb.seqnum << " dir=" << urb.direction << " ep=" << urb.ep
                           << " len=" << urb.transfer_buffer_length);

            std::vector<uint8_t> response;

            if (urb.ep == 0) {
                int rc = handle_control_submit(handle, rt, urb, response);

                if (rc < 0) {
                    int32_t usbip_status = libusb_to_usbip_status(rc);
                    log_and_count_ret_error(urb, usbip_status);
                    send_ret_submit(client_fd, urb.seqnum, usbip_status, 0, {});

                    if (usbip_status == -19) {
                        LOGI("device disconnected during control transfer, exit URB loop");
                        dump_stats("device-disconnected-control");
                        return;
                    }
                } else {
                    send_ret_submit(client_fd, urb.seqnum, 0,
                                    static_cast<uint32_t>(response.size()), response);
                }
            } else {
                int transferred = 0;
                int rc = handle_bulk_submit(handle, rt, urb, response, transferred);

                if (rc < 0) {
                    int32_t usbip_status = libusb_to_usbip_status(rc);
                    log_and_count_ret_error(urb, usbip_status);
                    send_ret_submit(client_fd, urb.seqnum, usbip_status, 0, {});

                    if (usbip_status == -19) {
                        LOGI("device disconnected during bulk transfer, exit URB loop");
                        dump_stats("device-disconnected-bulk");
                        return;
                    }
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
            uint32_t seqnum = get_be32(header + 4);
            uint32_t unlinkSeqnum = get_be32(header + 20);
            auto n = ++g_stats.unlinkReq;

            LOGW("UNLINK request count=" << n << " seq=" << seqnum
                                         << " unlink_seq=" << unlinkSeqnum);

            /*
             * The current implementation uses synchronous libusb_bulk_transfer().
             * When an UNLINK request is received, the target URB has typically already
             * completed or cannot be canceled. Therefore, we return -ENOENT to indicate
             * that no pending URB could be found for cancellation.
             *
             * Proper cancellation handling will be implemented later when switching
             * to asynchronous libusb_transfer.
             */
            send_ret_unlink(client_fd, seqnum, -2);

            if ((n % 20) == 0) {
                dump_stats("unlink");
            }

            continue;
        }

        LOGE("unknown URB command=0x" << std::hex << command << std::dec);
        dump_stats("unknown-command");
        return;
    }
}
