#include <errno.h>
#include <esp_eth.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_tls_crypto.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <netdb.h>
#include <nvs_flash.h>
#include <mdns.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_softap.h>

#include "debug.h"
#include "main.h"
#include "utils.h"
#include "wifi.h"
#include "http.h"
#include "mqtt.h"

#define INVALID_SOCKET (-1)

#define TCP_SERVER_PORT            "23"
#define DEBUG_MSG_QUEUE_DEPTH      10

typedef struct
{
  uint8_t msg[48];
  uint8_t msg_len;
  int     socket_dest;
} debug_msg_t;

static const char s_mdns_host_name[] = "esp32_template";

#define MAX_OPEN_SOCKETS    ( CONFIG_LWIP_MAX_SOCKETS - 1 )

typedef struct
{
  volatile bool       initialized;
 
  StaticQueue_t       debug_msg_queue_ctx;
  debug_msg_t         debug_msg_queue_buffer[ DEBUG_MSG_QUEUE_DEPTH ];
  QueueHandle_t       debug_msg_queue;
  
  char                ip_addr_str[16];
  bool                provisioning_in_progress;
  bool                handle_event_got_ip_address;
  bool                handle_event_disconnected;
  
  int                 listen_socket;
  int                 sockets[MAX_OPEN_SOCKETS];
  int                 socket_flags;
  debug_handle_t      debug_handles[MAX_OPEN_SOCKETS];
  
  httpd_handle_t      http_server;
  
  bool                ntp_time_set;
  
  bool                provisioned;
} stdio_task_context_t;

static stdio_task_context_t s_task = { 0 };

static void _prepare_provisioning();
static void _prepare_stdout_sockets();
static void _handle_stdout_sockets();
static void _handle_wifi_connection_changes();
//static void _write_stdout_msg_to_sockets( const char * p_msg );
static int _write_to_socket(const int socket, const uint8_t * data, const size_t len);

static void _wifi_task( void *Param );

static void _provisioning_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void _wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void _ip_event_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data );

static void _ntp_init( void );
static void _ntp_time_sync_notification_cb(struct timeval *tv);

//-----------------------------------------------------------------------------
static void _provisioning_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  #define CONFIG_EXAMPLE_PROV_MGR_MAX_RETRY_CNT 5

  static int retries;
  switch (event_id)
  {
    case WIFI_PROV_START:
      print( "Provisioning started\n");
      break;
        
    case WIFI_PROV_CRED_RECV:
    {
      wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
      print( "Received Wi-Fi credentials\n" );
      print( "\tSSID     : %s\n", (const char *) wifi_sta_cfg->ssid );
      print( "\tPassword : %s\n", (const char *) wifi_sta_cfg->password);
      break;
    }
    
    case WIFI_PROV_CRED_FAIL:
    {
      wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
      print( "Provisioning failed!\n");
      print( "\tReason : %s\n", (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
      print( "\tPlease reset to factory and retry provisioning\n");
      retries++;
      if (retries >= CONFIG_EXAMPLE_PROV_MGR_MAX_RETRY_CNT)
      {
        print( "Failed to connect with provisioned AP, reseting provisioned credentials\n");
        wifi_prov_mgr_reset_sm_state_on_failure();
        retries = 0;
      }
      break;
    }
    
    case WIFI_PROV_CRED_SUCCESS:
      print( "Provisioning successful");
      retries = 0;
      break;
        
    case WIFI_PROV_END:
      // De-initialize manager once provisioning is finished
      wifi_prov_mgr_deinit();
      s_task.provisioning_in_progress = false;
      s_task.provisioned = true;
      break;
        
    default:
      print("Unhandled WIFI_PROV_EVENT event: %i\n", event_id );
      break;
  }

  fflush(stdout);
}

//-----------------------------------------------------------------------------
static void _wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  switch ( event_id )
  {
    case WIFI_EVENT_STA_START:
      esp_wifi_connect();
      break;

    case WIFI_EVENT_STA_CONNECTED:
      print( "Connected to the AP\n");
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
      print( "Disconnected. Connecting to the AP again...\n");
      esp_wifi_connect();
      s_task.handle_event_disconnected = true;
      break;

    default:
        print("Unhandled WIFI_EVENT event: %i\n", event_id );
        break;
  }

  fflush(stdout);
}   

