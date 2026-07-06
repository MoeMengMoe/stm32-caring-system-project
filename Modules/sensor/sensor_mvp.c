#include "sensor_mvp.h"

#include "adc.h"
#include "bme280.h"
#include "gpio.h"
#include "i2c.h"
#include "main.h"
#include "radar_features.h"
#include "rd03_v2.h"
#include "usart.h"

#include <math.h>
#include <stdio.h>

#define SENSOR_MVP_ENV_PERIOD_MS      (2000U)
#define SENSOR_MVP_DIGITAL_PERIOD_MS  (500U)
#define SENSOR_MVP_RADAR_LOG_PERIOD_MS (2000U)
#define SENSOR_MVP_RADAR_GATES_PER_LINE (8U)
#define SENSOR_MVP_MQ_R_TOP_OHM       (2000U)
#define SENSOR_MVP_MQ_R_BOTTOM_OHM    (3300U)
#define SENSOR_MVP_MQ_SAMPLE_COUNT    (32U)
#define SENSOR_MVP_MQ_EMA_NUMERATOR   (1U)
#define SENSOR_MVP_MQ_EMA_DENOMINATOR (8U)
#define SENSOR_MVP_MQ_BASELINE_MARGIN_MV (80U)
#define SENSOR_MVP_MQ_BASELINE_EMA_DENOMINATOR (64U)
#define SENSOR_MVP_MQ_SUPPLY_MV       (5000U)
#define SENSOR_MVP_MQ_CLEAN_AIR_FACTOR (9.83f)
#define SENSOR_MVP_MQ_SMOKE_CURVE_A   (11.5428f)
#define SENSOR_MVP_MQ_SMOKE_CURVE_B_ABS (0.6549f)
#define SENSOR_MVP_MQ_PPM_EST_MAX     (9999U)

static SensorMvp_LogFn s_log;
static uint32_t s_last_env_tick;
static uint32_t s_last_digital_tick;
static uint32_t s_last_radar_log_tick;
static uint8_t s_bme_ready;
static uint8_t s_adc_ready;
static GPIO_PinState s_last_pir = GPIO_PIN_RESET;
static GPIO_PinState s_last_rd03 = GPIO_PIN_RESET;
static SensorMvp_Status_t s_status;
static uint16_t s_mq_filtered_ao_mv;
static uint16_t s_mq_baseline_ao_mv;
static uint8_t s_mq_filter_ready;
static uint8_t s_mq_baseline_ready;

static void Apply_Radar_Features(const RadarFeatures_t *features)
{
  if (features == NULL)
  {
    return;
  }

  s_status.radar_valid = features->valid;
  s_status.radar_presence = features->presence;
  s_status.radar_distance_cm = features->distance_cm;
  s_status.radar_zone = features->zone;
  s_status.radar_peak_gate = features->peak_gate;
  s_status.radar_peak_gate_cm = features->peak_gate_cm;
  s_status.radar_peak_energy = features->peak_energy;
  s_status.radar_energy_sum = features->energy_sum;
  s_status.radar_motion_score = features->motion_score;
  s_status.radar_active_gate_count = features->active_gate_count;
  s_status.radar_occupied_seconds = features->occupied_seconds;
  s_status.radar_still_seconds = features->still_seconds;
  s_status.radar_last_seen_age_ms = features->last_seen_age_ms;
}

static void Log_Line(const char *text)
{
  if (s_log != NULL)
  {
    s_log(text);
  }
}

static void Format_Signed_Centi(char *buffer, size_t len, int32_t value)
{
  const char *sign = "";
  int32_t whole;
  int32_t frac;

  if (value < 0)
  {
    sign = "-";
    value = -value;
  }

  whole = value / 100;
  frac = value % 100;
  (void)snprintf(buffer, len, "%s%ld.%02ld", sign, (long)whole, (long)frac);
}

static void Format_Unsigned_Centi(char *buffer, size_t len, uint32_t value)
{
  (void)snprintf(buffer, len, "%lu.%02lu", (unsigned long)(value / 100U), (unsigned long)(value % 100U));
}

