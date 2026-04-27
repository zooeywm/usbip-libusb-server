#include "NetUtil.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>

bool read_exact(int fd, void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    while (len > 0) {
        ssize_t n = ::recv(fd, p, len, 0);
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

bool write_exact(int fd, const void* buf, size_t len) {
    const auto* p = static_cast<const uint8_t*>(buf);
    while (len > 0) {
        ssize_t n = ::send(fd, p, len, 0);
        if (n <= 0) {
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

uint32_t get_be32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return ntohl(v);
}

int32_t get_be32s(const uint8_t* p) {
    return static_cast<int32_t>(get_be32(p));
}

void put_u8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

void put_u16(std::vector<uint8_t>& out, uint16_t v) {
    uint16_t n = htons(v);
    auto* p = reinterpret_cast<uint8_t*>(&n);
    out.insert(out.end(), p, p + sizeof(n));
}

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    uint32_t n = htonl(v);
    auto* p = reinterpret_cast<uint8_t*>(&n);
    out.insert(out.end(), p, p + sizeof(n));
}

void put_fixed_string(std::vector<uint8_t>& out, const std::string& s, size_t n) {
    std::vector<uint8_t> buf(n, 0);
    if (n > 0) {
        std::memcpy(buf.data(), s.data(), std::min(s.size(), n - 1));
    }
    out.insert(out.end(), buf.begin(), buf.end());
}

std::string trim_c_string(const char* data, size_t len) {
    size_t n = 0;
    while (n < len && data[n] != '\0') {
        ++n;
    }
    return std::string(data, n);
}

void dump_hex(const char* tag, const std::vector<uint8_t>& data, size_t max) {
    std::cout << tag << " [" << data.size() << "]:";
    for (size_t i = 0; i < data.size() && i < max; ++i) {
        std::cout << " " << std::hex << std::uppercase << static_cast<int>(data[i]);
    }
    std::cout << std::dec << std::nouppercase << std::endl;
}