//-----------------------------------------------------------------------------
static void _ip_event_handler( void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data )
{
  switch ( event_id )
  {
    case IP_EVENT_STA_GOT_IP:
      // Note, I had to disable an ARP check on LWIP:
      // menuconfig -> Component config -> LWIP -> DISABLE 'DHCP: Perform ARP check on any offered address'
      // https://www.esp32.com/viewtopic.php?t=12859
      s_task.handle_event_got_ip_address = true;
      break;

    default:
        print("Unhandled IP_EVENT event: %i\n", event_id );
        break;
  }
}

//-----------------------------------------------------------------------------
static void _wifi_task( void *Param )
{
  print( "Wifi & OTA task starting!\n" );
     
 s_task.debug_msg_queue = xQueueCreateStatic( DEBUG_MSG_QUEUE_DEPTH,
                                              sizeof(debug_msg_t),
                                              (uint8_t*)s_task.debug_msg_queue_buffer,
                                              &s_task.debug_msg_queue_ctx );
  
  s_task.initialized = true;

  esp_netif_init();
  
  // Register our event handler for Wi-Fi, IP and Provisioning related events
  esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &_provisioning_event_handler, NULL);
  esp_event_handler_register(WIFI_EVENT,      ESP_EVENT_ANY_ID, &_wifi_event_handler, NULL );
  esp_event_handler_register(IP_EVENT,        ESP_EVENT_ANY_ID, &_ip_event_handler, NULL );

  const uint32_t task_delay_ms = 10;
 
  _prepare_provisioning();  
  while ( !s_task.provisioned )
  {
    delay_ms( task_delay_ms );
  }
  _prepare_stdout_sockets(); 
  _ntp_init();
  http_init();
  //mqtt_init();
  
  mdns_init();
  mdns_hostname_set(s_mdns_host_name);
 
  while(1)
  {
    _handle_stdout_sockets();    
    _handle_wifi_connection_changes();
    //mqtt_do_work();
    
    debug_msg_t debug_msg;
    while ( xQueueReceive( s_task.debug_msg_queue, &debug_msg, 0 ) == pdTRUE )
    {
      _write_to_socket( debug_msg.socket_dest, debug_msg.msg, debug_msg.msg_len );
    }
  
    delay_ms( task_delay_ms );
  }
}

//-----------------------------------------------------------------------------
static void _handle_wifi_connection_changes( void )
{
  if ( !s_task.provisioning_in_progress && s_task.handle_event_got_ip_address )
  {
    s_task.handle_event_got_ip_address = false;
    tcpip_adapter_ip_info_t ipInfo; 
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ipInfo);
    
    sprintf( s_task.ip_addr_str, "%i.%i.%i.%i", 
      ( ipInfo.ip.addr >>  0 ) & 0xFF, ( ipInfo.ip.addr >>  8 ) & 0xFF,
      ( ipInfo.ip.addr >> 16 ) & 0xFF, ( ipInfo.ip.addr >> 24 ) & 0xFF );

    print("Got IP Address - %s\n", s_task.ip_addr_str );
    if ( s_task.http_server == NULL )
    {
      print( "Starting webserver\n" );
      http_start_webserver( &s_task.http_server );
    }
  }
  
  if ( s_task.handle_event_disconnected )
  {
    s_task.handle_event_disconnected = false;

    if ( s_task.http_server )
    {
      print( "Stopping webserver\n" );
      http_stop_webserver( &s_task.http_server );
      s_task.http_server = NULL;
    }
  }
}

