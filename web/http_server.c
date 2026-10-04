#include "main.h"
#include "http_server.h"
#include "fs.h"
#include "log_system.h"
#include "web_time_handler.h"
#include "bsp_relay.h"    
#include "bsp_beeper.h"   
#include "bsp_power_module.h"
#include "bsp_smoke_sensor.h"
#include "flash_storage.h"
#include "bsp_iap.h" // Include new IAP header
#include <string.h>

#ifndef APP_SIZE_LIMIT
#define APP_SIZE_LIMIT (112 * 1024)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "sys.h"
#include "sys_arch.h"
#include "lwip/sys.h"
#include "lwip/timers.h"
#include "lwip/netif.h"

/* Extern for Polling */
extern void LwIP_Pkt_Handle(void);

/* ========================== Security Config ========================== */
static uint32_t g_whitelist_ip = 0; // 0 = Disabled (Default Deny for external subnets)

// Debug: Hardcode your PC IP here to verify logic if needed
// static uint32_t g_whitelist_ip = 0xB408A8C0; // 192.168.8.180 (Little Endian: C0 A8 08 B4)

static bool check_password_complexity(const char *pwd) {
    bool has_upper = false;
    bool has_lower = false;
    bool has_special = false;
    
    if (!pwd) return false;
    
    for (const char *p = pwd; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') has_upper = true;
        else if (*p >= 'a' && *p <= 'z') has_lower = true;
        else if (!(*p >= '0' && *p <= '9')) has_special = true;
    }
    
    return has_upper && has_lower && has_special;
}

/* ========================== ???????????? ========================== */
#define MAX_HTTP_CONNECTIONS 4     // Set to 4 (Optimized RAM: recv_buf 2048->1024)
#define HTTP_STATIC_BUF_SIZE 512   
// ??????????????? Keep-Alive ?????????????????????????
// ???? 6 ??????????????? 2 ?????????????????????
#define KEEP_ALIVE_TIMEOUT   500   // 5000ms
#define IDLE_TIMEOUT         5000   // 5000ms

/* ========================== ?????? ========================== */
static struct tcp_pcb *http_pcb = NULL;
static http_conn_state_t conn_pool[MAX_HTTP_CONNECTIONS];
static uint8_t next_conn_id = 1;
#define http_debug_enabled false // FORCE DISABLED
/* Global Lock for Firmware Upgrade */
volatile bool g_upgrade_in_progress = false;
extern volatile uint8_t g_system_reset_pending;

// Debug Hook: ??????? Keil ?????????????????????
http_conn_state_t* debug_conn_pool_ptr = conn_pool;
// static char http_static_buf[HTTP_STATIC_BUF_SIZE]; // ??????????????

// Independent PSU Logical State - Removed, using PowerModule_GetPSUEnabled instead
// static bool psu1_enabled_logical = false;
// static bool psu2_enabled_logical = false;

// Session Management
static char g_auth_key[32] = "";
static uint32_t g_auth_expiry = 0;
// Default 30s
#define DEFAULT_SESSION_TIMEOUT_MS 30000
static uint32_t g_session_timeout = DEFAULT_SESSION_TIMEOUT_MS;
static Whitelist_Rule_T g_whitelist_rules[5];

static bool g_trigger_ip_change = false;
static uint32_t g_ip_change_timestamp = 0;
static Flash_Network_Config_T g_pending_config;

// Helper to check if any whitelist rule is enabled
static bool is_whitelist_active(void) {
    for (int i = 0; i < 5; i++) {
        if (g_whitelist_rules[i].enabled) return true;
    }
    return false;
}

/* ========================== ???????? ========================== */
static void start_new_session(const char *password) {
    uint32_t random_num = 12345678;
    
    // Simple Encryption: Random XOR Password
    // Just a basic obfuscation to mix password into key as requested
    uint32_t mixed_key = random_num;
    if (password) {
        for (int i = 0; i < strlen(password); i++) {
            mixed_key ^= (password[i] << (i % 24));
        }
    }
    
    snprintf(g_auth_key, sizeof(g_auth_key), "%u", mixed_key);
    g_auth_expiry = sys_now() + g_session_timeout;
}

static bool is_session_valid(const char *key) {
    if (!key || strlen(key) == 0) return false;
    if (strlen(g_auth_key) == 0) return false;
    
    // Check expiry (handling wrap-around safely)
    if ((int32_t)(g_auth_expiry - sys_now()) < 0) {
        g_auth_key[0] = '\0'; // Expire session
        return false;
    }
    
    if (strcmp(key, g_auth_key) == 0) {
        // g_auth_expiry = sys_now() + AUTH_SESSION_TIMEOUT_MS; // Extend session
        return true;
    }
    return false;
}

static void refresh_session(void) {
    if (strlen(g_auth_key) > 0) {
        g_auth_expiry = sys_now() + g_session_timeout;
    }
}

static bool get_json_string(const char *json, const char *key, char *out_buf, size_t max_len) {
    char key_pattern[64];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key); // Only search for "key" first
    char *start = strstr(json, key_pattern);
    if (!start) return false;
    
    start += strlen(key_pattern);
    while (*start && (*start == ' ' || *start == '\t')) start++; // Skip whitespace between "key" and :
    
    if (*start != ':') return false; // Must have colon
    start++; // Skip colon
    
    while (*start && (*start == ' ' || *start == '\t')) start++; // Skip whitespace between : and value
    
    if (*start != '"') {
        // [Fallback] Support numeric values (without quotes)
        if (*start >= '0' && *start <= '9') {
             size_t len = 0;
             while (start[len] >= '0' && start[len] <= '9') len++;
             if (len >= max_len) len = max_len - 1;
             strncpy(out_buf, start, len);
             out_buf[len] = '\0';
             return true;
        }
        return false;
    }
    start++; // Skip opening quote
    
    char *end = strchr(start, '"');
    if (!end) return false;
    
    size_t len = end - start;
    if (len >= max_len) len = max_len - 1;
    
    strncpy(out_buf, start, len);
    out_buf[len] = '\0';
    return true;
}

static err_t http_process_buffer(http_conn_state_t *conn_state);
static err_t http_continue_send(http_conn_state_t *state);
// Flash to RAM copy removed as we now use zero-copy for Flash data

static inline void safe_strncpy(char *dst, const char *src, size_t size) {
    if (!dst || !src || size == 0) return;
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void url_decode(char *dst, const char *src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return;
    
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], '\0'};
            dst[j++] = (char)strtoul(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}

static char* get_query_param(const char *uri, const char *param, char *result, size_t result_size) {
    if (!uri || !param || !result || result_size == 0) return NULL;
    
    // Optimization: Avoid large stack allocation (256B).
    // Use a pointer scan instead of copying the whole URI.
    const char *query = strchr(uri, '?');
    if (!query) return NULL;
    query++; // Skip '?'
    
    // We need to parse "key=value&key2=val2"
    // Since we can't modify 'uri' (const), we scan manually or use a small buffer for the token if needed.
    // But standard strtok modifies string. 
    // Let's use a smaller buffer just for the query part if it's not too long, 
    // OR just search for "param=" in the query string manually to be safe and stack-friendly.
    
    // Manual search strategy:
    // 1. Search for "param=" at start of query OR "&param="
    int param_len = strlen(param);
    const char *p = query;
    
    while (*p) {
        bool match = false;
        if (p == query && strncmp(p, param, param_len) == 0 && p[param_len] == '=') {
            match = true;
            p += param_len + 1;
        } else if (*p == '&' && strncmp(p + 1, param, param_len) == 0 && p[1 + param_len] == '=') {
            match = true;
            p += 1 + param_len + 1;
        }
        
        if (match) {
            // Found it. Copy value until '&' or end of string.
            const char *val_end = strchr(p, '&');
            size_t val_len = val_end ? (size_t)(val_end - p) : strlen(p);
            
            if (val_len >= result_size) val_len = result_size - 1;
            
            strncpy(result, p, val_len);
            result[val_len] = '\0';
            
            // Decode in-place or use a small temp buffer? 
            // result is already a buffer provided by caller. We can decode in-place if implementation allows,
            // but our url_decode writes to dst from src.
            // Let's use a small stack buffer for decoding, or just reuse result if we are careful.
            // Actually, url_decode(dst, src) can work in-place if src==dst and we implement it carefully,
            // but standard implementation might not.
            // Let's alloc a small temp buffer on stack, strictly limited.
            char encoded[64]; 
            if (val_len < sizeof(encoded)) {
                strcpy(encoded, result);
                url_decode(result, encoded, result_size);
            } else {
                // If too long, just return raw or truncated decode
                // For now, keep it simple: Just return raw if too long for temp stack
            }
            return result;
        }
        
        // Move to next char
        p++;
    }
    
    return NULL;
}

/* Flash to RAM function removed */

/* ========================== ???????? ========================== */
static http_conn_state_t* alloc_conn_state(struct tcp_pcb *pcb) {
    // 1. Try to find free slot
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (conn_pool[i].conn_id == 0) {
            memset(&conn_pool[i], 0, sizeof(http_conn_state_t));
            conn_pool[i].conn_id = next_conn_id++;
            conn_pool[i].is_logged_in = false;
            conn_pool[i].last_active_time = sys_now();
            conn_pool[i].pcb = pcb;
            tcp_arg(pcb, (void *)&conn_pool[i]);
            
            if (http_debug_enabled) {
                printf("[HTTP] Alloc conn: id=%d, PCB=%p\n", conn_pool[i].conn_id, pcb);
            }
            return &conn_pool[i];
        }
    }

    // 2. No free slot: Find best candidate to evict
    int oldest_idx = -1;
    uint32_t max_idle_time = 0;
    uint32_t now = sys_now();

    // ??????????? (Priority Eviction Strategy):
    // Tier 1: Zombies (Marked for close)
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (conn_pool[i].close_after_send) {
            oldest_idx = i;
            if (http_debug_enabled) printf("[HTTP] Evict Tier 1 (Zombie): id=%d\n", conn_pool[i].conn_id);
            goto do_evict;
        }
    }

    // Tier 2: Not Logged In & Empty (Pre-connects)
    // If recv_len == 0 and not logged in, it's likely a speculative connection
    for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
        if (!conn_pool[i].is_logged_in && conn_pool[i].recv_len == 0) {
            // Check if it's at least slightly old (e.g. > 100ms) to avoid killing fresh handshakes
            if (now - conn_pool[i].last_active_time > 100) {
                oldest_idx = i;
                if (http_debug_enabled) printf("[HTTP] Evict Tier 2 (Empty/Anon): id=%d\n", conn_pool[i].conn_id);
                goto do_evict;
            }
        }
    }

    // Tier 3: Not Logged In (Oldest first)
    // Prefer killing anonymous sessions over logged-in admins
    {
        int candidate = -1;
        uint32_t max_idle = 0;
        for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
            if (conn_pool[i].is_uploading) continue; // Protect Uploads

            if (!conn_pool[i].is_logged_in) {
                uint32_t idle = now - conn_pool[i].last_active_time;
                if (candidate == -1 || idle > max_idle) {
                    max_idle = idle;
                    candidate = i;
                }
            }
        }
        if (candidate != -1) {
            oldest_idx = candidate;
            if (http_debug_enabled) printf("[HTTP] Evict Tier 3 (Anon): id=%d\n", conn_pool[oldest_idx].conn_id);
            goto do_evict;
        }
    }

    // Tier 4: Logged In (Oldest first)
    // Last resort: Kill the oldest logged-in session
    {
        oldest_idx = -1;
        max_idle_time = 0;
        
        for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
            if (conn_pool[i].is_uploading) continue; // Protect Uploads
            
            uint32_t idle = now - conn_pool[i].last_active_time;
            if (oldest_idx == -1 || idle > max_idle_time) {
                max_idle_time = idle;
                oldest_idx = i;
            }
        }
        
        if (oldest_idx != -1) {
            if (http_debug_enabled) printf("[HTTP] Evict Tier 4 (LoggedIn): id=%d\n", conn_pool[oldest_idx].conn_id);
        } else {
            // Should not happen unless ALL connections are uploading (unlikely)
            // In that case, we MUST fail allocation to protect the uploads
            if (http_debug_enabled) printf("[HTTP] Eviction Failed: All connections uploading!\n");
            return NULL;
        }
    }

