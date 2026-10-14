// The 1 KB image header at 0x90000000 (spec §11.1). mkimage.py fills it in Plan 04; until then it
// reads as erased flash.
#include <stdint.h>

__attribute__((section(".image_header"), used)) const uint8_t image_header[1024] = {[0 ... 1023] = 0xFF};
