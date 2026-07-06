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
#include "stm32u5xx_hal.h"

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
#define RELAY4_IN_Pin GPIO_PIN_0
#define RELAY4_IN_GPIO_Port GPIOC
#define RD03_OUT_Pin GPIO_PIN_1
#define RD03_OUT_GPIO_Port GPIOC
#define PIR_IN_Pin GPIO_PIN_0
#define PIR_IN_GPIO_Port GPIOB
#define TFT_RST_Pin GPIO_PIN_12
#define TFT_RST_GPIO_Port GPIOF
#define TFT_DC_Pin GPIO_PIN_13
#define TFT_DC_GPIO_Port GPIOF
#define RELAY2_IN_Pin GPIO_PIN_14
#define RELAY2_IN_GPIO_Port GPIOF
#define BUZZER_IO_Pin GPIO_PIN_9
#define BUZZER_IO_GPIO_Port GPIOE
#define RELAY3_IN_Pin GPIO_PIN_11
#define RELAY3_IN_GPIO_Port GPIOE
#define RELAY1_IN_Pin GPIO_PIN_13
#define RELAY1_IN_GPIO_Port GPIOE
#define TFT_CS_Pin GPIO_PIN_14
#define TFT_CS_GPIO_Port GPIOD
#define TFT_BL_Pin GPIO_PIN_15
#define TFT_BL_GPIO_Port GPIOD
#define ACK_BUTTON_Pin GPIO_PIN_7
#define ACK_BUTTON_GPIO_Port GPIOG
#define SOS_BUTTON_Pin GPIO_PIN_8
#define SOS_BUTTON_GPIO_Port GPIOG
#define LED_STATUS_Pin GPIO_PIN_7
#define LED_STATUS_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
