#include <freertos/freertos.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "debug.h"
#include "nvm.h"
#include "utils.h"
#include "application.h"
#include "hardware.h"
#include "wifi.h"
#include "esp_ota_ops.h"

static void _application_task(void *Param);

//-----------------------------------------------------------------------------
static void _application_task(void *Param)
{ 
  const uint32_t task_delay_ms = 250;

  while(1)
  {
    delay_ms(task_delay_ms);
    hardware_toggle_led();
  }  
}

//-----------------------------------------------------------------------------
char const * application_get_html( const char *p_custom_header )
{
  static char buffer[2048] = { 0 };
  char *p_buffer = buffer;
  
  if ( p_custom_header )
  {
    p_buffer += sprintf(p_buffer, "%s", p_custom_header);
  }

  const esp_partition_t *partition = esp_ota_get_running_partition();

  int partition_ota = 0;

   switch (partition->address) {

      case 0x30000:
        partition_ota = 3;
      break;

      case 0x130000:
      partition_ota = 0;
      break;

      case 0x230000:
      partition_ota = 1;
      break;
   }
  
  p_buffer += sprintf(p_buffer, "<h1>System Info</h1>");
  p_buffer += sprintf(p_buffer, "System Time: %s<br>", get_system_time_str());
  p_buffer += sprintf(p_buffer, "Firmware Build: %s %s, Boot Count: %i<br>", __DATE__, __TIME__, nvm_get_param_int32( NVM_PARAM_RESET_COUNTER ));
  p_buffer += sprintf(p_buffer, "Up-time: ");
  p_buffer += add_formatted_duration_str( p_buffer, system_uptime_s() );
  p_buffer += sprintf(p_buffer, "<br><b>Partition: %d</b><br>", partition_ota);
  p_buffer += sprintf(p_buffer, "<br>");
  

  return buffer;
}

//-----------------------------------------------------------------------------
char const * application_post_html(const char *p_post_data)
{
  return application_get_html( NULL );
}

//-----------------------------------------------------------------------------
/*char const * application_get_mqtt_status_msg(void)
{
  static char status_msg[256];
  static uint32_t status_msg_id = 0;
  
  char *p_msg = status_msg;
  status_msg_id++;
  p_msg += sprintf( p_msg, "{ \"message_id\":%i,",   status_msg_id );
  p_msg += sprintf( p_msg, "\"uptime\":%u,",         (uint32_t)system_uptime_s() ); 
  p_msg += sprintf( p_msg, "\"system_time\":\"%s\"", get_system_time_str() );
  p_msg += sprintf( p_msg, "}" );
  
  return status_msg;
}*/


//-----------------------------------------------------------------------------
//void application_handle_mqtt_request_msg( char *p_msg )
//{
//  print("MQTT request message data: %s\n", p_msg );
//}

//-----------------------------------------------------------------------------
void application_handle_user_button_press(void)
{
}

//-----------------------------------------------------------------------------
void application_init(void)
{
  xTaskCreate( _application_task, "app_task", 8192, NULL, 5, NULL);
}
