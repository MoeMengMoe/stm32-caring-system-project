#include "board_io.h"

#include "main.h"

#if defined(__has_include)
#if __has_include("tim.h")
#include "tim.h"
#define BOARD_IO_HAS_TIM_HEADER 1U
#endif
#endif

#include <stddef.h>
#include <stdio.h>

#define BOARD_IO_DEBOUNCE_MS           40UL
#define BOARD_IO_PASSIVE_TOGGLE_MS     1UL
#define BOARD_IO_ACK_BEEP_PERIOD_MS    1000UL
#define BOARD_IO_ACK_BEEP_ON_MS        120UL
#define BOARD_IO_ALARM_BEEP_PERIOD_MS  400UL
#define BOARD_IO_ALARM_BEEP_ON_MS      180UL
#define BOARD_IO_NOTICE_PERIOD_MS      2000UL
#define BOARD_IO_NOTICE_ON_MS          80UL
#define BOARD_IO_RELAY_COUNT           4U
#define BOARD_IO_RELAY_ACTIVE_LOW      0U

#if defined(BOARD_IO_HAS_TIM_HEADER) && defined(HAL_TIM_MODULE_ENABLED)
#define BOARD_IO_BUZZER_PWM_AVAILABLE  1U
#else
#define BOARD_IO_BUZZER_PWM_AVAILABLE  0U
#endif

typedef struct
{
  uint8_t stable_pressed;
  uint8_t last_raw_pressed;
  uint32_t last_change_ms;
} BoardIo_Button_t;

static BoardIo_LogFn s_log_fn;
static BoardIo_Button_t s_sos_button;
static BoardIo_Button_t s_ack_button;
static uint8_t s_buzzer_level;
static uint32_t s_buzzer_toggle_ms;
static uint8_t s_buzzer_pwm_active;
static uint8_t s_relay_mask;

static void log_line(const char *text)
{
  if ((s_log_fn != NULL) && (text != NULL))
  {
    s_log_fn(text);
  }
}

static void button_init(BoardIo_Button_t *button, uint8_t raw_pressed, uint32_t now_ms)
{
  if (button == NULL)
  {
    return;
  }

  button->stable_pressed = raw_pressed;
  button->last_raw_pressed = raw_pressed;
  button->last_change_ms = now_ms;
}

static uint8_t button_update(BoardIo_Button_t *button, uint8_t raw_pressed, uint32_t now_ms)
{
  if (button == NULL)
  {
    return 0U;
  }

  if (raw_pressed != button->last_raw_pressed)
  {
    button->last_raw_pressed = raw_pressed;
    button->last_change_ms = now_ms;
  }

  if ((raw_pressed != button->stable_pressed) &&
      ((now_ms - button->last_change_ms) >= BOARD_IO_DEBOUNCE_MS))
  {
    button->stable_pressed = raw_pressed;
    return raw_pressed;
  }

  return 0U;
}

