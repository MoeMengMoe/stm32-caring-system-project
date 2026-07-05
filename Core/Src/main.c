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
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_state_machine.h"
#include "sensor_mvp.h"
#include "status_display.h"
#include "rd03_v2.h"
#include <stdio.h>
#include <string.h>
#include "comm_wifi.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MAIN_STATUS_TX_PERIOD_MS 2000U
#define MAIN_DISPLAY_STATUS_PERIOD_MS 1000U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t s_relay_state_mask = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart){
    if(huart==&huart2){
        CommWifi_OnTxComplete();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
    if(huart==&huart2){
        CommWifi_OnRxComplete();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size){
    if(huart==&huart3){
        Rd03V2_OnUartRxEvent(huart, Size);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart){
    if(huart==&huart2){
        CommWifi_OnUartError();
    } else if(huart==&huart3){
        Rd03V2_OnUartError(huart);
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

static AppCommandType_t AppCommand_FromInt(int value)
{
  if ((value < (int)APP_COMMAND_TRIGGER_SCENARIO) || (value > (int)APP_COMMAND_SET_RELAY))
  {
    return APP_COMMAND_CLEAR_ALARM;
  }

  return (AppCommandType_t)value;
}

static AppScenario_t AppScenario_FromInt(int value)
{
  if ((value < (int)APP_SCENARIO_NONE) || (value > (int)APP_SCENARIO_OFFLINE_AUTONOMY))
  {
    return APP_SCENARIO_NONE;
  }

  return (AppScenario_t)value;
}

static void Handle_Relay_Command(const CommWifi_RelayCommand_t *cmd)
{
  char line[96];
  uint8_t relay_bit;
  CommWifi_Result result;

  if (cmd == NULL)
  {
    return;
  }

  relay_bit = (uint8_t)(1U << (cmd->relay_id - 1U));
  if (cmd->action == COMM_WIFI_RELAY_ACTION_ON)
  {
    s_relay_state_mask |= relay_bit;
  }
  else
  {
    s_relay_state_mask &= (uint8_t)(~relay_bit);
  }

  AppStateMachine_SetRelayStateMask(s_relay_state_mask);
  result = CommWifi_SendRelayResult(cmd->request_id,
                                    cmd->relay_id,
                                    COMM_WIFI_RELAY_RESULT_OK,
                                    cmd->action,
                                    COMM_WIFI_RELAY_REASON_NONE);

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] relay cmd id=%lu relay=%u action=%s mask=%u result=%d",
                 (unsigned long)cmd->request_id,
                 (unsigned int)cmd->relay_id,
                 (cmd->action == COMM_WIFI_RELAY_ACTION_ON) ? "ON" : "OFF",
                 (unsigned int)s_relay_state_mask,
                 (int)result);
  Debug_WriteLine(line);
}

static void Process_Cloud_Commands(uint32_t now)
{
  CommWifi_Command_t command;

  while (CommWifi_PollCommand(&command) == COMM_WIFI_OK)
  {
    if (command.type == COMM_WIFI_COMMAND_RELAY)
    {
      Handle_Relay_Command(&command.data.relay);
    }
    else if (command.type == COMM_WIFI_COMMAND_DEMO)
    {
      char line[96];
      AppStateMachine_HandleDemoCommand(command.data.demo.request_id,
                                        AppCommand_FromInt(command.data.demo.command_type),
                                        AppScenario_FromInt(command.data.demo.scenario),
                                        command.data.demo.value,
                                        now);
      (void)snprintf(line,
                     sizeof(line),
                     "[INFO] demo cmd id=%lu type=%d scenario=%d value=%d",
                     (unsigned long)command.data.demo.request_id,
                     command.data.demo.command_type,
                     command.data.demo.scenario,
                     command.data.demo.value);
      Debug_WriteLine(line);
    }
  }
}

static void Flush_App_Events(void)
{
  char line[128];
  AppEventRecord_t event;

  while (AppStateMachine_PollEvent(&event))
  {
    const CommWifi_Result result = CommWifi_SendEvent(event.event_id,
                                                       (int)event.scenario,
                                                       (int)event.event_type,
                                                       (int)event.trigger_source,
                                                       (int)event.state_before,
                                                       (int)event.state_after,
                                                       event.risk,
                                                       (int)event.result,
                                                       (int)event.network_state,
                                                       (int)event.power_state,
                                                       event.flags,
                                                       event.timestamp_ms);

    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] app event id=%lu scenario=%d type=%d state=%d->%d risk=%d tx=%d",
                   (unsigned long)event.event_id,
                   (int)event.scenario,
                   (int)event.event_type,
                   (int)event.state_before,
                   (int)event.state_after,
                   event.risk,
                   (int)result);
    Debug_WriteLine(line);
  }
}

static void Update_App(uint32_t now)
{
  SensorMvp_Status_t status;

  if (SensorMvp_GetStatus(&status) == HAL_OK)
  {
    AppStateMachine_Update(&status, now);
  }
  else
  {
    AppStateMachine_Update(NULL, now);
  }
}

static void Send_Status_ToWifi(void)
{
  char line[160];
  SensorMvp_Status_t status;
  AppStatus_t app_status;

  if (SensorMvp_GetStatus(&status) != HAL_OK)
  {
    Debug_WriteLine("[WARN] status get failed");
    return;
  }

  AppStateMachine_GetStatus(&app_status);

  const CommWifi_Result result = CommWifi_SendStatusV2(status.temperature_c,
                                                       status.humidity_pct,
                                                       status.gas,
                                                       status.presence,
                                                       app_status.risk,
                                                       app_status.relay_state_mask,
                                                       app_status.cloud_perm_mask);

  if (result == COMM_WIFI_OK)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] status tx temp=%.1f hum=%.1f gas=%d presence=%d risk=%d state=%s relay=%u env_valid=%u gas_valid=%u",
                   status.temperature_c,
                   status.humidity_pct,
                   status.gas,
                   status.presence,
                   app_status.risk,
                   AppStatus_ToDisplayText(&app_status),
                   (unsigned int)app_status.relay_state_mask,
                   (unsigned int)status.env_valid,
                   (unsigned int)status.gas_valid);
  }
  else
  {
    (void)snprintf(line, sizeof(line), "[WARN] status tx failed err=%d", (int)result);
  }

  Debug_WriteLine(line);
}

static void Update_Local_Display(void)
{
  SensorMvp_Status_t status;
  AppStatus_t app_status;

  if (SensorMvp_GetStatus(&status) == HAL_OK)
  {
    AppStateMachine_GetStatus(&app_status);
    StatusDisplay_SetStatus(&status, &app_status);
  }
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
  MX_SPI1_Init();
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
  (void)StatusDisplay_Init(&hspi1, Debug_WriteLine);
  AppStateMachine_Init();
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
    static uint32_t last_display_status_tick = 0U;
    uint32_t now = HAL_GetTick();

    SensorMvp_Update();
    Update_App(now);
    Process_Cloud_Commands(now);
    Flush_App_Events();
    StatusDisplay_Process();

    if ((now - last_display_status_tick) >= MAIN_DISPLAY_STATUS_PERIOD_MS)
    {
      last_display_status_tick = now;
      Update_Local_Display();
    }

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
