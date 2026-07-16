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
#include "icache.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_state_machine.h"
#include "board_io.h"
#include "edge_ai.h"
#include "sensor_mvp.h"
#include "scene_engine.h"
#include "status_display.h"
#include "rd03_v2.h"
#include <stdio.h>
#include <string.h>
#include "comm_wifi.h"
#include "comm_local.h"
#include "tft_lcd.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define MAIN_STATUS_TX_PERIOD_MS 2000U
#define MAIN_DISPLAY_STATUS_PERIOD_MS 1000U
#define MAIN_AI_SAMPLE_PERIOD_MS 500U
#define MAIN_BUZZER_TEST_MS 1000U
#define MAIN_RELAY_ALERT_MASK ((uint8_t)(1U << 0U))
#define MAIN_RELAY_OFFLINE_MASK ((uint8_t)(1U << 1U))
#define MAIN_RELAY_GAS_VENT_MASK ((uint8_t)(1U << 2U))
#define MAIN_GAS_WARN_PPM_EST 100U
#define MAIN_WIFI_HEARTBEAT_TIMEOUT_MS 8000U

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
static uint8_t s_display_frozen = 0U;
static uint8_t s_tft_inversion = 0U;
static uint32_t s_buzzer_test_until = 0UL;
static uint32_t s_ai_session_id = 0UL;
static const char *s_ai_session_label = "idle";
static uint32_t s_wifi_last_heartbeat_tick = 0UL;
static uint8_t s_wifi_heartbeat_seen = 0U;
static uint8_t s_wifi_link_online = 0U;

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
    else if(huart==&huart4){
        CommLocal_OnRxComplete();
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
    } else if(huart==&huart4){
        CommLocal_OnUartError();
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
  if ((value < (int)APP_COMMAND_TRIGGER_SCENARIO) || (value > (int)APP_COMMAND_DEBUG_SET_GAS_PPM_OFFSET))
  {
    return APP_COMMAND_CLEAR_ALARM;
  }

  return (AppCommandType_t)value;
}

static AppScenario_t AppScenario_FromInt(int value)
{
  if (value == (int)APP_SCENARIO_GAS_RISK)
  {
    return APP_SCENARIO_GAS_RISK;
  }

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

  if ((status->scenario == APP_SCENARIO_GAS_RISK) &&
      ((status->state == APP_STATE_ACK_WAIT) ||
       (status->state == APP_STATE_ALARM) ||
       (status->state == APP_STATE_NO_RESPONSE)))
  {
    mask |= MAIN_RELAY_GAS_VENT_MASK;
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

static void Clear_Relay_Manual_Mask(const char *source)
{
  char line[128];
  const uint8_t previous_manual_mask = s_relay_manual_mask;

  s_relay_manual_mask = 0U;
  Update_Relay_Automation();
  Update_Relay_Output();

  if (previous_manual_mask != 0U)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] %s relay manual cleared previous=%u auto=%u output=%u",
                   (source != NULL) ? source : "ack",
                   (unsigned int)previous_manual_mask,
                   (unsigned int)s_relay_auto_mask,
                   (unsigned int)s_relay_state_mask);
    Debug_WriteLine(line);
  }
}

static void Stop_Gas_Demo_Source(const char *source)
{
  char line[96];

  if ((SensorMvp_IsGasFlameDemoEnabled() == 0U) &&
      (SensorMvp_GetGasPpmDebugOffset() == 0U))
  {
    return;
  }

  SensorMvp_StopGasFlameDemo();
  SensorMvp_SetGasPpmDebugOffset(0U);
  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] %s gas demo cleared",
                 (source != NULL) ? source : "app");
  Debug_WriteLine(line);
}

static void Handle_Relay_Command(const CommWifi_RelayCommand_t *cmd, const char *source)
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
                 "[INFO] %s relay cmd id=%lu relay=%u request=%s final=%s manual=%u auto=%u output=%u result=%d",
                 (source != NULL) ? source : "remote",
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

static void Apply_Wifi_Link_State(uint8_t online, uint32_t seq, uint32_t now, const char *source)
{
  char line[160];
  AppStatus_t app_status;

  AppStateMachine_GetStatus(&app_status);

  if (online != 0U)
  {
    if (app_status.network_state == APP_NETWORK_OFFLINE)
    {
      AppStateMachine_HandleDemoCommand(seq,
                                        APP_COMMAND_SIMULATE_NETWORK,
                                        APP_SCENARIO_OFFLINE_AUTONOMY,
                                        1,
                                        now);
      Update_Relay_Automation();
      Debug_WriteLine("[INFO] network restored by ESP heartbeat");
    }
    s_wifi_link_online = 1U;
  }
  else
  {
    if (app_status.network_state != APP_NETWORK_OFFLINE)
    {
      AppStateMachine_HandleDemoCommand(seq,
                                        APP_COMMAND_SIMULATE_NETWORK,
                                        APP_SCENARIO_OFFLINE_AUTONOMY,
                                        0,
                                        now);
      Update_Relay_Automation();
      Debug_WriteLine("[WARN] network offline by ESP heartbeat");
    }
    s_wifi_link_online = 0U;
  }

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] %s heartbeat seq=%lu online=%u",
                 (source != NULL) ? source : "wifi",
                 (unsigned long)seq,
                 (unsigned int)online);
  Debug_WriteLine(line);
}

