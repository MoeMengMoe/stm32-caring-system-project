#include "status_display.h"

#include "tft_lcd.h"

#include <stdio.h>
#include <string.h>

#define DISPLAY_FIELD_COUNT  (6U)
#define DISPLAY_FIELD_CHARS  (11U)
#define DISPLAY_VALUE_SCALE  (2U)
#define STATUS_DISPLAY_DIAGNOSTIC  (0U)

#define COLOR_BACKGROUND  TFT_LCD_RGB565(7U, 18U, 28U)
#define COLOR_PANEL       TFT_LCD_RGB565(13U, 32U, 45U)
#define COLOR_HEADER      TFT_LCD_RGB565(0U, 105U, 112U)
#define COLOR_BORDER      TFT_LCD_RGB565(40U, 78U, 92U)
#define COLOR_TEXT        TFT_LCD_RGB565(231U, 244U, 247U)
#define COLOR_MUTED       TFT_LCD_RGB565(130U, 162U, 172U)
#define COLOR_CYAN        TFT_LCD_RGB565(76U, 201U, 240U)
#define COLOR_GREEN       TFT_LCD_RGB565(82U, 210U, 132U)
#define COLOR_YELLOW      TFT_LCD_RGB565(255U, 204U, 77U)
#define COLOR_RED         TFT_LCD_RGB565(255U, 91U, 91U)
#define COLOR_BLACK       TFT_LCD_RGB565(0U, 0U, 0U)
#define COLOR_WHITE       TFT_LCD_RGB565(255U, 255U, 255U)
#define COLOR_BLUE        TFT_LCD_RGB565(66U, 135U, 245U)

typedef struct
{
  uint16_t x;
  uint16_t y;
  uint16_t foreground;
  char desired[DISPLAY_FIELD_CHARS + 1U];
  char shown[DISPLAY_FIELD_CHARS + 1U];
} DisplayField_t;

enum
{
  FIELD_TEMP = 0,
  FIELD_HUMIDITY,
  FIELD_GAS,
  FIELD_PRESENCE,
  FIELD_RADAR,
  FIELD_RISK
};

static StatusDisplay_LogFn s_log;
static uint8_t s_ready;
static DisplayField_t s_fields[DISPLAY_FIELD_COUNT] =
{
  {16U, 66U, COLOR_TEXT, {0}, {0}},
  {170U, 66U, COLOR_TEXT, {0}, {0}},
  {16U, 128U, COLOR_TEXT, {0}, {0}},
  {170U, 128U, COLOR_TEXT, {0}, {0}},
  {16U, 190U, COLOR_TEXT, {0}, {0}},
  {170U, 190U, COLOR_TEXT, {0}, {0}}
};

static void Log_Line(const char *text)
{
  if (s_log != NULL)
  {
    s_log(text);
  }
}

