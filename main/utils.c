#include <stdio.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "utils.h"

//-----------------------------------------------------------------------------
void throttle_task( void )
{
  // Allow other things to run by dropping priority for a split second.  This avoids
  // enforcing a hard sleep, while still ensuring that everything gets at least an
  // opportunity to run.  There's a risk that some other task will starve us from ever
  // getting back, but it's unlikely since we feed the data that drives most of the rest
  // of the load.
  UBaseType_t oldPriority = uxTaskPriorityGet( NULL );
  // vTaskPrioritySet automatically requests a yield if another higher priority task is ready
  vTaskPrioritySet( NULL, tskIDLE_PRIORITY );
  vTaskPrioritySet( NULL, oldPriority );
}

//-----------------------------------------------------------------------------
const char * get_system_time_str()
{
  static char strftime_buf[48];
  
  // wait for time to be set
  time_t now = 0;
  struct tm timeinfo = { 0 };

  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%I:%M %p, %x", &timeinfo);
  return strftime_buf;
}

//-----------------------------------------------------------------------------
uint16_t add_offset_time_of_day_str( char *p_buffer, uint32_t offset_minutes )
{
  uint16_t h = offset_minutes / 60; 
  uint16_t m = offset_minutes % 60;
  
  return sprintf( p_buffer, "%2i:%02i %s",
    ( ( h % 12 ) == 0 ? 12 : h % 12 ),
    m,
    ( h < 12 ? "AM" : "PM" ));   
}

//-----------------------------------------------------------------------------
uint16_t add_formatted_duration_str( char *p_buffer, uint32_t duration_s )
{
  uint32_t s = duration_s;
  uint16_t h = (s/3600); 
  uint16_t m = (s -(3600*h))/60;
  s -= (60*60*h)+(m*60);

  uint16_t retv = 0;
  if ( h )
    retv += sprintf( p_buffer + retv, "%i hours%s", h, ( m || s ) ? ", " : "" );
  if ( m )
    retv += sprintf( p_buffer + retv, "%i minutes%s", m, ( s ) ? ", " : "" );
  if ( s )
    retv += sprintf( p_buffer + retv, "%i seconds", s );

  return retv;
}

//-----------------------------------------------------------------------------
uint16_t add_formatted_timestamp( char *p_buffer, char *p_prefix, uint32_t unix_time_s, char *p_postfix )
{
  char tmp[48] = { 0 };
  
  struct tm *timeinfo;
  time_t t = unix_time_s;
  timeinfo = localtime( &t );
  
  strftime(tmp, sizeof(tmp), "%I:%M %p, %x", timeinfo);
  return sprintf( p_buffer, "%s%s%s", p_prefix ? p_prefix : "", tmp, p_postfix ? p_postfix : "" );
}

//-----------------------------------------------------------------------------
int str_replace_inplace(char *str, const char* pattern, const char* replacement, size_t mlen)
{
  // We don't know how long the string is, but we know that it ends with a NUL byte, so every time we hit a NUL byte, we reset the output pointer.
  char* left = str + mlen;
  char* right = left;
  while (left > str)
  {
    if (!*--left) right = str + mlen;
    *--right = *left;
  }

  // Naive left-to-right scan
  size_t patlen = strlen(pattern);
  size_t replen = strlen(replacement);
  for (;;)
  {
    if (0 == strncmp(pattern, right, patlen))
    {
      right += patlen;
      if (right - left < replen) return -1;
      memcpy(left, replacement, replen);
      left += replen;
    }
    else
    {
      if (!(*left++ = *right++))
      {
          break;
      }
    }
  }
  return 0;
}

//-----------------------------------------------------------------------------
uint64_t system_uptime_usec( void )
{
  return esp_timer_get_time();
}

//-----------------------------------------------------------------------------
float system_uptime_s( void )
{
  // esp_timer_get_time returns time in uSec
  return (float)(((double)system_uptime_usec()) / 1000000);
}

//-----------------------------------------------------------------------------
static void _delay_us( uint32_t usec, bool blocking, bool tight_timing )
{
  uint64_t now_time_usec = system_uptime_usec();
  uint64_t stop_time_usec = now_time_usec + usec;

  // 'do' loop ensures we have at least one taskYIELD for cases where usec = 0
  while ( now_time_usec < stop_time_usec )
  {
    if ( !blocking )
    {
      // Use vTaskDelay for large delays so that the task doesn't hog the CPU (even if it's yielding)
      uint64_t delay_tics = pdMS_TO_TICKS( ( stop_time_usec - now_time_usec ) / 1000 );
      if ( !tight_timing )
      {
        delay_tics = MAX( 1, delay_tics );
      }

      if ( delay_tics )
      {
        vTaskDelay( delay_tics );
      }
      else
      {
        // FreeRTOS doesn't provide a way to delay the task more fine than 1 rtos tic
        // Throttle the task's priority to give other tasks a chance at running at least once
        throttle_task();
      }
    }
    now_time_usec = system_uptime_usec();
  }
}

//-----------------------------------------------------------------------------
// A call to delay_non_blocking_us is guaranteed to delay a task of at least usec.  Note, however,
// the true delayed time might be significantly longer as a result of yielding to other pending tasks
void delay_us( uint32_t usec )
{
  _delay_us( usec, false, true );
}

//-----------------------------------------------------------------------------
void delay_ms( uint32_t msec )
{
  _delay_us( msec * 1000, false, false );
}

//-----------------------------------------------------------------------------
void delay_s( uint32_t sec )
{
  _delay_us( sec * 1000 * 1000, false, false );
}

//-----------------------------------------------------------------------------
void delay_blocking_us( uint32_t usec )
{
  _delay_us( usec, true, true );
}

//-----------------------------------------------------------------------------
void delay_blocking_ms( uint32_t msec )
{
  _delay_us( msec * 1000, true, false );
}

//-----------------------------------------------------------------------------
void delay_blocking_s( uint32_t sec )
{
  _delay_us( sec * 100 * 100, true, false );
}
