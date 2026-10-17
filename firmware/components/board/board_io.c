#include "board.h"
#include "main.h"  // pin labels from the .ioc; boot and app use the same names

void board_iwdg_reload(void)
{
    IWDG1->KR = 0x0000AAAAu;  // reload; harmless while IWDG1 is not running
}

void board_wdi_toggle(void)
{
    HAL_GPIO_TogglePin(WDI_LED_GPIO_Port, WDI_LED_Pin);
}

void board_kick(void)
{
    board_wdi_toggle();
    board_iwdg_reload();
}

static void latch_zero(void)
{
    HAL_GPIO_WritePin(LATCH_OE_GPIO_Port, LATCH_OE_Pin, GPIO_PIN_SET);  // outputs hi-Z: relays released
    HAL_GPIO_WritePin(DO1_D_GPIO_Port, DO1_D_Pin | DO2_D_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_SET);  // transparent: Q follows D
    (void)LATCH_LE_GPIO_Port->ODR;                                       // the write has reached the port
    __DSB();
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_RESET);  // latch the zeros
    HAL_GPIO_WritePin(LATCH_OE_GPIO_Port, LATCH_OE_Pin, GPIO_PIN_RESET);  // and drive them
}

void board_latch_init(bool hold)
{
    __HAL_RCC_GPIOD_CLK_ENABLE();
    // The levels first, then the mode: a pin becomes an output already at its level.
    LATCH_LE_GPIO_Port->BSRR = (uint32_t)LATCH_LE_Pin << 16;  // LE low: latched
    LATCH_OE_GPIO_Port->BSRR = hold ? (uint32_t)LATCH_OE_Pin << 16 : LATCH_OE_Pin;  // hold: enabled; else hi-Z
    GPIO_InitTypeDef g = {.Pin = LATCH_LE_Pin | LATCH_OE_Pin, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                          .Speed = GPIO_SPEED_FREQ_LOW};
    HAL_GPIO_Init(LATCH_LE_GPIO_Port, &g);
    if (!hold) {
        GPIO_InitTypeDef d = {.Pin = DO1_D_Pin | DO2_D_Pin, .Mode = GPIO_MODE_OUTPUT_PP, .Pull = GPIO_NOPULL,
                              .Speed = GPIO_SPEED_FREQ_LOW};
        HAL_GPIO_Init(DO1_D_GPIO_Port, &d);  // D1/D2, which MX_GPIO_Init may not have set up yet
        latch_zero();
    }
}
