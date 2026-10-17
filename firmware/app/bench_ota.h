// The app side of OTA and anti-clone for the bench (Plan 04b): the binding check of spec §11.5,
// and the UART7 console commands that report the images and stream a new one into staging.
// POST /api/ota (Plan 06) uses the same ota_stage_* calls.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool app_bind_ok(void);       // bind_valid() on the record read through XIP at QSPI_BIND
// The board's MAC and SN from that record (Plan 11a): false when it lacks them (the app then halts).
bool app_identity(uint8_t mac[6], char sn[13]);
void bench_ota_init(void);    // SPI4 flash
void bench_ota_report(void);  // 'o': the images in QSPI, staging and the factory slot
// 'u': u32 little-endian length, then the image in blocks of up to 4 KB. The app answers
// "ota: ready <len> <rc>", "ota: <bytes so far>" after each block, "ota: done <rc>"; on rc 0 it
// resets and the bootloader installs the image. 'U' (bench): the same without the reset, so a
// test can choose how the next boot happens.
void bench_ota_upload(bool reset);
