#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include <stdint.h>
#include <stdbool.h>
#include "tcp.h"
#include "bsp_led.h"

// HTTP Connection State
typedef struct {
    bool is_logged_in;
    bool close_after_send;
    bool close_pending;     // Close pending
    bool is_login_page;     // Is login page
    bool send_complete;     // Send complete
    uint32_t last_active_time;
    uint32_t last_send_time;// Last send time
    uint8_t conn_id;
    struct tcp_pcb *pcb;
    uint32_t recv_len;      // Received length
    char recv_buf[1024];    // Receive buffer
    
    // Send buffer and state
    char send_buf[1024];     // Send buffer
    const char *unsent_ptr; // Pointer to unsent data
    int unsent_len;         // Unsent length
    bool is_uploading;
    uint32_t upload_content_len;
    uint32_t upload_received_len;
	    // Log download state
    bool is_downloading_log;
    int log_download_index;
    
    // Security
    bool access_denied; // Marked true if ACL failed
} http_conn_state_t;

// HTTP Server Functions
void Http_Server_Init(void);
void Http_Server_Task(void);
err_t send_http_response(struct tcp_pcb *pcb, const char *content_type, 
                        const void *data, int len, bool keep_alive);
err_t send_http_response_ex(struct tcp_pcb *pcb, const char *content_type, 
                         const void *data, int len, bool keep_alive, bool is_login_page, const char *extra_headers);
void Http_SetDebugMode(bool enabled);

/* Internal helper declarations */
static err_t http_poll_callback(void *arg, struct tcp_pcb *pcb);

/* Global Lock for Firmware Upgrade - Exposed for Main Loop Check */
extern volatile bool g_upgrade_in_progress;

#endif /* __HTTP_SERVER_H__ */
