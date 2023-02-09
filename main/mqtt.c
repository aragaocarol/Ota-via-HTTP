#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include <esp_event.h>
#include <esp_system.h>
#include <esp_app_format.h>
#include <esp_netif.h>
#include <esp_eth.h>
#include <mqtt_client.h>

#include "utils.h"
#include "wifi.h"
#include "application.h"

#define MQTT_TOPIC            "reef/template"
#define MQTT_IP_TOPIC         MQTT_TOPIC "/ip"
#define MQTT_STATUS_TOPIC     MQTT_TOPIC "/status"
#define MQTT_REQUEST_TOPIC    MQTT_TOPIC "/request"
#define MQTT_SERVER_IP        "mqtt.local"

//-----------------------------------------------------------------------------
static bool s_mqtt_subscribed = false;
static bool s_force_mqtt_updates = false;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;

//-----------------------------------------------------------------------------
void force_mqtt_update()
{
  s_force_mqtt_updates = true;
}

//-----------------------------------------------------------------------------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    switch ((esp_mqtt_event_id_t)event_id)
    {
      case MQTT_EVENT_CONNECTED:
          print( "MQTT Connected to server\n" );
          esp_mqtt_client_subscribe(client, MQTT_REQUEST_TOPIC, 0);
          break;

      case MQTT_EVENT_DISCONNECTED:
          s_mqtt_subscribed = false;
          print( "MQTT Server Disconnect!\n");
          break;

      case MQTT_EVENT_DATA:
          event->data[event->data_len] = 0;
          application_handle_mqtt_request_msg( event->data );
          break;

      case MQTT_EVENT_ERROR:
          print( "MQTT Event Error!\n" );
          break;

      case MQTT_EVENT_SUBSCRIBED:
        s_mqtt_subscribed = true;
        break;
        
      case MQTT_EVENT_UNSUBSCRIBED:
        s_mqtt_subscribed = false;
        break;
      
      case MQTT_EVENT_PUBLISHED:
      default:
        break;
    }
    fflush(stdout);
}

//-----------------------------------------------------------------------------
void mqtt_init()
{
  esp_mqtt_client_config_t mqtt_cfg =
  {
      .uri = "mqtt://" MQTT_SERVER_IP,
  };

  s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(s_mqtt_client);
}

//-----------------------------------------------------------------------------
// Run on the wifi task
void mqtt_do_work()
{
  static float last_status_update = 0;
  static float last_ip_update = 0;
  
  if ( !s_mqtt_client )
  {
    return;
  }
  
  if ( s_force_mqtt_updates )
  {
    last_status_update = 0;
    last_ip_update     = 0;
    s_force_mqtt_updates = false;
  }
  
  uint32_t status_update_rate_s = 5;
  if ( ( last_status_update == 0 ) || ( ( system_uptime_s() - last_status_update ) > status_update_rate_s ) )
  {    
    esp_mqtt_client_publish( s_mqtt_client, MQTT_STATUS_TOPIC, application_get_mqtt_status_msg(), 0, 1, 0);
    last_status_update = system_uptime_s();
  }
  
  if ( ( last_ip_update == 0 ) || ( ( system_uptime_s() - last_ip_update ) > 60 ) )
  {
    char ip_msg[64];
    char *p_msg = ip_msg;
    
    p_msg += sprintf( p_msg, "{ \"ip_addr\":\"%s\",", wifi_get_ip_addr_str() );
    p_msg += sprintf( p_msg, "\"mdns_name\":\"%s\"", wifi_get_mdns_name_str() );
    p_msg += sprintf( p_msg, "}" );
    
    esp_mqtt_client_publish( s_mqtt_client, MQTT_IP_TOPIC, ip_msg, 0, 1, 0);
    last_ip_update = system_uptime_s();
  }
}