do_evict:

    if (oldest_idx >= 0) {
        if (http_debug_enabled) {
            printf("[HTTP] Pool full. Evicting id=%d (idle %ums)\n", conn_pool[oldest_idx].conn_id, max_idle_time);
        }
        
        struct tcp_pcb *old_pcb = conn_pool[oldest_idx].pcb;
        if (old_pcb) {
            tcp_arg(old_pcb, NULL);
            tcp_abort(old_pcb); // Abort old connection
        }

        memset(&conn_pool[oldest_idx], 0, sizeof(http_conn_state_t));
        conn_pool[oldest_idx].conn_id = next_conn_id++;
        conn_pool[oldest_idx].is_logged_in = false;
        conn_pool[oldest_idx].last_active_time = sys_now();
        conn_pool[oldest_idx].pcb = pcb;
        tcp_arg(pcb, (void *)&conn_pool[oldest_idx]);
        
        return &conn_pool[oldest_idx];
    }

    if (http_debug_enabled) {
        printf("[HTTP] No free conn state and eviction failed\n");
    }
    return NULL;
}

static void free_conn_state(struct tcp_pcb *pcb) {
    http_conn_state_t *state = (http_conn_state_t *)pcb->callback_arg;
    if (state) {
        if (http_debug_enabled) {
            printf("[HTTP] Free conn: id=%d, PCB=%p\n", state->conn_id, pcb);
        }
        memset(state, 0, sizeof(http_conn_state_t));
        tcp_arg(pcb, NULL);
    }
}

static err_t http_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len);
static err_t http_continue_log_download(http_conn_state_t *state);

/* ========================== TCP??????? ========================== */
static err_t http_sent_callback(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)len;
    http_conn_state_t *state = (http_conn_state_t *)arg;
    
    if (state) {
        state->last_active_time = sys_now();
        
        // Log Download Continuation
        if (state->is_downloading_log) {
            http_continue_log_download(state);
            return ERR_OK;
        }

        if (state->unsent_len > 0) {
            http_continue_send(state);
        }
        
        // ???????????????????????????????????????????
        if (state->unsent_len == 0 && state->recv_len > 0) {
             http_process_buffer(state);
        }
    }
    
    return ERR_OK;
}

static void http_err_callback(void *arg, err_t err) {
    http_conn_state_t *conn_state = (http_conn_state_t *)arg;
    if (http_debug_enabled) {
        printf("[HTTP] Conn error: %d, id=%d\n", err, conn_state ? conn_state->conn_id : 0);
    }
    
    // ??????? PCB ????? LwIP ?????????
    // ?????????????? tcp_close()????????????????????
    // ????????????????
    if (conn_state) {
        // ??? pcb ????????????? LwIP ???????????????????????
        if (conn_state->pcb) {
            tcp_arg(conn_state->pcb, NULL);
            conn_state->pcb = NULL;
        }
        memset(conn_state, 0, sizeof(http_conn_state_t));
    }
}

/* ========================== ???????????????? ========================== */
static err_t http_continue_send(http_conn_state_t *state) {
    if (!state || !state->pcb || state->unsent_len <= 0 || !state->unsent_ptr) {
        return ERR_OK;
    }

    int snd_buf = tcp_sndbuf(state->pcb);
    if (snd_buf <= 0) return ERR_OK; // ????????????

    int len_to_send = state->unsent_len;
    if (len_to_send > snd_buf) len_to_send = snd_buf;

    // ????????????????????????Flash??conn_state->send_buf??????????????0 (?????)
    err_t err = tcp_write(state->pcb, state->unsent_ptr, len_to_send, 0);
    
    if (err == ERR_OK) {
        state->unsent_ptr += len_to_send;
        state->unsent_len -= len_to_send;
        tcp_output(state->pcb);
        
        if (state->unsent_len == 0) {
            state->send_complete = true;
        }
    }
    return err;
}

/* ========================== HTTP?????????????????? ========================== */
err_t send_http_response(struct tcp_pcb *pcb, const char *content_type, 
                        const void *data, int len, bool keep_alive) {
    return send_http_response_ex(pcb, content_type, data, len, keep_alive, false, NULL);
}

err_t send_http_response_ex(struct tcp_pcb *pcb, const char *content_type, 
                         const void *data, int len, bool keep_alive, bool is_login_page, const char *extra_headers) {
    if (!pcb) {
        if (http_debug_enabled) printf("[HTTP] ERR: PCB NULL\n");
        return ERR_ARG;
    }

    http_conn_state_t *state = (http_conn_state_t *)pcb->callback_arg;
    
    // ????????????
    if (state) {
        state->unsent_len = 0;
        state->unsent_ptr = NULL;
        state->send_complete = false;
    }

    // 1. ??????????????
    int actual_len = 0;
    if (data != NULL) {
        if (len >= 0) {
            actual_len = len;
        } else {
            actual_len = strlen((const char *)data);
        }
    }

    if (http_debug_enabled) {
        printf("[HTTP] Send: type=%s, len=%d\n", 
               (content_type != NULL) ? content_type : "text/plain", actual_len);
    }

    // 2. Build HTTP Header
    // Respect keep_alive parameter
    // keep_alive = false; // Removed hardcoded disable
    char header[512]; 
    const char *connection = keep_alive ? "keep-alive" : "close";
    const char *ctype = (content_type != NULL) ? content_type : "text/html; charset=UTF-8";
    
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: %s\r\n"
        "Cache-Control: no-cache\r\n"
        "%s%s"
        "\r\n", 
        ctype, actual_len, connection,
        extra_headers ? extra_headers : "",
        extra_headers ? "\r\n" : "");
    
    if (header_len < 0 || header_len >= (int)sizeof(header)) {
        return ERR_MEM;
    }

    // 3. ???? HTTP ? (??? Copy????? header ?????)
    err_t err = tcp_write(pcb, header, header_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        if (http_debug_enabled) printf("[HTTP] ERR: Send header failed (%d)\n", err);
        return err;
    }

    // 4. ???????????? (???????)
    int sent_bytes = 0;
    if (actual_len > 0 && data) {
        // ???????? Flash ??? (STM32 Flash ??? 0x08000000)
        uint8_t apiflags = TCP_WRITE_FLAG_COPY;
        bool is_flash = false;
        // Check Flash range OR Static Buffer (Zero Copy)
        if (((uint32_t)data >= 0x08000000 && (uint32_t)data < 0x08100000) || Log_IsStaticBuffer(data)) {
            apiflags = 0; // ??????
            is_flash = true;
        }

        int snd_buf = tcp_sndbuf(pcb);
        int to_send = actual_len;
        
        if (to_send > snd_buf) {
             tcp_output(pcb); // ???????
             snd_buf = tcp_sndbuf(pcb);
             if (to_send > snd_buf) to_send = snd_buf;
        }

        err = tcp_write(pcb, data, to_send, apiflags);
        if (err == ERR_OK) {
            sent_bytes = to_send;
        } else {
            // ?????????????? ERR_MEM
            if (http_debug_enabled) printf("[HTTP] ERR: tcp_write data failed (%d)\n", err);
            
            if (err == ERR_MEM) {
                // ??????????????????????????????????? sent_bytes=0 ?????????????
                tcp_output(pcb);
            }
            sent_bytes = 0;
        }

        // ???????????
        if (sent_bytes < actual_len && state) {
            int remaining = actual_len - sent_bytes;
            const char *remain_ptr = (const char *)data + sent_bytes;
            
            if (is_flash) {
                // Flash??????????????
                state->unsent_ptr = remain_ptr;
                state->unsent_len = remaining;
            } else {
                // RAM?????????????????? send_buf
                if (remaining > sizeof(state->send_buf)) {
                    if (http_debug_enabled) printf("[HTTP] ERR: Send buf overflow %d\n", remaining);
                    remaining = sizeof(state->send_buf); // ??????
                }
                memcpy(state->send_buf, remain_ptr, remaining);
                state->unsent_ptr = state->send_buf;
                state->unsent_len = remaining;
            }
            if (http_debug_enabled) printf("[HTTP] Partial send %d/%d, queued %d\n", sent_bytes, actual_len, remaining);
        }
    }
    
    // ???????
    tcp_output(pcb);

    // 5. ??????
    if (state) {
        state->close_after_send = !keep_alive;
        state->is_login_page = is_login_page;
        state->last_active_time = sys_now();
        state->send_complete = (state->unsent_len == 0);
        state->last_send_time = sys_now();
    }

    return ERR_OK;
}


/* ========================== ????????? ========================== */
static err_t handle_relay_status(struct tcp_pcb *pcb) {
    Relay_State_T relay1 = Relay_GetState(RELAY1); // Use GetState (Software State)
    Relay_State_T relay2 = Relay_GetState(RELAY2);
    Relay_State_T relay3 = Relay_GetState(RELAY3);
    Relay_State_T relay4 = Relay_GetState(RELAY4);
    
    static char response[1024]; // Use static to avoid stack overflow (1KB on stack is dangerous)
    memset(response, 0, sizeof(response)); // Clear buffer before use
    
    // Convert floats to integers for safe printing (avoid %f issues in embedded snprintf)
    /* Removed per user request: PMBus readings
    int psu1_vin = (int)(g_power_module_status.psu_data[0].vin * 100);
    int psu1_vout = (int)(g_power_module_status.psu_data[0].vout * 100);
    int psu1_iout = (int)(g_power_module_status.psu_data[0].iout * 100);
    int psu1_temp = (int)(g_power_module_status.psu_data[0].temp * 10);
    
    int psu2_vin = (int)(g_power_module_status.psu_data[1].vin * 100);
    int psu2_vout = (int)(g_power_module_status.psu_data[1].vout * 100);
    int psu2_iout = (int)(g_power_module_status.psu_data[1].iout * 100);
    int psu2_temp = (int)(g_power_module_status.psu_data[1].temp * 10);
    */

    // Board Voltages
    int board1_v = (int)(g_power_module_status.board_status[0].voltage * 100);
    int board2_v = (int)(g_power_module_status.board_status[1].voltage * 100);
    int board3_v = (int)(g_power_module_status.board_status[2].voltage * 100);
    int board4_v = (int)(g_power_module_status.board_status[3].voltage * 100);
    
    // PSU Global State
    // int psu_enabled = PowerModule_GetPSUState() ? 1 : 0; // Legacy

	// Calculate Durations (in seconds)
			uint32_t current_time = sys_now();
			
			uint32_t psu1_dur = 0;
			if (g_power_module_status.psu_data[0].online) {
					psu1_dur = (current_time - g_power_module_status.psu_data[0].last_on_time) / 1000;
			}
			
			uint32_t psu2_dur = 0;
			if (g_power_module_status.psu_data[1].online) {
					psu2_dur = (current_time - g_power_module_status.psu_data[1].last_on_time) / 1000;
			}
			
			uint32_t board_dur[4] = {0};
			for(int i=0; i<4; i++) {
					if (g_power_module_status.board_status[i].state == POWER_OUTPUT_ON) {
							 board_dur[i] = (current_time - g_power_module_status.board_status[i].last_on_time) / 1000;
					}
			}
    int len = snprintf(response, sizeof(response),
             "{\"relay1\":%d,\"relay2\":%d,\"relay3\":%d,\"relay4\":%d,"
             "\"board1_v\":%d.%02d,\"board2_v\":%d.%02d,\"board3_v\":%d.%02d,\"board4_v\":%d.%02d,"
             "\"board1_runtime\":%u,\"board2_runtime\":%u,\"board3_runtime\":%u,\"board4_runtime\":%u,"
             "\"psu1_enabled\":%d,\"psu2_enabled\":%d,"
             "\"psu1\":{\"online\":%d,\"present\":%d,\"normal\":%d,\"runtime\":%u},"
             "\"psu2\":{\"online\":%d,\"present\":%d,\"normal\":%d,\"runtime\":%u},"
             "\"dc_output_normal\":%d,\"alarm_status\":%d,"
             "\"smoke_alarm\":%d,\"smoke_density\":%d.%01d,\"smoke_voltage\":%d.%02d,"
             "\"smoke_enabled\":%d,"
             "\"active_alarms\":%u,\"muted_alarms\":%u}",
             relay1, relay2, relay3, relay4,
             board1_v/100, board1_v%100,
             board2_v/100, board2_v%100,
             board3_v/100, board3_v%100,
             board4_v/100, board4_v%100,
             board_dur[0], board_dur[1], board_dur[2], board_dur[3],
             PowerModule_GetPSUEnabled(0), PowerModule_GetPSUEnabled(1),
             g_power_module_status.psu1_present, // [Fix] Use physical presence for online status regardless of enabled state
             g_power_module_status.psu1_present, g_power_module_status.psu1_normal, psu1_dur,
             g_power_module_status.psu2_present, // [Fix] Use physical presence for online status regardless of enabled state
             g_power_module_status.psu2_present, g_power_module_status.psu2_normal, psu2_dur,
             g_power_module_status.dc_output_normal, g_power_module_status.alarm_status,
             g_smoke_sensor_status.dout_state,
             (int)g_smoke_sensor_status.smoke_density, (int)(g_smoke_sensor_status.smoke_density*10)%10,
             (int)g_smoke_sensor_status.aout_voltage, (int)(g_smoke_sensor_status.aout_voltage*100)%100,
						  g_smoke_enabled,
              Beeper_GetActiveAlarmMask(), Beeper_GetMutedAlarmMask());
    
    return send_http_response(pcb, "application/json", response, len, true);
}

