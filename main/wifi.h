#ifndef _WIFI_H_
#define _WIFI_H_

void wifi_task_init();
void wifi_reset_provisioning(void);
bool wifi_ntp_time_is_set();
const char *wifi_get_ip_addr_str();
const char *wifi_get_mdns_name_str();

#endif
