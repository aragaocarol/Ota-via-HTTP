#ifndef _HARDWARE_H_
#define _HARDWARE_H_

#include <stdbool.h>

void hardware_init();
bool hardware_user_button_pressed(void);
void hardware_turn_on_led(void);
void hardware_turn_off_led(void);
void hardware_toggle_led(void);

#endif
