#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "nvs_flash.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include "main.h"
#include "nvm.h"
#include "wifi.h"
#include "debug.h"
#include "utils.h"
#include "application.h"
#include "hardware.h"

//-----------------------------------------------------------------------------
void app_main( void )
{ 
  printf( "****************************\n" ); 
  esp_event_loop_create_default();
  
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
 
  debug_init();
  nvm_init();
  wifi_task_init();
  hardware_init();
  application_init();
  
  const uint32_t task_delay_ms = 100;

  while(1)
  {
    static float button_down_time_s = 0;
    delay_ms( task_delay_ms );
    
    bool button_pressed = hardware_user_button_pressed();
    if ( button_pressed )
    {
      if ( button_down_time_s == 0 )
      {
        print("Button pressed!\n");
        button_down_time_s = system_uptime_s();
        application_handle_user_button_press();
      }
      else if ( system_uptime_s() > ( button_down_time_s + 10 ) )
      {
        print("Resetting provisioning\n");
        button_down_time_s += ( 60 * 60 );
        wifi_reset_provisioning();
      }
    }
    else if ( button_down_time_s )
    {
      print("Button released!\n");
      button_down_time_s = 0;
    }
  }  
}





