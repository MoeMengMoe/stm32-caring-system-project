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
#include "board_io.h"
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
#define MAIN_RELAY_ALERT_MASK ((uint8_t)(1U << 0U))
#define MAIN_RELAY_OFFLINE_MASK ((uint8_t)(1U << 1U))

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static uint8_t s_relay_state_mask = 0U;
static uint8_t s_relay_manual_mask = 0U;
static uint8_t s_relay_auto_mask = 0U;
static uint32_t s_debug_request_id = 9000UL;
static uint8_t s_debug_rx_byte = 0U;
static volatile uint8_t s_debug_rx_pending = 0U;

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
    if(huart==&huart1){
        s_debug_rx_pending = 1U;
    }
    else if(huart==&huart2){
        CommWifi_OnRxComplete();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size){
    if(huart==&huart3){
        Rd03V2_OnUartRxEvent(huart, Size);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart){
    if(huart==&huart1){
        (void)HAL_UART_Receive_IT(&huart1, &s_debug_rx_byte, 1U);
    } else if(huart==&huart2){
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

static void Apply_Relay_Mask(uint8_t relay_mask)
{
  s_relay_state_mask = (uint8_t)(relay_mask & 0x0FU);
  BoardIo_SetRelayMask(s_relay_state_mask);
  AppStateMachine_SetRelayStateMask(s_relay_state_mask);
}

static uint8_t Build_Auto_Relay_Mask(const AppStatus_t *status)
{
  uint8_t mask = 0U;

  if (status == NULL)
  {
    return 0U;
  }

  if ((status->state == APP_STATE_ACK_WAIT) ||
      (status->state == APP_STATE_ALARM) ||
      (status->state == APP_STATE_NO_RESPONSE))
  {
    mask |= MAIN_RELAY_ALERT_MASK;
  }

  if (status->network_state == APP_NETWORK_OFFLINE)
  {
    mask |= MAIN_RELAY_OFFLINE_MASK;
  }

  return mask;
}

static void Update_Relay_Output(void)
{
  Apply_Relay_Mask((uint8_t)(s_relay_manual_mask | s_relay_auto_mask));
}

static void Update_Relay_Automation(void)
{
  char line[128];
  AppStatus_t app_status;
  uint8_t next_auto_mask;

  AppStateMachine_GetStatus(&app_status);
  next_auto_mask = Build_Auto_Relay_Mask(&app_status);
  if (next_auto_mask == s_relay_auto_mask)
  {
    return;
  }

  s_relay_auto_mask = next_auto_mask;
  Update_Relay_Output();

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] relay auto manual=%u auto=%u output=%u state=%s risk=%d",
                 (unsigned int)s_relay_manual_mask,
                 (unsigned int)s_relay_auto_mask,
                 (unsigned int)s_relay_state_mask,
                 AppStatus_ToDisplayText(&app_status),
                 app_status.risk);
  Debug_WriteLine(line);
}

static void Handle_Relay_Command(const CommWifi_RelayCommand_t *cmd)
{
  char line[128];
  uint8_t relay_bit;
  CommWifi_RelayAction_t final_state;
  CommWifi_RelayResult_t relay_result;
  CommWifi_RelayReason_t relay_reason;
  CommWifi_Result result;

  if (cmd == NULL)
  {
    return;
  }

  relay_bit = (uint8_t)(1U << (cmd->relay_id - 1U));
  if (cmd->action == COMM_WIFI_RELAY_ACTION_ON)
  {
    s_relay_manual_mask = (uint8_t)(s_relay_manual_mask | relay_bit);
  }
  else
  {
    s_relay_manual_mask = (uint8_t)(s_relay_manual_mask & (uint8_t)(~relay_bit));
  }

  Update_Relay_Output();
  final_state = ((s_relay_state_mask & relay_bit) != 0U) ?
                COMM_WIFI_RELAY_ACTION_ON :
                COMM_WIFI_RELAY_ACTION_OFF;
  relay_result = (final_state == cmd->action) ?
                 COMM_WIFI_RELAY_RESULT_OK :
                 COMM_WIFI_RELAY_RESULT_DENY;
  relay_reason = (relay_result == COMM_WIFI_RELAY_RESULT_OK) ?
                 COMM_WIFI_RELAY_REASON_NONE :
                 COMM_WIFI_RELAY_REASON_BUSY;

  result = CommWifi_SendRelayResult(cmd->request_id,
                                    cmd->relay_id,
                                    relay_result,
                                    final_state,
                                    relay_reason);

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] relay cmd id=%lu relay=%u request=%s final=%s manual=%u auto=%u output=%u result=%d",
                 (unsigned long)cmd->request_id,
                 (unsigned int)cmd->relay_id,
                 (cmd->action == COMM_WIFI_RELAY_ACTION_ON) ? "ON" : "OFF",
                 (final_state == COMM_WIFI_RELAY_ACTION_ON) ? "ON" : "OFF",
                 (unsigned int)s_relay_manual_mask,
                 (unsigned int)s_relay_auto_mask,
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
      Update_Relay_Automation();
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

static void Update_BoardIo(uint32_t now)
{
  AppStatus_t app_status;
  BoardIo_Events_t events;

  AppStateMachine_GetStatus(&app_status);
  BoardIo_Update(now, &app_status, &events);

  if (events.sos_pressed != 0U)
  {
    Debug_WriteLine("[INFO] local sos button pressed");
    AppStateMachine_HandleLocalSos(now);
  }

  if (events.ack_pressed != 0U)
  {
    Debug_WriteLine("[INFO] local ack button pressed");
    AppStateMachine_HandleLocalAck(now);
  }
}

static void Print_DebugConsole_Help(void)
{
  Debug_WriteLine("[INFO] console: s=SOS a=ACK c=clear 1=fall-demo 2=still-demo o=offline n=online h=help");
  Debug_WriteLine("[INFO] console: r/t/y/u=toggle manual relay1/2/3/4 p=status");
}

static void DebugConsole_StartRx(void)
{
  HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);

  if (HAL_UART_Receive_IT(&huart1, &s_debug_rx_byte, 1U) == HAL_OK)
  {
    Debug_WriteLine("[INFO] console rx armed");
  }
  else
  {
    Debug_WriteLine("[WARN] console rx arm failed");
  }
}

