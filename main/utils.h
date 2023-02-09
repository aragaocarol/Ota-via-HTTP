#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#define mHz   ( 1000 * 1000 )
#define kHz   ( 1000 )
#define Hz    ( 1 )

#define XSTR(a) STR(a)
#define STR(a) #a

#ifndef MIN
  #define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
  #define MAX(a,b) (((a)>(b))?(a):(b))
#endif


#ifndef ARRAY_SIZE
  #define ARRAY_SIZE(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))
#endif

#ifndef CLAMP
  #define CLAMP(x, min, max)  ( MIN( (max), MAX( (x), (min) ) ) )
#endif

#ifndef FCLAMP
  #define FCLAMP(x, min, max)  ( fmin( (max), fmax( (x), (min) ) ) )
#endif


uint64_t      system_uptime_usec( void );
float         system_uptime_s( void );
const char *  get_system_time_str();
int           str_replace_inplace(char *str, const char* pattern, const char* replacement, size_t mlen);
uint16_t      add_formatted_duration_str( char *p_buffer, uint32_t duration_s );
uint16_t      add_formatted_timestamp( char *p_buffer, char *p_prefix, uint32_t unix_time_s, char *p_postfix );
uint16_t      add_offset_time_of_day_str( char *p_buffer, uint32_t offset_minutes );
void          throttle_task( void );

extern void print(const char *p_msg, ...);                        // Implemented in the stdio task
extern void print_no_ts(const char *p_msg, ...);                  // Implemented in the stdio task, no timestamp

void print_buff( void *p_buffer, uint16_t buffer_size );
void print_buff_custom_format( void *p_buffer, uint16_t buffer_size, uint8_t bytes_per_line, uint32_t base_header_line_cnt, uint32_t base_header_address_offset );

void delay_us( uint32_t usec );        // Provides tight timing delays, attempts not to overshoot
void delay_ms( uint32_t msec );        // Provides loose timing delays.  May overshoot by as much as 1 OS tic
void delay_s( uint32_t sec );

void delay_blocking_us( uint32_t usec );
void delay_blocking_ms( uint32_t msec );
void delay_blocking_s( uint32_t sec );

#endif /* SRC_UTILS_H_ */