static HAL_StatusTypeDef Read_Mq_Adc(uint16_t *raw, uint16_t *adc_millivolt, uint16_t *ao_est_millivolt)
{
  uint32_t adc_sum = 0U;
  uint32_t adc_value;
  uint32_t adc_mv;
  uint32_t min_value = 0xFFFFFFFFUL;
  uint32_t max_value = 0U;

  for (uint32_t i = 0U; i < SENSOR_MVP_MQ_SAMPLE_COUNT; i++)
  {
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
      return HAL_ERROR;
    }

    if (HAL_ADC_PollForConversion(&hadc1, 10U) != HAL_OK)
    {
      (void)HAL_ADC_Stop(&hadc1);
      return HAL_ERROR;
    }

    adc_value = HAL_ADC_GetValue(&hadc1);
    (void)HAL_ADC_Stop(&hadc1);

    adc_sum += adc_value;
    if (adc_value < min_value)
    {
      min_value = adc_value;
    }
    if (adc_value > max_value)
    {
      max_value = adc_value;
    }
  }

  adc_sum = adc_sum - min_value - max_value;
  adc_value = adc_sum / (SENSOR_MVP_MQ_SAMPLE_COUNT - 2U);

  adc_mv = (adc_value * 3300U) / 4095U;
  *raw = (uint16_t)adc_value;
  *adc_millivolt = (uint16_t)adc_mv;
  *ao_est_millivolt = (uint16_t)((adc_mv * (SENSOR_MVP_MQ_R_TOP_OHM + SENSOR_MVP_MQ_R_BOTTOM_OHM)) /
                                 SENSOR_MVP_MQ_R_BOTTOM_OHM);
  return HAL_OK;
}

static void Update_Mq_Baseline(uint16_t filtered_mv)
{
  uint32_t baseline;

  if (s_mq_baseline_ready == 0U)
  {
    s_mq_baseline_ao_mv = filtered_mv;
    s_mq_baseline_ready = 1U;
  }
  else if (filtered_mv < s_mq_baseline_ao_mv)
  {
    s_mq_baseline_ao_mv = filtered_mv;
  }
  else if ((uint32_t)filtered_mv <= ((uint32_t)s_mq_baseline_ao_mv + SENSOR_MVP_MQ_BASELINE_MARGIN_MV))
  {
    baseline = ((uint32_t)s_mq_baseline_ao_mv * (SENSOR_MVP_MQ_BASELINE_EMA_DENOMINATOR - 1U)) +
               (uint32_t)filtered_mv;
    s_mq_baseline_ao_mv = (uint16_t)(baseline / SENSOR_MVP_MQ_BASELINE_EMA_DENOMINATOR);
  }

  s_status.gas_baseline_mv = s_mq_baseline_ao_mv;
  s_status.gas_delta_mv = (filtered_mv > s_mq_baseline_ao_mv) ?
                          (uint16_t)(filtered_mv - s_mq_baseline_ao_mv) : 0U;
}

static uint16_t Estimate_Mq_Ppm(uint16_t filtered_mv, uint16_t baseline_mv)
{
  float rs_relative;
  float baseline_rs_relative;
  float rs_r0_ratio;
  float ppm;

  if ((filtered_mv == 0U) ||
      (baseline_mv == 0U) ||
      (filtered_mv >= SENSOR_MVP_MQ_SUPPLY_MV) ||
      (baseline_mv >= SENSOR_MVP_MQ_SUPPLY_MV))
  {
    return 0U;
  }

  rs_relative = ((float)SENSOR_MVP_MQ_SUPPLY_MV / (float)filtered_mv) - 1.0f;
  baseline_rs_relative = ((float)SENSOR_MVP_MQ_SUPPLY_MV / (float)baseline_mv) - 1.0f;
  if ((rs_relative <= 0.0f) || (baseline_rs_relative <= 0.0f))
  {
    return 0U;
  }

  rs_r0_ratio = SENSOR_MVP_MQ_CLEAN_AIR_FACTOR * (rs_relative / baseline_rs_relative);
  ppm = powf(SENSOR_MVP_MQ_SMOKE_CURVE_A / rs_r0_ratio, 1.0f / SENSOR_MVP_MQ_SMOKE_CURVE_B_ABS);
  if (!(ppm > 0.0f))
  {
    return 0U;
  }
  if (ppm > (float)SENSOR_MVP_MQ_PPM_EST_MAX)
  {
    return SENSOR_MVP_MQ_PPM_EST_MAX;
  }

  return (uint16_t)(ppm + 0.5f);
}

static void Update_Environment(void)
{
  char line[128];
  char temp_text[16];
  char hum_text[16];
  char pressure_text[16];
  Bme280_Data_t env;

  if (s_bme_ready == 0U)
  {
    return;
  }

  if (Bme280_Read(&env) != HAL_OK)
  {
    Log_Line("[WARN] bme280 read failed");
    return;
  }

  Format_Signed_Centi(temp_text, sizeof(temp_text), env.temperature_centi_c);
  Format_Unsigned_Centi(hum_text, sizeof(hum_text), env.humidity_centi_pct);
  Format_Unsigned_Centi(pressure_text, sizeof(pressure_text), env.pressure_centi_hpa);
  s_status.temperature_c = (float)env.temperature_centi_c / 100.0f;
  s_status.humidity_pct = (float)env.humidity_centi_pct / 100.0f;
  s_status.env_valid = 1U;
  (void)snprintf(line, sizeof(line), "[ENV] temp=%sC hum=%s%% pressure=%shPa",
                 temp_text, hum_text, pressure_text);
  Log_Line(line);
}

