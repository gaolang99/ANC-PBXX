#include "log_system.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "flash_storage.h"
#include "web_time_handler.h"
#include <stdlib.h>

#define LOG_BUFFER_SIZE 8
#define LOG_MESSAGE_MAX 48

/* RAM Buffer */
static Log_Entry_T log_buffer[LOG_BUFFER_SIZE];
static int log_count = 0;
static int log_index = 0;
static bool log_initialized = false;
static bool log_dirty = false;

/* Module/Level Strings */
static const char* module_names[] = { "SYSTEM", "USER", "NETWORK", "TIME" };
static const char* level_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };

/* MAPPING */
#define EVENT_START    0x0001
#define EVENT_LOGIN    0x0002
#define EVENT_RELAY    0x0003
#define EVENT_ALARM    0x0004
#define EVENT_TEST     0xFFFF
#define EVENT_CUSTOM   0xFFFE

/* Helper: Map string to code */
static void Log_Map_To_Flash(const Log_Entry_T *ram, Flash_Log_Entry_T *flash) {
    flash->timestamp = Get_Current_Unix_Time();
    flash->level = ram->level;
    flash->module = ram->module;
    flash->param1 = 0;
    flash->param2 = 0;
    
    const char *msg = ram->message;
    
    // Mapping Logic
    if (strstr(msg, "start") || strstr(msg, "Start")) {
        return; // Don't log to Flash
    } else if (strstr(msg, "Login")) {
        return; // Don't log to Flash
    } else if (strstr(msg, "Custom/Unknown Event")) {
        return; // Don't log "Custom/Unknown Event" to Flash
    } else if (strstr(msg, "Channel")) { // "Channel X Output Enabled/Disabled"
        flash->msg_code = EVENT_RELAY;
        int ch = 0;
        if (strstr(msg, "Channel 1")) ch = 1;
        else if (strstr(msg, "Channel 2")) ch = 2;
        else if (strstr(msg, "Channel 3")) ch = 3;
        else if (strstr(msg, "Channel 4")) ch = 4;
        flash->param1 = ch;
        flash->param2 = (strstr(msg, "Enabled") != NULL) ? 1 : 0;
    } else if (strstr(msg, "Power Supply Unit") && (strstr(msg, "Connected") || strstr(msg, "Enabled") || strstr(msg, "Disabled") || strstr(msg, "Disconnected"))) {
        flash->msg_code = EVENT_PSU;
        int psu = 0;
        if (strstr(msg, "Unit 1")) psu = 1;
        else if (strstr(msg, "Unit 2")) psu = 2;
        flash->param1 = psu;
        flash->param2 = (strstr(msg, "Enabled") || strstr(msg, "Connected")) ? 1 : 0;
    } else if (strstr(msg, "Smoke Sensor Detection")) {
        flash->msg_code = EVENT_SMOKE;
        flash->param2 = strstr(msg, "Enabled") ? 1 : 0;
    } else if (strstr(msg, "Smoke Alarm")) {
        flash->msg_code = EVENT_ALARM;
        flash->param1 = 1; 
        flash->param2 = 0;
    } else if (strstr(msg, "Power Supply Unit") && strstr(msg, "Failure")) {
        flash->msg_code = EVENT_ALARM;
        flash->param1 = 2; // Power Alarm
        flash->param2 = 0;
    } else if (strstr(msg, "DC Output Failure")) {
        flash->msg_code = EVENT_ALARM;
        flash->param1 = 2; // Power Alarm
        flash->param2 = 0;
    } else if (strstr(msg, "Muted")) {
        flash->msg_code = EVENT_ALARM;
        flash->param1 = 3;
    } else if (strstr(msg, "System time synchronized successfully")) {
        flash->msg_code = EVENT_TIME;
    } else if (strstr(msg, "Reset")) {
        flash->msg_code = EVENT_RESET;
    } else if (strstr(msg, "Test")) {
        flash->msg_code = EVENT_TEST;
        flash->param1 = 0xDEAD;
        flash->param2 = 0xBEEF;
    } else {
        flash->msg_code = EVENT_CUSTOM;
        // param1 = crc32(msg)? Just leave 0 for now
    }
}

