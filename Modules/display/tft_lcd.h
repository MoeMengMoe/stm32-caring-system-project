#ifndef TFT_LCD_H
#define TFT_LCD_H

#include "stm32u5xx_hal.h"

#include <stdint.h>

typedef void (*TftLcd_LogFn)(const char *text);

typedef enum
{
  TFT_LCD_CONTROLLER_UNKNOWN = 0,
  TFT_LCD_CONTROLLER_ILI9341,
  TFT_LCD_CONTROLLER_ST7789
} TftLcd_Controller_t;

/*
 * This 8-pin module has no SDO line, so the controller cannot be read back.
 * Its 2.0-inch 240 x 320 form factor and write-only SPI interface most closely
 * match ST7789 modules. Change this one selection if later supplier evidence
 * proves that the panel uses ILI9341.
 */
#define TFT_LCD_SELECTED_CONTROLLER TFT_LCD_CONTROLLER_ST7789

enum
{
  TFT_LCD_WIDTH = 320,
  TFT_LCD_HEIGHT = 240
};

#define TFT_LCD_RGB565(r, g, b) \
  ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8U) | (((uint16_t)(g) & 0xFCU) << 3U) | ((uint16_t)(b) >> 3U)))

HAL_StatusTypeDef TftLcd_Init(SPI_HandleTypeDef *hspi, TftLcd_LogFn log_fn);
TftLcd_Controller_t TftLcd_GetController(void);
void TftLcd_SetBacklight(uint8_t enabled);
HAL_StatusTypeDef TftLcd_SetInversion(uint8_t enabled);
HAL_StatusTypeDef TftLcd_FillScreen(uint16_t color);
HAL_StatusTypeDef TftLcd_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
HAL_StatusTypeDef TftLcd_DrawChar(uint16_t x,
                                 uint16_t y,
                                 char ch,
                                 uint16_t foreground,
                                 uint16_t background,
                                 uint8_t scale);
HAL_StatusTypeDef TftLcd_DrawText(uint16_t x,
                                 uint16_t y,
                                 const char *text,
                                 uint16_t foreground,
                                 uint16_t background,
                                 uint8_t scale);
HAL_StatusTypeDef TftLcd_DrawTextFixed(uint16_t x,
                                       uint16_t y,
                                       const char *text,
                                       uint16_t char_count,
                                       uint16_t foreground,
                                       uint16_t background,
                                       uint8_t scale);

#endif
