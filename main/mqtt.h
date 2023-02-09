#ifndef _MQTT_H_
#define _MQTT_H_

#include <stdbool.h>

void mqtt_init( void );
void mqtt_do_work( void );
void mqtt_force_update( void );

#endif