static void Update_Digital_And_Adc(void)
{
  char line[320];
  uint16_t mq_raw = 0U;
  uint16_t mq_adc_mv = 0U;
  uint16_t mq_ao_est_mv = 0U;
  uint16_t mq_filtered_mv = 0U;
  GPIO_PinState pir = HAL_GPIO_ReadPin(PIR_IN_GPIO_Port, PIR_IN_Pin);
  GPIO_PinState rd03_ot2 = HAL_GPIO_ReadPin(RD03_OUT_GPIO_Port, RD03_OUT_Pin);
  Rd03V2_Status_t radar;
  RadarFeatures_t radar_features;

  if (s_adc_ready != 0U && Read_Mq_Adc(&mq_raw, &mq_adc_mv, &mq_ao_est_mv) != HAL_OK)
  {
    Log_Line("[WARN] mq adc read failed");
  }
  else if (s_adc_ready != 0U)
  {
    if (s_mq_filter_ready == 0U)
    {
      s_mq_filtered_ao_mv = mq_ao_est_mv;
      s_mq_filter_ready = 1U;
    }
    else
    {
      uint32_t filtered = ((uint32_t)s_mq_filtered_ao_mv *
                           (SENSOR_MVP_MQ_EMA_DENOMINATOR - SENSOR_MVP_MQ_EMA_NUMERATOR)) +
                          ((uint32_t)mq_ao_est_mv * SENSOR_MVP_MQ_EMA_NUMERATOR);
      s_mq_filtered_ao_mv = (uint16_t)(filtered / SENSOR_MVP_MQ_EMA_DENOMINATOR);
    }

    mq_filtered_mv = s_mq_filtered_ao_mv;
    Update_Mq_Baseline(mq_filtered_mv);
    s_status.gas_ppm_est = Estimate_Mq_Ppm(mq_filtered_mv, s_status.gas_baseline_mv);
    s_status.gas = (int)mq_filtered_mv;
    s_status.gas_valid = 1U;
  }

  if (Rd03V2_GetStatus(&radar) == HAL_OK)
  {
    RadarFeatures_Update(&radar, HAL_GetTick(), &radar_features);
    Apply_Radar_Features(&radar_features);
  }

  s_status.presence = ((pir == GPIO_PIN_SET) ||
                       (rd03_ot2 == GPIO_PIN_SET) ||
                       ((s_status.radar_valid != 0U) && (s_status.radar_presence != 0U))) ? 1 : 0;

  (void)snprintf(line, sizeof(line),
                 "[DETECT] pir=%u rd03_ot2=%u radar_valid=%u radar_presence=%u radar_cm=%u mq_raw=%u mq_adc_mv=%u mq_ao_est_mv=%u mq_filtered_mv=%u mq_base_mv=%u mq_delta_mv=%u mq_ppm_est=%u",
                 (unsigned int)(pir == GPIO_PIN_SET),
                 (unsigned int)(rd03_ot2 == GPIO_PIN_SET),
                 (unsigned int)s_status.radar_valid,
                 (unsigned int)s_status.radar_presence,
                 (unsigned int)s_status.radar_distance_cm,
                 (unsigned int)mq_raw,
                 (unsigned int)mq_adc_mv,
                 (unsigned int)mq_ao_est_mv,
                 (unsigned int)mq_filtered_mv,
                 (unsigned int)s_status.gas_baseline_mv,
                 (unsigned int)s_status.gas_delta_mv,
                 (unsigned int)s_status.gas_ppm_est);
  Log_Line(line);

  if (pir != s_last_pir)
  {
    Log_Line((pir == GPIO_PIN_SET) ? "[EVENT] pir active" : "[EVENT] pir inactive");
    s_last_pir = pir;
  }

  if (rd03_ot2 != s_last_rd03)
  {
    Log_Line((rd03_ot2 == GPIO_PIN_SET) ? "[EVENT] rd03 ot2 active" : "[EVENT] rd03 ot2 inactive");
    s_last_rd03 = rd03_ot2;
  }
}