/* Helper: Decode Flash Entry to String */
void Log_Decode_Entry(uint16_t msg_code, uint32_t param1, uint32_t param2, char *buffer, int max_len) {
    if (!buffer || max_len <= 0) return;
    
    switch (msg_code) {
        // case EVENT_START: strncpy(buffer, "System Started", max_len); break; // Disabled
        // case EVENT_LOGIN: snprintf(buffer, max_len, "Login Success (ID:%u)", param1); break; // Disabled
        case EVENT_RELAY: 
            if (param1 == 0) snprintf(buffer, max_len, "Relay? %s", param2 ? "ON" : "OFF");
            else snprintf(buffer, max_len, "Channel %u (CH%u) Output %s", param1, param1, param2 ? "Enabled" : "Disabled"); 
            break;
        case EVENT_ALARM: 
            if (param1 == 1) strncpy(buffer, "Smoke Alarm Triggered", max_len);
            else if (param1 == 2) strncpy(buffer, "Power Supply Alarm Triggered", max_len);
            else if (param1 == 3) strncpy(buffer, "System Alarm Muted by User", max_len);
            else strncpy(buffer, "General Alarm Triggered", max_len);
            break;
        case EVENT_TEST: strncpy(buffer, "Flash Memory Write Test Performed", max_len); break;
        case EVENT_PSU: 
            if (param1 > 0) snprintf(buffer, max_len, "Power Supply Unit %u %s", param1, param2 ? "Connected/Enabled" : "Disconnected/Disabled");
            else snprintf(buffer, max_len, "Power Supply Unit %s", param2 ? "Connected/Enabled" : "Disconnected/Disabled"); 
            break;
        case EVENT_SMOKE: snprintf(buffer, max_len, "Smoke Sensor Detection %s", param2 ? "Enabled" : "Disabled"); break;
        case EVENT_TIME: strncpy(buffer, "time synced", max_len); break;
        case EVENT_RESET: strncpy(buffer, "Network Reset to Defaults (192.168.1.199)", max_len); break;
        default: 
            // Don't show "Custom/Unknown Event", just empty string or skip
            strncpy(buffer, "", max_len); 
            break;
    }
    // Ensure null termination
    buffer[max_len - 1] = '\0';
}

/* Reverse Mapping for RAM Display */
static void Log_Map_From_Flash(const Flash_Log_Entry_T *flash, Log_Entry_T *ram) {
    // Time
    extern void Unix_Time_To_Str(uint32_t unix_time, char *buf, int len);
    if (flash->timestamp > 0) Unix_Time_To_Str(flash->timestamp, ram->time_str, sizeof(ram->time_str));
    else strcpy(ram->time_str, "No-Sync");
    
    ram->level = (Log_Level_T)flash->level;
    ram->module = (Log_Module_T)flash->module;
    
    // Message
    Log_Decode_Entry(flash->msg_code, flash->param1, flash->param2, ram->message, sizeof(ram->message));
}

void Log_System_Init(void) {
    __disable_irq();
    Flash_Storage_Init();
    Log_LoadFromFlash(); 
    log_initialized = true;
    __enable_irq();
}

static void Log_SaveToFlash_Entry(Log_Entry_T *entry) {
    if (!log_initialized || entry == NULL) return;
    
    Flash_Log_Entry_T flash_entry;
    memset(&flash_entry, 0, sizeof(flash_entry));
    
    // Check filter BEFORE processing
    if (strstr(entry->message, "start") || strstr(entry->message, "Start") || strstr(entry->message, "Login") || strstr(entry->message, "Custom/Unknown Event")) {
        return; // Skip these logs
    }
    
    Log_Map_To_Flash(entry, &flash_entry);
    
    // Safety check after mapping (if msg_code is 0, it means ignored or invalid)
    if (flash_entry.msg_code == 0) return;

    Flash_Write_Log(&flash_entry);
}

void Log_Write_With_Time(Log_Level_T level, Log_Module_T module, 
                        const char *time_str, const char *format, ...) {
    va_list args;
    char message[LOG_MESSAGE_MAX];
    
    if (!log_initialized) return;
    
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    __disable_irq();
    Log_Entry_T *entry = &log_buffer[log_index];
    memset(entry, 0, sizeof(Log_Entry_T));
    
    if (time_str) strncpy(entry->time_str, time_str, sizeof(entry->time_str)-1);
    else Get_Time_String_To_Buffer(entry->time_str, sizeof(entry->time_str));
    
    extern volatile uint32_t g_sys_tick;
    entry->timestamp = g_sys_tick;
    entry->level = level;
    entry->module = module;
    strncpy(entry->message, message, sizeof(entry->message)-1);
    
    log_index = (log_index + 1) % LOG_BUFFER_SIZE;
    if (log_count < LOG_BUFFER_SIZE) log_count++;
    log_dirty = true;
    __enable_irq();
    
    Log_SaveToFlash_Entry(entry);
}