//-----------------------------------------------------------------------------
static void _prepare_provisioning( void )
{
  // Initialize Wi-Fi including netif with default config
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);

  // Configuration for the provisioning manager
  wifi_prov_mgr_config_t config =
  {
      .scheme = wifi_prov_scheme_softap,
      .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
  };

  // Initialize provisioning manager with the configuration parameters set above
  wifi_prov_mgr_init(config);

  s_task.provisioned = false;
  wifi_prov_mgr_is_provisioned(&s_task.provisioned);
  
  if ( !s_task.provisioned )
  {
    print( "Starting provisioning\n");
    s_task.provisioning_in_progress = true;

    char    service_name[12];    // Wifi SSID when scheme is wifi_prov_scheme_softap   
    uint8_t eth_mac[6];
    const char *ssid_prefix = "PROV_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, sizeof(service_name), "%s%02X%02X%02X", ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);

    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;   // secure handshake using X25519 key exchange

    // proof-of-possession (ignored if Security 0 is selected): pass string or NULL if not used
    const char *pop = NULL;

    // WiFi softapp security key, null if not used
    const char *service_key = NULL;

    // Start provisioning service
    wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key);
  }
  else
  {
    print( "Already provisioned, starting Wi-Fi STA\n");

    // We don't need the manager as device is already provisioned, so let's release it's resources
    wifi_prov_mgr_deinit();

    // Start Wi-Fi station
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
  }
}

//-----------------------------------------------------------------------------
static int _write_to_socket(const int socket, const uint8_t * data, const size_t len)
{
  if (socket == INVALID_SOCKET)
  {
    return -1;
  }

  int to_write = len;
  while (to_write > 0)
  {
    int written = send(socket, data + (len - to_write), to_write, 0);
    if ( written < 0 && errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK )
    {
      return -1;
    }
    to_write -= written;
  }

  return len;
}

//-----------------------------------------------------------------------------
static void _prepare_stdout_sockets()
{
  s_task.listen_socket = INVALID_SOCKET;

  struct addrinfo hints = { .ai_socktype = SOCK_STREAM };
  struct addrinfo *address_info;

  // Prepare a list of file descriptors to hold client's s_task.sockets, mark all of them as invalid, i.e. available
  for (int i=0; i < ARRAY_SIZE( s_task.sockets ); i++)
  {
    s_task.sockets[i] = INVALID_SOCKET;
  }

  // Translating the hostname or a string representation of an IP to address_info
  int res = getaddrinfo("0.0.0.0", TCP_SERVER_PORT, &hints, &address_info);
  if (res != 0 || address_info == NULL)
  {
    print(  "couldn't get hostname, getaddrinfo() returns %d, addrinfo=%p", res, address_info);
    return;
  }

  // Create a listener socket
  s_task.listen_socket = socket(address_info->ai_family, address_info->ai_socktype, address_info->ai_protocol);
  s_task.socket_flags = fcntl(s_task.listen_socket, F_GETFL);
  fcntl(s_task.listen_socket, F_SETFL, s_task.socket_flags | O_NONBLOCK);
  bind(s_task.listen_socket, address_info->ai_addr, address_info->ai_addrlen);
  listen(s_task.listen_socket, 1);
  
  free(address_info);
}

//-----------------------------------------------------------------------------
static void _debug_drain( const char *p_msg, uint8_t bytecnt, uint8_t handle )
{
  // TODO: Thread safety
  // Find the matching socket
  for ( uint8_t socket_idx = 0; socket_idx < ARRAY_SIZE( s_task.sockets ); socket_idx++ )
  {
    if ( s_task.debug_handles[socket_idx] == handle )
    {
      debug_msg_t debug_msg;
      debug_msg.socket_dest = s_task.sockets[socket_idx];
      
      while ( bytecnt )
      {
        uint8_t bytes_to_copy = MIN( bytecnt, sizeof( debug_msg.msg ) );
        memcpy( debug_msg.msg, p_msg, bytes_to_copy );
        debug_msg.msg_len = bytes_to_copy;

        if ( s_task.initialized )
        {
          xQueueSendToBack( s_task.debug_msg_queue, &debug_msg, 10 );
        }
        
        p_msg   += bytes_to_copy;
        bytecnt -= bytes_to_copy;
      }
      
      // _write_to_socket(, (const uint8_t *)p_msg, bytecnt);
      return;
    }
  }
}

