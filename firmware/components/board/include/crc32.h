// CRC-32 (IEEE 802.3, reflected, init and final XOR 0xFFFFFFFF): the zlib/binascii crc32.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Continues a CRC: start with crc32_update(0, ...), pass the previous result to continue.
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
