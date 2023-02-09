#include <string.h>
#include <stdarg.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "utils.h"
#include "debug.h"
#include "wifi.h"

#define VALIDITY_CHECK_EXPECTED_VALUE   ( 0xE1F512ED )

#define DRAIN_CNT     ( 5 )

#pragma pack(1)
struct
{
  uint32_t header_valid_check;
  uint16_t buffer_length;
  uint16_t fill_idx;
} s_buffer_ctx;
#pragma pack()

#define _DEBUG_BUFFER_SIZE         ( 4 * 1024 )

static uint8_t s_debug_data[_DEBUG_BUFFER_SIZE];
static uint8_t * const p_debug_data = s_debug_data;

typedef struct
{
  volatile bool       initialized;
  
  debug_handle_t      null_handle;
  debug_handle_t      uart_handle;

  StaticSemaphore_t   buffer_mutex_buffer;
  SemaphoreHandle_t   buffer_mutex;
  
  debug_drain_func_t  drains[DRAIN_CNT];
  uint16_t            drain_idx[DRAIN_CNT];
} stdio_task_context_t;

static stdio_task_context_t s_task = { 0 };

static void _null_drain( const char *p_msg, uint8_t bytecnt, uint8_t handle );
static void _uart_drain( const char *p_msg, uint8_t bytecnt, uint8_t handle );
static void _debug_task( void *pvParameters );
static uint16_t _debug_drain( debug_handle_t idx, char *buf, uint16_t bufsize );

static void _write_stdout( bool print_timestamp, const char *p_msg, va_list args );
static inline void _buffer_fill( const char *p_data, uint16_t len );

//-----------------------------------------------------------------------------
void debug_init( void )
{
  xTaskCreate( _debug_task,  "debug_task", 2048, NULL, 0, NULL );
  while ( !s_task.initialized ) { delay_ms( 1 ); };
}

//-----------------------------------------------------------------------------
static void _debug_task( void *pvParameters )
{
  const uint32_t thread_period_ms = 10;

  s_task.buffer_mutex = xSemaphoreCreateRecursiveMutexStatic( &s_task.buffer_mutex_buffer );  
  s_task.initialized = true;

  if ( ( s_buffer_ctx.header_valid_check != VALIDITY_CHECK_EXPECTED_VALUE ) ||
       ( s_buffer_ctx.buffer_length      != _DEBUG_BUFFER_SIZE ) )
  {
    debug_clear();
  }
  
  s_task.null_handle = debug_reserve( _null_drain );
  s_task.uart_handle = debug_reserve( _uart_drain );

  char io_buffer[32];

  bool thread_active;
  while ( 1 )
  {
    thread_active = false;   
    if ( xSemaphoreTakeRecursive( s_task.buffer_mutex, 10 ) != pdPASS )
    {
      continue;
    }

    // Dump debug to all the drains
    for ( debug_handle_t idx = 0; idx < ARRAY_SIZE(s_task.drains); idx++ )
    {
      // Don't drain the null drain, which keeps track of the lowest fill idx
      if ( ( s_task.drains[idx] != NULL ) && ( idx != s_task.null_handle ) )
      {
        uint8_t msg_len = _debug_drain( idx, io_buffer, sizeof( io_buffer ) - 1 );
        if ( msg_len )
        {
          s_task.drains[idx]( io_buffer, msg_len, idx );
          thread_active = true;
        }
      }
    }

    xSemaphoreGiveRecursive( s_task.buffer_mutex );
    
    if ( !thread_active )  // Don't sleep if we're actively draining buffers
    {
      delay_ms( thread_period_ms );
    }
  }
}

//-----------------------------------------------------------------------------
static void _null_drain( const char *p_msg, uint8_t bytecnt, uint8_t handle )
{
}

//-----------------------------------------------------------------------------
static void _uart_drain( const char *p_msg, uint8_t bytecnt, uint8_t handle )
{
  printf("%.*s", bytecnt, p_msg); 
  fflush(stdout);
}

//-----------------------------------------------------------------------------
debug_handle_t debug_reserve( debug_drain_func_t drain_func )
{
  debug_handle_t retv = -1;

  if ( s_task.initialized && ( xSemaphoreTakeRecursive( s_task.buffer_mutex, 10 ) == pdPASS ) )
  {
    for ( debug_handle_t idx = 0; idx < ARRAY_SIZE(s_task.drains); idx++ )
    {
      if ( s_task.drains[idx] == NULL )
      {
        s_task.drains[idx] = drain_func;
        debug_rewind( idx );
        retv = idx;
        break;

      }
    }
    
    xSemaphoreGiveRecursive( s_task.buffer_mutex );
  }
  
  return retv;
}

//-----------------------------------------------------------------------------
void debug_release( debug_handle_t idx )
{
  if ( ( idx >= 0 ) && ( idx < ARRAY_SIZE( s_task.drains ) ) )
  {
    s_task.drains[idx] = NULL;
  }
}

