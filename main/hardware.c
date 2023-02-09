#include "driver/ledc.h"
#include "driver/gpio.h"

#include "utils.h"
#include "hardware.h"

#define PIN_BUTTON                (  0 )
#define PIN_LED                   ( 14 )

static bool s_led_on = false;

//-----------------------------------------------------------------------------
bool hardware_user_button_pressed(void)
{
  return !gpio_get_level( PIN_BUTTON );
}

//-----------------------------------------------------------------------------
void hardware_turn_on_led(void)
{
  gpio_set_level( PIN_LED, 1 );
  s_led_on = true;
}

//-----------------------------------------------------------------------------
void hardware_turn_off_led(void)
{
  gpio_set_level( PIN_LED, 0 );
  s_led_on = false;
}

//-----------------------------------------------------------------------------
void hardware_toggle_led(void)
{
  s_led_on = !s_led_on;
  gpio_set_level( PIN_LED, s_led_on );
}

//-----------------------------------------------------------------------------
void hardware_init()
{
  gpio_config_t io_conf;
  io_conf.pin_bit_mask = ( 1ULL << PIN_BUTTON );
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.pull_up_en = 1;
  io_conf.pull_down_en = 0;
  gpio_config(&io_conf);
  
  io_conf.pin_bit_mask = ( 1ULL << PIN_LED );
  io_conf.mode = GPIO_MODE_OUTPUT;
  gpio_set_level( PIN_LED, s_led_on );
  gpio_config(&io_conf);  
}