static err_t handle_single_relay_control(struct tcp_pcb *pcb, const char *action) {
    char json_response[64];
    const char *msg = "Unknown";

    if (strcmp(action, "RELAY1_ON") == 0) { 
        PowerModule_Board_Enable(BOARD_1);
        msg = "Channel 1 Output Enabled";
    }
    else if (strcmp(action, "RELAY1_OFF") == 0) { 
        PowerModule_Board_Disable(BOARD_1);
        msg = "Channel 1 Output Disabled";
    }
    else if (strcmp(action, "RELAY2_ON") == 0) { 
        PowerModule_Board_Enable(BOARD_2);
        msg = "Channel 2 Output Enabled";
    }
    else if (strcmp(action, "RELAY2_OFF") == 0) { 
        PowerModule_Board_Disable(BOARD_2);
        msg = "Channel 2 Output Disabled";
    }
    else if (strcmp(action, "RELAY3_ON") == 0) { 
        PowerModule_Board_Enable(BOARD_3);
        msg = "Channel 3 Output Enabled";
    }
    else if (strcmp(action, "RELAY3_OFF") == 0) { 
        PowerModule_Board_Disable(BOARD_3);
        msg = "Channel 3 Output Disabled";
    }
    else if (strcmp(action, "RELAY4_ON") == 0) { 
        PowerModule_Board_Enable(BOARD_4);
        msg = "Channel 4 Output Enabled";
    }
    else if (strcmp(action, "RELAY4_OFF") == 0) { 
        PowerModule_Board_Disable(BOARD_4);
        msg = "Channel 4 Output Disabled";
    }
    else if (strcmp(action, "PSU1_ON") == 0) {
        if (!g_power_module_status.psu1_present) {
             snprintf(json_response, sizeof(json_response), "{\"status\":\"Error\",\"message\":\"PSU1 Offline / Not Present\"}");
             return send_http_response(pcb, "application/json", json_response, strlen(json_response), true);
        }
        PowerModule_SetPSUEnabled(0, true);
        msg = "Power Supply Unit 1 Enabled";
    }
    else if (strcmp(action, "PSU1_OFF") == 0) {
        if (!g_power_module_status.psu1_present) {
             snprintf(json_response, sizeof(json_response), "{\"status\":\"Error\",\"message\":\"PSU1 Offline / Not Present\"}");
             return send_http_response(pcb, "application/json", json_response, strlen(json_response), true);
        }
        PowerModule_SetPSUEnabled(0, false);
        msg = "Power Supply Unit 1 Disabled";
    }
    else if (strcmp(action, "PSU2_ON") == 0) {
        if (!g_power_module_status.psu2_present) {
             snprintf(json_response, sizeof(json_response), "{\"status\":\"Error\",\"message\":\"PSU2 Offline / Not Present\"}");
             return send_http_response(pcb, "application/json", json_response, strlen(json_response), true);
        }
        PowerModule_SetPSUEnabled(1, true);
        msg = "Power Supply Unit 2 Enabled";
    }
    else if (strcmp(action, "PSU2_OFF") == 0) {
        if (!g_power_module_status.psu2_present) {
             snprintf(json_response, sizeof(json_response), "{\"status\":\"Error\",\"message\":\"PSU2 Offline / Not Present\"}");
             return send_http_response(pcb, "application/json", json_response, strlen(json_response), true);
        }
        PowerModule_SetPSUEnabled(1, false);
        msg = "Power Supply Unit 2 Disabled";
    }
    else if (strcmp(action, "PSU_ON") == 0) { // Legacy or Global
        PowerModule_SetPSUState(true);
        PowerModule_SetPSUEnabled(0, true);
        PowerModule_SetPSUEnabled(1, true);
        // PMBus_SetOperation(0x58, true);
        // PMBus_SetOperation(0x59, true);
        msg = "All Power Supply Units Enabled";
    }
    else if (strcmp(action, "PSU_OFF") == 0) { // Legacy or Global
        PowerModule_SetPSUState(false);
        PowerModule_SetPSUEnabled(0, false);
        PowerModule_SetPSUEnabled(1, false);
        // PMBus_SetOperation(0x58, false);
        // PMBus_SetOperation(0x59, false);
        msg = "All Power Supply Units Disabled";
			}
    else if (strcmp(action, "SMOKE_ON") == 0) {
        g_smoke_enabled = 1;
        GPIO_SetBit(SMOKE_DOUT_PORT, SMOKE_DOUT_PIN);
        // Update Flash
        // Flash_PSU_Config_T config;
        // if (Flash_Read_PSU_Config(&config)) {
        //     config.smoke_enabled = 1;
        //     Flash_Write_PSU_Config(&config);
        // } else {
        //      // Init new config
        //      memset(&config, 0, sizeof(config));
        //      config.smoke_enabled = 1;
        //      config.psu1_enabled = 1; // Default
        //      config.psu2_enabled = 1; // Default
        //      Flash_Write_PSU_Config(&config);
        // }
        msg = "Smoke Sensor Detection Enabled";
    }
    else if (strcmp(action, "SMOKE_OFF") == 0) {
        g_smoke_enabled = 0;
        GPIO_ResetBit(SMOKE_DOUT_PORT, SMOKE_DOUT_PIN);
        // Update Flash
        // Flash_PSU_Config_T config;
        // if (Flash_Read_PSU_Config(&config)) {
        //     config.smoke_enabled = 0;
        //     Flash_Write_PSU_Config(&config);
        // } else {
        //      // Init new config
        //      memset(&config, 0, sizeof(config));
        //      config.smoke_enabled = 0;
        //      config.psu1_enabled = 1; // Default
        //      config.psu2_enabled = 1; // Default
        //      Flash_Write_PSU_Config(&config);
        // }
        msg = "Smoke Sensor Detection Disabled";
    }
		
		else if (strcmp(action, "MUTE_ALARM") == 0) {
        Beeper_SetMute(true);
        msg = "System Alarm Muted by User";
    }
    else if (strcmp(action, "TEST_FLASH_WRITE") == 0) {
        int ret = Log_TestFlashWrite();
        if (ret >= 0) {
            snprintf(json_response, sizeof(json_response), "{\"status\":\"OK\",\"message\":\"Flash Write Test OK (Idx:%d)\"}", ret);
        } else {
            snprintf(json_response, sizeof(json_response), "{\"status\":\"Error\",\"message\":\"Flash Write Failed (%d)\"}", ret);
        }
        return send_http_response(pcb, "application/json", json_response, strlen(json_response), true);
    }
    else { 
        snprintf(json_response, sizeof(json_response), "{\"status\":\"Error\",\"message\":\"Invalid action\"}");
        return send_http_response(pcb, "application/json", json_response, strlen(json_response), true);
    }

    Log_Write(LOG_LEVEL_INFO, LOG_MODULE_USER, "%s", msg);
    snprintf(json_response, sizeof(json_response), "{\"status\":\"OK\",\"message\":\"%s\"}", msg);
    return send_http_response(pcb, "application/json", json_response, strlen(json_response), true); 
}



/* ========================== Log Download ========================== */
static const char* log_module_names[] = { "SYSTEM", "USER", "NETWORK", "TIME" };
static const char* log_level_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };

extern void Unix_Time_To_Str(uint32_t unix_time, char *buf, int len);

static err_t handle_log_download(struct tcp_pcb *pcb) {
    if (!pcb) return ERR_ARG;
    
    char buffer[512];
    
    // 1. Send HTTP Headers (TXT file)
    snprintf(buffer, sizeof(buffer), 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain; charset=UTF-8\r\n"
        "Content-Disposition: attachment; filename=\"system_log_codes.txt\"\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache\r\n"
        "\r\n");
    
    tcp_write(pcb, buffer, strlen(buffer), TCP_WRITE_FLAG_COPY);
    
    // 2. Header
    const char *head = "Time(Unix),Code(Hex),Param1,Param2,Description\r\n";
    tcp_write(pcb, head, strlen(head), TCP_WRITE_FLAG_COPY);
    
    // 3. Init State
    http_conn_state_t *state = (http_conn_state_t *)pcb->callback_arg;
    if (state) {
        state->is_downloading_log = true;
        state->log_download_index = 0; 
        state->close_after_send = true; 
        state->last_send_time = sys_now();
        tcp_output(pcb);
    }
    
    return ERR_OK;
}

static err_t http_continue_log_download(http_conn_state_t *state) {
    if (!state || !state->pcb) return ERR_ARG;
    struct tcp_pcb *pcb = state->pcb;
    char buffer[1024];
    int buf_pos = 0;
    int items_in_batch = 0;

    int count = Flash_Get_Log_Count();
    int start_index = state->log_download_index;
    
    // Debug info at start
    if (state->log_download_index == 0) {
        printf("[HTTP] Start Log Download. Total: %d\n", count);
        // Header with human readable time column
        int len = snprintf(buffer, sizeof(buffer), "Time,Code(Hex),Param1,Param2,Description\r\n# Total Entries: %d\r\n", count);
        buf_pos += len;
    }
    
    extern void Unix_Time_To_Str(uint32_t unix_time, char *buf, int len);
    
    // Send in chunks
    int processed_count = 0;
    while (state->log_download_index < count) {
        // Yield if we processed too many entries (even if skipped) to avoid watchdog/latency
        if (processed_count++ > 100) break;

        Flash_Log_Entry_T entry;
        bool read_ok = Flash_Get_Log_Entry_ByIndex(state->log_download_index, &entry);
        
        if (read_ok) {
            // Filter out empty/erased entries (0xFFFF) and Custom placeholders (0xFFFE)
            if (entry.msg_code == 0xFFFF || entry.msg_code == 0xFFFE) {
                state->log_download_index++;
                continue;
            }
        }

        char line_buf[128];
        int len = 0;
        if (read_ok) {
            char time_str[32];
            if (entry.timestamp > 0 && entry.timestamp != 0xFFFFFFFF) {
                Unix_Time_To_Str(entry.timestamp, time_str, sizeof(time_str));
            } else {
                strcpy(time_str, "No-Time");
            }
            
            char desc[64];
            Log_Decode_Entry(entry.msg_code, entry.param1, entry.param2, desc, sizeof(desc));
            
            len = snprintf(line_buf, sizeof(line_buf), "%s,0x%04X,%u,%u,%s\r\n", 
                time_str, entry.msg_code, entry.param1, entry.param2, desc);
        } else {
             len = snprintf(line_buf, sizeof(line_buf), "# Error reading index %d\r\n", state->log_download_index);
        }

        // Check buffer space
        if (buf_pos + len >= sizeof(buffer)) {
            break;
        }

        memcpy(buffer + buf_pos, line_buf, len);
        buf_pos += len;
        state->log_download_index++;
        items_in_batch++;

        // Batch limit (20 items or ~1KB)
        if (items_in_batch >= 20) break;
    }

    if (buf_pos > 0) {
        if (tcp_sndbuf(pcb) < buf_pos) {
            // Buffer Full! Rollback and wait
            state->log_download_index = start_index; 
            tcp_output(pcb); 
            return ERR_OK; 
        }
        
        err_t err = tcp_write(pcb, buffer, buf_pos, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            printf("[HTTP] tcp_write failed: %d at index %d\n", err, start_index);
            state->log_download_index = start_index; // Rollback
            return err;
        }
        tcp_output(pcb);
    }

    // Check completion
    if (state->log_download_index >= count) {
        const char *footer = "# End of Log\r\n";
        if (tcp_sndbuf(pcb) >= strlen(footer)) {
            tcp_write(pcb, footer, strlen(footer), TCP_WRITE_FLAG_COPY);
            state->is_downloading_log = false;
            state->send_complete = true;
            printf("[HTTP] Log Download Complete.\n");
            tcp_output(pcb);
        } else {
            // Wait for next call to send footer
            return ERR_OK; 
        }
    }
    
    return ERR_OK;
}