static HAL_StatusTypeDef DrawPanel(uint16_t x, uint16_t y, const char *label)
{
  if ((TftLcd_FillRect(x, y, 142U, 54U, COLOR_PANEL) != HAL_OK) ||
      (TftLcd_FillRect(x, y, 142U, 1U, COLOR_BORDER) != HAL_OK) ||
      (TftLcd_FillRect(x, (uint16_t)(y + 53U), 142U, 1U, COLOR_BORDER) != HAL_OK) ||
      (TftLcd_FillRect(x, y, 1U, 54U, COLOR_BORDER) != HAL_OK) ||
      (TftLcd_FillRect((uint16_t)(x + 141U), y, 1U, 54U, COLOR_BORDER) != HAL_OK) ||
      (TftLcd_DrawText((uint16_t)(x + 8U), (uint16_t)(y + 7U), label, COLOR_MUTED, COLOR_PANEL, 1U) != HAL_OK))
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

#if STATUS_DISPLAY_DIAGNOSTIC != 0U
static HAL_StatusTypeDef DrawDiagnosticPattern(void)
{
  if ((TftLcd_FillScreen(COLOR_BLACK) != HAL_OK) ||
      (TftLcd_FillRect(0U, 0U, 320U, 40U, COLOR_WHITE) != HAL_OK) ||
      (TftLcd_FillRect(0U, 40U, 320U, 40U, COLOR_RED) != HAL_OK) ||
      (TftLcd_FillRect(0U, 80U, 320U, 40U, COLOR_GREEN) != HAL_OK) ||
      (TftLcd_FillRect(0U, 120U, 320U, 40U, COLOR_BLUE) != HAL_OK) ||
      (TftLcd_FillRect(0U, 160U, 320U, 40U, COLOR_BLACK) != HAL_OK) ||
      (TftLcd_FillRect(0U, 200U, 320U, 40U, COLOR_YELLOW) != HAL_OK) ||
      (TftLcd_FillRect(0U, 0U, 28U, 28U, COLOR_RED) != HAL_OK) ||
      (TftLcd_FillRect(292U, 0U, 28U, 28U, COLOR_GREEN) != HAL_OK) ||
      (TftLcd_FillRect(0U, 212U, 28U, 28U, COLOR_BLUE) != HAL_OK) ||
      (TftLcd_FillRect(292U, 212U, 28U, 28U, COLOR_WHITE) != HAL_OK) ||
      (TftLcd_DrawText(94U, 104U, "TFT TEST", COLOR_WHITE, COLOR_BLACK, 2U) != HAL_OK) ||
      (TftLcd_DrawText(88U, 132U, "RGB565 WRITE", COLOR_WHITE, COLOR_BLACK, 1U) != HAL_OK))
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}
#endif

static void SetField(uint32_t index, const char *text, uint16_t foreground)
{
  DisplayField_t *field;
  size_t length;

  if ((index >= DISPLAY_FIELD_COUNT) || (text == NULL))
  {
    return;
  }

  field = &s_fields[index];
  length = strlen(text);
  if (length > DISPLAY_FIELD_CHARS)
  {
    length = DISPLAY_FIELD_CHARS;
  }

  memset(field->desired, ' ', DISPLAY_FIELD_CHARS);
  memcpy(field->desired, text, length);
  field->desired[DISPLAY_FIELD_CHARS] = '\0';

  if (field->foreground != foreground)
  {
    field->foreground = foreground;
    memset(field->shown, 0, sizeof(field->shown));
  }
}

HAL_StatusTypeDef StatusDisplay_Init(SPI_HandleTypeDef *hspi, StatusDisplay_LogFn log_fn)
{
  s_log = log_fn;
  s_ready = 0U;

  if (TftLcd_Init(hspi, log_fn) != HAL_OK)
  {
    Log_Line("[WARN] status display init failed");
    return HAL_ERROR;
  }

#if STATUS_DISPLAY_DIAGNOSTIC != 0U
  if (DrawDiagnosticPattern() != HAL_OK)
  {
    Log_Line("[WARN] status display diagnostic failed");
    return HAL_ERROR;
  }

  Log_Line("[INFO] status display diagnostic pattern ready");
  return HAL_OK;
#endif

  if ((TftLcd_FillScreen(COLOR_BACKGROUND) != HAL_OK) ||
      (TftLcd_FillRect(0U, 0U, TFT_LCD_WIDTH, 32U, COLOR_HEADER) != HAL_OK) ||
      (TftLcd_DrawText(12U, 8U, "CARING NODE", COLOR_TEXT, COLOR_HEADER, 2U) != HAL_OK) ||
      (TftLcd_DrawText(226U, 12U, "LOCAL MONITOR", COLOR_TEXT, COLOR_HEADER, 1U) != HAL_OK) ||
      (DrawPanel(8U, 42U, "TEMPERATURE") != HAL_OK) ||
      (DrawPanel(162U, 42U, "HUMIDITY") != HAL_OK) ||
      (DrawPanel(8U, 104U, "GAS AO") != HAL_OK) ||
      (DrawPanel(162U, 104U, "PRESENCE") != HAL_OK) ||
      (DrawPanel(8U, 166U, "RADAR ZONE") != HAL_OK) ||
      (DrawPanel(162U, 166U, "RISK LEVEL") != HAL_OK))
  {
    Log_Line("[WARN] status display layout failed");
    return HAL_ERROR;
  }

  for (uint32_t i = 0U; i < DISPLAY_FIELD_COUNT; i++)
  {
    memset(s_fields[i].desired, ' ', DISPLAY_FIELD_CHARS);
    memset(s_fields[i].shown, 0, sizeof(s_fields[i].shown));
    s_fields[i].desired[DISPLAY_FIELD_CHARS] = '\0';
  }

  SetField(FIELD_TEMP, "--.- C", COLOR_TEXT);
  SetField(FIELD_HUMIDITY, "--.- %", COLOR_TEXT);
  SetField(FIELD_GAS, "--- mV", COLOR_TEXT);
  SetField(FIELD_PRESENCE, "UNKNOWN", COLOR_MUTED);
  SetField(FIELD_RADAR, "NO DATA", COLOR_MUTED);
  SetField(FIELD_RISK, "STARTING", COLOR_MUTED);

  s_ready = 1U;
  Log_Line("[INFO] status display ready");
  return HAL_OK;
}

void StatusDisplay_SetStatus(const SensorMvp_Status_t *status, int risk)
{
  char text[20];
  uint16_t gas_color = COLOR_TEXT;

  if ((s_ready == 0U) || (status == NULL))
  {
    return;
  }

  if (status->env_valid != 0U)
  {
    (void)snprintf(text, sizeof(text), "%.1f C", status->temperature_c);
    SetField(FIELD_TEMP, text, COLOR_CYAN);
    (void)snprintf(text, sizeof(text), "%.1f %%", status->humidity_pct);
    SetField(FIELD_HUMIDITY, text, COLOR_CYAN);
  }
  else
  {
    SetField(FIELD_TEMP, "--.- C", COLOR_MUTED);
    SetField(FIELD_HUMIDITY, "--.- %", COLOR_MUTED);
  }

  if (status->gas_valid != 0U)
  {
    if (status->gas >= 3000)
    {
      gas_color = COLOR_RED;
    }
    else if (status->gas >= 2000)
    {
      gas_color = COLOR_YELLOW;
    }
    (void)snprintf(text, sizeof(text), "%d mV", status->gas);
    SetField(FIELD_GAS, text, gas_color);
  }
  else
  {
    SetField(FIELD_GAS, "--- mV", COLOR_MUTED);
  }

  SetField(FIELD_PRESENCE, status->presence != 0 ? "YES" : "NO", status->presence != 0 ? COLOR_GREEN : COLOR_MUTED);

  if (status->radar_valid != 0U)
  {
    if (status->radar_presence != 0U)
    {
      (void)snprintf(text,
                     sizeof(text),
                     "%ucm Z%u",
                     (unsigned int)status->radar_distance_cm,
                     (unsigned int)status->radar_zone);
      SetField(FIELD_RADAR, text, COLOR_CYAN);
    }
    else
    {
      SetField(FIELD_RADAR, "CLEAR", COLOR_MUTED);
    }
  }
  else
  {
    SetField(FIELD_RADAR, "NO DATA", COLOR_MUTED);
  }

  switch (risk)
  {
    case 1:
      SetField(FIELD_RISK, "NOTICE", COLOR_YELLOW);
      break;
    case 2:
      SetField(FIELD_RISK, "WARNING", COLOR_YELLOW);
      break;
    case 3:
      SetField(FIELD_RISK, "ALARM", COLOR_RED);
      break;
    default:
      SetField(FIELD_RISK, "NORMAL", COLOR_GREEN);
      break;
  }
}

void StatusDisplay_Process(void)
{
  if (s_ready == 0U)
  {
    return;
  }

  /*
   * Draw one character cell per call. This cooperative refresh keeps each SPI
   * transaction short so the current polling-based radar UART parser continues
   * to receive bytes between display updates.
   */
  for (uint32_t field_index = 0U; field_index < DISPLAY_FIELD_COUNT; field_index++)
  {
    DisplayField_t *field = &s_fields[field_index];
    for (uint32_t char_index = 0U; char_index < DISPLAY_FIELD_CHARS; char_index++)
    {
      if (field->shown[char_index] != field->desired[char_index])
      {
        uint16_t x = (uint16_t)(field->x + (char_index * 6U * DISPLAY_VALUE_SCALE));
        if (TftLcd_DrawChar(x,
                            field->y,
                            field->desired[char_index],
                            field->foreground,
                            COLOR_PANEL,
                            DISPLAY_VALUE_SCALE) == HAL_OK)
        {
          field->shown[char_index] = field->desired[char_index];
        }
        return;
      }
    }
  }
}
