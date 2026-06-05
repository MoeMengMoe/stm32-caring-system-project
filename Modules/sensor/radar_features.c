#include "radar_features.h"

#include <limits.h>
#include <string.h>

#define RADAR_GATE_DISTANCE_CM       (10U)
#define RADAR_NEAR_MAX_CM           (80U)
#define RADAR_ACTIVE_MAX_CM         (180U)
#define RADAR_REST_MAX_CM           (300U)
#define RADAR_ACTIVE_GATE_DIVISOR   (6U)
#define RADAR_MOTION_SCALE          (1000U)
#define RADAR_STILL_SCORE_MAX       (30U)

static uint32_t s_previous_energy[RD03_V2_GATE_COUNT];
static uint8_t s_have_previous;
static uint32_t s_occupied_since_tick;
static uint32_t s_still_since_tick;
static uint32_t s_last_seen_tick;

static uint32_t Saturating_Add_U32(uint32_t a, uint32_t b)
{
  if (UINT_MAX - a < b)
  {
    return UINT_MAX;
  }

  return a + b;
}

static uint8_t Classify_Zone(uint8_t valid, uint8_t presence, uint16_t distance_cm)
{
  if (valid == 0U)
  {
    return RADAR_ZONE_UNKNOWN;
  }

  if (presence == 0U)
  {
    return RADAR_ZONE_CLEAR;
  }

  if (distance_cm < RADAR_NEAR_MAX_CM)
  {
    return RADAR_ZONE_NEAR;
  }

  if (distance_cm < RADAR_ACTIVE_MAX_CM)
  {
    return RADAR_ZONE_ACTIVE;
  }

  if (distance_cm < RADAR_REST_MAX_CM)
  {
    return RADAR_ZONE_REST;
  }

  return RADAR_ZONE_FAR;
}

void RadarFeatures_Reset(void)
{
  memset(s_previous_energy, 0, sizeof(s_previous_energy));
  s_have_previous = 0U;
  s_occupied_since_tick = 0U;
  s_still_since_tick = 0U;
  s_last_seen_tick = 0U;
}

void RadarFeatures_Update(const Rd03V2_Status_t *radar, uint32_t now_tick, RadarFeatures_t *features)
{
  uint32_t delta_sum = 0U;
  uint32_t active_threshold = 0U;

  if (features == NULL)
  {
    return;
  }

  memset(features, 0, sizeof(*features));

  if (radar == NULL)
  {
    features->zone = RADAR_ZONE_UNKNOWN;
    return;
  }

  features->valid = radar->valid;
  features->presence = radar->presence;
  features->distance_cm = radar->distance_cm;
  features->zone = Classify_Zone(radar->valid, radar->presence, radar->distance_cm);

  if (radar->valid == 0U)
  {
    s_have_previous = 0U;
    return;
  }

  for (uint32_t gate = 0U; gate < RD03_V2_GATE_COUNT; gate++)
  {
    uint32_t energy = radar->gate_energy[gate];

    features->energy_sum = Saturating_Add_U32(features->energy_sum, energy);
    if (energy > features->peak_energy)
    {
      features->peak_energy = energy;
      features->peak_gate = (uint8_t)gate;
    }

    if (s_have_previous != 0U)
    {
      delta_sum = Saturating_Add_U32(delta_sum,
                                     (energy > s_previous_energy[gate]) ?
                                     (energy - s_previous_energy[gate]) :
                                     (s_previous_energy[gate] - energy));
    }
  }

  features->peak_gate_cm = (uint16_t)((uint16_t)features->peak_gate * RADAR_GATE_DISTANCE_CM);
  features->motion_score = delta_sum / RADAR_MOTION_SCALE;

  if (features->peak_energy > 0U)
  {
    active_threshold = features->peak_energy / RADAR_ACTIVE_GATE_DIVISOR;
    if (active_threshold == 0U)
    {
      active_threshold = 1U;
    }

    for (uint32_t gate = 0U; gate < RD03_V2_GATE_COUNT; gate++)
    {
      if (radar->gate_energy[gate] >= active_threshold)
      {
        features->active_gate_count++;
      }
    }
  }

  if (radar->presence != 0U)
  {
    s_last_seen_tick = now_tick;

    if (s_occupied_since_tick == 0U)
    {
      s_occupied_since_tick = now_tick;
    }

    if ((s_have_previous != 0U) && (features->motion_score <= RADAR_STILL_SCORE_MAX))
    {
      if (s_still_since_tick == 0U)
      {
        s_still_since_tick = now_tick;
      }
    }
    else
    {
      s_still_since_tick = 0U;
    }
  }
  else
  {
    s_occupied_since_tick = 0U;
    s_still_since_tick = 0U;
  }

  if ((radar->presence != 0U) && (s_occupied_since_tick != 0U))
  {
    features->occupied_seconds = (now_tick - s_occupied_since_tick) / 1000U;
  }

  if ((radar->presence != 0U) && (s_still_since_tick != 0U))
  {
    features->still_seconds = (now_tick - s_still_since_tick) / 1000U;
  }

  if ((radar->presence == 0U) && (s_last_seen_tick != 0U))
  {
    features->last_seen_age_ms = now_tick - s_last_seen_tick;
  }

  memcpy(s_previous_energy, radar->gate_energy, sizeof(s_previous_energy));
  s_have_previous = 1U;
}

const char *RadarFeatures_ZoneName(uint8_t zone)
{
  switch (zone)
  {
    case RADAR_ZONE_NEAR:
      return "NEAR";
    case RADAR_ZONE_ACTIVE:
      return "ACTIVE";
    case RADAR_ZONE_REST:
      return "REST";
    case RADAR_ZONE_FAR:
      return "FAR";
    case RADAR_ZONE_CLEAR:
      return "CLEAR";
    default:
      return "UNKNOWN";
  }
}