static void Handle_Network_Heartbeat(const CommWifi_NetworkHeartbeat_t *heartbeat, uint32_t now, const char *source)
{
  char line[192];
  uint8_t online;
  uint8_t first_heartbeat;
  uint8_t previous_online;

  if (heartbeat == NULL)
  {
    return;
  }

  online = ((heartbeat->online != 0U) &&
            (heartbeat->wifi_connected != 0U) &&
            (heartbeat->mqtt_connected != 0U)) ? 1U : 0U;
  first_heartbeat = (s_wifi_heartbeat_seen == 0U) ? 1U : 0U;
  previous_online = s_wifi_link_online;
  s_wifi_heartbeat_seen = 1U;
  s_wifi_last_heartbeat_tick = now;

  Apply_Wifi_Link_State(online, heartbeat->seq, now, source);

  if ((first_heartbeat != 0U) || (previous_online != online))
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] %s link detail seq=%lu online=%u wifi=%u mqtt=%u",
                   (source != NULL) ? source : "wifi",
                   (unsigned long)heartbeat->seq,
                   (unsigned int)heartbeat->online,
                   (unsigned int)heartbeat->wifi_connected,
                   (unsigned int)heartbeat->mqtt_connected);
    Debug_WriteLine(line);
  }
}

static void Check_Wifi_Heartbeat_Timeout(uint32_t now)
{
  if ((s_wifi_heartbeat_seen == 0U) || (s_wifi_link_online == 0U))
  {
    return;
  }

  if ((now - s_wifi_last_heartbeat_tick) >= MAIN_WIFI_HEARTBEAT_TIMEOUT_MS)
  {
    Debug_WriteLine("[WARN] ESP heartbeat timeout, mark network offline");
    Apply_Wifi_Link_State(0U, 0UL, now, "wifi-timeout");
  }
}

static void Handle_Protocol_Command(const CommWifi_Command_t *command, uint32_t now, const char *source)
{
  char line[128];

  if (command == NULL)
  {
    return;
  }

  if (command->type == COMM_WIFI_COMMAND_RELAY)
  {
    Handle_Relay_Command(&command->data.relay, source);
  }
  else if (command->type == COMM_WIFI_COMMAND_DEMO)
  {
    const AppCommandType_t app_command = AppCommand_FromInt(command->data.demo.command_type);
    const AppScenario_t app_scenario = AppScenario_FromInt(command->data.demo.scenario);

    if (app_command == APP_COMMAND_DEBUG_SET_GAS_PPM_OFFSET)
    {
      uint16_t offset = 0U;

      if ((app_scenario == APP_SCENARIO_GAS_RISK) && (command->data.demo.value == 1))
      {
        SensorMvp_StartGasFlameDemo();
        AppStateMachine_HandleDemoCommand(command->data.demo.request_id,
                                          APP_COMMAND_TRIGGER_SCENARIO,
                                          APP_SCENARIO_GAS_RISK,
                                          1,
                                          now);
        Update_Relay_Automation();
        (void)snprintf(line,
                       sizeof(line),
                       "[INFO] %s gas flame-rise demo start and trigger id=%lu scenario=%d value=%d",
                       (source != NULL) ? source : "remote",
                       (unsigned long)command->data.demo.request_id,
                       command->data.demo.scenario,
                       command->data.demo.value);
        Debug_WriteLine(line);
        return;
      }

      if (command->data.demo.value > 0)
      {
        offset = (command->data.demo.value > 9999) ? 9999U : (uint16_t)command->data.demo.value;
      }

      SensorMvp_StopGasFlameDemo();
      SensorMvp_SetGasPpmDebugOffset(offset);
      (void)snprintf(line,
                     sizeof(line),
                     "[INFO] %s gas ppm debug offset=%u id=%lu scenario=%d value=%d",
                     (source != NULL) ? source : "remote",
                     (unsigned int)offset,
                     (unsigned long)command->data.demo.request_id,
                     command->data.demo.scenario,
                     command->data.demo.value);
      Debug_WriteLine(line);
      return;
    }

    if ((app_command == APP_COMMAND_TRIGGER_SCENARIO) &&
        (app_scenario == APP_SCENARIO_GAS_RISK))
    {
      SensorMvp_StartGasFlameDemo();
      Debug_WriteLine("[INFO] gas scenario trigger also starts flame-rise ppm demo");
    }

    AppStateMachine_HandleDemoCommand(command->data.demo.request_id,
                                      app_command,
                                      app_scenario,
                                      command->data.demo.value,
                                      now);
    if ((app_command == APP_COMMAND_USER_ACK) || (app_command == APP_COMMAND_CLEAR_ALARM))
    {
      Stop_Gas_Demo_Source(source);
      Clear_Relay_Manual_Mask(source);
    }
    else
    {
      Update_Relay_Automation();
    }
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] %s demo cmd id=%lu type=%d scenario=%d value=%d",
                   (source != NULL) ? source : "remote",
                   (unsigned long)command->data.demo.request_id,
                   command->data.demo.command_type,
                   command->data.demo.scenario,
                   command->data.demo.value);
    Debug_WriteLine(line);
  }
  else if (command->type == COMM_WIFI_COMMAND_NETWORK_HEARTBEAT)
  {
    Handle_Network_Heartbeat(&command->data.heartbeat, now, source);
  }
}

