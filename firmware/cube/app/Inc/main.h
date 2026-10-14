/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI4_NSS_Pin GPIO_PIN_4
#define SPI4_NSS_GPIO_Port GPIOE
#define LTE_RESET_Pin GPIO_PIN_4
#define LTE_RESET_GPIO_Port GPIOA
#define LTE_PWR_Pin GPIO_PIN_5
#define LTE_PWR_GPIO_Port GPIOA
#define LED_NET_Pin GPIO_PIN_11
#define LED_NET_GPIO_Port GPIOE
#define DO1_D_Pin GPIO_PIN_10
#define DO1_D_GPIO_Port GPIOD
#define DO2_D_Pin GPIO_PIN_11
#define DO2_D_GPIO_Port GPIOD
#define ETH_RESET_Pin GPIO_PIN_13
#define ETH_RESET_GPIO_Port GPIOD
#define LATCH_LE_Pin GPIO_PIN_14
#define LATCH_LE_GPIO_Port GPIOD
#define LATCH_OE_Pin GPIO_PIN_15
#define LATCH_OE_GPIO_Port GPIOD
#define RELOAD_Pin GPIO_PIN_8
#define RELOAD_GPIO_Port GPIOA
#define WDI_LED_Pin GPIO_PIN_10
#define WDI_LED_GPIO_Port GPIOC
#define DI2_Pin GPIO_PIN_6
#define DI2_GPIO_Port GPIOB
#define DI1_Pin GPIO_PIN_7
#define DI1_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