//-----------------------------------------------------------------------------
static void _handle_stdout_sockets( void )
{
  static char rx_buffer[128];

  // Main loop for accepting new connections and serving all connected clients
  struct sockaddr_storage source_addr;
  socklen_t addr_len = sizeof(source_addr);

  // Find a free socket
  int new_sock_index = 0;
  for ( new_sock_index = 0; new_sock_index < ARRAY_SIZE( s_task.sockets ); new_sock_index++ )
  {
    if ( s_task.sockets[new_sock_index] == INVALID_SOCKET )
    {
        break;
    }
  }

  // We accept a new connection if we have a free socket
  if ( new_sock_index < ARRAY_SIZE( s_task.sockets ) )
  {
    // Try to accept a new connections
    s_task.sockets[new_sock_index] = accept(s_task.listen_socket, (struct sockaddr *)&source_addr, &addr_len);

    if (s_task.sockets[new_sock_index] < 0)
    {
      if (errno != EWOULDBLOCK)
      {
        print( "ERROR when accepting connection");
      }
    }
    else
    {
      print( "New connection on socket %i\n", s_task.sockets[new_sock_index] );

      // Set the client's socket non-blocking
      s_task.socket_flags = fcntl(s_task.sockets[new_sock_index], F_GETFL);
      if (fcntl(s_task.sockets[new_sock_index], F_SETFL, s_task.socket_flags | O_NONBLOCK) == -1)
      {
        print( "ERROR: unable to set socket non blocking");
        close(s_task.sockets[new_sock_index]);
        s_task.sockets[new_sock_index] = INVALID_SOCKET;
      }
      else
      {
        s_task.debug_handles[new_sock_index] = debug_reserve( _debug_drain );
      }
    }
  }

  // Serve all the connected clients in this loop
  for ( int i = 0; i < ARRAY_SIZE( s_task.sockets ); i++ )
  {
    if (s_task.sockets[i] != INVALID_SOCKET)
    {
      // This is an open socket -> try to serve it
      int len = recv(s_task.sockets[i], rx_buffer, sizeof(rx_buffer), 0);
      if (len < 0)
      {
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK)
        {
          // Not an error
        }
        else
        {
          // Error occurred within this client's socket -> close and mark invalid
          print( "Error with socket %i (disconnected?), closing\n", s_task.sockets[i]);
          close(s_task.sockets[i]);
          s_task.sockets[i] = INVALID_SOCKET;
          debug_release( s_task.debug_handles[i] );
        }
      }
      else if (len > 0)
      {
        // Received some data -> echo back
        // _write_to_socket(s_task.sockets[i], rx_buffer, len);
      }
    }
  }
}

//-----------------------------------------------------------------------------
static void _ntp_init( void )
{
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_set_time_sync_notification_cb(_ntp_time_sync_notification_cb);
  sntp_init();
}

//-----------------------------------------------------------------------------
void _ntp_time_sync_notification_cb(struct timeval *tv)
{
  s_task.ntp_time_set = true;
  print( "Date/Time updated at: %s\n", get_system_time_str() );
}


//-----------------------------------------------------------------------------
void wifi_task_init(void)
{
  xTaskCreate( _wifi_task,  "wifi_task", 4096, NULL, 0, NULL );
}

//-----------------------------------------------------------------------------
void wifi_reset_provisioning(void)
{
  wifi_prov_mgr_reset_provisioning();
}

//-----------------------------------------------------------------------------
//void wifi_write_stdout_msg( stdout_msg_t *p_stdout_msg )
//{
//  if ( s_task.initialized )
//  {
//    xQueueSendToBack( s_task.stdout_queue, p_stdout_msg, 0 );
//  }
//}

//-----------------------------------------------------------------------------
bool wifi_ntp_time_is_set()
{
  return s_task.ntp_time_set;
}

//-----------------------------------------------------------------------------
const char *wifi_get_ip_addr_str()
{
  return ( const char *)s_task.ip_addr_str;
}

//-----------------------------------------------------------------------------
const char *wifi_get_mdns_name_str()
{
  return s_mdns_host_name;
}