static err_t handle_flash_dump(struct tcp_pcb *pcb) {
    char buffer[1024];
    int len = 0;
    
    Flash_Log_Entry_T *flash_ptr = (Flash_Log_Entry_T *)FLASH_LOG_START_ADDR;
    
    len += snprintf(buffer+len, sizeof(buffer)-len, "--- FLASH DUMP (Start: 0x%08X) ---\r\n", FLASH_LOG_START_ADDR);
    
    for(int i=0; i<16; i++) {
        uint32_t *p = (uint32_t*)&flash_ptr[i];
        len += snprintf(buffer+len, sizeof(buffer)-len, 
            "[%02d] %08X %08X ", 
            i, p[0], p[1]);
            
        if(flash_ptr[i].timestamp != 0xFFFFFFFF) {
             len += snprintf(buffer+len, sizeof(buffer)-len, "TS:%u Code:0x%04X P1:%u P2:%u\r\n", 
                 flash_ptr[i].timestamp, flash_ptr[i].msg_code, flash_ptr[i].param1, flash_ptr[i].param2);
        } else {
             len += snprintf(buffer+len, sizeof(buffer)-len, "EMPTY\r\n");
        }
    }
    
    return send_http_response(pcb, "text/plain", buffer, len, true);
}

static err_t http_process_buffer(http_conn_state_t *conn_state) {
    if (!conn_state || !conn_state->pcb) return ERR_ARG;
    struct tcp_pcb *pcb = conn_state->pcb;

    // Process all requests in the buffer (Pipelining support)
    while (1) {
        if (conn_state->unsent_len > 0) {
            break;
        }

        // Find HTTP Header End
        char *header_end = strstr(conn_state->recv_buf, "\r\n\r\n");
        if (!header_end) {
            if (conn_state->recv_len >= sizeof(conn_state->recv_buf) - 1) {
                // Buffer full but no header end found -> Bad Request
                const char *err_resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                tcp_write(pcb, err_resp, strlen(err_resp), TCP_WRITE_FLAG_COPY);
                tcp_output(pcb);
                free_conn_state(pcb);
                tcp_close(pcb);
                return ERR_OK;
            }
            // Wait for more data
            break; 
        }

        // --- 1. Parse Method and URI EARLY (Peek) ---
        char method[16] = {0}, uri[256] = {0};
        char *space = strchr(conn_state->recv_buf, ' ');
        if (!space || space > header_end) {
             // Malformed request line? Should not happen if \r\n\r\n found
             // Unless it's just newlines? Treat as garbage/skip?
             // For now, assume valid HTTP if header end exists.
             break;
        }
        
        int method_len = space - conn_state->recv_buf;
        if (method_len > 15) method_len = 15;
        memcpy(method, conn_state->recv_buf, method_len);
        method[method_len] = '\0';

        char *uri_start = space + 1;
        space = strchr(uri_start, ' ');
        if (space && space < header_end) {
            int uri_len = space - uri_start;
            if (uri_len > 255) uri_len = 255;
            memcpy(uri, uri_start, uri_len);
            uri[uri_len] = '\0';
        } else {
            safe_strncpy(uri, uri_start, sizeof(uri));
        }

        // ??????PCB?????????????LwIP?????????????????
        if (http_debug_enabled) {
            printf("[HTTP] Processing: %s %s\n", method, uri);
        }

        /* REJECT ALL OTHER REQUESTS if upgrade is in progress (except the upload itself) */
        if (g_upgrade_in_progress) {
            if (strncmp(uri, "/api/firmware/upload", 20) != 0) {
                 // printf("[HTTP] Ignore req %s (Upgrade Busy)\n", uri);
                 // Just return OK to consume buffer but do nothing, or send 503
                 // Sending 503 might use buffers, better to just drop or close
                 send_http_response(pcb, "text/plain", "503 Upgrade Busy", 16, false);
                 conn_state->recv_len = 0;
                 return ERR_OK;
            }
        }

        // --- Check for Cookie (Session Restore) BEFORE handling specific requests ---
        if (!conn_state->is_logged_in) {
            char *cookie_ptr = strstr(conn_state->recv_buf, "Cookie:");
            if (cookie_ptr && cookie_ptr < header_end) {
                char *line_end = strstr(cookie_ptr, "\r\n");
                if (line_end && line_end > cookie_ptr) {
                    char saved_char = *line_end;
                    *line_end = '\0';
                    
                    if (http_debug_enabled) {
                         printf("[HTTP] Cookie Header: %s\n", cookie_ptr);
                         printf("[HTTP] Current Key: %s\n", g_auth_key);
                    }

                    if (strlen(g_auth_key) > 0) {
                        char session_cookie[64];
                        snprintf(session_cookie, sizeof(session_cookie), "session=%s", g_auth_key);
                        if (strstr(cookie_ptr, session_cookie)) {
                            if (is_session_valid(g_auth_key)) {
                                conn_state->is_logged_in = true;
                                if (http_debug_enabled) printf("[HTTP] Session Restored!\n");
                            } else {
                                if (http_debug_enabled) printf("[HTTP] Session Invalid!\n");
                            }
                        } else {
                             if (http_debug_enabled) printf("[HTTP] Cookie mismatch! Expected: %s\n", session_cookie);
                        }
                    }
                    *line_end = saved_char; 
                }
            } else {
                 if (http_debug_enabled) printf("[HTTP] No Cookie Header found\n");
            }
        }

        // Refresh session for non-polling requests
        if (conn_state->is_logged_in) {
            // [FIX] Double check expiry! Even if connection is kept alive, 
            // the session might have expired in the background.
            if (!is_session_valid(g_auth_key)) {
                conn_state->is_logged_in = false;
                if (http_debug_enabled) printf("[HTTP] Session Expired during Keep-Alive!\n");
            }
        }

        if (conn_state->is_logged_in) {
            // Filter out polling endpoints to avoid infinite session extension
            char clean_uri[256];
            safe_strncpy(clean_uri, uri, sizeof(clean_uri));
            char *q = strchr(clean_uri, '?');
            if (q) *q = '\0'; // Remove query string for comparison

            bool is_polling = false;
            if (strcmp(clean_uri, "/status") == 0) is_polling = true;
            else if (strcmp(clean_uri, "/log") == 0) is_polling = true;
            else if (strcmp(clean_uri, "/api/time") == 0 && strcmp(method, "GET") == 0) is_polling = true;
            else if (strcmp(clean_uri, "/favicon.ico") == 0) is_polling = true;
            else if (strcmp(clean_uri, "/logo.png") == 0) is_polling = true;
            
            // Only refresh if it's NOT a polling request
            if (!is_polling) {
                if (http_debug_enabled) printf("[HTTP] Session Refreshed by: %s\n", clean_uri);
                refresh_session();
            } else {
                // if (http_debug_enabled) printf("[HTTP] Polling (No Refresh): %s\n", clean_uri);
            }
        }

        // DEBUG TRAP
        if (strstr(uri, "upload") != NULL) {
             volatile int trap = 1; // POINT C
             (void)trap;
        }

        // --- 2. Special Handling for Firmware Upload ---
        // Must switch to streaming mode immediately, DO NOT wait for full body
        if (strncmp(method, "POST", 4) == 0 && strncmp(uri, "/api/firmware/upload", 20) == 0) {
             if (g_system_reset_pending) {
                 send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"System Restarting...\"}", -1, true);
                 conn_state->recv_len = 0;
                 return ERR_OK;
             }

             if (conn_state->is_logged_in) {
                 printf("[HTTP] Upload: Entering Stream Mode\n");
                 
                 conn_state->is_uploading = true;
                 conn_state->upload_received_len = 0;
                 conn_state->upload_content_len = 0;

                 // Parse Content-Length
                 char *cl_ptr = strstr(conn_state->recv_buf, "Content-Length:");
                 if (cl_ptr && cl_ptr < header_end) {
                     conn_state->upload_content_len = atoi(cl_ptr + 15);
                 }
                  // Check Content-Type for "multipart/form-data" or "application/octet-stream"
                 // And try to find "filename=" if possible, but raw binary upload might not have it in header.
                 // However, many browsers send "Content-Disposition: form-data; name="file"; filename="xxx.bin""
                 // Let's check for .bin extension if Content-Disposition exists.
                 char *cd_ptr = strstr(conn_state->recv_buf, "Content-Disposition:");
                 if (cd_ptr && cd_ptr < header_end) {
                     char *fn_ptr = strstr(cd_ptr, "filename=\"");
                     if (fn_ptr && fn_ptr < header_end) {
                         fn_ptr += 10; // Skip filename="
                         char *fn_end = strchr(fn_ptr, '"');
                         if (fn_end && fn_end < header_end) {
                             // Check extension
                             if (fn_end - fn_ptr > 4) {
                                 char *ext = fn_end - 4;
                                 if (strncasecmp(ext, ".bin", 4) != 0) {
                                     printf("[HTTP] Upload Rejected: Invalid Extension (%.4s)\n", ext);
                                     conn_state->is_uploading = false;
                                     g_upgrade_in_progress = false;
                                     send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Only .bin files allowed\"}", -1, true);
                                     // Fully reset state
                                     conn_state->upload_received_len = 0;
                                     conn_state->upload_content_len = 0;
                                     conn_state->recv_len = 0;
                                     conn_state->recv_buf[0] = '\0';
                                     return ERR_OK;
                                 }
                             }
                         }
                     }
                 }
                 
                 /* DEBUG: Print parsed content length */
                 printf("[HTTP] Upload Start. Content-Length: %d\n", conn_state->upload_content_len);

                 /* Size Check */
                 if (conn_state->upload_content_len > APP_SIZE_LIMIT) {
                     printf("[HTTP] Upload Rejected: Too Large (%d > %d)\n", conn_state->upload_content_len, APP_SIZE_LIMIT);
                     conn_state->is_uploading = false;
                     g_upgrade_in_progress = false;
                     send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"File too large\"}", -1, true);
                     conn_state->recv_len = 0;
                     return ERR_OK;
                 }

                 /* Set Global Lock */
                 g_upgrade_in_progress = true;
                 
                 // Process Initial Body Chunk (if any)
                 char *body_start = header_end + 4;
                 int body_len = conn_state->recv_len - (body_start - conn_state->recv_buf);
                 
                 if (body_len > 0) {
                     printf("[HTTP] Upload: Processing initial chunk %d bytes\n", body_len);
                     
                     // First Chunk Check: Validate Firmware Header
                     if (conn_state->upload_received_len == 0) {
                          if (body_len >= 8) { // Need 8 bytes for SP + ResetVector
                              uint8_t *d = (uint8_t*)body_start;
                              // Check for STM32 Binary Magic (Stack Pointer usually in RAM 0x200xxxxx)
                              // Common pattern: Little Endian [0x??, 0x??, 0x00-0x01, 0x20]
                              // E.g. 0x20001000 -> 00 10 00 20
                              if (d[3] != 0x20) {
                                  printf("[HTTP] FW Upload Rejected: Invalid Header SP 0x%02X\n", d[3]);
                                  conn_state->is_uploading = false;
                                  g_upgrade_in_progress = false;
                                  send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid Firmware File (SP)\"}", -1, true);
                                  
                                  // Reset state (No IAP to finish because it wasn't started)
                                  conn_state->recv_len = 0;
                                  conn_state->recv_buf[0] = '\0';
                                  conn_state->upload_received_len = 0;
                                  conn_state->upload_content_len = 0;
                                  return ERR_OK;
                              }
                              // Check Reset Vector (Must be in Flash 0x08xxxxxx)
                              if (d[7] != 0x08) {
                                  printf("[HTTP] FW Upload Rejected: Invalid Header ResetVector 0x%02X\n", d[7]);
                                  conn_state->is_uploading = false;
                                  g_upgrade_in_progress = false;
                                  send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid Firmware File (Vector)\"}", -1, true);
                                  
                                  conn_state->recv_len = 0;
                                  conn_state->recv_buf[0] = '\0';
                                  conn_state->upload_received_len = 0;
                                  conn_state->upload_content_len = 0;
                                  return ERR_OK;
                              }
                              printf("[HTTP] FW Header Valid. SP Base: 0x%02X, Vector Base: 0x%02X\n", d[3], d[7]);
                          }
                     }
                     
                     // Initialize IAP Update Process ONLY if checks pass
                     IAP_Web_Init(conn_state->upload_content_len);

                     int ret = IAP_Web_Write((uint8_t*)body_start, body_len);
                     if (ret != 0) {
                         conn_state->is_uploading = false;
                         g_upgrade_in_progress = false;
                         send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Flash Write Error\"}", -1, true);
                         
                         // Reset state
                         IAP_Web_Finish(); 
                         conn_state->recv_len = 0;
                         conn_state->recv_buf[0] = '\0';
                         conn_state->upload_received_len = 0;
                         conn_state->upload_content_len = 0;
                         return ERR_OK;
                     }
                     conn_state->upload_received_len += body_len;
                 } else {
                     // No initial body data, just init IAP
                     IAP_Web_Init(conn_state->upload_content_len);
                 }
                 
                 // Clear buffer so http_recv_callback can stream subsequent packets
                 conn_state->recv_len = 0;
                 conn_state->recv_buf[0] = '\0';
                 return ERR_OK; // Return to allow streaming in recv callback
             } else {
                 // Unauthorized - let it fall through to normal 401 handler or handle here
                 // But better to let normal flow handle it (will check auth later)
                 // Actually, normal flow waits for body. We should reject immediately.
                 send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Unauthorized\"}", -1, true);
                 conn_state->recv_len = 0;
                 return ERR_OK;
             }
        }

        // --- 3. Normal Request: Check Content-Length and Wait for Body ---
        char *request_end = header_end + 4; 
        int content_len = 0;
        char *content_len_ptr = strstr(conn_state->recv_buf, "Content-Length:");
        if (content_len_ptr && content_len_ptr < header_end) {
            content_len = atoi(content_len_ptr + 15);
            if (content_len > 0) {
                request_end += content_len;
                // Wait for full body
                if (conn_state->recv_len < (request_end - conn_state->recv_buf)) {
                    break; 
                }
            }
        }

        // ????????
        err_t ret = ERR_OK;
        if (strcmp(method, "GET") == 0) {
            // Security Check
            bool is_authorized = conn_state->is_logged_in;
            
            // Allow API key (password) in URL for stateless access
            // NOTE: Colleague requirement: "Web sends Key+Option". 
            // So we check if "key" param matches g_auth_key.
            if (!is_authorized && (strstr(uri, "/control") != NULL || strcmp(uri, "/status") == 0 || strcmp(uri, "/log") == 0)) {
                char api_key[64] = {0};
                if (get_query_param(uri, "key", api_key, sizeof(api_key))) {
                    if (is_session_valid(api_key)) {
                        is_authorized = true;
                    }
                }
            }

            // Security: Protect /status and /control and /log
            if (!is_authorized && 
                (strcmp(uri, "/status") == 0 || strstr(uri, "/control") != NULL || strcmp(uri, "/log") == 0)) {
                 // Redirect to login or return 401
                 // For API calls (like status/control), return 401/JSON
                 const char *err = "{\"error\":\"Unauthorized\",\"message\":\"Login required or provide valid key\"}";
                 
                 // [FIX] Manually construct 401 response because send_http_response sends 200 OK
                 char resp[512];
                 snprintf(resp, sizeof(resp), 
                    "HTTP/1.1 401 Unauthorized\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: %d\r\n"
                    "Connection: keep-alive\r\n"
                    "\r\n"
                    "%s", strlen(err), err);
                 
                 tcp_write(pcb, resp, strlen(resp), TCP_WRITE_FLAG_COPY);
                 tcp_output(pcb);
                 
                 conn_state->recv_len = 0;
                 break;
            }

            if (strcmp(uri, "/status") == 0) {
                ret = handle_relay_status(pcb);
            }
            else if (strstr(uri, "/control") != NULL) {
                char action[64] = {0};
                get_query_param(uri, "action", action, sizeof(action));
                ret = handle_single_relay_control(pcb, action);
            }
            else if (strcmp(uri, "/log") == 0) {
                // Parse page param
                // Format: /log?page=0
                // Since we don't have full query parsing for this path yet, let's add it.
                // Or just use the generic get_query_param we have.
                char page_str[8] = {0};
                int page = 0;
                
                // Need to find '?' in raw URI because strcmp(uri, "/log") only matches if exact.
                // But http_process_buffer splits URI at '?', so 'uri' here is just "/log".
                // Wait, http_process_buffer:
                // char *q = strchr(clean_uri, '?'); if (q) *q = '\0';
                // Ah, the 'uri' variable passed here was ALREADY cleaned in polling check? 
                // No, 'uri' in http_process_buffer is raw from request line.
                // Let's check where 'uri' comes from.
                // Line 1051: safe_strncpy(uri, uri_start, sizeof(uri)); -> Raw URI including query.
                // So we need to use strncmp or check prefix.
                
                // Let's fix the routing logic first.
            }
            // Fix routing to handle query params
            else if (strncmp(uri, "/log", 4) == 0 && (uri[4] == '\0' || uri[4] == '?')) {
                char page_str[8] = {0};
                int page = 0;
                if (get_query_param(uri, "page", page_str, sizeof(page_str))) {
                    page = atoi(page_str);
                }
                
                const char *log = Log_GetEntriesJSON(page); // Modified to accept page
                send_http_response(pcb, "application/json; charset=GBK", log ? log : "{\"error\":\"No logs\"}", log ? strlen(log) : 18, true);
            }
            else if (strcmp(uri, "/system_log.csv") == 0 || strcmp(uri, "/log_download") == 0) {
                 handle_log_download(pcb);
            }
            else if (strcmp(uri, "/dump_flash") == 0) {
                 handle_flash_dump(pcb);
            }
            else if (strcmp(uri, "/api/time") == 0) {
                // ???? /api/time ????
                char response[128];
                bool synced = Is_Time_Synchronized();
                char *time_str = Get_Current_Time_String();
                
                snprintf(response, sizeof(response), 
                         "{\"synced\":%s,\"time\":\"%s\"}", 
                         synced ? "true" : "false", 
                         (time_str && strlen(time_str) > 0) ? time_str : "No-Sync");
                         
                send_http_response(pcb, "application/json", response, strlen(response), true);
            }
            else if (strcmp(uri, "/api/system/config") == 0) {
                #include "fs.h"
                Flash_System_Config_T config;
                char json_resp[512];
                
                if (Flash_Read_System_Config(&config)) {
                    /* Ensure null termination for safety */
                    config.device_name[sizeof(config.device_name)-1] = '\0';
                    for(int i=0; i<4; i++) config.channel_names[i][31] = '\0';
                    
                    snprintf(json_resp, sizeof(json_resp), 
                        "{\"name\":\"%s\",\"version\":\"%s\",\"ch1\":\"%s\",\"ch2\":\"%s\",\"ch3\":\"%s\",\"ch4\":\"%s\"}", 
                        config.device_name, FIRMWARE_VERSION,
                        config.channel_names[0], config.channel_names[1],
                        config.channel_names[2], config.channel_names[3]);
                } else {
                    /* Return default if not initialized (UTF-8: Yongda Emergency Module) */
                    snprintf(json_resp, sizeof(json_resp), 
                        "{\"name\":\"\xE6\xB0\xB8\xE8\xBE\xBE\xE5\xBA\x94\xE6\x80\xA5\xE6\xA8\xA1\xE5\x9D\x97\",\"version\":\"%s\",\"ch1\":\"\xE9\x80\x9A\xE9\x81\x93 1 (CH1)\",\"ch2\":\"\xE9\x80\x9A\xE9\x81\x93 2 (CH2)\",\"ch3\":\"\xE9\x80\x9A\xE9\x81\x93 3 (CH3)\",\"ch4\":\"\xE9\x80\x9A\xE9\x81\x93 4 (CH4)\"}", FIRMWARE_VERSION);
                }
                send_http_response(pcb, "application/json", json_resp, strlen(json_resp), true);
            }
            else if (strcmp(uri, "/api/network/config") == 0) {
                Flash_Network_Config_T config;
                char json_resp[1024]; // Increase buffer size for multi-rule config
                
                if (Flash_Read_Network_Config(&config)) {
                     int offset = snprintf(json_resp, sizeof(json_resp), 
                        "{\"ip\":\"%d.%d.%d.%d\",\"netmask\":\"%d.%d.%d.%d\",\"gateway\":\"%d.%d.%d.%d\",\"timeout\":%u,\"web_port\":%u",
                        ip4_addr1((struct ip_addr*)&config.ip_addr), ip4_addr2((struct ip_addr*)&config.ip_addr), ip4_addr3((struct ip_addr*)&config.ip_addr), ip4_addr4((struct ip_addr*)&config.ip_addr),
                        ip4_addr1((struct ip_addr*)&config.netmask), ip4_addr2((struct ip_addr*)&config.netmask), ip4_addr3((struct ip_addr*)&config.netmask), ip4_addr4((struct ip_addr*)&config.netmask),
                        ip4_addr1((struct ip_addr*)&config.gateway), ip4_addr2((struct ip_addr*)&config.gateway), ip4_addr3((struct ip_addr*)&config.gateway), ip4_addr4((struct ip_addr*)&config.gateway),
                        config.session_timeout < 5000 ? DEFAULT_SESSION_TIMEOUT_MS : config.session_timeout,
                        (config.web_port > 0 && config.web_port != 0xFFFF) ? config.web_port : 80);
                        
                     // Append 5 Whitelist Rules
                     for (int i = 0; i < 5; i++) {
                         offset += snprintf(json_resp + offset, sizeof(json_resp) - offset,
                            ",\"wl_ip_%d\":\"%d.%d.%d.%d\",\"wl_mask_%d\":\"%d.%d.%d.%d\",\"wl_en_%d\":%u",
                            i,
                            ip4_addr1((struct ip_addr*)&config.whitelist_rules[i].ip), ip4_addr2((struct ip_addr*)&config.whitelist_rules[i].ip), ip4_addr3((struct ip_addr*)&config.whitelist_rules[i].ip), ip4_addr4((struct ip_addr*)&config.whitelist_rules[i].ip),
                            i,
                            ip4_addr1((struct ip_addr*)&config.whitelist_rules[i].mask), ip4_addr2((struct ip_addr*)&config.whitelist_rules[i].mask), ip4_addr3((struct ip_addr*)&config.whitelist_rules[i].mask), ip4_addr4((struct ip_addr*)&config.whitelist_rules[i].mask),
                            i,
                            config.whitelist_rules[i].enabled);
                     }
                     
                     snprintf(json_resp + offset, sizeof(json_resp) - offset, "}");
                     
                } else {
                     // Default
                     snprintf(json_resp, sizeof(json_resp), 
                        "{\"ip\":\"%s\",\"netmask\":\"%s\",\"gateway\":\"%s\",\"timeout\":%u,\"web_port\":80,\"wl_en_0\":0,\"wl_en_1\":0,\"wl_en_2\":0,\"wl_en_3\":0,\"wl_en_4\":0}",
                        ETH_IP_ADDR, ETH_NET_MASK, ETH_GATEWAY, DEFAULT_SESSION_TIMEOUT_MS);
                }
                send_http_response(pcb, "application/json", json_resp, strlen(json_resp), true);
            }
            else if (strcmp(uri, "/favicon.ico") == 0) {
                struct fs_file *file = fs_open("/favicon.ico");
                if (file) {
                    send_http_response_ex(pcb, "image/x-icon", file->data, file->len, true, false, "Cache-Control: max-age=86400");
                    fs_close(file);
                } else {
                    send_http_response(pcb, "image/x-icon", "", 0, true);
                }
            }
						else if (strcmp(uri, "/logo.png") == 0) {
                struct fs_file *file = fs_open("/logo.png");
                if (file) {
                    send_http_response_ex(pcb, "image/png", file->data, file->len, true, false, "Cache-Control: max-age=86400");
                    fs_close(file);
                } else {
                    send_http_response(pcb, "image/png", "", 0, true); // Should be 404? But favicon does this.
                }
            }
            else if (strcmp(uri, "/upload") == 0) {
                 if (conn_state && conn_state->is_logged_in) {
                     const char *upload_html = 
                        "<!DOCTYPE html><html><body><h2>Firmware Upload</h2>"
                        "<input type=\"file\" id=\"fw\" />"
                        "<button onclick=\"upload()\">Upload</button>"
                        "<div id=\"status\"></div>"
                        "<script>"
                        "function upload() {"
                        "var file = document.getElementById('fw').files[0];"
                        "if(!file) return;"
                        "var xhr = new XMLHttpRequest();"
                        "xhr.open('POST', '/api/firmware/upload', true);"
                        "xhr.onload = function() {"
                        "document.getElementById('status').innerText = xhr.responseText;"
                        "};"
                        "xhr.send(file);"
                        "}"
                        "</script></body></html>";
                     send_http_response_ex(pcb, "text/html", upload_html, strlen(upload_html), true, false, NULL);
                 } else {
                    const char *redirect = "HTTP/1.1 302 Found\r\nLocation: /login\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    tcp_write(pcb, redirect, strlen(redirect), TCP_WRITE_FLAG_COPY);
                    tcp_output(pcb);
                 }
            }
            else if (strcmp(uri, "/") == 0 || strcmp(uri, "/index.html") == 0) {
                 if (conn_state && conn_state->is_logged_in) {
                    struct fs_file *file = fs_open("/index.html");
                    if (file) {
                        send_http_response_ex(pcb, fs_get_content_type(uri), file->data, file->len, true, true, "Cache-Control: no-cache, no-store, must-revalidate\r\nPragma: no-cache\r\nExpires: 0");
                        fs_close(file);
                    } else {
                        // ?????FS ?????????????? (??????????)
                        if (http_debug_enabled) printf("[HTTP] ERR: fs_open failed (OOM?)\n");
                        const char *err_msg = "503 Service Unavailable (FS Open Failed)";
                        send_http_response_ex(pcb, "text/plain", err_msg, strlen(err_msg), false, false, "Connection: close");
                    }
                 } else {
                    const char *redirect = "HTTP/1.1 302 Found\r\nLocation: /login\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    tcp_write(pcb, redirect, strlen(redirect), TCP_WRITE_FLAG_COPY);
                    tcp_output(pcb);
                 }
            }
            else if (strcmp(uri, "/login") == 0 || strcmp(uri, "/login.html") == 0) {
                struct fs_file *file = fs_open(uri);
                if (file) {
                    send_http_response_ex(pcb, fs_get_content_type(uri), file->data, file->len, true, true, "Cache-Control: no-cache, no-store, must-revalidate\r\nPragma: no-cache\r\nExpires: 0");
                    fs_close(file);
                } else {
                    const char *err = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    tcp_write(pcb, err, strlen(err), TCP_WRITE_FLAG_COPY);
                    tcp_output(pcb);
                }
            }
            else {
                // 404
                const char *err = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n";
                tcp_write(pcb, err, strlen(err), TCP_WRITE_FLAG_COPY);
                tcp_output(pcb);
            }
        } 
        else if (strncmp(method, "POST", 4) == 0) {
            if (strncmp(uri, "/api/firmware/upload",20) == 0) {
                 if (!conn_state->is_logged_in) {
                     send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Unauthorized\"}", -1, true);
                     return ERR_OK;
                 }
                 conn_state->is_uploading = true;
                 conn_state->upload_content_len = content_len;
                 conn_state->upload_received_len = 0;
                 
                 /* Initialize Local Web Update */
                 IAP_Web_Init(conn_state->upload_content_len);
                 
                 /* Set Global Lock */
                 g_upgrade_in_progress = true;
                 
                 char *body_start = header_end + 4;
                 int initial_body_len = conn_state->recv_len - (body_start - conn_state->recv_buf);
                 
                 if (initial_body_len > 0) {
                     /* Process Initial Chunk Locally */
                     if (IAP_Web_Write((uint8_t*)body_start, initial_body_len) != 0) {
                         conn_state->is_uploading = false;
                         g_upgrade_in_progress = false; // Release Lock (Error)
                         send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Flash Write Error\"}", -1, true);
                         return ERR_OK;
                     }
                     conn_state->upload_received_len += initial_body_len;
                 }
                 
                 conn_state->recv_len = 0;
                 
                 if (conn_state->upload_received_len >= conn_state->upload_content_len) {
                     IAP_Web_Finish();
                     conn_state->is_uploading = false;
                     g_upgrade_in_progress = false; // Release Lock
                     send_http_response(pcb, "application/json", "{\"success\":true,\"message\":\"Upload Complete\"}", -1, true);
                 }
                 return ERR_OK;
            }
            else if (strcmp(uri, "/api/time/sync") == 0) {
                // Simple JSON parsing
                char *body = strstr(conn_state->recv_buf, "\r\n\r\n");
                if (body) {
                    body += 4;
                    
                    uint32_t timestamp = 0;
                    int8_t timezone = 0;
                    
                    char *ts_ptr = strstr(body, "\"timestamp\":");
                    if (ts_ptr) {
                        timestamp = (uint32_t)strtoul(ts_ptr + 12, NULL, 10);
                    }
                    
                    char *tz_ptr = strstr(body, "\"timezone\":");
                    if (tz_ptr) {
                        timezone = (int8_t)atoi(tz_ptr + 11);
                    }
                    
                    if (timestamp > 0) {
                        Time_Manager_Sync_Async(timestamp, timezone);
                        send_http_response(pcb, "application/json", "{\"success\":true}", 16, true);
                    } else {
                        send_http_response(pcb, "application/json", "{\"success\":false,\"error\":\"Invalid data\"}", 39, true);
                    }
                } else {
                    send_http_response(pcb, "application/json", "{\"success\":false,\"error\":\"No body\"}", 35, true);
                }
            }
            else if (strcmp(uri, "/api/login") == 0) {
             char *body = strstr(conn_state->recv_buf, "\r\n\r\n");
             if (body) {
                 body += 4;
                 char password[64] = {0};
                 if (get_json_string(body, "password", password, sizeof(password))) {
                     if (Flash_Verify_Password(password)) {
                        conn_state->is_logged_in = true;
                        // Generate new Session Key (Encrypted with Password) as per requirement
                        start_new_session(password);
                        
                        char cookie_header[128];
                         snprintf(cookie_header, sizeof(cookie_header), "Set-Cookie: session=%s; Path=/; HttpOnly", g_auth_key);
                         
                         // Return Key in JSON as well for scripts
                         char json_resp[128];
                         snprintf(json_resp, sizeof(json_resp), "{\"success\":true,\"message\":\"Login OK\",\"key\":\"%s\"}", g_auth_key);
                         
                         send_http_response_ex(pcb, "application/json", json_resp, strlen(json_resp), true, false, cookie_header);
                     } else {
                         send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid password\"}", -1, true);
                     }
                 } else {
                      send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid Request\"}", -1, true);
                 }
             }
         }
				else if (strcmp(uri, "/api/logout") == 0) {
            conn_state->is_logged_in = false;
            // Clear Cookie
            const char *cookie_header = "Set-Cookie: session=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT";
            send_http_response_ex(pcb, "application/json", "{\"success\":true}", 16, true, false, cookie_header);
        }
        else if (strcmp(uri, "/api/change_password") == 0) {
            char *body = strstr(conn_state->recv_buf, "\r\n\r\n");
            if (body) {
                body += 4;
                char old_pwd[64] = {0};
                char new_pwd[64] = {0};
                
                get_json_string(body, "old_password", old_pwd, sizeof(old_pwd));
                get_json_string(body, "new_password", new_pwd, sizeof(new_pwd));
                
                if (Flash_Verify_Password(old_pwd)) {
                     if (strcmp(new_pwd, "admin") == 0) {
                         send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Cannot use default password\"}", -1, true);
                     }
                     else if (!check_password_complexity(new_pwd)) {
                         send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Password must contain uppercase, lowercase, and special characters\"}", -1, true);
                     }
                     else if (strlen(new_pwd) > 0 && strlen(new_pwd) < 32) {
                         if (Flash_Update_Password(new_pwd)) {
                             send_http_response(pcb, "application/json", "{\"success\":true,\"message\":\"Password Changed\"}", -1, true);
                         } else {
                             send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Update Failed\"}", -1, true);
                         }
                     } else {
                         send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid Password Length\"}", -1, true);
                     }
                } else {
                    send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Wrong Old Password\"}", -1, true);
                }
            }
        }
        else if (strcmp(uri, "/api/system/config") == 0) {
             char *body = strstr(conn_state->recv_buf, "\r\n\r\n");
             if (body) {
                 body += 4;
                 Flash_System_Config_T config;
                 
                 /* Read existing first to preserve password */
                 if (!Flash_Read_System_Config(&config)) {
                     memset(&config, 0, sizeof(config));
                     strcpy(config.password, "admin");
                     strcpy(config.device_name, "\xE6\xB0\xB8\xE8\xBE\xBE\xE5\xBA\x94\xE6\x80\xA5\xE6\xA8\xA1\xE5\x9D\x97");
                     strcpy(config.fw_version, "");
                     strcpy(config.channel_names[0], "\xE9\x80\x9A\xE9\x81\x93 1 (CH1)");
                     strcpy(config.channel_names[1], "\xE9\x80\x9A\xE9\x81\x93 2 (CH2)");
                     strcpy(config.channel_names[2], "\xE9\x80\x9A\xE9\x81\x93 3 (CH3)");
                     strcpy(config.channel_names[3], "\xE9\x80\x9A\xE9\x81\x93 4 (CH4)");
                 }
                 
                 char name[64] = {0};
                 char ver[32] = {0};
                 char ch[4][32]; memset(ch, 0, sizeof(ch));
                 
                 bool has_name = get_json_string(body, "name", name, sizeof(name));
                 bool has_ver = get_json_string(body, "version", ver, sizeof(ver));
                 bool has_ch1 = get_json_string(body, "ch1", ch[0], 32);
                 bool has_ch2 = get_json_string(body, "ch2", ch[1], 32);
                 bool has_ch3 = get_json_string(body, "ch3", ch[2], 32);
                 bool has_ch4 = get_json_string(body, "ch4", ch[3], 32);
                 
                 if (has_name || has_ver || has_ch1 || has_ch2 || has_ch3 || has_ch4) {
                     if (has_name) strncpy(config.device_name, name, sizeof(config.device_name)-1);
                     if (has_ver) strncpy(config.fw_version, ver, sizeof(config.fw_version)-1);
                     if (has_ch1) strncpy(config.channel_names[0], ch[0], 31);
                     if (has_ch2) strncpy(config.channel_names[1], ch[1], 31);
                     if (has_ch3) strncpy(config.channel_names[2], ch[2], 31);
                     if (has_ch4) strncpy(config.channel_names[3], ch[3], 31);
                     
                     if (Flash_Write_System_Config(&config)) {
                         send_http_response(pcb, "application/json", "{\"success\":true,\"message\":\"Saved\"}", -1, true);
                     } else {
                         send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Flash Write Failed\"}", -1, true);
                     }
                 } else {
                     send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"No data\"}", -1, true);
                 }
             }
        }
        else if (strcmp(uri, "/api/network/config") == 0) {
            char *body = strstr(conn_state->recv_buf, "\r\n\r\n");
            if (body) {
                body += 4;
                Flash_Network_Config_T config;
                memset(&config, 0, sizeof(config));
                
                char ip_str[16], nm_str[16], gw_str[16], to_str[16], port_str[8];
                bool has_ip = get_json_string(body, "ip", ip_str, sizeof(ip_str));
                bool has_nm = get_json_string(body, "netmask", nm_str, sizeof(nm_str));
                bool has_gw = get_json_string(body, "gateway", gw_str, sizeof(gw_str));
                bool has_to = get_json_string(body, "timeout", to_str, sizeof(to_str));
                bool has_port = get_json_string(body, "web_port", port_str, sizeof(port_str));
                
                if (has_ip && has_nm && has_gw) {
                    // Parse strings to uint32_t (Network Byte Order)
                    // Note: ipaddr_addr returns u32_t in Network Byte Order
                    config.ip_addr = ipaddr_addr(ip_str);
                    config.netmask = ipaddr_addr(nm_str);
                    config.gateway = ipaddr_addr(gw_str);
                    
                    if (has_to) {
                        config.session_timeout = (uint32_t)strtoul(to_str, NULL, 10);
                        if (config.session_timeout < 5000) config.session_timeout = 5000; // Min 5s
                    } else {
                        config.session_timeout = DEFAULT_SESSION_TIMEOUT_MS;
                    }
                    
                    if (has_port) {
                         config.web_port = (uint16_t)strtoul(port_str, NULL, 10);
                         if (config.web_port == 0) config.web_port = 80;
                    } else {
                         config.web_port = 80;
                    }
                    
                    // Parse 5 Whitelist Rules
                    for (int i = 0; i < 5; i++) {
                        char key_ip[16], key_mask[16], key_en[16];
                        char val_ip[16], val_mask[16], val_en[8];
                        
                        snprintf(key_ip, sizeof(key_ip), "wl_ip_%d", i);
                        snprintf(key_mask, sizeof(key_mask), "wl_mask_%d", i);
                        snprintf(key_en, sizeof(key_en), "wl_en_%d", i);
                        
                        if (get_json_string(body, key_ip, val_ip, sizeof(val_ip))) {
                            config.whitelist_rules[i].ip = ipaddr_addr(val_ip);
                            
                            // Default mask to Host (255.255.255.255) if missing
                            if (get_json_string(body, key_mask, val_mask, sizeof(val_mask))) {
                                config.whitelist_rules[i].mask = ipaddr_addr(val_mask);
                            } else {
                                config.whitelist_rules[i].mask = 0xFFFFFFFF; 
                            }
                            
                            if (get_json_string(body, key_en, val_en, sizeof(val_en))) {
                                config.whitelist_rules[i].enabled = (uint8_t)atoi(val_en);
                            } else {
                                config.whitelist_rules[i].enabled = 0;
                            }
                        } else {
                            memset(&config.whitelist_rules[i], 0, sizeof(Whitelist_Rule_T));
                        }
                    }
                    
                    if (config.ip_addr != IPADDR_NONE && config.netmask != IPADDR_NONE && config.gateway != IPADDR_NONE) {
                        if (Flash_Write_Network_Config(&config)) {
                            // Update global timeout immediately
                            g_session_timeout = config.session_timeout;
                            // Update global rules
                            for (int i = 0; i < 5; i++) {
                                g_whitelist_rules[i] = config.whitelist_rules[i];
                            }
                            
                            // Trigger Dynamic IP Change
                            g_pending_config = config;
                            g_trigger_ip_change = true;
                            g_ip_change_timestamp = sys_now();
                            
                            send_http_response(pcb, "application/json", "{\"success\":true,\"message\":\"Saved. Changing IP now...\"}", -1, true);
                        } else {
                            send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Flash Write Failed\"}", -1, true);
                        }
                    } else {
                        send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid IP Format\"}", -1, true);
                    }
                } else {
                    send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Missing fields\"}", -1, true);
                }
            }
        }
        else {
             send_http_response(pcb, "text/plain", "POST OK", 7, true);
        }
        }

        // ?????????????? (Memmove remaining data to start)
        int request_len = request_end - conn_state->recv_buf;
        int remaining_len = conn_state->recv_len - request_len;
        
        if (remaining_len > 0) {
            memmove(conn_state->recv_buf, request_end, remaining_len);
            conn_state->recv_len = remaining_len;
            conn_state->recv_buf[remaining_len] = '\0';
        } else {
            conn_state->recv_len = 0;
            conn_state->recv_buf[0] = '\0';
        }
    }
    
    return ERR_OK;
}