static void Log_Radar_Energy_Line(const Rd03V2_Status_t *radar, uint32_t first_gate)
{
  char line[192];
  int used;
  uint32_t end_gate;

  if (radar == NULL)
  {
    return;
  }

  used = snprintf(line, sizeof(line), "[RADAR_E]");
  if ((used < 0) || (used >= (int)sizeof(line)))
  {
    return;
  }

  end_gate = first_gate + SENSOR_MVP_RADAR_GATES_PER_LINE;
  if (end_gate > RD03_V2_GATE_COUNT)
  {
    end_gate = RD03_V2_GATE_COUNT;
  }

  for (uint32_t gate = first_gate; gate < end_gate; gate++)
  {
    int written = snprintf(&line[used],
                           sizeof(line) - (size_t)used,
                           " g%02lu=%lu",
                           (unsigned long)gate,
                           (unsigned long)radar->gate_energy[gate]);
    if ((written < 0) || (written >= (int)(sizeof(line) - (size_t)used)))
    {
      break;
    }

    used += written;
  }

  Log_Line(line);
}

static void Log_Radar_Protocol_Status(void)
{
  char line[256];
  uint32_t peak_gate = 0U;
  uint32_t peak_energy = 0U;
  Rd03V2_Status_t radar;

  if (Rd03V2_GetStatus(&radar) != HAL_OK)
  {
    Log_Line("[RADAR] status read failed");
    return;
  }

  if (radar.valid == 0U)
  {
    uint32_t last_rx_age_ms = HAL_GetTick() - radar.last_rx_tick;

    (void)snprintf(line,
                   sizeof(line),
                   "[RADAR] valid=0 rx_bytes=%lu init_rx=%lu dma_evt=%lu dma_restart=%lu reinit=%lu last_rx_age_ms=%lu rx_ovf=%lu ack=%u/%u/%u header_sync=%lu bad_len=%lu bad_footer=%lu uart_err=%lu last_err=0x%lX",
                   (unsigned long)radar.rx_byte_count,
                   (unsigned long)radar.init_rx_byte_count,
                   (unsigned long)radar.dma_event_count,
                   (unsigned long)radar.dma_restart_count,
                   (unsigned long)radar.auto_reinit_count,
                   (unsigned long)last_rx_age_ms,
                   (unsigned long)radar.rx_overflow_count,
                   (unsigned int)radar.open_command_ack_ok,
                   (unsigned int)radar.report_mode_ack_ok,
                   (unsigned int)radar.close_command_ack_ok,
                   (unsigned long)radar.header_sync_count,
                   (unsigned long)radar.invalid_length_count,
                   (unsigned long)radar.invalid_footer_count,
                   (unsigned long)radar.uart_error_count,
                   (unsigned long)radar.last_uart_error);
    Log_Line(line);
    return;
  }

  for (uint32_t gate = 0U; gate < RD03_V2_GATE_COUNT; gate++)
  {
    if (radar.gate_energy[gate] > peak_energy)
    {
      peak_gate = gate;
      peak_energy = radar.gate_energy[gate];
    }
  }

  (void)snprintf(line,
                 sizeof(line),
                 "[RADAR] valid=1 frames=%lu rx_bytes=%lu dma_evt=%lu reinit=%lu rx_ovf=%lu presence=%u distance_cm=%u peak_gate=%lu peak_gate_cm=%lu peak_energy=%lu",
                 (unsigned long)radar.frame_count,
                 (unsigned long)radar.rx_byte_count,
                 (unsigned long)radar.dma_event_count,
                 (unsigned long)radar.auto_reinit_count,
                 (unsigned long)radar.rx_overflow_count,
                 (unsigned int)radar.presence,
                 (unsigned int)radar.distance_cm,
                 (unsigned long)peak_gate,
                 (unsigned long)(peak_gate * 10U),
                 (unsigned long)peak_energy);
  Log_Line(line);

  (void)snprintf(line,
                 sizeof(line),
                 "[RADAR_F] zone=%u/%s dist_cm=%u peak_gate=%u peak_cm=%u energy=%lu sum=%lu motion=%lu active_gates=%u occupied_s=%lu still_s=%lu",
                 (unsigned int)s_status.radar_zone,
                 RadarFeatures_ZoneName(s_status.radar_zone),
                 (unsigned int)s_status.radar_distance_cm,
                 (unsigned int)s_status.radar_peak_gate,
                 (unsigned int)s_status.radar_peak_gate_cm,
                 (unsigned long)s_status.radar_peak_energy,
                 (unsigned long)s_status.radar_energy_sum,
                 (unsigned long)s_status.radar_motion_score,
                 (unsigned int)s_status.radar_active_gate_count,
                 (unsigned long)s_status.radar_occupied_seconds,
                 (unsigned long)s_status.radar_still_seconds);
  Log_Line(line);

  for (uint32_t first_gate = 0U; first_gate < RD03_V2_GATE_COUNT; first_gate += SENSOR_MVP_RADAR_GATES_PER_LINE)
  {
    Log_Radar_Energy_Line(&radar, first_gate);
  }
}

