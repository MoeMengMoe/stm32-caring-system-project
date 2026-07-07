#include "tft_lcd.h"

#include "main.h"

#include <stdio.h>
#include <string.h>

#define TFT_CMD_SWRESET  (0x01U)
#define TFT_CMD_SLPOUT   (0x11U)
#define TFT_CMD_INVOFF   (0x20U)
#define TFT_CMD_INVON    (0x21U)
#define TFT_CMD_DISPON   (0x29U)
#define TFT_CMD_CASET    (0x2AU)
#define TFT_CMD_RASET    (0x2BU)
#define TFT_CMD_RAMWR    (0x2CU)
#define TFT_CMD_MADCTL   (0x36U)
#define TFT_CMD_COLMOD   (0x3AU)

#define TFT_SPI_TIMEOUT_MS  (50U)
#define TFT_FILL_TILE_WIDTH  (128U)
#define TFT_FILL_TILE_ROWS   (4U)
#define TFT_TEXT_TILE_ROWS   (2U)
#define TFT_FONT_FIRST_CHAR  (32U)
#define TFT_FONT_LAST_CHAR   (90U)
#define TFT_FONT_WIDTH       (5U)
#define TFT_FONT_HEIGHT      (7U)
#define TFT_MADCTL_LANDSCAPE_ROT180 (0xA8U)
#define TFT_TX_BUFFER_BYTES  (TFT_FILL_TILE_WIDTH * TFT_FILL_TILE_ROWS * 2U)

static SPI_HandleTypeDef *s_hspi;
static TftLcd_LogFn s_log;
static TftLcd_Controller_t s_controller = TFT_LCD_CONTROLLER_UNKNOWN;
static uint8_t s_tx_buffer[TFT_TX_BUFFER_BYTES];

/* ASCII 0x20 through 0x5A, stored as five vertical columns per character. */
static const uint8_t s_font_5x7[] =
{
  0x00, 0x00, 0x00, 0x00, 0x00, /*   */
  0x00, 0x00, 0x5F, 0x00, 0x00, /* ! */
  0x00, 0x07, 0x00, 0x07, 0x00, /* " */
  0x14, 0x7F, 0x14, 0x7F, 0x14, /* # */
  0x24, 0x2A, 0x7F, 0x2A, 0x12, /* $ */
  0x23, 0x13, 0x08, 0x64, 0x62, /* % */
  0x36, 0x49, 0x55, 0x22, 0x50, /* & */
  0x00, 0x05, 0x03, 0x00, 0x00, /* ' */
  0x00, 0x1C, 0x22, 0x41, 0x00, /* ( */
  0x00, 0x41, 0x22, 0x1C, 0x00, /* ) */
  0x14, 0x08, 0x3E, 0x08, 0x14, /* * */
  0x08, 0x08, 0x3E, 0x08, 0x08, /* + */
  0x00, 0x50, 0x30, 0x00, 0x00, /* , */
  0x08, 0x08, 0x08, 0x08, 0x08, /* - */
  0x00, 0x60, 0x60, 0x00, 0x00, /* . */
  0x20, 0x10, 0x08, 0x04, 0x02, /* / */
  0x3E, 0x51, 0x49, 0x45, 0x3E, /* 0 */
  0x00, 0x42, 0x7F, 0x40, 0x00, /* 1 */
  0x42, 0x61, 0x51, 0x49, 0x46, /* 2 */
  0x21, 0x41, 0x45, 0x4B, 0x31, /* 3 */
  0x18, 0x14, 0x12, 0x7F, 0x10, /* 4 */
  0x27, 0x45, 0x45, 0x45, 0x39, /* 5 */
  0x3C, 0x4A, 0x49, 0x49, 0x30, /* 6 */
  0x01, 0x71, 0x09, 0x05, 0x03, /* 7 */
  0x36, 0x49, 0x49, 0x49, 0x36, /* 8 */
  0x06, 0x49, 0x49, 0x29, 0x1E, /* 9 */
  0x00, 0x36, 0x36, 0x00, 0x00, /* : */
  0x00, 0x56, 0x36, 0x00, 0x00, /* ; */
  0x08, 0x14, 0x22, 0x41, 0x00, /* < */
  0x14, 0x14, 0x14, 0x14, 0x14, /* = */
  0x00, 0x41, 0x22, 0x14, 0x08, /* > */
  0x02, 0x01, 0x51, 0x09, 0x06, /* ? */
  0x32, 0x49, 0x79, 0x41, 0x3E, /* @ */
  0x7E, 0x11, 0x11, 0x11, 0x7E, /* A */
  0x7F, 0x49, 0x49, 0x49, 0x36, /* B */
  0x3E, 0x41, 0x41, 0x41, 0x22, /* C */
  0x7F, 0x41, 0x41, 0x22, 0x1C, /* D */
  0x7F, 0x49, 0x49, 0x49, 0x41, /* E */
  0x7F, 0x09, 0x09, 0x09, 0x01, /* F */
  0x3E, 0x41, 0x49, 0x49, 0x7A, /* G */
  0x7F, 0x08, 0x08, 0x08, 0x7F, /* H */
  0x00, 0x41, 0x7F, 0x41, 0x00, /* I */
  0x20, 0x40, 0x41, 0x3F, 0x01, /* J */
  0x7F, 0x08, 0x14, 0x22, 0x41, /* K */
  0x7F, 0x40, 0x40, 0x40, 0x40, /* L */
  0x7F, 0x02, 0x0C, 0x02, 0x7F, /* M */
  0x7F, 0x04, 0x08, 0x10, 0x7F, /* N */
  0x3E, 0x41, 0x41, 0x41, 0x3E, /* O */
  0x7F, 0x09, 0x09, 0x09, 0x06, /* P */
  0x3E, 0x41, 0x51, 0x21, 0x5E, /* Q */
  0x7F, 0x09, 0x19, 0x29, 0x46, /* R */
  0x46, 0x49, 0x49, 0x49, 0x31, /* S */
  0x01, 0x01, 0x7F, 0x01, 0x01, /* T */
  0x3F, 0x40, 0x40, 0x40, 0x3F, /* U */
  0x1F, 0x20, 0x40, 0x20, 0x1F, /* V */
  0x3F, 0x40, 0x38, 0x40, 0x3F, /* W */
  0x63, 0x14, 0x08, 0x14, 0x63, /* X */
  0x07, 0x08, 0x70, 0x08, 0x07, /* Y */
  0x61, 0x51, 0x49, 0x45, 0x43  /* Z */
};

