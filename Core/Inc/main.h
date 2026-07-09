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
#include "stm32h5xx_hal.h"

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
#define GPS_CPW_Pin GPIO_PIN_2
#define GPS_CPW_GPIO_Port GPIOC
#define GPS_RST_Pin GPIO_PIN_3
#define GPS_RST_GPIO_Port GPIOC
#define UART5_S0_Pin GPIO_PIN_7
#define UART5_S0_GPIO_Port GPIOA
#define UART5_S1_Pin GPIO_PIN_4
#define UART5_S1_GPIO_Port GPIOC
#define UART3_RDE_Pin GPIO_PIN_2
#define UART3_RDE_GPIO_Port GPIOB
#define LTE_RX_Pin GPIO_PIN_14
#define LTE_RX_GPIO_Port GPIOB
#define LTE_TX_Pin GPIO_PIN_15
#define LTE_TX_GPIO_Port GPIOB
#define LTE_RST_Pin GPIO_PIN_11
#define LTE_RST_GPIO_Port GPIOD
#define LTE_PWR_KEY_Pin GPIO_PIN_12
#define LTE_PWR_KEY_GPIO_Port GPIOD
#define EN_PW_PUMP_Pin GPIO_PIN_8
#define EN_PW_PUMP_GPIO_Port GPIOA
#define EN_PW_DUST_Pin GPIO_PIN_9
#define EN_PW_DUST_GPIO_Port GPIOA
#define SPI1_CS_Pin GPIO_PIN_12
#define SPI1_CS_GPIO_Port GPIOC
#define I2C1_RST_Pin GPIO_PIN_8
#define I2C1_RST_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