static err_t http_recv_callback(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    http_conn_state_t *conn_state = (http_conn_state_t *)arg;

    // Check Access Denied Flag
    if (conn_state && conn_state->access_denied) {
        if (p) {
            tcp_recved(pcb, p->tot_len); // Acknowledge data to keep window open
            pbuf_free(p);
        }
        
        // Send 403 and Close
        const char *msg = "HTTP/1.1 403 Forbidden\r\nContent-Length: 15\r\n\r\nAccess Denied.\n";
        tcp_write(pcb, msg, strlen(msg), TCP_WRITE_FLAG_COPY);
        tcp_output(pcb);
        
        // Mark for close (handled by poll or sent callback)
        // Or just close now if write buffer allows
        conn_state->close_after_send = true;
        // http_close_conn(pcb, conn_state); // Immediate close might be too fast for browser to read 403
        return ERR_OK;
    }
    
    if (p == NULL) {
        if (http_debug_enabled) {
            printf("[HTTP] Closed: id=%d\n", conn_state ? conn_state->conn_id : 0);
        }
        
        /* Explicitly unbind callback arg to prevent dangling references */
        tcp_arg(pcb, NULL);
        tcp_sent(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_poll(pcb, NULL, 0);
        
        if (conn_state) {
            memset(conn_state, 0, sizeof(http_conn_state_t));
        }
        tcp_close(pcb);
        return ERR_OK;
    }
    
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }
    
    if (conn_state) {
        conn_state->last_active_time = sys_now();

        if (conn_state->is_uploading) {
            struct pbuf *q;
            
            for (q = p; q != NULL; q = q->next) {
							 if (conn_state->upload_received_len == 0) {
                     if (q->len >= 4) {
                         uint8_t *d = (uint8_t*)q->payload;
                         // Check for STM32 Binary Magic (Stack Pointer usually in RAM 0x200xxxxx)
                         // Common pattern: Little Endian [0x??, 0x??, 0x00-0x01, 0x20]
                         // E.g. 0x20001000 -> 00 10 00 20
                         if (d[3] != 0x20) {
                             printf("[HTTP] FW Upload Rejected: Invalid Header 0x%02X\n", d[3]);
                             conn_state->is_uploading = false;
                             g_upgrade_in_progress = false;
                             send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid Firmware File\"}", -1, true);
                             
                             // Reset I2C and state
                             IAP_Web_Finish(); 
                             conn_state->recv_len = 0;
                             conn_state->recv_buf[0] = '\0';
                             conn_state->upload_received_len = 0;
                             conn_state->upload_content_len = 0;

                             tcp_recved(pcb, p->tot_len); 
                             pbuf_free(p);
                             return ERR_OK;
                         }
                         printf("[HTTP] FW Header Valid. SP Base: 0x%02X\n", d[3]);
                     }
                }
                int ret = IAP_Web_Write((uint8_t*)q->payload, q->len);
                if (ret != 0) {
                     conn_state->is_uploading = false;
                     g_upgrade_in_progress = false; // Release Lock (Error)
                     printf("[HTTP] Flash Write Error: %d at offset %u\n", ret, conn_state->upload_received_len);
                     send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Flash Write Error\"}", -1, true);
                     
                     // Reset state
                     IAP_Web_Finish();
                     conn_state->upload_received_len = 0;
                     conn_state->upload_content_len = 0;
                     conn_state->recv_len = 0;
                     conn_state->recv_buf[0] = '\0';
                     
                     tcp_recved(pcb, p->tot_len);
                     pbuf_free(p);
                     return ERR_OK; 
                }
                conn_state->upload_received_len += q->len;
                
                /* Explicitly yield to LwIP every 4KB to process ACKs/ARP/etc */
                /* if (conn_state->upload_received_len % 4096 == 0) { ... } */
            }
            
            /* Critical: Update TCP Window immediately to prevent "Window Full" deadlock */
            tcp_recved(pcb, p->tot_len);
            
            /* Force output to send ACK now, not later */
            // tcp_output(pcb); // Usually called by LwIP internal, but can help
            
            pbuf_free(p);
            
            if (conn_state->upload_received_len >= conn_state->upload_content_len) {
                 printf("[HTTP] Upload Finish. Recv: %d / %d\n", conn_state->upload_received_len, conn_state->upload_content_len);
                 IAP_Web_Finish();
                 conn_state->is_uploading = false;
                 g_upgrade_in_progress = false; // Release Lock
                 send_http_response(pcb, "application/json", "{\"success\":true,\"message\":\"Upload Complete\"}", -1, true);
                 // Reset State
                 conn_state->upload_received_len = 0;
                 conn_state->upload_content_len = 0;
                 conn_state->recv_len = 0;
                 conn_state->recv_buf[0] = '\0';
            }
            return ERR_OK;
        }
        
        // Not uploading yet - Buffer data and check for mode switch
        struct pbuf *q;
        int copied_total = 0;
        
        for (q = p; q != NULL && conn_state->recv_len < sizeof(conn_state->recv_buf) - 1; q = q->next) {
            uint16_t copy_len = q->len;
            if (conn_state->recv_len + copy_len >= sizeof(conn_state->recv_buf)) {
                copy_len = sizeof(conn_state->recv_buf) - 1 - conn_state->recv_len;
            }
            memcpy(conn_state->recv_buf + conn_state->recv_len, q->payload, copy_len);
            conn_state->recv_len += copy_len;
            copied_total += copy_len;
        }
        conn_state->recv_buf[conn_state->recv_len] = '\0';
        
        // Try processing immediately to catch Upload start
        http_process_buffer(conn_state);
        
        // Check if we switched to Upload Mode during processing
        if (conn_state->is_uploading) {
            // Process the overflow data (data in p that didn't fit in recv_buf)
            int remaining_in_p = p->tot_len - copied_total;
            if (remaining_in_p > 0) {
                 printf("[HTTP] Upload: Processing overflow data %d bytes\n", remaining_in_p);
                 
                 struct pbuf *iter = p;
                 int skip = copied_total;
                 
                 // Advance to the first uncopied byte
                 while (iter != NULL && skip >= iter->len) {
                     skip -= iter->len;
                     iter = iter->next;
                 }
                 
                 // Write remaining data
                 while (iter != NULL) {
                     uint8_t *payload = (uint8_t*)iter->payload + skip;
                     int len = iter->len - skip;
                     
                     if (len > 0) {
                         // First Chunk Check: Validate Firmware Header
                         if (conn_state->upload_received_len == 0) {
                              if (len >= 4) {
                                  uint8_t *d = payload;
                                  // Check for STM32 Binary Magic (Stack Pointer usually in RAM 0x200xxxxx)
                                  if (d[3] != 0x20) {
                                      printf("[HTTP] FW Upload Rejected: Invalid Header 0x%02X\n", d[3]);
                                      conn_state->is_uploading = false;
                                      g_upgrade_in_progress = false;
                                      send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Invalid Firmware File\"}", -1, true);
                                      
                                      // Reset state
                                      IAP_Web_Finish();
                                      conn_state->upload_received_len = 0;
                                      conn_state->upload_content_len = 0;
                                      conn_state->recv_len = 0;
                                      conn_state->recv_buf[0] = '\0';
                                      return ERR_OK;
                                  }
                                  printf("[HTTP] FW Header Valid. SP Base: 0x%02X\n", d[3]);
                              }
                         }

                         int ret = IAP_Web_Write(payload, len);
                         if (ret != 0) {
                             conn_state->is_uploading = false;
                             g_upgrade_in_progress = false;
                             send_http_response(pcb, "application/json", "{\"success\":false,\"message\":\"Flash Write Error\"}", -1, true);
                             
                             // Reset state
                             IAP_Web_Finish();
                             conn_state->upload_received_len = 0;
                             conn_state->upload_content_len = 0;
                             conn_state->recv_len = 0;
                             conn_state->recv_buf[0] = '\0';
                             return ERR_OK;
                             break;
                         }
                         conn_state->upload_received_len += len;
                     }
                     skip = 0;
                     iter = iter->next;
                 }
                 
                 if (conn_state->is_uploading && conn_state->upload_received_len >= conn_state->upload_content_len) {
                     IAP_Web_Finish();
                     conn_state->is_uploading = false;
                     g_upgrade_in_progress = false;
                     send_http_response(pcb, "application/json", "{\"success\":true,\"message\":\"Upload Complete\"}", -1, true);
                 }
            }
        }
    }
    
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    
    if (!conn_state) return ERR_OK;

    // Only process again if not uploading (upload handled above)
    if (!conn_state->is_uploading) {
        return http_process_buffer(conn_state);
    }
    
    return ERR_OK;
}

