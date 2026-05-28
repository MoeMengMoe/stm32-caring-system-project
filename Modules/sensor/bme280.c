#include "bme280.h"

#define BME280_REG_CHIP_ID     (0xD0U)
#define BME280_REG_RESET       (0xE0U)
#define BME280_REG_CTRL_HUM    (0xF2U)
#define BME280_REG_STATUS      (0xF3U)
#define BME280_REG_CTRL_MEAS   (0xF4U)
#define BME280_REG_CONFIG      (0xF5U)
#define BME280_REG_PRESS_MSB   (0xF7U)
#define BME280_REG_CALIB_00    (0x88U)
#define BME280_REG_CALIB_26    (0xE1U)

#define BME280_RESET_VALUE     (0xB6U)
#define BME280_TIMEOUT_MS      (100U)

typedef struct
{
  uint16_t dig_T1;
  int16_t dig_T2;
  int16_t dig_T3;
  uint16_t dig_P1;
  int16_t dig_P2;
  int16_t dig_P3;
  int16_t dig_P4;
  int16_t dig_P5;
  int16_t dig_P6;
  int16_t dig_P7;
  int16_t dig_P8;
  int16_t dig_P9;
  uint8_t dig_H1;
  int16_t dig_H2;
  uint8_t dig_H3;
  int16_t dig_H4;
  int16_t dig_H5;
  int8_t dig_H6;
  int32_t t_fine;
} Bme280_Calib_t;

static I2C_HandleTypeDef *s_hi2c;
static Bme280_Calib_t s_calib;
static uint8_t s_chip_id;

static uint16_t Read_U16_LE(const uint8_t *buffer)
{
  return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8));
}

static int16_t Read_S16_LE(const uint8_t *buffer)
{
  return (int16_t)Read_U16_LE(buffer);
}

static HAL_StatusTypeDef Read_Register(uint8_t reg, uint8_t *data, uint16_t len)
{
  return HAL_I2C_Mem_Read(s_hi2c, (uint16_t)(BME280_I2C_ADDR_PRIMARY << 1U), reg,
                          I2C_MEMADD_SIZE_8BIT, data, len, BME280_TIMEOUT_MS);
}

static HAL_StatusTypeDef Write_Register(uint8_t reg, uint8_t value)
{
  return HAL_I2C_Mem_Write(s_hi2c, (uint16_t)(BME280_I2C_ADDR_PRIMARY << 1U), reg,
                           I2C_MEMADD_SIZE_8BIT, &value, 1U, BME280_TIMEOUT_MS);
}