static void Log_Line(const char *text)
{
  if (s_log != NULL)
  {
    s_log(text);
  }
}

static void ChipSelect(uint8_t selected)
{
  HAL_GPIO_WritePin(TFT_CS_GPIO_Port, TFT_CS_Pin, selected != 0U ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static HAL_StatusTypeDef WriteCommand(uint8_t command)
{
  HAL_StatusTypeDef result;

  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_RESET);
  ChipSelect(1U);
  result = HAL_SPI_Transmit(s_hspi, &command, 1U, TFT_SPI_TIMEOUT_MS);
  ChipSelect(0U);
  return result;
}

static HAL_StatusTypeDef WriteData(const uint8_t *data, uint16_t length)
{
  HAL_StatusTypeDef result;

  if ((data == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  HAL_GPIO_WritePin(TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);
  ChipSelect(1U);
  result = HAL_SPI_Transmit(s_hspi, (uint8_t *)data, length, TFT_SPI_TIMEOUT_MS);
  ChipSelect(0U);
  return result;
}

static HAL_StatusTypeDef WriteCommandData(uint8_t command, const uint8_t *data, uint16_t length)
{
  if (WriteCommand(command) != HAL_OK)
  {
    return HAL_ERROR;
  }

  if (length == 0U)
  {
    return HAL_OK;
  }

  return WriteData(data, length);
}

static void HardwareReset(void)
{
  ChipSelect(0U);
  HAL_GPIO_WritePin(TFT_BL_GPIO_Port, TFT_BL_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(20U);
  HAL_GPIO_WritePin(TFT_RST_GPIO_Port, TFT_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(150U);
}

static const uint8_t *GetGlyph(char ch)
{
  uint8_t code = (uint8_t)ch;

  if ((code >= (uint8_t)'a') && (code <= (uint8_t)'z'))
  {
    code = (uint8_t)(code - ((uint8_t)'a' - (uint8_t)'A'));
  }
  if ((code < TFT_FONT_FIRST_CHAR) || (code > TFT_FONT_LAST_CHAR))
  {
    code = (uint8_t)'?';
  }

  return &s_font_5x7[(uint32_t)(code - TFT_FONT_FIRST_CHAR) * TFT_FONT_WIDTH];
}

static HAL_StatusTypeDef InitIli9341(void)
{
  static const uint8_t d_ef[] = {0x03U, 0x80U, 0x02U};
  static const uint8_t d_cf[] = {0x00U, 0xC1U, 0x30U};
  static const uint8_t d_ed[] = {0x64U, 0x03U, 0x12U, 0x81U};
  static const uint8_t d_e8[] = {0x85U, 0x00U, 0x78U};
  static const uint8_t d_cb[] = {0x39U, 0x2CU, 0x00U, 0x34U, 0x02U};
  static const uint8_t d_f7[] = {0x20U};
  static const uint8_t d_ea[] = {0x00U, 0x00U};
  static const uint8_t d_c0[] = {0x23U};
  static const uint8_t d_c1[] = {0x10U};
  static const uint8_t d_c5[] = {0x3EU, 0x28U};
  static const uint8_t d_c7[] = {0x86U};
  static const uint8_t d_36[] = {TFT_MADCTL_LANDSCAPE_ROT180};
  static const uint8_t d_3a[] = {0x55U};
  static const uint8_t d_b1[] = {0x00U, 0x18U};
  static const uint8_t d_b6[] = {0x08U, 0x82U, 0x27U};
  static const uint8_t d_f2[] = {0x00U};
  static const uint8_t d_26[] = {0x01U};
  static const uint8_t d_e0[] = {0x0FU, 0x31U, 0x2BU, 0x0CU, 0x0EU, 0x08U, 0x4EU, 0xF1U,
                                 0x37U, 0x07U, 0x10U, 0x03U, 0x0EU, 0x09U, 0x00U};
  static const uint8_t d_e1[] = {0x00U, 0x0EU, 0x14U, 0x03U, 0x11U, 0x07U, 0x31U, 0xC1U,
                                 0x48U, 0x08U, 0x0FU, 0x0CU, 0x31U, 0x36U, 0x0FU};

  if ((WriteCommandData(0xEFU, d_ef, sizeof(d_ef)) != HAL_OK) ||
      (WriteCommandData(0xCFU, d_cf, sizeof(d_cf)) != HAL_OK) ||
      (WriteCommandData(0xEDU, d_ed, sizeof(d_ed)) != HAL_OK) ||
      (WriteCommandData(0xE8U, d_e8, sizeof(d_e8)) != HAL_OK) ||
      (WriteCommandData(0xCBU, d_cb, sizeof(d_cb)) != HAL_OK) ||
      (WriteCommandData(0xF7U, d_f7, sizeof(d_f7)) != HAL_OK) ||
      (WriteCommandData(0xEAU, d_ea, sizeof(d_ea)) != HAL_OK) ||
      (WriteCommandData(0xC0U, d_c0, sizeof(d_c0)) != HAL_OK) ||
      (WriteCommandData(0xC1U, d_c1, sizeof(d_c1)) != HAL_OK) ||
      (WriteCommandData(0xC5U, d_c5, sizeof(d_c5)) != HAL_OK) ||
      (WriteCommandData(0xC7U, d_c7, sizeof(d_c7)) != HAL_OK) ||
      (WriteCommandData(TFT_CMD_MADCTL, d_36, sizeof(d_36)) != HAL_OK) ||
      (WriteCommandData(TFT_CMD_COLMOD, d_3a, sizeof(d_3a)) != HAL_OK) ||
      (WriteCommandData(0xB1U, d_b1, sizeof(d_b1)) != HAL_OK) ||
      (WriteCommandData(0xB6U, d_b6, sizeof(d_b6)) != HAL_OK) ||
      (WriteCommandData(0xF2U, d_f2, sizeof(d_f2)) != HAL_OK) ||
      (WriteCommandData(0x26U, d_26, sizeof(d_26)) != HAL_OK) ||
      (WriteCommandData(0xE0U, d_e0, sizeof(d_e0)) != HAL_OK) ||
      (WriteCommandData(0xE1U, d_e1, sizeof(d_e1)) != HAL_OK))
  {
    return HAL_ERROR;
  }

  if (WriteCommand(TFT_CMD_SLPOUT) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(120U);
  if (WriteCommand(TFT_CMD_DISPON) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(20U);
  return HAL_OK;
}

static HAL_StatusTypeDef InitSt7789(void)
{
  static const uint8_t d_36[] = {TFT_MADCTL_LANDSCAPE_ROT180};
  static const uint8_t d_3a[] = {0x55U};
  static const uint8_t d_b2[] = {0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U};
  static const uint8_t d_b7[] = {0x35U};
  static const uint8_t d_bb[] = {0x19U};
  static const uint8_t d_c0[] = {0x2CU};
  static const uint8_t d_c2[] = {0x01U};
  static const uint8_t d_c3[] = {0x12U};
  static const uint8_t d_c4[] = {0x20U};
  static const uint8_t d_c6[] = {0x0FU};
  static const uint8_t d_d0[] = {0xA4U, 0xA1U};
  static const uint8_t d_e0[] = {0xD0U, 0x04U, 0x0DU, 0x11U, 0x13U, 0x2BU, 0x3FU,
                                 0x54U, 0x4CU, 0x18U, 0x0DU, 0x0BU, 0x1FU, 0x23U};
  static const uint8_t d_e1[] = {0xD0U, 0x04U, 0x0CU, 0x11U, 0x13U, 0x2CU, 0x3FU,
                                 0x44U, 0x51U, 0x2FU, 0x1FU, 0x1FU, 0x20U, 0x23U};

  if (WriteCommand(TFT_CMD_SWRESET) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(150U);
  if (WriteCommand(TFT_CMD_SLPOUT) != HAL_OK)
  {
    return HAL_ERROR;
  }
  HAL_Delay(120U);

  if ((WriteCommandData(TFT_CMD_MADCTL, d_36, sizeof(d_36)) != HAL_OK) ||
      (WriteCommandData(TFT_CMD_COLMOD, d_3a, sizeof(d_3a)) != HAL_OK) ||
      (WriteCommandData(0xB2U, d_b2, sizeof(d_b2)) != HAL_OK) ||
      (WriteCommandData(0xB7U, d_b7, sizeof(d_b7)) != HAL_OK) ||
      (WriteCommandData(0xBBU, d_bb, sizeof(d_bb)) != HAL_OK) ||
      (WriteCommandData(0xC0U, d_c0, sizeof(d_c0)) != HAL_OK) ||
      (WriteCommandData(0xC2U, d_c2, sizeof(d_c2)) != HAL_OK) ||
      (WriteCommandData(0xC3U, d_c3, sizeof(d_c3)) != HAL_OK) ||
      (WriteCommandData(0xC4U, d_c4, sizeof(d_c4)) != HAL_OK) ||
      (WriteCommandData(0xC6U, d_c6, sizeof(d_c6)) != HAL_OK) ||
      (WriteCommandData(0xD0U, d_d0, sizeof(d_d0)) != HAL_OK) ||
      (WriteCommandData(0xE0U, d_e0, sizeof(d_e0)) != HAL_OK) ||
      (WriteCommandData(0xE1U, d_e1, sizeof(d_e1)) != HAL_OK) ||
      (WriteCommand(TFT_CMD_INVOFF) != HAL_OK) ||
      (WriteCommand(TFT_CMD_DISPON) != HAL_OK))
  {
    return HAL_ERROR;
  }

  HAL_Delay(20U);
  return HAL_OK;
}

static HAL_StatusTypeDef SetAddressWindow(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
  uint8_t data[4];
  uint16_t x_end;
  uint16_t y_end;

  if ((width == 0U) || (height == 0U) || (x >= TFT_LCD_WIDTH) || (y >= TFT_LCD_HEIGHT))
  {
    return HAL_ERROR;
  }

  x_end = (uint16_t)(x + width - 1U);
  y_end = (uint16_t)(y + height - 1U);
  if ((x_end >= TFT_LCD_WIDTH) || (y_end >= TFT_LCD_HEIGHT))
  {
    return HAL_ERROR;
  }

  data[0] = (uint8_t)(x >> 8U);
  data[1] = (uint8_t)x;
  data[2] = (uint8_t)(x_end >> 8U);
  data[3] = (uint8_t)x_end;
  if (WriteCommandData(TFT_CMD_CASET, data, sizeof(data)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  data[0] = (uint8_t)(y >> 8U);
  data[1] = (uint8_t)y;
  data[2] = (uint8_t)(y_end >> 8U);
  data[3] = (uint8_t)y_end;
  if (WriteCommandData(TFT_CMD_RASET, data, sizeof(data)) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return WriteCommand(TFT_CMD_RAMWR);
}

HAL_StatusTypeDef TftLcd_Init(SPI_HandleTypeDef *hspi, TftLcd_LogFn log_fn)
{
  HAL_StatusTypeDef result;

  if (hspi == NULL)
  {
    return HAL_ERROR;
  }

  s_hspi = hspi;
  s_log = log_fn;
  HardwareReset();
  s_controller = TFT_LCD_SELECTED_CONTROLLER;

  if (s_controller == TFT_LCD_CONTROLLER_ILI9341)
  {
    Log_Line("[INFO] tft controller configured ili9341");
    result = InitIli9341();
  }
  else if (s_controller == TFT_LCD_CONTROLLER_ST7789)
  {
    Log_Line("[INFO] tft controller configured st7789");
    result = InitSt7789();
  }
  else
  {
    Log_Line("[WARN] tft controller selection invalid");
    return HAL_ERROR;
  }

  if (result != HAL_OK)
  {
    Log_Line("[WARN] tft controller init failed");
    return HAL_ERROR;
  }

  TftLcd_SetBacklight(1U);
  return HAL_OK;
}

TftLcd_Controller_t TftLcd_GetController(void)
{
  return s_controller;
}

void TftLcd_SetBacklight(uint8_t enabled)
{
  HAL_GPIO_WritePin(TFT_BL_GPIO_Port, TFT_BL_Pin, enabled != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

HAL_StatusTypeDef TftLcd_SetInversion(uint8_t enabled)
{
  return WriteCommand((enabled != 0U) ? TFT_CMD_INVON : TFT_CMD_INVOFF);
}

HAL_StatusTypeDef TftLcd_FillScreen(uint16_t color)
{
  return TftLcd_FillRect(0U, 0U, TFT_LCD_WIDTH, TFT_LCD_HEIGHT, color);
}

HAL_StatusTypeDef TftLcd_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
  for (uint32_t i = 0U; i < (TFT_TX_BUFFER_BYTES / 2U); i++)
  {
    s_tx_buffer[i * 2U] = (uint8_t)(color >> 8U);
    s_tx_buffer[(i * 2U) + 1U] = (uint8_t)color;
  }

  for (uint16_t y_offset = 0U; y_offset < height; y_offset = (uint16_t)(y_offset + TFT_FILL_TILE_ROWS))
  {
    uint16_t tile_rows = (uint16_t)(height - y_offset);
    if (tile_rows > TFT_FILL_TILE_ROWS)
    {
      tile_rows = TFT_FILL_TILE_ROWS;
    }

    for (uint16_t x_offset = 0U; x_offset < width; x_offset = (uint16_t)(x_offset + TFT_FILL_TILE_WIDTH))
    {
      uint16_t tile_width = (uint16_t)(width - x_offset);
      uint32_t pixel_count;

      if (tile_width > TFT_FILL_TILE_WIDTH)
      {
        tile_width = TFT_FILL_TILE_WIDTH;
      }

      pixel_count = (uint32_t)tile_width * (uint32_t)tile_rows;
      if ((SetAddressWindow((uint16_t)(x + x_offset), (uint16_t)(y + y_offset), tile_width, tile_rows) != HAL_OK) ||
          (WriteData(s_tx_buffer, (uint16_t)(pixel_count * 2U)) != HAL_OK))
      {
        return HAL_ERROR;
      }
    }
  }

  return HAL_OK;
}

HAL_StatusTypeDef TftLcd_DrawChar(uint16_t x,
                                 uint16_t y,
                                 char ch,
                                 uint16_t foreground,
                                 uint16_t background,
                                 uint8_t scale)
{
  const uint8_t *glyph;
  uint16_t width;
  uint16_t height;
  uint32_t offset = 0U;

  if ((scale == 0U) || (scale > 2U))
  {
    return HAL_ERROR;
  }

  glyph = GetGlyph(ch);
  width = (uint16_t)(6U * scale);
  height = (uint16_t)(8U * scale);
  if ((x + width > TFT_LCD_WIDTH) || (y + height > TFT_LCD_HEIGHT))
  {
    return HAL_ERROR;
  }

  for (uint16_t pixel_y = 0U; pixel_y < height; pixel_y++)
  {
    uint8_t source_y = (uint8_t)(pixel_y / scale);
    for (uint16_t pixel_x = 0U; pixel_x < width; pixel_x++)
    {
      uint8_t source_x = (uint8_t)(pixel_x / scale);
      uint16_t color = background;

      if ((source_x < TFT_FONT_WIDTH) &&
          (source_y < TFT_FONT_HEIGHT) &&
          ((glyph[source_x] & (uint8_t)(1U << source_y)) != 0U))
      {
        color = foreground;
      }

      s_tx_buffer[offset++] = (uint8_t)(color >> 8U);
      s_tx_buffer[offset++] = (uint8_t)color;
    }
  }

  if (SetAddressWindow(x, y, width, height) != HAL_OK)
  {
    return HAL_ERROR;
  }
  return WriteData(s_tx_buffer, (uint16_t)offset);
}

HAL_StatusTypeDef TftLcd_DrawText(uint16_t x,
                                 uint16_t y,
                                 const char *text,
                                 uint16_t foreground,
                                 uint16_t background,
                                 uint8_t scale)
{
  uint16_t cursor_x = x;

  if (text == NULL)
  {
    return HAL_ERROR;
  }

  while (*text != '\0')
  {
    if (TftLcd_DrawChar(cursor_x, y, *text, foreground, background, scale) != HAL_OK)
    {
      return HAL_ERROR;
    }
    cursor_x = (uint16_t)(cursor_x + (6U * scale));
    text++;
  }

  return HAL_OK;
}

HAL_StatusTypeDef TftLcd_DrawTextFixed(uint16_t x,
                                       uint16_t y,
                                       const char *text,
                                       uint16_t char_count,
                                       uint16_t foreground,
                                       uint16_t background,
                                       uint8_t scale)
{
  uint16_t cell_width;
  uint16_t width;
  uint16_t height;

  if ((text == NULL) || (char_count == 0U) || (scale == 0U) || (scale > 2U))
  {
    return HAL_ERROR;
  }

  cell_width = (uint16_t)(6U * scale);
  width = (uint16_t)(char_count * cell_width);
  height = (uint16_t)(8U * scale);
  if ((x + width > TFT_LCD_WIDTH) || (y + height > TFT_LCD_HEIGHT))
  {
    return HAL_ERROR;
  }

  for (uint16_t y_offset = 0U; y_offset < height; y_offset = (uint16_t)(y_offset + TFT_TEXT_TILE_ROWS))
  {
    uint16_t tile_rows = (uint16_t)(height - y_offset);
    uint32_t offset = 0U;

    if (tile_rows > TFT_TEXT_TILE_ROWS)
    {
      tile_rows = TFT_TEXT_TILE_ROWS;
    }

    for (uint16_t row = 0U; row < tile_rows; row++)
    {
      uint8_t source_y = (uint8_t)((y_offset + row) / scale);

      for (uint16_t char_index = 0U; char_index < char_count; char_index++)
      {
        const uint8_t *glyph = GetGlyph(text[char_index]);

        for (uint16_t pixel_x = 0U; pixel_x < cell_width; pixel_x++)
        {
          uint8_t source_x = (uint8_t)(pixel_x / scale);
          uint16_t color = background;

          if ((source_x < TFT_FONT_WIDTH) &&
              (source_y < TFT_FONT_HEIGHT) &&
              ((glyph[source_x] & (uint8_t)(1U << source_y)) != 0U))
          {
            color = foreground;
          }

          s_tx_buffer[offset++] = (uint8_t)(color >> 8U);
          s_tx_buffer[offset++] = (uint8_t)color;
        }
      }
    }

    if ((SetAddressWindow(x, (uint16_t)(y + y_offset), width, tile_rows) != HAL_OK) ||
        (WriteData(s_tx_buffer, (uint16_t)offset) != HAL_OK))
    {
      return HAL_ERROR;
    }
  }

  return HAL_OK;
}
