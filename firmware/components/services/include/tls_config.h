// mbedTLS 3.6 LTS for the SBIOT-H750 (spec §7.3, Plan 08b): a TLS 1.2 client only. Passed as
// MBEDTLS_CONFIG_FILE="tls_config.h": the name must not be mbedtls_config.h, which build_info.h would
// find in its own directory (the library's default config) before this one.
#pragma once
#include <time.h>

// ---- Platform: the system clock, the hardware RNG, calloc/free (already heap_5's wrapper, §5.5) ----
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MS_TIME_ALT   // mbedtls_ms_time: tls_port.c (nothing in this config calls it)
#define MBEDTLS_PLATFORM_GMTIME_R_ALT  // mbedtls_platform_gmtime_r: tls_core.c, on clock_from_unix
#define MBEDTLS_PLATFORM_TIME_MACRO tls_port_time
/** UTC seconds of the system clock, 0 while it is not set (never the 2020 display time). */
time_t tls_port_time(time_t *t);
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT  // mbedtls_hardware_poll: board_rng
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

// ---- Symmetric ciphers and hashes ----
#define MBEDTLS_AES_C
#define MBEDTLS_AES_ROM_TABLES
#define MBEDTLS_AES_FEWER_TABLES
#define MBEDTLS_GCM_C
#define MBEDTLS_CHACHA20_C
#define MBEDTLS_POLY1305_C
#define MBEDTLS_CHACHAPOLY_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

// ---- Public key ----
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_MPI_WINDOW_SIZE 2
#define MBEDTLS_MPI_MAX_SIZE 512
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_CURVE25519_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

// ---- Certificates and keys ----
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

// ---- TLS 1.2 client ----
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
#define MBEDTLS_SSL_EXTENDED_MASTER_SECRET
#define MBEDTLS_SSL_ENCRYPT_THEN_MAC
#define MBEDTLS_SSL_KEEP_PEER_CERTIFICATE
#define MBEDTLS_SSL_MAX_FRAGMENT_LENGTH
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define MBEDTLS_SSL_CIPHERSUITES                                                                          \
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,         \
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,     \
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,                                              \
        MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256, MBEDTLS_TLS_RSA_WITH_AES_128_GCM_SHA256,      \
        MBEDTLS_TLS_RSA_WITH_AES_256_GCM_SHA384
#define MBEDTLS_ERROR_C

#ifdef TLS_HOST_TEST
#define MBEDTLS_SSL_SRV_C  // test_tlsconn's server only
#endif
