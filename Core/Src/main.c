/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "gpdma.h"
#include "i2c.h"
#include "stm32u5xx_hal_uart.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sensor_mvp.h"
#include <stdio.h>
#include <string.h>
#include"comm_wifi.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MAIN_STATUS_TX_PERIOD_MS 2000U
#define MAIN_RISK_GAS_WARN      2000
#define MAIN_RISK_GAS_ALARM     3000

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart){
    if(huart==&huart2){
        CommWifi_OnTxComplete();
    }
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Debug_WriteLine(const char *text)
{
  const uint8_t newline[] = "\r\n";

  if (text == NULL)
  {
    return;
  }

  if (HAL_UART_Transmit(&huart1, (const uint8_t *)text, (uint16_t)strlen(text), 100U) != HAL_OK)
  {
    Error_Handler();
  }

  if (HAL_UART_Transmit(&huart1, newline, (uint16_t)(sizeof(newline) - 1U), 100U) != HAL_OK)
  {
    Error_Handler();
  }
}

static int Build_PlaceholderRisk(const SensorMvp_Status_t *status)
{
  if (status == NULL)
  {
    return 0;
  }

  if ((status->gas_valid != 0U) && (status->gas >= MAIN_RISK_GAS_ALARM))
  {
    return 3;
  }

  if ((status->gas_valid != 0U) && (status->gas >= MAIN_RISK_GAS_WARN))
  {
    return 2;
  }

  if (status->presence != 0)
  {
    return 1;
  }

  return 0;
}

static void Send_Status_ToWifi(void)
{
  char line[128];
  SensorMvp_Status_t status;

  if (SensorMvp_GetStatus(&status) != HAL_OK)
  {
    Debug_WriteLine("[WARN] status get failed");
    return;
  }

  const int risk = Build_PlaceholderRisk(&status);
  const CommWifi_Result result = CommWifi_SendStatus(status.temperature_c,
                                                     status.humidity_pct,
                                                     status.gas,
                                                     status.presence,
                                                     risk);

  if (result == COMM_WIFI_OK)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] status tx temp=%.1f hum=%.1f gas=%d presence=%d risk=%d env_valid=%u gas_valid=%u",
                   status.temperature_c,
                   status.humidity_pct,
                   status.gas,
                   status.presence,
                   risk,
                   (unsigned int)status.env_valid,
                   (unsigned int)status.gas_valid);
  }
  else
  {
    (void)snprintf(line, sizeof(line), "[WARN] status tx failed err=%d", (int)result);
  }

  Debug_WriteLine(line);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_GPDMA1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_USART3_UART_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  Debug_WriteLine("[INFO] system boot");
  if (CommWifi_Init() == COMM_WIFI_OK)
  {
    Debug_WriteLine("[INFO] comm wifi init ok");
  }
  else
  {
    Debug_WriteLine("[WARN] comm wifi init failed");
  }
  SensorMvp_Init(Debug_WriteLine);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    static uint32_t last_led_tick = 0U;
    static uint32_t last_status_tx_tick = 0U;
    uint32_t now = HAL_GetTick();

    SensorMvp_Update();

    if ((now - last_status_tx_tick) >= MAIN_STATUS_TX_PERIOD_MS)
    {
      last_status_tx_tick = now;
      Send_Status_ToWifi();
    }

    if ((now - last_led_tick) >= 500U)
    {
      last_led_tick = now;
      HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_4;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  const uint8_t message[] = "[FAIL] error handler\r\n";

  (void)HAL_UART_Transmit(&huart1, message, (uint16_t)(sizeof(message) - 1U), 100U);

  __disable_irq();
  while (1)
  {
    HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
    HAL_Delay(100U);
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