static void Process_Cloud_Commands(uint32_t now)
{
  CommWifi_Command_t command;

  while (CommWifi_PollCommand(&command) == COMM_WIFI_OK)
  {
    Handle_Protocol_Command(&command, now, "cloud");
  }
}

static void Handle_Voice_Risk_Command(const CommWifi_VoiceRiskCommand_t *command, uint32_t now)
{
  char line[128];
  AppStatus_t app_status;

  if (command == NULL)
  {
    return;
  }

  AppStateMachine_HandleVoiceRisk(command->request_id, command->risk_level, now);
  if (command->risk_level == 0U)
  {
    Clear_Relay_Manual_Mask("voice");
  }
  else
  {
    Update_Relay_Automation();
  }
  AppStateMachine_GetStatus(&app_status);

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] voice risk cmd id=%lu level=%u state=%s risk=%d relay=%u manual=%u auto=%u",
                 (unsigned long)command->request_id,
                 (unsigned int)command->risk_level,
                 AppStatus_ToDisplayText(&app_status),
                 app_status.risk,
                 (unsigned int)app_status.relay_state_mask,
                 (unsigned int)s_relay_manual_mask,
                 (unsigned int)s_relay_auto_mask);
  Debug_WriteLine(line);
}

static void Process_LocalVoice_Commands(uint32_t now)
{
  CommWifi_Command_t command;

  while (CommLocal_PollCommand(&command) == COMM_WIFI_OK)
  {
    if (command.type == COMM_WIFI_COMMAND_VOICE_RISK)
    {
      Handle_Voice_Risk_Command(&command.data.voice_risk, now);
    }
    else
    {
      Handle_Protocol_Command(&command, now, "voice");
    }
  }
}

static void Flush_App_Events(void)
{
  char line[256];
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
                   "[INFO] app event id=%lu scenario=%s(%d) type=%s(%d) source=%s state=%s->%s risk=%d result=%s tx=%d",
                   (unsigned long)event.event_id,
                   AppScenario_ToShortText(event.scenario),
                   (int)event.scenario,
                   AppEventType_ToText(event.event_type),
                   (int)event.event_type,
                   AppTriggerSource_ToText(event.trigger_source),
                   AppState_ToText(event.state_before),
                   AppState_ToText(event.state_after),
                   event.risk,
                   AppResult_ToText(event.result),
                   (int)result);
    Debug_WriteLine(line);

    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] app event detail network=%s power=%s flags=0x%08lX t=%lu",
                   AppNetworkState_ToText(event.network_state),
                   AppPowerState_ToText(event.power_state),
                   (unsigned long)event.flags,
                   (unsigned long)event.timestamp_ms);
    Debug_WriteLine(line);
  }
}

static void Update_App(uint32_t now)
{
  SensorMvp_Status_t status;
  AppStatus_t app_status;
  EdgeAi_Result_t edge_ai;

  if (SensorMvp_GetStatus(&status) == HAL_OK)
  {
    AppStateMachine_GetStatus(&app_status);
    (void)EdgeAi_UpdateIfDue(&status, &app_status, now);
    EdgeAi_GetResult(&edge_ai);
    AppStateMachine_SetEdgeAiHint((edge_ai.stale == 0U) ? edge_ai.valid : 0U,
                                  (uint8_t)edge_ai.scene,
                                  (uint8_t)edge_ai.raw_scene,
                                  edge_ai.risk_level,
                                  edge_ai.confidence,
                                  edge_ai.stability,
                                  edge_ai.evidence_mask,
                                  edge_ai.anomaly_score,
                                  edge_ai.trend_score);
    AppStateMachine_Update(&status, now);
    AppStateMachine_GetStatus(&app_status);
    SceneEngine_Update(&status, &app_status, now);
  }
  else
  {
    AppStateMachine_GetStatus(&app_status);
    (void)EdgeAi_UpdateIfDue(NULL, &app_status, now);
    EdgeAi_GetResult(&edge_ai);
    AppStateMachine_SetEdgeAiHint((edge_ai.stale == 0U) ? edge_ai.valid : 0U,
                                  (uint8_t)edge_ai.scene,
                                  (uint8_t)edge_ai.raw_scene,
                                  edge_ai.risk_level,
                                  edge_ai.confidence,
                                  edge_ai.stability,
                                  edge_ai.evidence_mask,
                                  edge_ai.anomaly_score,
                                  edge_ai.trend_score);
    AppStateMachine_Update(NULL, now);
    AppStateMachine_GetStatus(&app_status);
    SceneEngine_Update(NULL, &app_status, now);
  }
}

