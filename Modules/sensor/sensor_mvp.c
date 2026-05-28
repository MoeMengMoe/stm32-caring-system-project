#include "sensor_mvp.h"

#include "adc.h"
#include "bme280.h"
#include "gpio.h"
#include "i2c.h"
#include "main.h"
#include "usart.h"

#include <stdio.h>

#define SENSOR_MVP_ENV_PERIOD_MS      (2000U)
#define SENSOR_MVP_DIGITAL_PERIOD_MS  (500U)
#define SENSOR_MVP_RD03_UART_BUDGET   (16U)
#define SENSOR_MVP_MQ_R_TOP_OHM       (2000U)
#define SENSOR_MVP_MQ_R_BOTTOM_OHM    (3300U)

static SensorMvp_LogFn s_log;
static uint32_t s_last_env_tick;
static uint32_t s_last_digital_tick;
static uint8_t s_bme_ready;
static uint8_t s_adc_ready;
static GPIO_PinState s_last_pir = GPIO_PIN_RESET;
static GPIO_PinState s_last_rd03 = GPIO_PIN_RESET;

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
  uint32_t adc_value;
  uint32_t adc_mv;

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

  adc_mv = (adc_value * 3300U) / 4095U;
  *raw = (uint16_t)adc_value;
  *adc_millivolt = (uint16_t)adc_mv;
  *ao_est_millivolt = (uint16_t)((adc_mv * (SENSOR_MVP_MQ_R_TOP_OHM + SENSOR_MVP_MQ_R_BOTTOM_OHM)) /
                                 SENSOR_MVP_MQ_R_BOTTOM_OHM);
  return HAL_OK;
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
  (void)snprintf(line, sizeof(line), "[ENV] temp=%sC hum=%s%% pressure=%shPa",
                 temp_text, hum_text, pressure_text);
  Log_Line(line);
}

static void Update_Digital_And_Adc(void)
{
  char line[96];
  uint16_t mq_raw = 0U;
  uint16_t mq_adc_mv = 0U;
  uint16_t mq_ao_est_mv = 0U;
  GPIO_PinState pir = HAL_GPIO_ReadPin(PIR_IN_GPIO_Port, PIR_IN_Pin);
  GPIO_PinState rd03 = HAL_GPIO_ReadPin(RD03_OUT_GPIO_Port, RD03_OUT_Pin);

  if (s_adc_ready != 0U && Read_Mq_Adc(&mq_raw, &mq_adc_mv, &mq_ao_est_mv) != HAL_OK)
  {
    Log_Line("[WARN] mq adc read failed");
  }

  (void)snprintf(line, sizeof(line), "[DETECT] pir=%u rd03=%u mq_raw=%u mq_adc_mv=%u mq_ao_est_mv=%u",
                 (unsigned int)(pir == GPIO_PIN_SET),
                 (unsigned int)(rd03 == GPIO_PIN_SET),
                 (unsigned int)mq_raw,
                 (unsigned int)mq_adc_mv,
                 (unsigned int)mq_ao_est_mv);
  Log_Line(line);

  if (pir != s_last_pir)
  {
    Log_Line((pir == GPIO_PIN_SET) ? "[EVENT] pir active" : "[EVENT] pir inactive");
    s_last_pir = pir;
  }

  if (rd03 != s_last_rd03)
  {
    Log_Line((rd03 == GPIO_PIN_SET) ? "[EVENT] rd03 active" : "[EVENT] rd03 inactive");
    s_last_rd03 = rd03;
  }
}

static void Drain_Rd03_Uart(void)
{
  char line[40];
  uint8_t byte;

  for (uint8_t i = 0U; i < SENSOR_MVP_RD03_UART_BUDGET; i++)
  {
    if (HAL_UART_Receive(&huart3, &byte, 1U, 0U) != HAL_OK)
    {
      break;
    }

    (void)snprintf(line, sizeof(line), "[RD03] uart byte=0x%02X", byte);
    Log_Line(line);
  }
}

void SensorMvp_Init(SensorMvp_LogFn log_fn)
{
  char line[64];

  s_log = log_fn;
  s_last_env_tick = HAL_GetTick();
  s_last_digital_tick = HAL_GetTick();

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

  s_last_pir = HAL_GPIO_ReadPin(PIR_IN_GPIO_Port, PIR_IN_Pin);
  s_last_rd03 = HAL_GPIO_ReadPin(RD03_OUT_GPIO_Port, RD03_OUT_Pin);
}

void SensorMvp_Update(void)
{
  uint32_t now = HAL_GetTick();

  Drain_Rd03_Uart();

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
