#ifndef _APPLICATION_H_
#define _APPLICATION_H_

void application_init(void);
void         application_handle_user_button_press(void);
char const * application_get_mqtt_status_msg(void);
void         application_handle_mqtt_request_msg( char *p_msg );
char const * application_get_html( const char *p_custom_header );
char const * application_post_html(const char *p_post_data);

#endif