static void Print_DebugConsole_Status(void)
{
  char line[128];
  SensorMvp_Status_t sensor_status;
  AppStatus_t app_status;

  AppStateMachine_GetStatus(&app_status);
  if (SensorMvp_GetStatus(&sensor_status) == HAL_OK)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] console status state=%s risk=%d relay=%u manual=%u auto=%u temp=%.1f hum=%.1f gas=%d presence=%d radar=%d cm=%d",
                   AppStatus_ToDisplayText(&app_status),
                   app_status.risk,
                   (unsigned int)app_status.relay_state_mask,
                   (unsigned int)s_relay_manual_mask,
                   (unsigned int)s_relay_auto_mask,
                   sensor_status.temperature_c,
                   sensor_status.humidity_pct,
                   sensor_status.gas,
                   sensor_status.presence,
                   sensor_status.radar_presence,
                   sensor_status.radar_distance_cm);
  }
  else
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] console status state=%s risk=%d relay=%u manual=%u auto=%u sensor=invalid",
                   AppStatus_ToDisplayText(&app_status),
                   app_status.risk,
                   (unsigned int)app_status.relay_state_mask,
                   (unsigned int)s_relay_manual_mask,
                   (unsigned int)s_relay_auto_mask);
  }

  Debug_WriteLine(line);
}

static void Toggle_DebugRelay(uint8_t relay_id)
{
  char line[128];
  uint8_t relay_bit;

  if ((relay_id == 0U) || (relay_id > 4U))
  {
    return;
  }

  relay_bit = (uint8_t)(1U << (relay_id - 1U));
  s_relay_manual_mask = (uint8_t)(s_relay_manual_mask ^ relay_bit);
  Update_Relay_Output();

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] console relay%u manual=%s manual=%u auto=%u output=%u",
                 (unsigned int)relay_id,
                 ((s_relay_manual_mask & relay_bit) != 0U) ? "ON" : "OFF",
                 (unsigned int)s_relay_manual_mask,
                 (unsigned int)s_relay_auto_mask,
                 (unsigned int)s_relay_state_mask);
  Debug_WriteLine(line);
}