static uint8_t read_sos_raw_pressed(void)
{
#if defined(SOS_BUTTON_Pin) && defined(SOS_BUTTON_GPIO_Port)
  return (HAL_GPIO_ReadPin(SOS_BUTTON_GPIO_Port, SOS_BUTTON_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
#else
  return 0U;
#endif
}

static uint8_t read_ack_raw_pressed(void)
{
#if defined(ACK_BUTTON_Pin) && defined(ACK_BUTTON_GPIO_Port)
  return (HAL_GPIO_ReadPin(ACK_BUTTON_GPIO_Port, ACK_BUTTON_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
#else
  return 0U;
#endif
}

static void write_buzzer_pin(uint8_t high)
{
#if defined(BUZZER_IO_Pin) && defined(BUZZER_IO_GPIO_Port)
  HAL_GPIO_WritePin(BUZZER_IO_GPIO_Port, BUZZER_IO_Pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  (void)high;
#endif
}

static void set_buzzer_pwm(uint8_t active)
{
#if BOARD_IO_BUZZER_PWM_AVAILABLE
  if (active != 0U)
  {
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, period / 2U);
    if (s_buzzer_pwm_active == 0U)
    {
      if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK)
      {
        s_buzzer_pwm_active = 1U;
      }
    }
  }
  else
  {
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
    if (s_buzzer_pwm_active != 0U)
    {
      (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
      s_buzzer_pwm_active = 0U;
    }
  }
#else
  (void)active;
#endif
}

static GPIO_PinState relay_pin_state(uint8_t on)
{
#if BOARD_IO_RELAY_ACTIVE_LOW
  return on ? GPIO_PIN_RESET : GPIO_PIN_SET;
#else
  return on ? GPIO_PIN_SET : GPIO_PIN_RESET;
#endif
}

static uint8_t relay_hw_available(uint8_t relay_id)
{
  switch (relay_id)
  {
    case 1U:
#if defined(RELAY1_IN_Pin) && defined(RELAY1_IN_GPIO_Port)
      return 1U;
#else
      return 0U;
#endif
    case 2U:
#if defined(RELAY2_IN_Pin) && defined(RELAY2_IN_GPIO_Port)
      return 1U;
#else
      return 0U;
#endif
    case 3U:
#if defined(RELAY3_IN_Pin) && defined(RELAY3_IN_GPIO_Port)
      return 1U;
#else
      return 0U;
#endif
    case 4U:
#if defined(RELAY4_IN_Pin) && defined(RELAY4_IN_GPIO_Port)
      return 1U;
#else
      return 0U;
#endif
    default:
      return 0U;
  }
}

static void write_relay_pin(uint8_t relay_id, uint8_t on)
{
  const GPIO_PinState state = relay_pin_state(on);
  (void)state;

  switch (relay_id)
  {
    case 1U:
#if defined(RELAY1_IN_Pin) && defined(RELAY1_IN_GPIO_Port)
      HAL_GPIO_WritePin(RELAY1_IN_GPIO_Port, RELAY1_IN_Pin, state);
#endif
      break;

    case 2U:
#if defined(RELAY2_IN_Pin) && defined(RELAY2_IN_GPIO_Port)
      HAL_GPIO_WritePin(RELAY2_IN_GPIO_Port, RELAY2_IN_Pin, state);
#endif
      break;

    case 3U:
#if defined(RELAY3_IN_Pin) && defined(RELAY3_IN_GPIO_Port)
      HAL_GPIO_WritePin(RELAY3_IN_GPIO_Port, RELAY3_IN_Pin, state);
#endif
      break;

    case 4U:
#if defined(RELAY4_IN_Pin) && defined(RELAY4_IN_GPIO_Port)
      HAL_GPIO_WritePin(RELAY4_IN_GPIO_Port, RELAY4_IN_Pin, state);
#endif
      break;

    default:
      break;
  }
}

static uint8_t buzzer_gate_active(uint32_t now_ms, const AppStatus_t *status)
{
  uint32_t phase;

  if (status == NULL)
  {
    return 0U;
  }

  if ((status->state == APP_STATE_ALARM) || (status->state == APP_STATE_NO_RESPONSE))
  {
    phase = now_ms % BOARD_IO_ALARM_BEEP_PERIOD_MS;
    return (phase < BOARD_IO_ALARM_BEEP_ON_MS) ? 1U : 0U;
  }

  if (status->state == APP_STATE_ACK_WAIT)
  {
    phase = now_ms % BOARD_IO_ACK_BEEP_PERIOD_MS;
    return (phase < BOARD_IO_ACK_BEEP_ON_MS) ? 1U : 0U;
  }

  if (status->risk >= 2)
  {
    phase = now_ms % BOARD_IO_NOTICE_PERIOD_MS;
    return (phase < BOARD_IO_NOTICE_ON_MS) ? 1U : 0U;
  }

  return 0U;
}

static void update_passive_buzzer(uint32_t now_ms, const AppStatus_t *status)
{
#if BOARD_IO_BUZZER_PWM_AVAILABLE
  set_buzzer_pwm(buzzer_gate_active(now_ms, status));
#else
  if (buzzer_gate_active(now_ms, status) == 0U)
  {
    s_buzzer_level = 0U;
    write_buzzer_pin(0U);
    return;
  }

  if ((now_ms - s_buzzer_toggle_ms) >= BOARD_IO_PASSIVE_TOGGLE_MS)
  {
    s_buzzer_toggle_ms = now_ms;
    s_buzzer_level = (s_buzzer_level == 0U) ? 1U : 0U;
    write_buzzer_pin(s_buzzer_level);
  }
#endif
}

void BoardIo_Init(BoardIo_LogFn log_fn)
{
  const uint32_t now = HAL_GetTick();
  char line[64];

  s_log_fn = log_fn;
  button_init(&s_sos_button, read_sos_raw_pressed(), now);
  button_init(&s_ack_button, read_ack_raw_pressed(), now);
  s_buzzer_level = 0U;
  s_buzzer_toggle_ms = now;
  s_buzzer_pwm_active = 0U;
  set_buzzer_pwm(0U);
  write_buzzer_pin(0U);
  BoardIo_SetRelayMask(0U);

#if defined(SOS_BUTTON_Pin) && defined(SOS_BUTTON_GPIO_Port)
  log_line("[INFO] board io sos button enabled");
#else
  log_line("[INFO] board io sos button disabled");
#endif

#if defined(ACK_BUTTON_Pin) && defined(ACK_BUTTON_GPIO_Port)
  log_line("[INFO] board io ack button enabled");
#else
  log_line("[INFO] board io ack button disabled");
#endif

#if BOARD_IO_BUZZER_PWM_AVAILABLE
  log_line("[INFO] board io passive buzzer pwm enabled");
#elif defined(BUZZER_IO_Pin) && defined(BUZZER_IO_GPIO_Port)
  log_line("[INFO] board io passive buzzer enabled");
#else
  log_line("[INFO] board io passive buzzer disabled");
#endif

  for (uint8_t relay_id = 1U; relay_id <= BOARD_IO_RELAY_COUNT; relay_id++)
  {
    (void)snprintf(line,
                   sizeof(line),
                   "[INFO] board io relay%u %s %s",
                   (unsigned int)relay_id,
                   relay_hw_available(relay_id) ? "enabled" : "disabled",
                   BOARD_IO_RELAY_ACTIVE_LOW ? "active-low" : "active-high");
    log_line(line);
  }
}

void BoardIo_Update(uint32_t now_ms, const AppStatus_t *status, BoardIo_Events_t *events)
{
  BoardIo_Events_t local_events = {0};

  local_events.sos_pressed = button_update(&s_sos_button, read_sos_raw_pressed(), now_ms);
  local_events.ack_pressed = button_update(&s_ack_button, read_ack_raw_pressed(), now_ms);
  update_passive_buzzer(now_ms, status);

  if (events != NULL)
  {
    *events = local_events;
  }
}

void BoardIo_SetRelayMask(uint8_t relay_mask)
{
  s_relay_mask = (uint8_t)(relay_mask & 0x0FU);

  for (uint8_t relay_id = 1U; relay_id <= BOARD_IO_RELAY_COUNT; relay_id++)
  {
    const uint8_t bit = (uint8_t)(1U << (relay_id - 1U));
    write_relay_pin(relay_id, (s_relay_mask & bit) != 0U);
  }
}
