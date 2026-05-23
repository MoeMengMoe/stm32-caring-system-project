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
#include "i2c.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

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

static void I2C1_ScanBus(void)
{
  char line[40];
  uint8_t found_count = 0U;
  uint8_t chip_id = 0U;
  const uint8_t id_register = 0xD0U;

  Debug_WriteLine("[INFO] i2c scan start");
  (void)snprintf(line, sizeof(line), "[INFO] i2c lines SCL=%u SDA=%u",
                 (unsigned int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8),
                 (unsigned int)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9));
  Debug_WriteLine(line);

  for (uint8_t address = 0x03U; address <= 0x77U; address++)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(address << 1U), 2U, 10U) == HAL_OK)
    {
      found_count++;
      (void)snprintf(line, sizeof(line), "[INFO] i2c device found: 0x%02X", address);
      Debug_WriteLine(line);
    }
  }

  if (found_count == 0U)
  {
    Debug_WriteLine("[WARN] no i2c device found");
  }

  if (HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(0x76U << 1U), id_register, I2C_MEMADD_SIZE_8BIT, &chip_id, 1U, 100U) == HAL_OK)
  {
    (void)snprintf(line, sizeof(line), "[INFO] bme280 0x76 chip id: 0x%02X", chip_id);
    Debug_WriteLine(line);
  }
  else if (HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(0x77U << 1U), id_register, I2C_MEMADD_SIZE_8BIT, &chip_id, 1U, 100U) == HAL_OK)
  {
    (void)snprintf(line, sizeof(line), "[INFO] bme280 0x77 chip id: 0x%02X", chip_id);
    Debug_WriteLine(line);
  }
  else
  {
    Debug_WriteLine("[WARN] bme280 chip id read failed");
  }

  Debug_WriteLine("[INFO] i2c scan done");
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
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  Debug_WriteLine("[INFO] system boot");
  I2C1_ScanBus();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
    HAL_Delay(500U);
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
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
