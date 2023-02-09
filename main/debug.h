#ifndef _stdio_task_H
#define _stdio_task_H

#include <stdint.h>

void debug_init( void );

void print( const char *p_msg, ... );
void print_no_ts( const char *p_msg, ... );     // No timestamp option

typedef int8_t debug_handle_t;
typedef void (*debug_drain_func_t)( const char *p_msg, uint8_t bytecnt, uint8_t handle );

debug_handle_t debug_reserve( debug_drain_func_t drain_func );
void           debug_release( debug_handle_t idx );

void           debug_rewind( debug_handle_t idx );
void           debug_clear( void );

#endif /*_stdio_task_H*/