void SensorMvp_Init(SensorMvp_LogFn log_fn)
{
  char line[160];
  HAL_StatusTypeDef rd03_init_result;
  Rd03V2_Status_t radar;

  s_log = log_fn;
  s_last_env_tick = HAL_GetTick();
  s_last_digital_tick = HAL_GetTick();
  s_last_radar_log_tick = HAL_GetTick();
  s_status.temperature_c = 0.0f;
  s_status.humidity_pct = 0.0f;
  s_status.gas = 0;
  s_status.gas_baseline_mv = 0U;
  s_status.gas_delta_mv = 0U;
  s_status.gas_ppm_est = 0U;
  s_status.presence = 0;
  s_status.radar_valid = 0U;
  s_status.radar_presence = 0U;
  s_status.radar_distance_cm = 0U;
  s_status.radar_zone = RADAR_ZONE_UNKNOWN;
  s_status.radar_peak_gate = 0U;
  s_status.radar_peak_gate_cm = 0U;
  s_status.radar_peak_energy = 0U;
  s_status.radar_energy_sum = 0U;
  s_status.radar_motion_score = 0U;
  s_status.radar_active_gate_count = 0U;
  s_status.radar_occupied_seconds = 0U;
  s_status.radar_still_seconds = 0U;
  s_status.radar_last_seen_age_ms = 0U;
  s_status.env_valid = 0U;
  s_status.gas_valid = 0U;
  s_mq_filtered_ao_mv = 0U;
  s_mq_baseline_ao_mv = 0U;
  s_mq_filter_ready = 0U;
  s_mq_baseline_ready = 0U;
  RadarFeatures_Reset();

  Log_Line("[INFO] sensor mvp init");

  if (Bme280_Init(&hi2c1) == HAL_OK)
  {
    s_bme_ready = 1U;
    (void)snprintf(line, sizeof(line), "[INFO] bme280 ready id=0x%02X addr=0x%02X",
                   Bme280_GetChipId(), BME280_I2C_ADDR_PRIMARY);
    Log_Line(line);
  }
  else
  {
    s_bme_ready = 0U;
    Log_Line("[WARN] bme280 init failed");
  }

  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) == HAL_OK)
  {
    s_adc_ready = 1U;
    Log_Line("[INFO] adc1 calibration ok");
  }
  else
  {
    s_adc_ready = 0U;
    Log_Line("[WARN] adc1 calibration failed");
  }

  rd03_init_result = Rd03V2_Init(&huart3);
  if (Rd03V2_GetStatus(&radar) == HAL_OK)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "%s rd03 config ack open=%u report=%u close=%u init_rx=%lu",
                   (rd03_init_result == HAL_OK) ? "[INFO]" : "[WARN]",
                   (unsigned int)radar.open_command_ack_ok,
                   (unsigned int)radar.report_mode_ack_ok,
                   (unsigned int)radar.close_command_ack_ok,
                   (unsigned long)radar.init_rx_byte_count);
    Log_Line(line);
  }
  else
  {
    Log_Line("[WARN] rd03 status read failed after init");
  }

  s_last_pir = HAL_GPIO_ReadPin(PIR_IN_GPIO_Port, PIR_IN_Pin);
  s_last_rd03 = HAL_GPIO_ReadPin(RD03_OUT_GPIO_Port, RD03_OUT_Pin);
}

void SensorMvp_Update(void)
{
  uint32_t now = HAL_GetTick();

  Rd03V2_Update();

  if ((now - s_last_radar_log_tick) >= SENSOR_MVP_RADAR_LOG_PERIOD_MS)
  {
    s_last_radar_log_tick = now;
    Log_Radar_Protocol_Status();
  }

  if ((now - s_last_env_tick) >= SENSOR_MVP_ENV_PERIOD_MS)
  {
    s_last_env_tick = now;
    Update_Environment();
  }

  if ((now - s_last_digital_tick) >= SENSOR_MVP_DIGITAL_PERIOD_MS)
  {
    s_last_digital_tick = now;
    Update_Digital_And_Adc();
  }
}

HAL_StatusTypeDef SensorMvp_GetStatus(SensorMvp_Status_t *status)
{
  if (status == NULL)
  {
    return HAL_ERROR;
  }

  *status = s_status;
  return HAL_OK;
}
