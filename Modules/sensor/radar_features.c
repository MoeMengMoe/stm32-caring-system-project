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
#define RADAR_MIN_RELIABLE_CM       (45U)
#define RADAR_MAX_RELIABLE_CM       (850U)
#define RADAR_DISTANCE_SPIKE_CM     (120U)
#define RADAR_DISTANCE_JUMP_CM      (80U)
#define RADAR_SPIKE_MOTION_MAX      (120U)
#define RADAR_SPIKE_ACTIVE_GATES_MAX (2U)
#define RADAR_STILL_QUALITY_MIN     (45U)
#define RADAR_STILL_ACTIVE_GATES_MIN (2U)

static uint32_t s_previous_energy[RD03_V2_GATE_COUNT];
static uint8_t s_have_previous;
static uint32_t s_occupied_since_tick;
static uint32_t s_still_since_tick;
static uint32_t s_last_seen_tick;
static uint16_t s_stable_distance_cm;
static uint8_t s_stable_distance_ready;

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

  if (distance_cm == 0U)
  {
    return RADAR_ZONE_UNKNOWN;
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

static uint16_t Abs_Diff_U16(uint16_t a, uint16_t b)
{
  return (a > b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint8_t Clamp_Quality_U8(uint32_t value)
{
  return (value > 100U) ? 100U : (uint8_t)value;
}

static uint8_t Estimate_Distance_Quality(const Rd03V2_Status_t *radar, const RadarFeatures_t *features)
{
  uint32_t quality = 0U;
  uint32_t peak_ratio_pct;
  uint16_t raw_distance_cm;
  uint16_t peak_cm;

  if ((radar == NULL) || (features == NULL) ||
      (radar->valid == 0U) || (radar->presence == 0U))
  {
    return 0U;
  }

  raw_distance_cm = radar->distance_cm;
  peak_cm = features->peak_gate_cm;

  if ((raw_distance_cm > 0U) && (raw_distance_cm <= RADAR_MAX_RELIABLE_CM))
  {
    quality += 25U;
  }

  if (raw_distance_cm >= RADAR_MIN_RELIABLE_CM)
  {
    quality += 10U;
  }

  if (features->active_gate_count >= 2U)
  {
    quality += 15U;
  }
  if (features->active_gate_count >= 4U)
  {
    quality += 10U;
  }
  if (features->active_gate_count > 12U)
  {
    quality = (quality > 10U) ? (quality - 10U) : 0U;
  }

  if (features->peak_energy >= 1000U)
  {
    quality += 10U;
  }
  if (features->peak_energy >= 10000U)
  {
    quality += 10U;
  }

  if ((features->energy_sum > 0U) && (features->peak_energy > 0U))
  {
    peak_ratio_pct = (features->peak_energy * 100U) / features->energy_sum;
    if (peak_ratio_pct >= 12U)
    {
      quality += 10U;
    }
  }

  if ((raw_distance_cm > 0U) && (peak_cm > 0U))
  {
    uint16_t mismatch_cm = Abs_Diff_U16(raw_distance_cm, peak_cm);
    if (mismatch_cm <= 40U)
    {
      quality += 20U;
    }
    else if (mismatch_cm <= 80U)
    {
      quality += 10U;
    }
  }

  return Clamp_Quality_U8(quality);
}

static uint16_t Stabilize_Distance(const Rd03V2_Status_t *radar, RadarFeatures_t *features)
{
  uint16_t raw_distance_cm;
  uint16_t target_cm;
  uint16_t delta_cm;

  if ((radar == NULL) || (features == NULL) ||
      (radar->valid == 0U) || (radar->presence == 0U) ||
      (radar->distance_cm == 0U) || (radar->distance_cm > RADAR_MAX_RELIABLE_CM))
  {
    s_stable_distance_ready = 0U;
    s_stable_distance_cm = 0U;
    features->distance_unstable = 0U;
    return 0U;
  }

  raw_distance_cm = radar->distance_cm;
  features->near_blind = (raw_distance_cm < RADAR_MIN_RELIABLE_CM) ? 1U : 0U;
  target_cm = (raw_distance_cm < RADAR_MIN_RELIABLE_CM) ? RADAR_MIN_RELIABLE_CM : raw_distance_cm;

  if (s_stable_distance_ready == 0U)
  {
    s_stable_distance_cm = target_cm;
    s_stable_distance_ready = 1U;
    features->distance_unstable = 0U;
    return s_stable_distance_cm;
  }

  delta_cm = Abs_Diff_U16(target_cm, s_stable_distance_cm);
  if ((delta_cm >= RADAR_DISTANCE_SPIKE_CM) &&
      (features->motion_score <= RADAR_SPIKE_MOTION_MAX) &&
      (features->active_gate_count <= RADAR_SPIKE_ACTIVE_GATES_MAX))
  {
    features->distance_unstable = 1U;
    return s_stable_distance_cm;
  }

  features->distance_unstable = (delta_cm >= RADAR_DISTANCE_JUMP_CM) ? 1U : 0U;

  if ((features->motion_score >= 500U) ||
      (features->active_gate_count >= 4U) ||
      (delta_cm >= RADAR_DISTANCE_SPIKE_CM))
  {
    s_stable_distance_cm = (uint16_t)(((uint32_t)s_stable_distance_cm + (uint32_t)target_cm) / 2U);
  }
  else
  {
    s_stable_distance_cm = (uint16_t)((((uint32_t)s_stable_distance_cm * 3U) + (uint32_t)target_cm) / 4U);
  }

  return s_stable_distance_cm;
}

void RadarFeatures_Reset(void)
{
  memset(s_previous_energy, 0, sizeof(s_previous_energy));
  s_have_previous = 0U;
  s_occupied_since_tick = 0U;
  s_still_since_tick = 0U;
  s_last_seen_tick = 0U;
  s_stable_distance_cm = 0U;
  s_stable_distance_ready = 0U;
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
  features->raw_distance_cm = radar->distance_cm;

  if (radar->valid == 0U)
  {
    s_have_previous = 0U;
    s_stable_distance_ready = 0U;
    features->zone = RADAR_ZONE_UNKNOWN;
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

  features->distance_quality = Estimate_Distance_Quality(radar, features);
  features->distance_cm = Stabilize_Distance(radar, features);
  features->zone = Classify_Zone(radar->valid, radar->presence, features->distance_cm);

  if (radar->presence != 0U)
  {
    s_last_seen_tick = now_tick;

    if (s_occupied_since_tick == 0U)
    {
      s_occupied_since_tick = now_tick;
    }

    if ((s_have_previous != 0U) &&
        (features->motion_score <= RADAR_STILL_SCORE_MAX) &&
        (features->distance_quality >= RADAR_STILL_QUALITY_MIN) &&
        (features->active_gate_count >= RADAR_STILL_ACTIVE_GATES_MIN) &&
        (features->distance_cm != 0U) &&
        (features->distance_unstable == 0U))
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