/* ========================== TCP????????? ========================== */
static err_t http_accept_callback(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    
    if (err != ERR_OK || !pcb) {
        if (pcb) tcp_close(pcb);
        return ERR_MEM;
    }

    // Check ACL
    bool allowed = true; // Default allow (if no rules active or internal error)
    
    if (netif_default) {
        // ... (ACL Logic) ...
        // Check if in same subnet
        // if ((pcb->remote_ip.addr & netmask->addr) != (local_ip->addr & netmask->addr)) {
            // Different subnet - Check Whitelist
            if(1){
                allowed = false; // Default deny if rules active
                
                // Debug: Print Access Control details
                if (http_debug_enabled) {
                    printf("[HTTP] ACL Check: Remote=%d.%d.%d.%d\n",
                        ip4_addr1(&pcb->remote_ip), ip4_addr2(&pcb->remote_ip),
                        ip4_addr3(&pcb->remote_ip), ip4_addr4(&pcb->remote_ip));
                }

                // 1. Check configured Whitelist Rules (5 Rules)
                if (!is_whitelist_active()) {
                    // If no rules are enabled, allow all
                    allowed = true;
                } else {
                    // Check rules...
                    for (int i = 0; i < 5; i++) {
                        if (g_whitelist_rules[i].enabled) {
                            uint32_t rule_ip = g_whitelist_rules[i].ip;
                            uint32_t rule_mask = g_whitelist_rules[i].mask;

                            // Check 1: Direct Match
                            if ((pcb->remote_ip.addr & rule_mask) == (rule_ip & rule_mask)) {
                                allowed = true;
                                if (http_debug_enabled) printf("[HTTP] Rule %d Matched (Direct).\n", i);
                                break;
                            }

                            // Check 2: Byte-Swap Match
                            uint32_t ip_rev = ((rule_ip & 0xFF) << 24) | ((rule_ip & 0xFF00) << 8) | ((rule_ip & 0xFF0000) >> 8) | ((rule_ip & 0xFF000000) >> 24);
                            uint32_t mask_rev = ((rule_mask & 0xFF) << 24) | ((rule_mask & 0xFF00) << 8) | ((rule_mask & 0xFF0000) >> 8) | ((rule_mask & 0xFF000000) >> 24);
                            
                            if ((pcb->remote_ip.addr & mask_rev) == (ip_rev & mask_rev)) {
                                 allowed = true;
                                 if (http_debug_enabled) printf("[HTTP] Rule %d Matched (Swapped).\n", i);
                                 break;
                            }
                        }
                    }
                }
                
                // 2. Check Rescue IP (192.168.1.2) - Hardcoded Backdoor
                // 192.168.1.2 in hex (Little Endian): 0x0201A8C0
                if (pcb->remote_ip.addr == 0x0201A8C0) {
                    allowed = true;
                    if (http_debug_enabled) printf("[HTTP] Rescue IP 192.168.1.2 Access Granted.\n");
                }
                
                if (!allowed) {
                    if (http_debug_enabled) {
                        char remote_ip_str[16];
                        snprintf(remote_ip_str, sizeof(remote_ip_str), "%d.%d.%d.%d",
                            ip4_addr1(&pcb->remote_ip), ip4_addr2(&pcb->remote_ip),
                            ip4_addr3(&pcb->remote_ip), ip4_addr4(&pcb->remote_ip));
                        printf("[HTTP] Access Denied for IP: %s (Will close gracefully)\n", remote_ip_str);
                    }
                    // Do NOT abort here. Let connection establish, then close with 403.
                }
            }
        // }
    }

    /* REJECT ALL NEW CONNECTIONS if upgrade is in progress */
    if (g_upgrade_in_progress) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    // Accept connection
    if (http_pcb) {
        tcp_accepted(http_pcb);
    }
    
    http_conn_state_t *conn = alloc_conn_state(pcb);
    if (!conn) {
        tcp_close(pcb);
        return ERR_MEM;
    }
    
    // Set Access Denied Flag
    conn->access_denied = !allowed;
    
    tcp_nagle_disable(pcb);
    tcp_recv(pcb, http_recv_callback);
    tcp_sent(pcb, http_sent_callback);
    tcp_err(pcb, http_err_callback);
    
    return ERR_OK;
}