static void Update_BoardIo(uint32_t now)
{
  AppStatus_t app_status;
  BoardIo_Events_t events;

  AppStateMachine_GetStatus(&app_status);
  if (s_buzzer_test_until != 0UL)
  {
    if ((int32_t)(now - s_buzzer_test_until) < 0)
    {
      app_status.state = APP_STATE_ALARM;
    }
    else
    {
      s_buzzer_test_until = 0UL;
    }
  }
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
    Clear_Relay_Manual_Mask("local ack");
  }
}

static void Print_DebugConsole_Help(void)
{
  Debug_WriteLine("[INFO] console: s=SOS a=ACK c=clear 1=fall-demo 2=still-demo 3=gas-demo o=offline n=online h=help");
  Debug_WriteLine("[INFO] console: 4=gas flame-rise demo 5=gas+350ppm 0=clear-gas-demo 6/7/8=voice-risk1/2/3 9=voice-clear");
  Debug_WriteLine("[INFO] console: r/t/y/u=relay b=buzzer p=status l=local-voice k=radar-cal d=display-freeze i=tft-invert");
  Debug_WriteLine("[INFO] console: ai labels e=env w=walk f=fall j=still g=gas v=voice x=idle");
}

static const char *Get_RiskSource_Text(const SensorMvp_Status_t *sensor, const AppStatus_t *app_status)
{
  if (app_status == NULL)
  {
    return "UNKNOWN";
  }

  if ((app_status->state == APP_STATE_ACK_WAIT) ||
      (app_status->state == APP_STATE_ALARM) ||
      (app_status->state == APP_STATE_NO_RESPONSE))
  {
    switch (app_status->last_trigger_source)
    {
      case APP_TRIGGER_VOICE:
        return "VOICE_ACK";
      case APP_TRIGGER_SENSOR:
        return "GAS_ACK";
      case APP_TRIGGER_RADAR:
        return "RADAR_ACK";
      case APP_TRIGGER_BUTTON:
        return "BUTTON_ACK";
      case APP_TRIGGER_REMOTE:
        return "REMOTE_ACK";
      case APP_TRIGGER_AI:
        return "EDGE_AI_ACK";
      default:
        return "STATE_ACK";
    }
  }

  if ((app_status->last_trigger_source == APP_TRIGGER_VOICE) && (app_status->risk > 0))
  {
    return "VOICE";
  }

  if ((app_status->last_trigger_source == APP_TRIGGER_AI) && (app_status->risk > 0))
  {
    return "EDGE_AI";
  }

  if ((app_status->risk <= 0) && (app_status->state != APP_STATE_NOTICE))
  {
    return "NONE";
  }

  if ((sensor != NULL) &&
      (sensor->gas_valid != 0U) &&
      (sensor->gas_ppm_est >= MAIN_GAS_WARN_PPM_EST))
  {
    return "GAS";
  }

  if ((app_status->edge_ai_valid != 0U) &&
      (app_status->edge_ai_risk > 0U) &&
      (app_status->edge_ai_confidence >= 60U) &&
      (app_status->edge_ai_stability >= 2U))
  {
    return "EDGE_AI";
  }

  if (app_status->network_state == APP_NETWORK_OFFLINE)
  {
    return "NETWORK";
  }

  if ((sensor != NULL) && (sensor->presence != 0))
  {
    if (sensor->radar_presence != 0U)
    {
      return "RADAR_UART";
    }
    if (sensor->rd03_ot2_presence != 0U)
    {
      return "RD03_OT2";
    }
    if (sensor->pir_presence != 0U)
    {
      return "PIR";
    }
    return "PRESENCE";
  }

  if (app_status->state == APP_STATE_NOTICE)
  {
    return "NOTICE";
  }

  return "NONE";
}

static void Handle_DebugVoiceRisk(uint8_t risk_level, uint32_t now)
{
  CommWifi_VoiceRiskCommand_t command;

  command.request_id = s_debug_request_id++;
  command.risk_level = risk_level;
  Handle_Voice_Risk_Command(&command, now);
}

