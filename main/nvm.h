#ifndef _NVM_H_
#define _NVM_H_

#include <stdint.h>

//-----------------------------------------------------------------------------
typedef enum
{
  NVM_PARAM_RESET_COUNTER,
  NVM_PARAM_COUNT,
} nvm_param_t;

#define NUM_FEED_SCHEDULES    (NVM_PARAM_FEED_SCHED_5 - NVM_PARAM_FEED_SCHED_0 + 1)

//-----------------------------------------------------------------------------
void nvm_init( void );

void    nvm_reset(void);
int32_t nvm_get_param_int32(nvm_param_t nvm_param);
float   nvm_get_param_float(nvm_param_t nvm_param);
void    nvm_get_param_blob(nvm_param_t nvm_param, void *p_dest);  // No bounds safety, use at your own risk

void    nvm_set_param_int32(nvm_param_t nvm_param, int32_t new_val);
void    nvm_set_param_float(nvm_param_t nvm_param, float new_val);
void    nvm_set_param_blob(nvm_param_t nvm_param, void *p_new_val);

#endif
