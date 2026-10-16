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

void board_latch_zero(void)
{
    HAL_GPIO_WritePin(LATCH_OE_GPIO_Port, LATCH_OE_Pin, GPIO_PIN_SET);  // outputs hi-Z: relays released
    HAL_GPIO_WritePin(DO1_D_GPIO_Port, DO1_D_Pin | DO2_D_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_SET);  // transparent: Q follows D
    (void)LATCH_LE_GPIO_Port->ODR;                                       // the write has reached the port
    __DSB();
    HAL_GPIO_WritePin(LATCH_LE_GPIO_Port, LATCH_LE_Pin, GPIO_PIN_RESET);  // latch the zeros
    HAL_GPIO_WritePin(LATCH_OE_GPIO_Port, LATCH_OE_Pin, GPIO_PIN_RESET);  // and drive them
}
