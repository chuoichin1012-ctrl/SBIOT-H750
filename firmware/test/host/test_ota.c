// Host tests for components/ota: SHA-256, the image header and checks, and the image written by
// tools/mkimage.py (the Makefile passes its path). make -C stm32/firmware/test/host
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "flashmap.h"
#include "nor_sim.h"
#include "ota_image.h"
#include "sha256.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static void hex(const uint8_t d[32], char out[65])
{
    for (int i = 0; i < 32; i++)
        sprintf(out + 2 * i, "%02x", d[i]);
}

static void sha_of(const void *data, size_t len, size_t chunk, char out[65])
{
    sha256_t s;
    sha256_init(&s);
    for (size_t i = 0; i < len; i += chunk)
        sha256_update(&s, (const uint8_t *)data + i, len - i < chunk ? len - i : chunk);
    uint8_t d[32];
    sha256_final(&s, d);
    hex(d, out);
}

static void test_sha256(void)
{
    // FIPS 180-4 examples, and 1 000 000 x 'a'
    char h[65];
    sha_of("", 0, 1, h);
    CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "empty: %s", h);
    sha_of("abc", 3, 1, h);
    CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "abc: %s", h);
    const char *m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (size_t chunk = 1; chunk <= 64; chunk *= 4) {
        sha_of(m, strlen(m), chunk, h);
        CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "448 bits in %zu-byte chunks", chunk);
    }
    char *a = malloc(1000000);
    memset(a, 'a', 1000000);
    sha_of(a, 1000000, 999, h);
    CHECK(!strcmp(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), "a x 10^6: %s", h);
    free(a);
}

static void test_header(void)
{
    uint8_t raw[IMG_CORE], sha[32] = {1, 2, 3};
    uint16_t v[3] = {1, 2, 3};
    img_hdr_t h;
    img_make(raw, v, 5000, sha);
    CHECK(img_parse(raw, &h) == IMG_OK && h.size == 5000 && h.version[2] == 3, "a made header parses");
    raw[0] ^= 1;
    CHECK(img_parse(raw, &h) == IMG_EMAGIC, "wrong magic");
    raw[0] ^= 1;
    raw[40] ^= 1;
    CHECK(img_parse(raw, &h) == IMG_ECRC, "header CRC");
    img_make(raw, v, 7, sha);
    CHECK(img_parse(raw, &h) == IMG_ESIZE, "body under 8 bytes");
    img_make(raw, v, IMG_MAX_BODY + 1, sha);
    CHECK(img_parse(raw, &h) == IMG_ESIZE, "body larger than the slot");
    img_make(raw, v, IMG_MAX_BODY, sha);
    CHECK(img_parse(raw, &h) == IMG_OK, "body exactly the slot");
}

static void test_vectors(void)
{
    const uint32_t body = QSPI_XIP_BASE + IMG_HDR_SIZE;
    CHECK(img_vectors_ok(0x20020000u, body + 0x1E9, 0x8000), "MSP at the top of DTCM, Thumb reset in the body");
    CHECK(img_vectors_ok(0x24080000u, body + 1, 8), "MSP at the top of AXI, reset at the vector table");
    CHECK(img_vectors_ok(0x20000008u, body + 7, 8), "lowest MSP, last byte of an 8-byte body");
    CHECK(!img_vectors_ok(0x20000000u, body + 1, 8), "MSP at the DTCM base: empty stack");
    CHECK(!img_vectors_ok(0x20020008u, body + 1, 8), "MSP above DTCM");
    CHECK(!img_vectors_ok(0x2001FFFCu, body + 1, 8), "MSP not 8-byte aligned");
    CHECK(!img_vectors_ok(0x30000000u, body + 1, 8), "MSP in SRAM1");
    CHECK(!img_vectors_ok(0x20020000u, body, 8), "reset vector without the Thumb bit");
    CHECK(!img_vectors_ok(0x20020000u, body - 0x1F, 8), "reset vector in the header");
    CHECK(!img_vectors_ok(0x20020000u, body + 9, 8), "reset vector past the body");
    CHECK(!img_vectors_ok(0x8809F380u, 0xF380E001u, 8), "the old firmware's code read as vectors");
    CHECK(!img_vectors_ok(0xFFFFFFFFu, 0xFFFFFFFFu, 8), "erased flash");
}

static void test_mkimage_output(const char *path)
{
    FILE *f = fopen(path, "rb");
    CHECK(f, "%s exists (tools/test_mkimage.py writes it)", path);
    if (!f)
        return;
    sim_world_t w;
    nor_sim_t n;
    sim_world_init(&w, 1);
    nor_sim_init(&n, &w, 1, 0x200000);
    size_t len = fread(n.mem + 0x10000, 1, 0x100000, f);
    fclose(f);
    img_hdr_t h;
    int r = img_check(&n.nor, 0x10000, NULL, &h);
    CHECK(r == IMG_OK && h.version[0] == 1 && h.version[1] == 2 && h.version[2] == 3 && len == IMG_HDR_SIZE + h.size,
          "mkimage.py's image passes img_check (%d)", r);
    n.mem[0x10000 + IMG_HDR_SIZE + 100] ^= 1;
    CHECK(img_check(&n.nor, 0x10000, NULL, &h) == IMG_ESHA, "and fails it with one body bit changed");
    nor_sim_free(&n);
}

int main(int argc, char **argv)
{
    test_sha256();
    test_header();
    test_vectors();
    test_mkimage_output(argc > 1 ? argv[1] : "build/mkimage_sample.img");
    printf("test_ota: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