/* ========================== HTTP??????????? ========================== */
void Http_Server_Init(void) {
    printf("[HTTP] Init...\n");
    
    memset(conn_pool, 0, sizeof(conn_pool));
    // memset(http_static_buf, 0, sizeof(http_static_buf)); // Removed: Buffer no longer used
    next_conn_id = 1;
    
    http_pcb = tcp_new();
    if (!http_pcb) {
        printf("[HTTP] ERR: Create PCB failed\n");
        return;
    }
    
    // Load network config for port and timeout
    Flash_Network_Config_T config;
    uint16_t port = 80;
    
    // Clear rules
    memset(g_whitelist_rules, 0, sizeof(g_whitelist_rules));
    
    if (Flash_Read_Network_Config(&config)) {
        if (config.session_timeout >= 5000) {
            g_session_timeout = config.session_timeout;
            printf("[HTTP] Session Timeout loaded: %u ms\n", g_session_timeout);
        }
        if (config.web_port > 0 && config.web_port != 0xFFFF) {
            port = config.web_port;
        }
        
        // Copy rules
        for (int i = 0; i < 5; i++) {
            g_whitelist_rules[i] = config.whitelist_rules[i];
        }
    }
    
    if (tcp_bind(http_pcb, IP_ADDR_ANY, port) != ERR_OK) {
        printf("[HTTP] ERR: Bind port %d failed\n", port);
        tcp_close(http_pcb);
        http_pcb = NULL;
        return;
    }
    
    http_pcb = tcp_listen(http_pcb);
    if (!http_pcb) {
        printf("[HTTP] ERR: Listen failed\n");
        return;
    }
    
    tcp_accept(http_pcb, http_accept_callback);
    printf("[HTTP] Started on port %d\n", port);
}

