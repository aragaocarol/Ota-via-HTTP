#include <esp_system.h>
#include <nvs_flash.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "debug.h"
#include "nvm.h"
#include "application.h"

typedef enum
{
  NVM_PARAM_TYPE_FLOAT,
  NVM_PARAM_TYPE_INT,
  NVM_PARAM_TYPE_BLOB,
} nvm_parameter_type_t;
  
typedef struct
{
  const char            *p_name;
  nvm_parameter_type_t  type;
  union
  {
    int32_t   value_int;
    float     value_float;
    void      *p_blob;
  };
  union
  {
    int32_t   default_value_int;     // for 'value' ints
    int32_t   default_value_float;   // for 'value' floats
    size_t    blob_length;           // for 'blobs' - aka binary arrays
  };
} nvm_parameter_t;

static SemaphoreHandle_t  s_access_mutex;
static bool               s_initialized = false;

//-----------------------------------------------------------------------------
nvm_parameter_t nvm_params[] = 
{
  [NVM_PARAM_RESET_COUNTER]     = { .p_name = "reset_counter", .type = NVM_PARAM_TYPE_INT, .value_int = -1, .default_value_int = 0 },
};

bool nvm_params_updated = false;

//-----------------------------------------------------------------------------
static void _nvm_task(void *Param);
static void _update_nvm();

//-----------------------------------------------------------------------------
void nvm_reset(void)
{
  nvs_flash_erase();
  nvs_flash_init();
}

//-----------------------------------------------------------------------------
static void _update_nvm()
{
  // Initialize NVS
  static bool first_pass = true;
  esp_err_t err;
  if ( first_pass )
  {
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }
  }

  nvs_handle flash_handle;
  err = nvs_open( "storage", NVS_READWRITE, &flash_handle );
  if (err != ESP_OK)
  {
      print( "Error (%d) opening NVS handle!\n", err);
  }
  else
  {
    bool table_dirty = false;

    for ( uint8_t idx = 0; idx < NVM_PARAM_COUNT; idx++ )
    {
      nvm_parameter_t *p_param = &nvm_params[idx];
      
      bool write_value = false;
      // Treat Floats as blobs as they're not formally supported
      if (( p_param->type == NVM_PARAM_TYPE_BLOB ) || ( p_param->type == NVM_PARAM_TYPE_FLOAT ))
      {
        uint8_t *p_dest   = (( p_param->type == NVM_PARAM_TYPE_FLOAT ) ? &p_param->value_float : p_param->p_blob );
        size_t  param_len = (( p_param->type == NVM_PARAM_TYPE_FLOAT ) ? sizeof( float )       : p_param->blob_length );
        
        uint8_t temp_blob[param_len];
        if ( ESP_OK == nvs_get_blob(flash_handle, p_param->p_name, temp_blob, &param_len) )
        {
          if ( first_pass )
          {
            memcpy( p_dest, temp_blob, param_len );
            if ( p_param->type == NVM_PARAM_TYPE_FLOAT )
              print( "Loaded NVM Param '%s': %f\n", p_param->p_name, p_param->value_float );
            else
              print( "Loaded NVM Param '%s'\n", p_param->p_name );
          }
          else if ( memcmp( p_dest, temp_blob, param_len ) != 0 )
          {
            write_value = true;
            if ( p_param->type == NVM_PARAM_TYPE_FLOAT )
              print( "Updating NVM Param '%s' from %f to %f\n", p_param->p_name, *(float*)temp_blob, p_param->value_float );
            else
              print( "Updating NVM Param '%s'\n", p_param->p_name );
              
            memcpy( temp_blob, p_dest, param_len );
          }
          else
          {
            print( "NOT Updating NVM Param '%s'\n", p_param->p_name );
          }
        }
        else
        {
          print( "Error reading NVM Param: '%s', loading default\n", p_param->p_name );
          err = nvs_erase_key(flash_handle, p_param->p_name);
          if ( err != ESP_OK )
          {
            print("Error erasing key - 0X%X\n", err );
          }
          
          if ( p_param->type == NVM_PARAM_TYPE_FLOAT )
            p_param->value_float = p_param->default_value_float;
          if ( p_param->type == NVM_PARAM_TYPE_BLOB )
            memset( temp_blob, 0, param_len );

          write_value = true;
        }

        if (write_value)
        {
          err = nvs_set_blob(flash_handle, p_param->p_name, temp_blob, param_len );
          if ( err != ESP_OK )
          {
            print("Error writing NVM - 0X%X\n", err );
          }

          table_dirty = true;
        }
      }
      else if ( p_param->type == NVM_PARAM_TYPE_INT )
      {
        int32_t temp_read_val;
        if ( ESP_OK == nvs_get_i32(flash_handle, p_param->p_name, &temp_read_val) )
        {
          if ( first_pass )
          {
            p_param->value_int = temp_read_val;
            print( "Loaded NVM Param '%s': %i\n", p_param->p_name, p_param->value_int );
          }
          else if ( p_param->value_int != temp_read_val )
          {
            print( "Updating NVM Param '%s' from %i to %i\n", p_param->p_name, temp_read_val, p_param->value_int );
            write_value = true;
          }
        }
        else
        {
          print( "Error reading NVM Param: '%s', loading default\n", p_param->p_name );
          err = nvs_erase_key(flash_handle, p_param->p_name);
          if ( err != ESP_OK )
          {
            print("Error erasing key - 0X%X\n", err );
          }

          p_param->value_int = p_param->default_value_int;
          write_value = true;
        }
        if (write_value)
        {
          err = nvs_set_i32(flash_handle, p_param->p_name, p_param->value_int );
          if ( err != ESP_OK )
          {
            print("Error writing NVM - 0X%X\n", err );
          }
          table_dirty = true;
        }
      }
    }
    
    if ( table_dirty )
    {
      if ( ESP_OK != nvs_commit(flash_handle) )
      {
        print("Error committing NVM\n" );
      }
    }

    nvs_close(flash_handle);
  }
  first_pass = false;
}