static void Set_AiSession_Label(const char *label)
{
  char line[128];

  if (label == NULL)
  {
    label = "idle";
  }

  s_ai_session_label = label;
  if (strcmp(label, "idle") == 0)
  {
    s_ai_session_id = 0UL;
  }
  else
  {
    s_ai_session_id++;
  }

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] ai label session=%lu label=%s",
                 (unsigned long)s_ai_session_id,
                 s_ai_session_label);
  Debug_WriteLine(line);
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
  char line[640];
  SensorMvp_Status_t sensor_status;
  AppStatus_t app_status;
  SceneEngine_Status_t scene_status;
  EdgeAi_Result_t edge_ai_result;
  const SensorMvp_Status_t *sensor_for_risk = NULL;
  uint32_t edge_ai_age_ms = 0UL;

  AppStateMachine_GetStatus(&app_status);
  SceneEngine_GetStatus(&scene_status);
  EdgeAi_GetResult(&edge_ai_result);
  if (edge_ai_result.sequence != 0UL)
  {
    edge_ai_age_ms = HAL_GetTick() - edge_ai_result.last_update_ms;
  }
  if (SensorMvp_GetStatus(&sensor_status) == HAL_OK)
  {
    sensor_for_risk = &sensor_status;
  }

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] console app state=%s scenario=%s risk=%d risk_src=%s ack_ms=%lu relay=%u manual=%u auto=%u ai_session=%lu ai_label=%s edge_ai=%s raw=%s risk=%u conf=%u stab=%u score=%u trend=%u ev=0x%02X ai_ms=%lu ai_max=%lu ai_age=%lu ai_skip=%lu ai_stale=%u",
                 AppStatus_ToDisplayText(&app_status),
                 AppScenario_ToShortText(app_status.scenario),
                 app_status.risk,
                 Get_RiskSource_Text(sensor_for_risk, &app_status),
                 (unsigned long)app_status.ack_remaining_ms,
                 (unsigned int)app_status.relay_state_mask,
                 (unsigned int)s_relay_manual_mask,
                 (unsigned int)s_relay_auto_mask,
                 (unsigned long)s_ai_session_id,
                 s_ai_session_label,
                 EdgeAi_SceneToText((EdgeAiScene_t)app_status.edge_ai_scene),
                 EdgeAi_SceneToText((EdgeAiScene_t)app_status.edge_ai_raw_scene),
                 (unsigned int)app_status.edge_ai_risk,
                 (unsigned int)app_status.edge_ai_confidence,
                 (unsigned int)app_status.edge_ai_stability,
                 (unsigned int)app_status.edge_ai_anomaly_score,
                 (unsigned int)app_status.edge_ai_trend_score,
                 (unsigned int)app_status.edge_ai_evidence_mask,
                 (unsigned long)edge_ai_result.last_run_ms,
                 (unsigned long)edge_ai_result.max_run_ms,
                 (unsigned long)edge_ai_age_ms,
                 (unsigned long)edge_ai_result.skipped_count,
                 (unsigned int)edge_ai_result.stale);
  Debug_WriteLine(line);

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] console scene top=%s action=%s severity=%u conf=%u count=%u mask=0x%08lX evidence=%u/%u",
                 SceneEngine_IdToText(scene_status.top.id),
                 SceneEngine_ActionToText(scene_status.top.action_hint),
                 (unsigned int)scene_status.top.severity,
                 (unsigned int)scene_status.top.confidence,
                 (unsigned int)scene_status.signal_count,
                 (unsigned long)scene_status.scene_mask,
                 (unsigned int)scene_status.top.evidence_primary,
                 (unsigned int)scene_status.top.evidence_secondary);
  Debug_WriteLine(line);

  if (sensor_for_risk != NULL)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] console sensor env_valid=%u gas_valid=%u temp=%.1f hum=%.1f gas_mv=%d base=%u delta=%u ppm=%u ppm_dbg=%u presence=%d",
                   (unsigned int)sensor_status.env_valid,
                   (unsigned int)sensor_status.gas_valid,
                   sensor_status.temperature_c,
                   sensor_status.humidity_pct,
                   sensor_status.gas,
                   (unsigned int)sensor_status.gas_baseline_mv,
                   (unsigned int)sensor_status.gas_delta_mv,
                   (unsigned int)sensor_status.gas_ppm_est,
                   (unsigned int)SensorMvp_GetGasPpmDebugOffset(),
                   sensor_status.presence);
    Debug_WriteLine(line);

    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] console radar pir=%u rd03_ot2=%u valid=%u presence=%u raw_cm=%u stable_cm=%u q=%u near=%u jump=%u zone=%u peak_gate=%u peak_cm=%u active=%u motion=%lu energy=%lu still=%lu occupied=%lu age_ms=%lu",
                   (unsigned int)sensor_status.pir_presence,
                   (unsigned int)sensor_status.rd03_ot2_presence,
                   (unsigned int)sensor_status.radar_valid,
                   (unsigned int)sensor_status.radar_presence,
                   (unsigned int)sensor_status.radar_raw_distance_cm,
                   (unsigned int)sensor_status.radar_distance_cm,
                   (unsigned int)sensor_status.radar_distance_quality,
                   (unsigned int)sensor_status.radar_near_blind,
                   (unsigned int)sensor_status.radar_distance_unstable,
                   (unsigned int)sensor_status.radar_zone,
                   (unsigned int)sensor_status.radar_peak_gate,
                   (unsigned int)sensor_status.radar_peak_gate_cm,
                   (unsigned int)sensor_status.radar_active_gate_count,
                   (unsigned long)sensor_status.radar_motion_score,
                   (unsigned long)sensor_status.radar_energy_sum,
                   (unsigned long)sensor_status.radar_still_seconds,
                   (unsigned long)sensor_status.radar_occupied_seconds,
                   (unsigned long)sensor_status.radar_last_seen_age_ms);
  }
  else
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] console sensor=invalid");
  }

  Debug_WriteLine(line);
}

