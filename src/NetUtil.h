#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

bool read_exact(int fd, void* buf, size_t len);
bool write_exact(int fd, const void* buf, size_t len);

uint32_t get_be32(const uint8_t* p);
int32_t get_be32s(const uint8_t* p);

void put_u8(std::vector<uint8_t>& out, uint8_t v);
void put_u16(std::vector<uint8_t>& out, uint16_t v);
void put_u32(std::vector<uint8_t>& out, uint32_t v);
void put_fixed_string(std::vector<uint8_t>& out, const std::string& s, size_t n);

std::string trim_c_string(const char* data, size_t len);
void dump_hex(const char* tag, const std::vector<uint8_t>& data, size_t max = 64);