static void Handle_DebugConsole_Command(uint8_t command, uint32_t now)
{
  char line[48];

  if ((command >= 32U) && (command <= 126U))
  {
    (void)snprintf(line, sizeof(line), "[INFO] console rx=%c", (char)command);
  }
  else
  {
    (void)snprintf(line, sizeof(line), "[INFO] console rx=0x%02X", (unsigned int)command);
  }
  Debug_WriteLine(line);

  switch (command)
  {
    case 's':
    case 'S':
      Debug_WriteLine("[INFO] console trigger local SOS");
      AppStateMachine_HandleLocalSos(now);
      break;

    case 'a':
    case 'A':
      Debug_WriteLine("[INFO] console trigger local ACK");
      AppStateMachine_HandleLocalAck(now);
      break;

    case 'c':
    case 'C':
      Debug_WriteLine("[INFO] console clear alarm");
      AppStateMachine_HandleDemoCommand(s_debug_request_id++,
                                        APP_COMMAND_CLEAR_ALARM,
                                        APP_SCENARIO_NONE,
                                        1,
                                        now);
      break;

    case '1':
      Debug_WriteLine("[INFO] console trigger scenario SOS/FALL");
      AppStateMachine_HandleDemoCommand(s_debug_request_id++,
                                        APP_COMMAND_TRIGGER_SCENARIO,
                                        APP_SCENARIO_SOS_OR_FALL_SIM,
                                        1,
                                        now);
      break;

    case '2':
      Debug_WriteLine("[INFO] console trigger scenario LONG_STILL");
      AppStateMachine_HandleDemoCommand(s_debug_request_id++,
                                        APP_COMMAND_TRIGGER_SCENARIO,
                                        APP_SCENARIO_LONG_STILL_NO_RESPONSE,
                                        1,
                                        now);
      break;

    case 'o':
    case 'O':
      Debug_WriteLine("[INFO] console simulate network offline");
      AppStateMachine_HandleDemoCommand(s_debug_request_id++,
                                        APP_COMMAND_SIMULATE_NETWORK,
                                        APP_SCENARIO_OFFLINE_AUTONOMY,
                                        0,
                                        now);
      break;

    case 'n':
    case 'N':
      Debug_WriteLine("[INFO] console simulate network restored");
      AppStateMachine_HandleDemoCommand(s_debug_request_id++,
                                        APP_COMMAND_SIMULATE_NETWORK,
                                        APP_SCENARIO_OFFLINE_AUTONOMY,
                                        1,
                                        now);
      break;

    case 'p':
    case 'P':
      Print_DebugConsole_Status();
      break;

    case 'r':
    case 'R':
      Toggle_DebugRelay(1U);
      break;

    case 't':
    case 'T':
      Toggle_DebugRelay(2U);
      break;

    case 'y':
    case 'Y':
      Toggle_DebugRelay(3U);
      break;

    case 'u':
    case 'U':
      Toggle_DebugRelay(4U);
      break;

    case 'h':
    case 'H':
    case '?':
      Print_DebugConsole_Help();
      break;

    case '\r':
    case '\n':
      break;

    default:
      Debug_WriteLine("[WARN] console unknown command, press h for help");
      break;
  }
}

static void Update_DebugConsole(uint32_t now)
{
  uint8_t command;

  if (s_debug_rx_pending == 0U)
  {
    return;
  }

  s_debug_rx_pending = 0U;
  command = s_debug_rx_byte;
  (void)HAL_UART_Receive_IT(&huart1, &s_debug_rx_byte, 1U);
  Handle_DebugConsole_Command(command, now);
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
  BoardIo_Init(Debug_WriteLine);
  s_relay_manual_mask = 0U;
  s_relay_auto_mask = 0U;
  Apply_Relay_Mask(0U);
  SensorMvp_Init(Debug_WriteLine);
  DebugConsole_StartRx();
  Print_DebugConsole_Help();

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
    Update_BoardIo(now);
    Update_DebugConsole(now);
    Process_Cloud_Commands(now);
    Update_Relay_Automation();
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