void Log_LoadFromFlash(void) {
    int max_to_read = LOG_BUFFER_SIZE;
    Flash_Log_Entry_T flash_entries[LOG_BUFFER_SIZE];
    int count = Flash_Read_Logs(flash_entries, max_to_read);
    
    log_count = 0;
    log_index = 0;
    
    for (int i = 0; i < count; i++) {
        Log_Entry_T *entry = &log_buffer[log_index];
        Log_Map_From_Flash(&flash_entries[count - 1 - i], entry); 
        
        log_index = (log_index + 1) % LOG_BUFFER_SIZE;
        log_count++;
    }
}

/* Wrappers */
void Log_Write(Log_Level_T level, Log_Module_T module, const char *format, ...) {
    va_list args;
    char message[LOG_MESSAGE_MAX];
    if (!log_initialized) return;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Log_Write_With_Time(level, module, NULL, "%s", message);
}

int Log_GetCount(void) { return log_count; }
void Log_Clear(void) {
    memset(log_buffer, 0, sizeof(log_buffer));
    log_count = 0; log_index = 0;
    Flash_Erase_Logs();
}
/* JSON Generation (Keep existing logic mostly, but use RAM buffer) */
static char json_buffer[6144]; // Increased to 6KB for 50 entries

static void json_escape_string(char *dest, const char *src, size_t dest_size) {    /* (Same as before) */
    char *d = dest; const char *s = src; char *end = dest + dest_size - 1;
    while (*s && d < end) {
        if (*s == '\"' || *s == '\\') { *d++ = '\\'; if(d<end) *d++ = *s; }
        else if (*s == '\n') { *d++ = '\\'; if(d<end) *d++ = 'n'; }
        else if (*s == '\r') { *d++ = '\\'; if(d<end) *d++ = 'r'; }
        else { *d++ = *s; }
        s++;
    }
    *d = '\0';
}

/* Paging Support */
const char *Log_GetEntriesJSON(int page) {
    char *ptr = json_buffer;
    int remaining = sizeof(json_buffer);
    
    // Page size = 50
    int page_size = 50;
    int start_index = page * page_size;
    int total_flash_count = Flash_Get_Log_Count();
    
    // Validate page
    if (start_index >= total_flash_count) {
        return "{\"entries\":[],\"flash_count\":0}";
    }

    int written = snprintf(ptr, remaining, "{\"entries\":[");
    ptr += written; remaining -= written;

    // Read from Flash directly for paging (RAM buffer only has last few)
    // We read newest first (Reverse Order)
    // Flash stores sequentially 0..N. We want to show N..0.
    // Page 0: Index (Total-1) to (Total-8)
    // Page 1: Index (Total-9) to (Total-16)
    
    int items_on_page = 0;
    
    for (int i = 0; i < page_size; i++) {
        int logical_idx = start_index + i;
        if (logical_idx >= total_flash_count) break;
        
        // Flash_Get_Log_Entry_ByIndex already handles the reverse logic (Index 0 = Newest)
        // So we just pass the logical index directly.
        
        Flash_Log_Entry_T flash_entry;
        Log_Entry_T ram_entry;
        
        if (Flash_Get_Log_Entry_ByIndex(logical_idx, &flash_entry)) {
            Log_Map_From_Flash(&flash_entry, &ram_entry);
            
            if (items_on_page > 0) { *ptr++ = ','; remaining--; }
            
            char esc_msg[64], esc_time[32];
            json_escape_string(esc_msg, ram_entry.message, sizeof(esc_msg));
            json_escape_string(esc_time, ram_entry.time_str, sizeof(esc_time));
            
            written = snprintf(ptr, remaining, "{\"time\":\"%s\",\"message\":\"%s\"}", esc_time, esc_msg);
            ptr += written; remaining -= written;
            
            items_on_page++;
        }
    }
    
    snprintf(ptr, remaining, "],\"total\":%d,\"page\":%d}", total_flash_count, page);
    return json_buffer;
}

int Log_GetFlashCount(void) { return Flash_Get_Log_Count(); }
bool Log_GetEntry_ByIndex(int index, Log_Entry_T *entry) {
    if (index < 0 || index >= log_count) return false;
    int idx = (log_index - 1 - index + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
    *entry = log_buffer[idx];
    return true;
}

int Log_TestFlashWrite(void) {
    Flash_Log_Entry_T entry;
    entry.timestamp = Get_Current_Unix_Time();
    entry.msg_code = EVENT_TEST;
    entry.level = LOG_LEVEL_INFO;
    entry.module = LOG_MODULE_SYSTEM;
    entry.param1 = 0xDEAD;
    entry.param2 = 0xBEEF;
    
    return Flash_Write_Log(&entry);
}

bool Log_IsStaticBuffer(const void *ptr) {
    if (ptr >= (void*)json_buffer && ptr < (void*)(json_buffer + sizeof(json_buffer))) return true;
    return false;
}