static void Print_LocalVoice_Diagnostics(void)
{
  char line[256];
  CommLocal_Diagnostics_t diag;

  CommLocal_GetDiagnostics(&diag);

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] local voice rx bytes=%lu lines=%lu queued_total=%lu queued_now=%u parse_ok=%lu parse_fail=%lu armed=%u rearm=%u",
                 (unsigned long)diag.rx_bytes,
                 (unsigned long)diag.rx_lines,
                 (unsigned long)diag.rx_queued,
                 (unsigned int)diag.queued_lines,
                 (unsigned long)diag.parse_ok,
                 (unsigned long)diag.parse_fail,
                 (unsigned int)diag.rx_armed,
                 (unsigned int)diag.rearm_needed);
  Debug_WriteLine(line);

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] local voice err filtered=%lu resync=%lu queue_full=%lu drop_old=%lu overflow=%lu uart_err=%lu restart_fail=%lu restart_ok=%lu service_rearm=%lu last_err=0x%lX last_byte=0x%02X current_len=%u",
                 (unsigned long)diag.rx_filtered,
                 (unsigned long)diag.rx_resynced,
                 (unsigned long)diag.rx_queue_full,
                 (unsigned long)diag.rx_drop_oldest,
                 (unsigned long)diag.rx_overflow,
                 (unsigned long)diag.rx_uart_errors,
                 (unsigned long)diag.rx_restart_fail,
                 (unsigned long)diag.rx_restart_ok,
                 (unsigned long)diag.rx_service_rearm,
                 (unsigned long)diag.last_uart_error,
                 (unsigned int)diag.last_rx_byte,
                 (unsigned int)diag.current_line_len);
  Debug_WriteLine(line);

  (void)snprintf(line,
                 sizeof(line),
                 "[INFO] local voice current=\"%s\" last_line=\"%s\" last_parsed=\"%s\"",
                 diag.current_line,
                 diag.last_line,
                 diag.last_parsed_line);
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
      Stop_Gas_Demo_Source("console ack");
      Clear_Relay_Manual_Mask("console ack");
      break;

    case 'c':
    case 'C':
      Debug_WriteLine("[INFO] console clear alarm");
      AppStateMachine_HandleDemoCommand(s_debug_request_id++,
                                        APP_COMMAND_CLEAR_ALARM,
                                        APP_SCENARIO_NONE,
                                        1,
                                        now);
      Stop_Gas_Demo_Source("console clear");
      Clear_Relay_Manual_Mask("console clear");
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

    case '3':
      Debug_WriteLine("[INFO] console trigger scenario GAS_RISK");
      SensorMvp_StartGasFlameDemo();
      AppStateMachine_HandleDemoCommand(s_debug_request_id++,
                                        APP_COMMAND_TRIGGER_SCENARIO,
                                        APP_SCENARIO_GAS_RISK,
                                        1,
                                        now);
      break;

    case '4':
      SensorMvp_StartGasFlameDemo();
      Debug_WriteLine("[INFO] console gas flame-rise demo start");
      break;

    case '5':
      SensorMvp_StopGasFlameDemo();
      SensorMvp_SetGasPpmDebugOffset(350U);
      Debug_WriteLine("[INFO] console gas ppm debug offset=350");
      break;

    case '0':
      SensorMvp_StopGasFlameDemo();
      SensorMvp_SetGasPpmDebugOffset(0U);
      Debug_WriteLine("[INFO] console gas demo cleared");
      break;

    case '6':
      Debug_WriteLine("[INFO] console simulate voice RISK:1");
      Handle_DebugVoiceRisk(1U, now);
      break;

    case '7':
      Debug_WriteLine("[INFO] console simulate voice RISK:2");
      Handle_DebugVoiceRisk(2U, now);
      break;

    case '8':
      Debug_WriteLine("[INFO] console simulate voice RISK:3");
      Handle_DebugVoiceRisk(3U, now);
      break;

    case '9':
      Debug_WriteLine("[INFO] console simulate voice RISK:0");
      Handle_DebugVoiceRisk(0U, now);
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

    case 'l':
    case 'L':
      Print_LocalVoice_Diagnostics();
      break;

    case 'k':
    case 'K':
      SensorMvp_SetRadarCalibrationLogEnabled((SensorMvp_GetRadarCalibrationLogEnabled() == 0U) ? 1U : 0U);
      break;

    case 'b':
    case 'B':
      s_buzzer_test_until = now + MAIN_BUZZER_TEST_MS;
      Debug_WriteLine("[INFO] console buzzer test 1000ms");
      break;

    case 'e':
    case 'E':
      Set_AiSession_Label("env_normal");
      break;

    case 'w':
    case 'W':
      Set_AiSession_Label("walk_motion");
      break;

    case 'f':
    case 'F':
      Set_AiSession_Label("fall_sim");
      break;

    case 'j':
    case 'J':
      Set_AiSession_Label("long_still");
      break;

    case 'g':
    case 'G':
      Set_AiSession_Label("gas_debug");
      break;

    case 'v':
    case 'V':
      Set_AiSession_Label("voice_risk");
      break;

    case 'x':
    case 'X':
      Set_AiSession_Label("idle");
      break;

    case 'd':
    case 'D':
      s_display_frozen = (s_display_frozen == 0U) ? 1U : 0U;
      StatusDisplay_SetFrozen(s_display_frozen);
      Debug_WriteLine(s_display_frozen != 0U ? "[INFO] console display frozen" : "[INFO] console display resumed");
      break;

    case 'i':
    case 'I':
      s_tft_inversion = (s_tft_inversion == 0U) ? 1U : 0U;
      if (TftLcd_SetInversion(s_tft_inversion) == HAL_OK)
      {
        Debug_WriteLine(s_tft_inversion != 0U ? "[INFO] console tft inversion on" : "[INFO] console tft inversion off");
      }
      else
      {
        Debug_WriteLine("[WARN] console tft inversion command failed");
      }
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
  char line[320];
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
                                                       (int)status.gas_ppm_est,
                                                       status.presence,
                                                       app_status.risk,
                                                       app_status.relay_state_mask,
                                                       app_status.cloud_perm_mask);

  if (result == COMM_WIFI_OK)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] status tx temp=%.1f hum=%.1f gas_ppm_est=%u gas_dbg_offset=%u gas_mv=%d presence=%d risk=%d risk_src=%s state=%s scenario=%s relay=%u env_valid=%u gas_valid=%u",
                   status.temperature_c,
                   status.humidity_pct,
                   (unsigned int)status.gas_ppm_est,
                   (unsigned int)SensorMvp_GetGasPpmDebugOffset(),
                   status.gas,
                   status.presence,
                   app_status.risk,
                   Get_RiskSource_Text(&status, &app_status),
                   AppStatus_ToDisplayText(&app_status),
                   AppScenario_ToShortText(app_status.scenario),
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