//-----------------------------------------------------------------------------
void print(const char *p_msg, ...)
{
  va_list args;
  va_start( args, p_msg );
  _write_stdout( true, p_msg, args );
  va_end( args );
}

//-----------------------------------------------------------------------------
void print_no_ts(const char *p_msg, ...)
{
  va_list args;
  va_start( args, p_msg );
  _write_stdout( false, p_msg, args );
  va_end( args );
}

//-----------------------------------------------------------------------------
static void _write_stdout( bool print_timestamp, const char *p_msg, va_list args )
{
  if ( !p_msg )
  {
    return;
  }

  static char msg_buffer[256];

  // Locking here instead of _buffer_fill() prevents our two calls to _buffer_fill() from getting interrupted & split
  if ( !s_task.initialized || ( xSemaphoreTakeRecursive( s_task.buffer_mutex, 10 ) != pdPASS ) )
  {
    vsnprintf( msg_buffer, sizeof( msg_buffer ), p_msg, args );
    printf( msg_buffer );
    fflush(stdout);
    return;
  }

  if ( print_timestamp )
  {
    uint32_t msec = system_uptime_usec() /  1000;
    uint32_t hours   = msec / 3600000; msec = msec % 3600000;
    uint32_t minutes = msec / 60000; msec = msec % 60000;
    uint32_t seconds = msec / 1000; msec = msec % 1000;

    snprintf( msg_buffer, sizeof( msg_buffer ), "%d:%02d:%02d.%03d: ", hours, minutes, seconds, msec );
    _buffer_fill( msg_buffer, strlen( msg_buffer ) );
  }

  vsnprintf( msg_buffer, sizeof( msg_buffer ), p_msg, args );
  _buffer_fill( msg_buffer, strlen( msg_buffer ) );

  xSemaphoreGiveRecursive( s_task.buffer_mutex );
}

//-----------------------------------------------------------------------------
// Does not provide thread safety, callers should lock the mutex themselves
static inline void _buffer_fill( const char *p_data, uint16_t len )
{
  while ( len-- )
  {
    // Fill the buffer
    p_debug_data[s_buffer_ctx.fill_idx++] = *p_data++;
    if ( s_buffer_ctx.fill_idx >= s_buffer_ctx.buffer_length )
    {
      s_buffer_ctx.fill_idx = 0;
    }

    // This loop is wasteful running for each char in, but there's no clean & easy alternative
    for ( debug_handle_t drain_idx = 0; drain_idx < DRAIN_CNT; drain_idx++ )
    {
      if ( s_task.drain_idx[drain_idx] == s_buffer_ctx.fill_idx )
      {
        s_task.drain_idx[drain_idx]++;
        if ( s_task.drain_idx[drain_idx] >= s_buffer_ctx.buffer_length )
        {
          s_task.drain_idx[drain_idx] = 0;
        }
      }
    }
  }
}

//-----------------------------------------------------------------------------
static uint16_t _debug_drain( debug_handle_t idx, char *buf, uint16_t bufsize )
{
  if ( !buf || !bufsize )
  {
    return false;
  }

  if ( !s_task.initialized || ( xSemaphoreTakeRecursive( s_task.buffer_mutex, 10 ) != pdPASS ) )
  {
    return 0;
  }

  size_t bytes_drained = 0;

  while ( bytes_drained < bufsize )
  {
    // Empty when equal
    if ( s_task.drain_idx[idx] == s_buffer_ctx.fill_idx )
    {
      break;
    }

    buf[bytes_drained++] = p_debug_data[s_task.drain_idx[idx]++];
    if ( s_task.drain_idx[idx] >= s_buffer_ctx.buffer_length )
    {
      s_task.drain_idx[idx] = 0;
    }

  }
  
  xSemaphoreGiveRecursive( s_task.buffer_mutex );

  return bytes_drained;
}

//-----------------------------------------------------------------------------
void debug_rewind( debug_handle_t idx )
{
  if ( !s_task.initialized || ( xSemaphoreTakeRecursive( s_task.buffer_mutex, 10 ) != pdPASS ) )
  {
    return;
  }

  s_task.drain_idx[idx] = s_task.drain_idx[s_task.null_handle];

  xSemaphoreGiveRecursive( s_task.buffer_mutex );
}


//-----------------------------------------------------------------------------
void debug_clear( void )
{
  if ( !s_task.initialized || ( xSemaphoreTakeRecursive( s_task.buffer_mutex, 10 ) != pdPASS ) )
  {
    return;
  }
  
  memset( &s_buffer_ctx, 0, sizeof( s_buffer_ctx ) );
  s_buffer_ctx.header_valid_check = VALIDITY_CHECK_EXPECTED_VALUE;
  s_buffer_ctx.buffer_length      = _DEBUG_BUFFER_SIZE;
  
  xSemaphoreGiveRecursive( s_task.buffer_mutex );
}