//-----------------------------------------------------------------------------
int32_t nvm_get_param_int32( nvm_param_t nvm_param )
{
  xSemaphoreTake(s_access_mutex, portMAX_DELAY);
  int32_t retv = nvm_params[nvm_param].value_int;
  xSemaphoreGive(s_access_mutex);
  return retv;
}

//-----------------------------------------------------------------------------
float nvm_get_param_float( nvm_param_t nvm_param )
{
  xSemaphoreTake(s_access_mutex, portMAX_DELAY);
  float retv = nvm_params[nvm_param].value_float;
  xSemaphoreGive(s_access_mutex);
  return retv;
}

//-----------------------------------------------------------------------------
void nvm_get_param_blob( nvm_param_t nvm_param, void *p_dest )
{
  xSemaphoreTake(s_access_mutex, portMAX_DELAY);
  memcpy( p_dest, nvm_params[nvm_param].p_blob, nvm_params[nvm_param].blob_length );  
  xSemaphoreGive(s_access_mutex);
}

//-----------------------------------------------------------------------------
void nvm_set_param_int32( nvm_param_t nvm_param, int32_t new_val  )
{
  xSemaphoreTake(s_access_mutex, portMAX_DELAY);
  nvm_params[nvm_param].value_int = new_val;
  nvm_params_updated = true;
  xSemaphoreGive(s_access_mutex);
}

//-----------------------------------------------------------------------------
void nvm_set_param_float( nvm_param_t nvm_param, float new_val )
{
  xSemaphoreTake(s_access_mutex, portMAX_DELAY);
  nvm_params[nvm_param].value_float = new_val;
  nvm_params_updated = true;
  xSemaphoreGive(s_access_mutex);
}

//-----------------------------------------------------------------------------
void nvm_set_param_blob( nvm_param_t nvm_param, void *p_new_val )
{
  xSemaphoreTake(s_access_mutex, portMAX_DELAY);
  memcpy( nvm_params[nvm_param].p_blob, p_new_val, nvm_params[nvm_param].blob_length );  
  nvm_params_updated = true;
  xSemaphoreGive(s_access_mutex);
}

//-----------------------------------------------------------------------------
static void _nvm_task(void *Param)
{  
  s_access_mutex = xSemaphoreCreateMutex();

  // Initialize NVS
  esp_err_t error = nvs_flash_init();
  if ( ( error == ESP_ERR_NVS_NO_FREE_PAGES ) || ( error == ESP_ERR_NVS_NEW_VERSION_FOUND ) )
  {
    // Don't bother checking return codes, it's not like we can do anything about failures here anyways
    nvs_flash_erase();
    nvs_flash_init();
  }
  
  _update_nvm();
  
  const uint32_t task_delay_ms = 1000;
  
  s_initialized = true;
  
  nvm_set_param_int32( NVM_PARAM_RESET_COUNTER, nvm_get_param_int32( NVM_PARAM_RESET_COUNTER ) + 1 );
  
  while(1)
  {
    // TODO: Refactor to use eventbits
    vTaskDelay( task_delay_ms / portTICK_RATE_MS);    
    if ( nvm_params_updated )
    {
      xSemaphoreTake(s_access_mutex, portMAX_DELAY);
      print("NVM Params updated, writing to flash\n");
      nvm_params_updated = false;
      _update_nvm();
      xSemaphoreGive(s_access_mutex);
    }
  }
}

//-----------------------------------------------------------------------------
void nvm_init( void )
{ 
  xTaskCreate( _nvm_task, "_nvm_task", 4096, NULL, 0, NULL );
  
  // Lots of things rely on NVM, block until the task is up and ready
  while ( !s_initialized )
  {
    vTaskDelay( 10 / portTICK_RATE_MS);
  }
}