static void Log_Ai_Sample(uint32_t now)
{
  char line[2048];
  SensorMvp_Status_t sensor;
  AppStatus_t app_status;
  SceneEngine_Status_t scene_status;
  EdgeAi_Result_t edge_ai_result;
  uint32_t edge_ai_age_ms = 0UL;

  if (SensorMvp_GetStatus(&sensor) != HAL_OK)
  {
    return;
  }

  AppStateMachine_GetStatus(&app_status);
  SceneEngine_GetStatus(&scene_status);
  EdgeAi_GetResult(&edge_ai_result);
  if (edge_ai_result.sequence != 0UL)
  {
    edge_ai_age_ms = now - edge_ai_result.last_update_ms;
  }
  (void)snprintf(line,
                 sizeof(line),
                 "[AI_SAMPLE] t=%lu session=%lu label=%s temp=%.1f hum=%.1f env_valid=%u gas_valid=%u gas_mv=%d gas_base=%u gas_ppm=%u gas_dbg_offset=%u gas_delta=%u presence=%d pir=%u rd03_ot2=%u radar_valid=%u radar_presence=%u radar_raw_cm=%u radar_cm=%u radar_q=%u radar_near=%u radar_jump=%u zone=%u peak_gate=%u peak_cm=%u peak_energy=%lu active_gates=%u motion=%lu energy=%lu still=%lu occupied=%lu radar_age_ms=%lu state=%s scenario=%s risk=%d risk_src=%s scene_top=%s scene_action=%s scene_sev=%u scene_conf=%u scene_count=%u scene_mask=0x%08lx scene_ev1=%u scene_ev2=%u edge_ai_scene=%s edge_ai_raw=%s edge_ai_risk=%u edge_ai_conf=%u edge_ai_stab=%u edge_ai_ev=0x%02X edge_ai_trend=%u edge_ai_score=%u edge_ai_ms=%lu edge_ai_max_ms=%lu edge_ai_age_ms=%lu edge_ai_skip=%lu edge_ai_ran=%u edge_ai_stale=%u event_id=%lu event_type=%s trigger=%s flags=0x%08lx ack_ms=%lu relay=%u manual=%u auto=%u",
                 (unsigned long)now,
                 (unsigned long)s_ai_session_id,
                 s_ai_session_label,
                 sensor.temperature_c,
                 sensor.humidity_pct,
                 (unsigned int)sensor.env_valid,
                 (unsigned int)sensor.gas_valid,
                 sensor.gas,
                 (unsigned int)sensor.gas_baseline_mv,
                 (unsigned int)sensor.gas_ppm_est,
                 (unsigned int)SensorMvp_GetGasPpmDebugOffset(),
                 (unsigned int)sensor.gas_delta_mv,
                 sensor.presence,
                 (unsigned int)sensor.pir_presence,
                 (unsigned int)sensor.rd03_ot2_presence,
                 (unsigned int)sensor.radar_valid,
                 (unsigned int)sensor.radar_presence,
                 (unsigned int)sensor.radar_raw_distance_cm,
                 (unsigned int)sensor.radar_distance_cm,
                 (unsigned int)sensor.radar_distance_quality,
                 (unsigned int)sensor.radar_near_blind,
                 (unsigned int)sensor.radar_distance_unstable,
                 (unsigned int)sensor.radar_zone,
                 (unsigned int)sensor.radar_peak_gate,
                 (unsigned int)sensor.radar_peak_gate_cm,
                 (unsigned long)sensor.radar_peak_energy,
                 (unsigned int)sensor.radar_active_gate_count,
                 (unsigned long)sensor.radar_motion_score,
                 (unsigned long)sensor.radar_energy_sum,
                 (unsigned long)sensor.radar_still_seconds,
                 (unsigned long)sensor.radar_occupied_seconds,
                 (unsigned long)sensor.radar_last_seen_age_ms,
                 AppState_ToText(app_status.state),
                 AppScenario_ToShortText(app_status.scenario),
                 app_status.risk,
                 Get_RiskSource_Text(&sensor, &app_status),
                 SceneEngine_IdToText(scene_status.top.id),
                 SceneEngine_ActionToText(scene_status.top.action_hint),
                 (unsigned int)scene_status.top.severity,
                 (unsigned int)scene_status.top.confidence,
                 (unsigned int)scene_status.signal_count,
                 (unsigned long)scene_status.scene_mask,
                 (unsigned int)scene_status.top.evidence_primary,
                 (unsigned int)scene_status.top.evidence_secondary,
                 EdgeAi_SceneToText((EdgeAiScene_t)app_status.edge_ai_scene),
                 EdgeAi_SceneToText((EdgeAiScene_t)app_status.edge_ai_raw_scene),
                 (unsigned int)app_status.edge_ai_risk,
                 (unsigned int)app_status.edge_ai_confidence,
                 (unsigned int)app_status.edge_ai_stability,
                 (unsigned int)app_status.edge_ai_evidence_mask,
                 (unsigned int)app_status.edge_ai_trend_score,
                 (unsigned int)app_status.edge_ai_anomaly_score,
                 (unsigned long)edge_ai_result.last_run_ms,
                 (unsigned long)edge_ai_result.max_run_ms,
                 (unsigned long)edge_ai_age_ms,
                 (unsigned long)edge_ai_result.skipped_count,
                 (unsigned int)edge_ai_result.ran_this_tick,
                 (unsigned int)edge_ai_result.stale,
                 (unsigned long)app_status.last_event_id,
                 AppEventType_ToText(app_status.last_event_type),
                 AppTriggerSource_ToText(app_status.last_trigger_source),
                 (unsigned long)app_status.last_event_flags,
                 (unsigned long)app_status.ack_remaining_ms,
                 (unsigned int)app_status.relay_state_mask,
                 (unsigned int)s_relay_manual_mask,
                 (unsigned int)s_relay_auto_mask);
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
  MX_SPI1_Init();
  MX_UART4_Init();
  MX_ICACHE_Init();
  MX_TIM1_Init();
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
  if (CommLocal_Init() == COMM_WIFI_OK)
  {
    Debug_WriteLine("[INFO] local voice uart init ok");
  }
  else
  {
    Debug_WriteLine("[WARN] local voice uart init failed");
  }
  (void)StatusDisplay_Init(&hspi1, Debug_WriteLine);
  AppStateMachine_Init();
  EdgeAi_Init();
  SceneEngine_Init();
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
    static uint32_t last_ai_sample_tick = 0U;
    uint32_t now = HAL_GetTick();

    SensorMvp_Update();
    Update_App(now);
    Update_BoardIo(now);
    Update_DebugConsole(now);
    Process_Cloud_Commands(now);
    Check_Wifi_Heartbeat_Timeout(now);
    CommLocal_Service();
    Process_LocalVoice_Commands(now);
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

    if ((now - last_ai_sample_tick) >= MAIN_AI_SAMPLE_PERIOD_MS)
    {
      last_ai_sample_tick = now;
      Log_Ai_Sample(now);
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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE2) != HAL_OK)
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
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
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
