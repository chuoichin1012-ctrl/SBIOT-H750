// The chip select pin label spi_nor.c takes from CubeMX's main.h, for host tests.
#pragma once
#include "stm32h7xx_hal.h"
extern GPIO_TypeDef g_fake_gpio;
#define SPI4_NSS_GPIO_Port (&g_fake_gpio)
#define SPI4_NSS_Pin 1u