void Http_Server_Task(void) {
    // Dynamic IP Change Task
    if (g_trigger_ip_change && (sys_now() - g_ip_change_timestamp > 500)) {
        g_trigger_ip_change = false;
        
        struct ip_addr ip, nm, gw;
        ip.addr = g_pending_config.ip_addr;
        nm.addr = g_pending_config.netmask;
        gw.addr = g_pending_config.gateway;
        
        // Use netif_default as it's the main interface
        if (netif_default) {
            // Force close all existing HTTP connections to prevent hanging
            for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
                if (conn_pool[i].pcb) {
                    tcp_arg(conn_pool[i].pcb, NULL);
                    tcp_abort(conn_pool[i].pcb); // Abort is safer than close here
                    memset(&conn_pool[i], 0, sizeof(http_conn_state_t));
                }
            }
            
            // Re-initialize PCB to bind to new address implicitly (or just clear old state)
            // Note: http_pcb is listening on IP_ADDR_ANY, so it might survive, 
            // but existing connections must die.
            
            netif_set_down(netif_default);
            netif_set_addr(netif_default, &ip, &nm, &gw);
            netif_set_up(netif_default); 
            
            // Restart HTTP Server to apply new port if changed
            if (http_pcb) {
                tcp_close(http_pcb);
                http_pcb = NULL;
            }
            Http_Server_Init();
            
            printf("[System] IP Changed dynamically to %d.%d.%d.%d\n", 
                   ip4_addr1(&ip), ip4_addr2(&ip), ip4_addr3(&ip), ip4_addr4(&ip));
        }
    }

    static uint32_t last_check = 0;
    uint32_t now = sys_now();

    // Reduce check interval from 100ms to 10ms for faster response
    if (now - last_check > 10) { 
        last_check = now;

        for (int i = 0; i < MAX_HTTP_CONNECTIONS; i++) {
            if (conn_pool[i].conn_id == 0 || !conn_pool[i].pcb) continue;

            // 1. Handle Log Download Continuation (New Logic)
            if (conn_pool[i].is_downloading_log) {
                 http_continue_log_download(&conn_pool[i]);
            }

            // ?????????????????
            if (conn_pool[i].unsent_len > 0) {
                 http_continue_send(&conn_pool[i]);
            }

            // ?????????
            bool send_complete = conn_pool[i].send_complete;
            uint32_t last_send_time = conn_pool[i].last_send_time;

            // ????1???????????? (Graceful Close)
            if (conn_pool[i].close_after_send && send_complete) {
                // ??????????????500ms?????50ms
                // ?????????????TCP????????LwIP?????????????FIN
                if (now - last_send_time > 50) {
                    if (http_debug_enabled) {
                        printf("[HTTP] Close conn: id=%d\n", conn_pool[i].conn_id);
                    }
                    tcp_arg(conn_pool[i].pcb, NULL); // ??????????
                    
                    // ?????????????????????????
                    if (tcp_close(conn_pool[i].pcb) != ERR_OK) {
                         tcp_abort(conn_pool[i].pcb);
                    }
                    
                    memset(&conn_pool[i], 0, sizeof(http_conn_state_t));
                }
            }

            // 2. Timeout Check (Timeout -> Abort)
            // Increased upload/download timeout to 300s (300000ms) to handle slow operations
            else if (now - conn_pool[i].last_active_time > ((conn_pool[i].is_uploading || conn_pool[i].is_downloading_log) ? 300000 : IDLE_TIMEOUT)) {
                // Check if any OTHER connection is active
                bool any_active = false;
                for (int k = 0; k < MAX_HTTP_CONNECTIONS; k++) {
                     if (conn_pool[k].conn_id != 0 && conn_pool[k].pcb && 
                         (now - conn_pool[k].last_active_time < ((conn_pool[k].is_uploading || conn_pool[k].is_downloading_log) ? 300000 : IDLE_TIMEOUT))) {
                         any_active = true;
                         break;
                     }
                }

                if (any_active || conn_pool[i].is_uploading || conn_pool[i].is_downloading_log) { // Also protect single active connection from self-timeout reset
                    // Just close this one
                    if (http_debug_enabled) {
                        printf("[HTTP] Timeout conn: id=%d. Closing single.\n", conn_pool[i].conn_id);
                    }
                    if (conn_pool[i].pcb) {
                        tcp_arg(conn_pool[i].pcb, NULL);
                        tcp_abort(conn_pool[i].pcb);
                    }
                    memset(&conn_pool[i], 0, sizeof(http_conn_state_t));
                } else {
                    if (http_debug_enabled) {
                        printf("[HTTP] Timeout conn: id=%d. Resetting ALL connections.\n", conn_pool[i].conn_id);
                    }
                    
                    // ???????????? TCP ??????????? Session
                    // ??? TCP IDLE_TIMEOUT ??? 500ms???? Session Timeout ?? 30s
                    // ????? is_session_valid() ???z??????????????????????
                    
                    for (int j = 0; j < MAX_HTTP_CONNECTIONS; j++) {
                        if (conn_pool[j].pcb) {
                            tcp_arg(conn_pool[j].pcb, NULL);
                            tcp_abort(conn_pool[j].pcb); // Abort
                        }
                        memset(&conn_pool[j], 0, sizeof(http_conn_state_t));
                    }

                    // ???????? Session????? stale session ????????????
                    //g_auth_key[0] = '\0';
                    //g_auth_expiry = 0;
                    
                    // ???????????? conn_pool ??????????
                    break; 
                }
            }
        }
    }
}

/* ?????????? */
void Http_SetDebugMode(bool enabled) {
    (void)enabled;
    // Disabled
}

/**
 * @brief  Restore Network Defaults (Called by RS485 Backdoor)
 */
void Restore_Network_Defaults(void) {
    printf("\n[NET] Restoring Defaults...\n");
    
    Flash_Network_Config_T default_config;
    memset(&default_config, 0, sizeof(default_config));
    
    // Default IP: 192.168.1.199
    // Little Endian: 0xC701A8C0
    default_config.ip_addr = 0xC701A8C0; 
    
    // Default Mask: 255.255.255.0
    // Little Endian: 0x00FFFFFF
    default_config.netmask = 0x00FFFFFF;
    
    // Default Gateway: 192.168.1.1
    // Little Endian: 0x0101A8C0
    default_config.gateway = 0x0101A8C0;
    
    default_config.session_timeout = 30000; // 30s
    
    // Write to Flash
    Flash_Write_Network_Config(&default_config);
    
    // printf("[NET] Done. Rebooting...\n");
    
    // Soft Reset
    NVIC_SystemReset();
}