static HAL_StatusTypeDef Read_Calibration(void)
{
  uint8_t calib1[26];
  uint8_t calib2[7];

  if (Read_Register(BME280_REG_CALIB_00, calib1, sizeof(calib1)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (Read_Register(BME280_REG_CALIB_26, calib2, sizeof(calib2)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  s_calib.dig_T1 = Read_U16_LE(&calib1[0]);
  s_calib.dig_T2 = Read_S16_LE(&calib1[2]);
  s_calib.dig_T3 = Read_S16_LE(&calib1[4]);
  s_calib.dig_P1 = Read_U16_LE(&calib1[6]);
  s_calib.dig_P2 = Read_S16_LE(&calib1[8]);
  s_calib.dig_P3 = Read_S16_LE(&calib1[10]);
  s_calib.dig_P4 = Read_S16_LE(&calib1[12]);
  s_calib.dig_P5 = Read_S16_LE(&calib1[14]);
  s_calib.dig_P6 = Read_S16_LE(&calib1[16]);
  s_calib.dig_P7 = Read_S16_LE(&calib1[18]);
  s_calib.dig_P8 = Read_S16_LE(&calib1[20]);
  s_calib.dig_P9 = Read_S16_LE(&calib1[22]);
  s_calib.dig_H1 = calib1[25];
  s_calib.dig_H2 = Read_S16_LE(&calib2[0]);
  s_calib.dig_H3 = calib2[2];
  s_calib.dig_H4 = (int16_t)(((int16_t)calib2[3] << 4) | (calib2[4] & 0x0FU));
  s_calib.dig_H5 = (int16_t)(((int16_t)calib2[5] << 4) | (calib2[4] >> 4));
  s_calib.dig_H6 = (int8_t)calib2[6];

  return HAL_OK;
}

static int32_t Compensate_Temperature(int32_t adc_t)
{
  int32_t var1;
  int32_t var2;

  var1 = ((((adc_t >> 3) - ((int32_t)s_calib.dig_T1 << 1))) * ((int32_t)s_calib.dig_T2)) >> 11;
  var2 = (((((adc_t >> 4) - ((int32_t)s_calib.dig_T1)) *
            ((adc_t >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) *
          ((int32_t)s_calib.dig_T3)) >> 14;

  s_calib.t_fine = var1 + var2;
  return (s_calib.t_fine * 5 + 128) >> 8;
}

static uint32_t Compensate_Pressure(int32_t adc_p)
{
  int64_t var1;
  int64_t var2;
  int64_t pressure;

  var1 = ((int64_t)s_calib.t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)s_calib.dig_P6;
  var2 = var2 + ((var1 * (int64_t)s_calib.dig_P5) << 17);
  var2 = var2 + (((int64_t)s_calib.dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)s_calib.dig_P3) >> 8) + ((var1 * (int64_t)s_calib.dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)s_calib.dig_P1) >> 33;

  if (var1 == 0)
  {
    return 0U;
  }

  pressure = 1048576 - adc_p;
  pressure = (((pressure << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)s_calib.dig_P9) * (pressure >> 13) * (pressure >> 13)) >> 25;
  var2 = (((int64_t)s_calib.dig_P8) * pressure) >> 19;
  pressure = ((pressure + var1 + var2) >> 8) + (((int64_t)s_calib.dig_P7) << 4);

  return (uint32_t)(pressure / 256);
}

static uint32_t Compensate_Humidity(int32_t adc_h)
{
  int32_t v_x1;

  v_x1 = s_calib.t_fine - 76800;
  v_x1 = (((((adc_h << 14) - (((int32_t)s_calib.dig_H4) << 20) -
             (((int32_t)s_calib.dig_H5) * v_x1)) + 16384) >> 15) *
          (((((((v_x1 * ((int32_t)s_calib.dig_H6)) >> 10) *
               (((v_x1 * ((int32_t)s_calib.dig_H3)) >> 11) + 32768)) >> 10) +
             2097152) * ((int32_t)s_calib.dig_H2) + 8192) >> 14));
  v_x1 = v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)s_calib.dig_H1)) >> 4);

  if (v_x1 < 0)
  {
    v_x1 = 0;
  }

  if (v_x1 > 419430400)
  {
    v_x1 = 419430400;
  }

  return (uint32_t)((v_x1 >> 12) * 100 / 1024);
}

HAL_StatusTypeDef Bme280_Init(I2C_HandleTypeDef *hi2c)
{
  uint8_t status;

  s_hi2c = hi2c;
  if (s_hi2c == NULL)
  {
    return HAL_ERROR;
  }

  if (Read_Register(BME280_REG_CHIP_ID, &s_chip_id, 1U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (s_chip_id != BME280_CHIP_ID_VALUE)
  {
    return HAL_ERROR;
  }

  (void)Write_Register(BME280_REG_RESET, BME280_RESET_VALUE);
  HAL_Delay(5U);

  do
  {
    if (Read_Register(BME280_REG_STATUS, &status, 1U) != HAL_OK)
    {
      return HAL_ERROR;
    }
  } while ((status & 0x01U) != 0U);

  if (Read_Calibration() != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (Write_Register(BME280_REG_CONFIG, 0x00U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (Write_Register(BME280_REG_CTRL_HUM, 0x01U) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return Write_Register(BME280_REG_CTRL_MEAS, 0x27U);
}

HAL_StatusTypeDef Bme280_Read(Bme280_Data_t *data)
{
  uint8_t raw[8];
  int32_t adc_p;
  int32_t adc_t;
  int32_t adc_h;

  if (data == NULL || s_hi2c == NULL)
  {
    return HAL_ERROR;
  }

  if (Read_Register(BME280_REG_PRESS_MSB, raw, sizeof(raw)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  adc_p = (int32_t)((((uint32_t)raw[0]) << 12) | (((uint32_t)raw[1]) << 4) | (((uint32_t)raw[2]) >> 4));
  adc_t = (int32_t)((((uint32_t)raw[3]) << 12) | (((uint32_t)raw[4]) << 4) | (((uint32_t)raw[5]) >> 4));
  adc_h = (int32_t)((((uint32_t)raw[6]) << 8) | ((uint32_t)raw[7]));

  data->temperature_centi_c = Compensate_Temperature(adc_t);
  data->pressure_centi_hpa = Compensate_Pressure(adc_p);
  data->humidity_centi_pct = Compensate_Humidity(adc_h);

  return HAL_OK;
}

uint8_t Bme280_GetChipId(void)
{
  return s_chip_id;
}
