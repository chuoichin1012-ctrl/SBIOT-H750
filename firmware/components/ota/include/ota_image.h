// Image format and checks (spec §11.1). An image is a 1 KB header followed by the body, which starts
// with the vector table; it runs from QSPI_XIP_BASE with the body at QSPI_XIP_BASE + IMG_HDR_SIZE.
// Header layout (little endian), written by tools/mkimage.py:
//   0  magic "SBIO"          4  hardware id "SBIOT-H750", NUL padded to 16 bytes
//   20 version major, minor, patch (3 x u16), 0 (u16)
//   28 size: body bytes (u32) 32 SHA-256 of the body
//   64 CRC-32 of bytes 0..63 (u32)   68..1023 0xFF
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nor.h"

#define IMG_HDR_SIZE 0x400u
#define IMG_CORE 68u  // the bytes that identify an image; the rest of the header is 0xFF
#define IMG_MAX_BODY (0x170000u - IMG_HDR_SIZE)

typedef struct {
    uint8_t raw[IMG_CORE];  // the header core as stored: compare two images with memcmp
    uint16_t version[3];
    uint32_t size;
    uint8_t sha256[32];
} img_hdr_t;

enum {
    IMG_OK = 0,
    IMG_EREAD = -1,     // flash read failed
    IMG_EMAGIC = -2,    // no image header (erased or foreign)
    IMG_ECRC = -3,      // header CRC wrong
    IMG_EHW = -4,       // built for another board
    IMG_ESIZE = -5,     // body empty or larger than the slot
    IMG_ESHA = -6,      // body does not match the header's SHA-256
    IMG_EVECTORS = -7,  // initial MSP or reset vector unusable
};

// Parses and checks the header core (magic, CRC, hardware id, size); fills h on success.
int img_parse(const uint8_t raw[IMG_CORE], img_hdr_t *h);
// Builds a header core (the C twin of mkimage.py, for the host tests).
void img_make(uint8_t raw[IMG_CORE], const uint16_t version[3], uint32_t size, const uint8_t sha256[32]);
// The two jump conditions of §11.1: MSP at the end of a stack in DTCM or AXI SRAM, Thumb reset
// vector inside [QSPI_XIP_BASE + IMG_HDR_SIZE, QSPI_XIP_BASE + IMG_HDR_SIZE + size).
bool img_vectors_ok(uint32_t msp, uint32_t reset, uint32_t size);
// Full check of the image at off on n: header, SHA-256 of the body, vectors. kick (may be NULL) is
// called between 4 KB chunks. Fills h (may be NULL) on IMG_OK.
int img_check(nor_t *n, uint32_t off, void (*kick)(void), img_hdr_t *h);
