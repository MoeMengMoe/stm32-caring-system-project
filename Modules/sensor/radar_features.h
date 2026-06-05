#ifndef RADAR_FEATURES_H
#define RADAR_FEATURES_H

#include "rd03_v2.h"

#include <stdint.h>

typedef enum
{
  RADAR_ZONE_UNKNOWN = 0,
  RADAR_ZONE_NEAR,
  RADAR_ZONE_ACTIVE,
  RADAR_ZONE_REST,
  RADAR_ZONE_FAR,
  RADAR_ZONE_CLEAR
} RadarFeatures_Zone_t;

typedef struct
{
  uint8_t valid;
  uint8_t presence;
  uint8_t zone;
  uint16_t distance_cm;
  uint8_t peak_gate;
  uint16_t peak_gate_cm;
  uint32_t peak_energy;
  uint32_t energy_sum;
  uint32_t motion_score;
  uint8_t active_gate_count;
  uint32_t occupied_seconds;
  uint32_t still_seconds;
  uint32_t last_seen_age_ms;
} RadarFeatures_t;

void RadarFeatures_Reset(void);
void RadarFeatures_Update(const Rd03V2_Status_t *radar, uint32_t now_tick, RadarFeatures_t *features);
const char *RadarFeatures_ZoneName(uint8_t zone);

#endif
