#ifndef APP_STATE_MACHINE_H
#define APP_STATE_MACHINE_H

#include "app_types.h"
#include "sensor_mvp.h"

#include <stdbool.h>
#include <stdint.h>

void AppStateMachine_Init(void);
void AppStateMachine_Update(const SensorMvp_Status_t *sensor, uint32_t now_ms);
void AppStateMachine_HandleDemoCommand(uint32_t request_id,
                                       AppCommandType_t command_type,
                                       AppScenario_t scenario,
                                       int value,
                                       uint32_t now_ms);
void AppStateMachine_HandleVoiceRisk(uint32_t request_id, uint8_t risk_level, uint32_t now_ms);
void AppStateMachine_HandleLocalSos(uint32_t now_ms);
void AppStateMachine_HandleLocalAck(uint32_t now_ms);
void AppStateMachine_SetEdgeAiHint(uint8_t valid,
                                   uint8_t scene,
                                   uint8_t raw_scene,
                                   uint8_t risk_level,
                                   uint8_t confidence,
                                   uint8_t stability,
                                   uint8_t evidence_mask,
                                   uint16_t anomaly_score,
                                   uint16_t trend_score);
void AppStateMachine_SetRelayStateMask(uint8_t relay_state_mask);
void AppStateMachine_GetStatus(AppStatus_t *status);
bool AppStateMachine_PollEvent(AppEventRecord_t *event);

#endif
