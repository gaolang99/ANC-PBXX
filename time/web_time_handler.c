#include "web_time_handler.h"
#include "log_system.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdbool.h>
#include "apm32f10x_fmc.h"
#include <stdlib.h>

/* ???flash_storage.h??????????????????? */
#include "flash_storage.h"

/* ????HTTP??????? */
extern err_t send_http_response(struct tcp_pcb *pcb, const char *content_type, 
                               const char *data, int len, bool keep_alive);

/* ??????????????????LED Flash????? */
#define RELAY_LOG_FLASH_ADDR        FLASH_LED_LOG_ADDR  /* 63?????flash_storage.h??? */
#define RELAY_LOG_MAX_ENTRIES      32U                 /* ???32????? */
#define RELAY_LOG_ENTRY_SIZE       16U                 /* ???16??? */

/* ??????????????????????????? */
typedef struct {
    uint32_t last_sync_time;     // ????????Unix????
    uint32_t last_sync_systick;  // ??????????????
    int8_t   timezone;           // ??????????UTC+8??
    uint8_t  flags;              // ?????
    uint32_t pending_sync_time;  // ???????????
    int8_t   pending_timezone;   // ??????????
    Relay_Id_T pending_relay_id;     // ??????????????
    Relay_State_T pending_relay_operation; // ???????????????
} Time_Manager_T;

/* ????????? */
#define TIME_FLAG_SYNCHRONIZED   (1 << 0)
#define TIME_FLAG_SAVED          (1 << 1)
#define TIME_FLAG_PENDING        (1 << 2)
#define TIME_FLAG_INITIALIZED    (1 << 3)
#define TIME_FLAG_SYNC_PENDING   (1 << 4)  // ??????????????????
#define TIME_FLAG_RELAY_PENDING  (1 << 5)  // ???????????????????????

static Time_Manager_T time_manager;
extern volatile uint32_t g_sys_tick;  // ??????1ms??????

/* ??????????4?????? */
typedef struct {
    uint32_t magic;             // ????? 0x54494D46 ("TIMF")
    uint32_t unix_time;         // Unix????
    uint32_t systick;           // ??????
    int8_t   timezone;          // ???
    uint8_t  checksum;          // ?????
    uint8_t  reserve[2];        // 4?????????
} __attribute__((packed, aligned(4))) Time_Flash_T;

/* ?????????????????4?????? */
typedef struct {
    uint32_t magic;             // ????? 0x52454C41 ("RELA")
    uint32_t unix_time;         // ??????????Unix??
    Relay_Id_T relay_id;        // ?????????0-3??
    Relay_State_T operation;    // ??????RELAY_OFF/RELAY_ON
    uint8_t  reserved[6];       // 16?????????
} __attribute__((packed, aligned(4))) Relay_Log_Entry_T;

/* ??????????? */
static uint8_t Calculate_Time_Checksum(const Time_Flash_T *time_data);
static void Load_Time_From_Flash(void);
// static void Unix_Time_To_DateTime(uint32_t unix_time, ...); // Made non-static below
void Unix_Time_To_DateTime(uint32_t unix_time, 
                                  uint16_t *year, 
                                  uint8_t *month, 
                                  uint8_t *day, 
                                  uint8_t *hour, 
                                  uint8_t *minute, 
                                  uint8_t *second);
static FMC_STATUS_T Safe_Flash_Erase(uint32_t address);
static FMC_STATUS_T Safe_Flash_Write(uint32_t address, uint32_t data);
static void Init_Relay_Log_System(void);
static void Erase_Relay_Log_Flash(void);
static void Write_Relay_Operation_To_Flash(Relay_Id_T relay_id, Relay_State_T operation);
static uint8_t is_leap_year(uint16_t year);
static const uint8_t* get_days_in_month_ptr(uint8_t leap);
static void Execute_Flash_Save(void);
static void Save_Time_To_Flash(uint32_t unix_time);

/* ???Flash???????? */
static bool Check_Flash_Write_Protection(uint32_t address) {
    uint32_t page_number = (address - 0x08000000) / FLASH_PAGE_SIZE;
    
    if (page_number < 60 || page_number > 64) {
        return false;
    }
    
    uint32_t option_bytes = 0x1FFFF800;
    uint32_t wrp = *(volatile uint32_t*)(option_bytes + 0x08);
    
    if (page_number <= 61) {
        if (wrp & FLASH_WRP_PAGE_60_61) {
            LOG_ERROR(LOG_MODULE_TIME, "Page %d is write-protected", page_number);
            return true;
        }
    } else {
        if (wrp & FLASH_WRP_PAGE_62_127) {
            LOG_ERROR(LOG_MODULE_TIME, "Page %d is write-protected", page_number);
            return true;
        }
    }
    
    return false;
}

/* ???????? */
static uint8_t is_leap_year(uint16_t year) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

/* ????????????? */
static const uint8_t* get_days_in_month_ptr(uint8_t leap) {
    static const uint8_t days_in_month_normal[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    static const uint8_t days_in_month_leap[] = {0,31,29,31,30,31,30,31,31,30,31,30,31};
    return leap ? days_in_month_leap : days_in_month_normal;
}

/* ?????Flash???? */
static FMC_STATUS_T Safe_Flash_Erase(uint32_t address) {
    if (address % FLASH_PAGE_SIZE != 0) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash address 0x%08X not page-aligned", address);
        return FMC_STATUS_ERROR_PG;
    }
    
    if (address < 0x08000000 || address >= 0x08040000) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash address 0x%08X out of range", address);
        return FMC_STATUS_ERROR_PG;
    }
    
    if (Check_Flash_Write_Protection(address)) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash page at address 0x%08X is write-protected", address);
        return FMC_STATUS_ERROR_WRP;
    }
    
    int retry_count = 0;
    const int max_retries = 2;
    FMC_STATUS_T final_status = FMC_STATUS_ERROR_PG;
    
    while (retry_count < max_retries) {
        retry_count++;
        
        FMC_Unlock();
        
        if (FMC_ReadStatusFlag(FMC_FLAG_WPE) == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write protected after unlock on attempt %d", retry_count);
            FMC_ClearStatusFlag(FMC_FLAG_WPE);
            if (FMC_ReadStatusFlag(FMC_FLAG_WPE) == SET) {
                LOG_ERROR(LOG_MODULE_TIME, "Cannot clear write protection on attempt %d", retry_count);
                FMC_Lock();
                continue;
            }
        }
        
        uint32_t timeout = 20000;
        while (FMC_ReadStatusFlag(FMC_FLAG_BUSY) == SET && timeout--);
        if (timeout == 0) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash busy timeout on attempt %d", retry_count);
            FMC_Lock();
            continue;
        }
        
        FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
        
        FMC_STATUS_T status = FMC_ErasePage(address);
        
        timeout = 20000;
        while (FMC_ReadStatusFlag(FMC_FLAG_BUSY) == SET && timeout--);
        
        FMC_FLAG_T oc_flag = FMC_ReadStatusFlag(FMC_FLAG_OC);
        FMC_FLAG_T pe_flag = FMC_ReadStatusFlag(FMC_FLAG_PE);
        FMC_FLAG_T wpe_flag = FMC_ReadStatusFlag(FMC_FLAG_WPE);
        
        FMC_Lock();
        
        if (timeout == 0) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash erase timeout on attempt %d", retry_count);
            continue;
        }
        
        if (oc_flag != SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash operation not complete on attempt %d", retry_count);
            continue;
        }
        
        if (pe_flag == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash programming error flag set on attempt %d", retry_count);
            continue;
        }
        
        if (wpe_flag == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write protection error flag set on attempt %d", retry_count);
            continue;
        }
        
        uint32_t *check_addr = (uint32_t *)address;
        bool erase_success = true;
        for (int i = 0; i < 4; i++) {
            if (check_addr[i] != 0xFFFFFFFF) {
                erase_success = false;
                break;
            }
        }
        
        if (!erase_success) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash erase verification failed on attempt %d", retry_count);
            continue;
        }
        
        final_status = FMC_STATUS_COMPLETE;
        break;
    }
    
    if (final_status != FMC_STATUS_COMPLETE) {
        LOG_ERROR(LOG_MODULE_TIME, "All erase attempts failed, page may be write-protected");
    }
    
    return final_status;
}

/* ?????Flash???? */
static FMC_STATUS_T Safe_Flash_Write(uint32_t address, uint32_t data) {
    if (address % 4 != 0) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash write address 0x%08X not word-aligned", address);
        return FMC_STATUS_ERROR_PG;
    }
    
    if (address < 0x08000000 || address >= 0x08040000) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash write address 0x%08X out of range", address);
        return FMC_STATUS_ERROR_PG;
    }
    
    int retry_count = 0;
    const int max_retries = 2;
    FMC_STATUS_T final_status = FMC_STATUS_ERROR_PG;
    
    while (retry_count < max_retries) {
        retry_count++;
        FMC_Unlock();
        
        if (FMC_ReadStatusFlag(FMC_FLAG_WPE) == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write protected after unlock on attempt %d", retry_count);
            FMC_ClearStatusFlag(FMC_FLAG_WPE);
            if (FMC_ReadStatusFlag(FMC_FLAG_WPE) == SET) {
                LOG_ERROR(LOG_MODULE_TIME, "Cannot clear write protection on attempt %d", retry_count);
                FMC_Lock();
                continue;
            }
        }
        
        uint32_t timeout = 20000;
        while (FMC_ReadStatusFlag(FMC_FLAG_BUSY) == SET && timeout--);
        if (timeout == 0) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash busy timeout on write attempt %d", retry_count);
            FMC_Lock();
            continue;
        }
        
        FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
        
        __disable_irq(); /* Critical Section */
        FMC_ProgramWord(address, data);
        __enable_irq(); /* End Critical Section */
        
        timeout = 20000;
        while (FMC_ReadStatusFlag(FMC_FLAG_BUSY) == SET && timeout--);
        
        FMC_FLAG_T oc_flag = FMC_ReadStatusFlag(FMC_FLAG_OC);
        FMC_FLAG_T pe_flag = FMC_ReadStatusFlag(FMC_FLAG_PE);
        FMC_FLAG_T wpe_flag = FMC_ReadStatusFlag(FMC_FLAG_WPE);
        
        FMC_Lock();
        
        if (timeout == 0) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write timeout on attempt %d", retry_count);
            continue;
        }
        
        if (oc_flag != SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write operation not complete on attempt %d, OC flag=%d", 
                     retry_count, oc_flag);
            continue;
        }
        
        if (pe_flag == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash programming error flag set on attempt %d", retry_count);
            continue;
        }
        
        if (wpe_flag == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write protection error flag set on attempt %d", retry_count);
            continue;
        }
        
        uint32_t read_data = *(volatile uint32_t *)address;
        if (read_data != data) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write verification failed on attempt %d: expected 0x%08X, read 0x%08X", 
                     retry_count, data, read_data);
            continue;
        }
        
        final_status = FMC_STATUS_COMPLETE;
        break;
    }
    
    if (final_status != FMC_STATUS_COMPLETE) {
        LOG_ERROR(LOG_MODULE_TIME, "All flash write attempts failed for address 0x%08X", address);
    }
    
    return final_status;
}

/* ??????????????? */
static void Init_Relay_Log_System(void) {
    uint32_t relay_log_index = 0;
    
    for (uint32_t i = 0; i < RELAY_LOG_MAX_ENTRIES; i++) {
        uint32_t entry_addr = RELAY_LOG_FLASH_ADDR + i * RELAY_LOG_ENTRY_SIZE;
        Relay_Log_Entry_T entry;
        
        memcpy(&entry, (void*)entry_addr, sizeof(entry));
        
        if (entry.magic == 0x52454C41) {
            relay_log_index = i + 1;
        } else {
            break;
        }
    }
}

/* ????????????Flash */
static void Erase_Relay_Log_Flash(void) {
    uint32_t page_addr = RELAY_LOG_FLASH_ADDR & ~(FLASH_PAGE_SIZE - 1);
    LOG_INFO(LOG_MODULE_TIME, "Starting Flash erase at address 0x%08X", page_addr);
    
    if (page_addr < 0x08000000 || page_addr >= 0x08040000) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash address 0x%08X out of range", page_addr);
        return;
    }
    
    if (Check_Flash_Write_Protection(page_addr)) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash page at address 0x%08X is write-protected", page_addr);
        return;
    }
    
    FMC_Unlock();
    
    if (FMC_ReadStatusFlag(FMC_FLAG_WPE) == SET) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash write protected after unlock");
        FMC_ClearStatusFlag(FMC_FLAG_WPE);
        if (FMC_ReadStatusFlag(FMC_FLAG_WPE) == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Cannot clear write protection");
            FMC_Lock();
            return;
        }
    }
    
    int retry_count = 0;
    const int max_retries = 3;
    bool erase_success = false;
    
    while (retry_count < max_retries && !erase_success) {
        retry_count++;
        LOG_INFO(LOG_MODULE_TIME, "Erase attempt %d/%d", retry_count, max_retries);
        
        if (FMC_ReadStatusFlag(FMC_FLAG_BUSY) == SET) {
            LOG_WARN(LOG_MODULE_TIME, "Flash busy, waiting...");
            uint32_t timeout = 1000000;
            while (FMC_ReadStatusFlag(FMC_FLAG_BUSY) == SET && timeout--);
            if (timeout == 0) {
                LOG_ERROR(LOG_MODULE_TIME, "Flash busy timeout");
                continue;
            }
        }
        
        FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
        
        __disable_irq(); /* Critical Section */
        FMC_STATUS_T status = FMC_ErasePage(page_addr);
        __enable_irq(); /* End Critical Section */
        
        uint32_t timeout = 1000000;
        while (FMC_ReadStatusFlag(FMC_FLAG_BUSY) == SET && timeout--);
        
        if (timeout == 0) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash erase timeout on attempt %d", retry_count);
            continue;
        }
        
        FMC_FLAG_T pe_flag = FMC_ReadStatusFlag(FMC_FLAG_PE);
        FMC_FLAG_T oc_flag = FMC_ReadStatusFlag(FMC_FLAG_OC);
        FMC_FLAG_T wpe_flag = FMC_ReadStatusFlag(FMC_FLAG_WPE);
        
        if (status != FMC_STATUS_COMPLETE || pe_flag == SET || wpe_flag == SET) {
            LOG_ERROR(LOG_MODULE_TIME, "Erase failed on attempt %d! Status: 0x%02X, PE:%d, OC:%d, WPE:%d", 
                   retry_count, status, pe_flag == SET, oc_flag == SET, wpe_flag == SET);
            
            if (wpe_flag == SET) {
                LOG_INFO(LOG_MODULE_TIME, "Write protection detected, attempting to re-unlock");
                FMC_Unlock();
                FMC_ClearStatusFlag(FMC_FLAG_WPE);
            }
        } else {
            if (oc_flag == SET) {
                LOG_INFO(LOG_MODULE_TIME, "Flash erase completed successfully on attempt %d", retry_count);
                erase_success = true;
            } else {
                LOG_ERROR(LOG_MODULE_TIME, "Flash erase operation not completed on attempt %d", retry_count);
            }
        }
        
        if (!erase_success && retry_count < max_retries) {
            volatile uint32_t delay = 100000;
            while (delay--);
        }
    }
    
    FMC_Lock();
    
    uint32_t relay_log_index = 0;
    
    if (erase_success) {
        uint32_t *check_addr = (uint32_t *)page_addr;
        bool verify_success = true;
        for (int i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
            if (check_addr[i] != 0xFFFFFFFF) {
                verify_success = false;
                break;
            }
        }
        
        if (verify_success) {
            LOG_INFO(LOG_MODULE_TIME, "Flash erase verification successful");
        } else {
            LOG_ERROR(LOG_MODULE_TIME, "Flash erase verification failed");
            erase_success = false;
        }
    }
    
    if (!erase_success) {
        LOG_ERROR(LOG_MODULE_TIME, "All erase attempts failed, page may be write-protected");
        LOG_ERROR(LOG_MODULE_TIME, "Please check Flash option bytes and write protection settings");
    }
}

/* ????????????Flash */
static void Write_Relay_Operation_To_Flash(Relay_Id_T relay_id, Relay_State_T operation) {
    // ????????????Flash???????????????????????
    LOG_INFO(LOG_MODULE_USER, "Relay %d %s at %s (Flash save skipped for stability)", 
             relay_id + 1, operation ? "ON" : "OFF", Get_Formatted_Time());
    return;
    
    if (!(time_manager.flags & TIME_FLAG_INITIALIZED)) return;
    
    uint32_t current_time = Get_Current_Unix_Time();
    if (current_time == 0) return;
    
    static uint32_t relay_log_index = 0;
    __disable_irq();
    uint32_t write_index = relay_log_index;
    relay_log_index++;
    __enable_irq();
    
    if (write_index >= RELAY_LOG_MAX_ENTRIES) {
        Erase_Relay_Log_Flash();
        write_index = 0;
        __disable_irq();
        relay_log_index = 1;
        __enable_irq();
    }
    
    Relay_Log_Entry_T entry = {
        .magic = 0x52454C41,
        .unix_time = current_time,
        .relay_id = relay_id,
        .operation = operation
    };
    memset(entry.reserved, 0, sizeof(entry.reserved));
    
    uint32_t write_addr = RELAY_LOG_FLASH_ADDR + write_index * RELAY_LOG_ENTRY_SIZE;
    FMC_Unlock();
    uint32_t *data = (uint32_t *)&entry;
    int words = sizeof(Relay_Log_Entry_T) / 4;
    for (int i = 0; i < words; i++) {
        Safe_Flash_Write(write_addr + i * 4, data[i]);
    }
    FMC_Lock();
    
    LOG_INFO(LOG_MODULE_USER, "Relay %d %s at %s", 
             relay_id + 1, operation ? "ON" : "OFF", Get_Formatted_Time());
}

/* ???????????? */
static uint8_t Calculate_Time_Checksum(const Time_Flash_T *time_data) {
    const uint8_t *data = (const uint8_t *)time_data;
    uint8_t sum = 0;
    for (size_t i = 0; i < sizeof(Time_Flash_T) - 1; i++) {
        sum ^= data[i];
    }
    return sum;
}

/* Unix???????????? */
void Unix_Time_To_DateTime(uint32_t unix_time, 
                                  uint16_t *year, uint8_t *month, uint8_t *day,
                                  uint8_t *hour, uint8_t *minute, uint8_t *second) {
    if (unix_time == 0) {
        *year = 1970; *month = 1; *day = 1;
        *hour = 0; *minute = 0; *second = 0;
        return;
    }
    
    // ?????????
    *second = unix_time % 60;
    unix_time /= 60;
    *minute = unix_time % 60;
    unix_time /= 60;
    *hour = unix_time % 24;
    unix_time /= 24;
    
    // ???????????
    uint32_t days = unix_time;
    uint16_t y = 1970;
    
    // ???????
    while (days >= 365) {
        uint16_t days_in_year = is_leap_year(y) ? 366 : 365;
        if (days < days_in_year) {
            break;
        }
        days -= days_in_year;
        y++;
    }
    
    // ????????
    uint8_t leap = is_leap_year(y) ? 1 : 0;
    const uint8_t* month_days = get_days_in_month_ptr(leap);
    uint8_t m = 1;
    
    while (m <= 12) {
        if (days < month_days[m]) {
            break;
        }
        days -= month_days[m];
        m++;
    }
    
    // ??????????
    int16_t adjusted_hour = *hour + time_manager.timezone;
    
    if (adjusted_hour < 0) {
        adjusted_hour += 24;
        days--;
        
        // ???????????????
        if (days < 0) {
            y--;
            leap = is_leap_year(y) ? 1 : 0;
            month_days = get_days_in_month_ptr(leap);
            days += (leap ? 366 : 365);
            
            // ???????????
            m = 1;
            while (m <= 12) {
                if (days < month_days[m]) {
                    break;
                }
                days -= month_days[m];
                m++;
            }
        }
    } else if (adjusted_hour >= 24) {
        adjusted_hour -= 24;
        days++;
        
        // ??????????????
        uint16_t days_in_year = is_leap_year(y) ? 366 : 365;
        if (days >= days_in_year) {
            y++;
            days -= days_in_year;
            leap = is_leap_year(y) ? 1 : 0;
            month_days = get_days_in_month_ptr(leap);
            m = 1;
        } else {
            // ?????????????????
            while (m <= 12 && days >= month_days[m]) {
                days -= month_days[m];
                m++;
            }
        }
    }
    
    // ???y??
    *year = y;
    *month = m;
    *day = (uint8_t)(days + 1); // ??????1???
    *hour = (uint8_t)adjusted_hour;
}

/* ???????????????JSON */
const char* Get_Relay_Operation_History(int max_entries) {
    static char json_buffer[2048];
    char *ptr = json_buffer;
    int remaining = sizeof(json_buffer);
    
    if (max_entries <= 0 || max_entries > 20) {
        max_entries = 10;
    }
    
    ptr += snprintf(ptr, remaining, "{\"relay_history\":[");
    remaining -= (ptr - json_buffer);
    
    if (remaining <= 100) {
        strcat(json_buffer, "],\"status\":\"buffer_error\",\"count\":0}");
        return json_buffer;
    }
    
    int entry_count = 0;
    static uint32_t relay_log_index = 0;
    int start_index = (relay_log_index > 0) ? (relay_log_index - 1) : 0;
    
    for (int i = 0; i < RELAY_LOG_MAX_ENTRIES && entry_count < max_entries; i++) {
        int read_index = (start_index - i + RELAY_LOG_MAX_ENTRIES) % RELAY_LOG_MAX_ENTRIES;
        uint32_t entry_addr = RELAY_LOG_FLASH_ADDR + read_index * RELAY_LOG_ENTRY_SIZE;
        Relay_Log_Entry_T entry;
        
        memcpy(&entry, (void*)entry_addr, sizeof(entry));
        
        if (entry.magic != 0x52454C41 || (entry.unix_time != 0 && entry.unix_time < 1577836800)) {
            continue;
        }
        
        char time_str[32];
        if (entry.unix_time > 0) {
            uint16_t year;
            uint8_t month, day, hour, minute, second;
            Unix_Time_To_DateTime(entry.unix_time, &year, &month, &day, &hour, &minute, &second);
            snprintf(time_str, sizeof(time_str), "%04u-%02u-%02u %02u:%02u:%02u",
                    year, month, day, hour, minute, second);
        } else {
            strcpy(time_str, "Time not synced");
        }
        
        if (entry_count > 0) {
            if (remaining < 2) break;
            *ptr++ = ',';
            remaining--;
        }
        
        int needed = snprintf(NULL, 0,
            "{\"time\":\"%s\",\"relay_num\":%d,\"operation\":\"%s\",\"unix_time\":%u}",
            time_str, entry.relay_id + 1, entry.operation ? "ON" : "OFF", entry.unix_time);
        
        if (needed >= remaining) break;
        
        int written = snprintf(ptr, remaining,
            "{\"time\":\"%s\",\"relay_num\":%d,\"operation\":\"%s\",\"unix_time\":%u}",
            time_str, entry.relay_id + 1, entry.operation ? "ON" : "OFF", entry.unix_time);
        
        if (written < 0 || written >= remaining) break;
        
        ptr += written;
        remaining -= written;
        entry_count++;
    }
    
    if (remaining < 50) {
        strcpy(json_buffer, "{\"relay_history\":[],\"status\":\"buffer_full\",\"count\":0}");
        return json_buffer;
    }
    
    snprintf(ptr, remaining, "],\"status\":\"success\",\"count\":%d}", entry_count);
    return json_buffer;
}

/* ?????????????? */
void Clear_Relay_Operation_History(void) {
    Erase_Relay_Log_Flash();
    LOG_INFO(LOG_MODULE_SYSTEM, "Relay history cleared");
}

/* ????????????? */
void Time_Manager_Init(void) {
    memset(&time_manager, 0, sizeof(time_manager));
    time_manager.timezone = 8;
    time_manager.flags = TIME_FLAG_INITIALIZED;
    time_manager.pending_relay_id = 0;
    time_manager.pending_relay_operation = RELAY_OFF;
    time_manager.pending_sync_time = 0;
    time_manager.pending_timezone = 8;
    
    if (g_sys_tick == 0) {
        LOG_WARN(LOG_MODULE_TIME, "System tick not initialized");
    }
    
    Init_Relay_Log_System();
    Load_Time_From_Flash();
    
    if (time_manager.flags & TIME_FLAG_SYNCHRONIZED) {
        LOG_INFO(LOG_MODULE_TIME, "Time Synced: %s", Get_Formatted_Time());
    } else {
        LOG_INFO(LOG_MODULE_TIME, "Time Init: Not Synced");
    }
}

/* ??Flash??????? */
static void Load_Time_From_Flash(void) {
    if (!(time_manager.flags & TIME_FLAG_INITIALIZED)) return;
    
    Time_Flash_T time_data;
    memcpy(&time_data, (void*)FLASH_TIME_ADDR, sizeof(time_data));
    
    if (time_data.magic != 0x54494D45) {
        // LOG_INFO(LOG_MODULE_TIME, "No valid time data in Flash"); // Removed per user request
        return;
    }
    
    uint8_t calc_checksum = Calculate_Time_Checksum(&time_data);
    if (time_data.checksum != calc_checksum || time_data.unix_time < 1577836800) {
        LOG_WARN(LOG_MODULE_TIME, "failed timer data");
        return;
    }
    
    time_manager.last_sync_time = time_data.unix_time;
    time_manager.last_sync_systick = time_data.systick;
    time_manager.timezone = time_data.timezone;
    time_manager.flags |= TIME_FLAG_SYNCHRONIZED | TIME_FLAG_SAVED;
    LOG_INFO(LOG_MODULE_TIME, "Flash load success: %s", Get_Formatted_Time());
}

/* ???????Flash */
void Save_Time_To_Flash(uint32_t unix_time) {
    if (!(time_manager.flags & TIME_FLAG_INITIALIZED)) return;
    
    time_manager.flags |= TIME_FLAG_PENDING;
    time_manager.last_sync_time = unix_time;
    time_manager.last_sync_systick = g_sys_tick;
}

/* ???Flash???? */
static void Execute_Flash_Save(void) {
    if (!(time_manager.flags & TIME_FLAG_PENDING)) return;
    
    Time_Flash_T time_data = {
        .magic = 0x54494D45,
        .unix_time = time_manager.last_sync_time,
        .systick = time_manager.last_sync_systick,
        .timezone = time_manager.timezone
    };
    time_data.checksum = Calculate_Time_Checksum(&time_data);
    
    FMC_Unlock();
    
    uint32_t page_addr = FLASH_TIME_ADDR & ~(FLASH_PAGE_SIZE - 1);
    if (Safe_Flash_Erase(page_addr) != FMC_STATUS_COMPLETE) {
        LOG_ERROR(LOG_MODULE_TIME, "Erase flash failed");
        FMC_Lock();
        time_manager.flags &= ~TIME_FLAG_PENDING;
        return;
    }
    
    uint32_t *check_addr = (uint32_t *)page_addr;
    bool erase_success = true;
    for (int i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        if (check_addr[i] != 0xFFFFFFFF) {
            erase_success = false;
            break;
        }
    }
    
    if (!erase_success) {
        LOG_ERROR(LOG_MODULE_TIME, "Flash erase verification failed");
        FMC_Lock();
        time_manager.flags &= ~TIME_FLAG_PENDING;
        return;
    }
    
    uint32_t *data = (uint32_t *)&time_data;
    int words = sizeof(Time_Flash_T) / 4;
    
    for (int i = 0; i < words; i++) {
        uint32_t write_addr = FLASH_TIME_ADDR + i * 4;
        FMC_STATUS_T write_status = Safe_Flash_Write(write_addr, data[i]);
        
        if (write_status != FMC_STATUS_COMPLETE) {
            LOG_ERROR(LOG_MODULE_TIME, "Write flash failed at address 0x%08X, status: %d", write_addr, write_status);
            FMC_Lock();
            time_manager.flags &= ~TIME_FLAG_PENDING;
            return;
        }
        
        uint32_t read_data = *(volatile uint32_t *)write_addr;
        if (read_data != data[i]) {
            LOG_ERROR(LOG_MODULE_TIME, "Flash write verification failed at address 0x%08X: expected 0x%08X, read 0x%08X", 
                     write_addr, data[i], read_data);
            FMC_Lock();
            time_manager.flags &= ~TIME_FLAG_PENDING;
            return;
        }
    }
    
    FMC_Lock();
    time_manager.flags |= TIME_FLAG_SAVED;
    time_manager.flags &= ~TIME_FLAG_PENDING;
}

/* ??????Unix???? */
uint32_t Get_Current_Unix_Time(void) {
    if (!(time_manager.flags & TIME_FLAG_INITIALIZED) || 
        !(time_manager.flags & TIME_FLAG_SYNCHRONIZED)) {
        return 0;
    }
    
    uint32_t current_tick = g_sys_tick;
    uint32_t elapsed_ms;
    
    if (current_tick >= time_manager.last_sync_systick) {
        elapsed_ms = current_tick - time_manager.last_sync_systick;
    } else {
        elapsed_ms = (UINT32_MAX - time_manager.last_sync_systick) + 1 + current_tick;
    }
    
    uint32_t elapsed_seconds = elapsed_ms / 1000;
    
    if (time_manager.last_sync_time > (0xFFFFFFFFU - elapsed_seconds)) {
        LOG_ERROR(LOG_MODULE_TIME, "Unix???????");
        time_manager.flags &= ~TIME_FLAG_SYNCHRONIZED;
        return 0;
    }
    
    return time_manager.last_sync_time + elapsed_seconds;
}

/* ???????????????? */
const char* Get_Formatted_Time(void) {
    static char time_str[32];
    
    if (!(time_manager.flags & TIME_FLAG_INITIALIZED)) {
        strcpy(time_str, "Uninitialized");
        return time_str;
    }
    
    uint32_t unix_time = Get_Current_Unix_Time();
    if (unix_time == 0) {
        strcpy(time_str, "Not Synced");
        return time_str;
    }
    
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    Unix_Time_To_DateTime(unix_time, &year, &month, &day, &hour, &minute, &second);
    
    snprintf(time_str, sizeof(time_str), "%04u-%02u-%02u %02u:%02u:%02u",
             year, month, day, hour, minute, second);
    
    return time_str;
}

/* ???????????? */
void Web_API_Sync_From_Host(uint32_t unix_time, int8_t timezone) {
    if (unix_time < 1577836800) {
        LOG_ERROR(LOG_MODULE_TIME, "Invalid timestamp: %u", unix_time);
        return;
    }
    
    if (timezone < -12 || timezone > 12) {
        timezone = 8;
        LOG_WARN(LOG_MODULE_TIME, "Timezone exceed limit (-12~12), use default UTC+8");
    }
    
    time_manager.last_sync_time = unix_time;
    time_manager.last_sync_systick = g_sys_tick;
    time_manager.timezone = timezone;
    time_manager.flags |= TIME_FLAG_SYNCHRONIZED;
    time_manager.flags &= ~TIME_FLAG_SAVED;
    
    Save_Time_To_Flash(unix_time);
    
    char fmt_time[32];
    strcpy(fmt_time, Get_Formatted_Time());
    LOG_INFO(LOG_MODULE_TIME, "System time synchronized successfully: %s (UTC+%d)", fmt_time, timezone);
}

/* ??????JSON */
const char* Web_API_Get_Time(void) {
    static char json_response[512];
    uint32_t unix_time = Get_Current_Unix_Time();
    uint32_t uptime_seconds = g_sys_tick / 1000;
    
    snprintf(json_response, sizeof(json_response),
             "{\"status\":\"%s\",\"unix_time\":%u,\"formatted_time\":\"%s\","
             "\"timezone\":%d,\"synchronized\":%s,\"uptime_seconds\":%lu,"
             "\"flash_saved\":%s,\"relay_log_count\":%u}",
             (time_manager.flags & TIME_FLAG_SYNCHRONIZED) ? "success" : "not_synced",
             unix_time, Get_Formatted_Time(), time_manager.timezone,
             (time_manager.flags & TIME_FLAG_SYNCHRONIZED) ? "true" : "false",
             uptime_seconds,
             (time_manager.flags & TIME_FLAG_SAVED) ? "true" : "false",
             RELAY_LOG_MAX_ENTRIES);
    
    return json_response;
}

/* ???????????JSON */
const char* Web_API_Get_Relay_History(void) {
    return Get_Relay_Operation_History(10);
}

/* ?????????????????????? */
void Record_Relay_Operation(Relay_Id_T relay_id, Relay_State_T operation) {
    if (relay_id >= RELAY_NUM) return;
    
    
    time_manager.pending_relay_id = relay_id;
    time_manager.pending_relay_operation = operation;
    time_manager.flags |= TIME_FLAG_RELAY_PENDING;
    
}

/* ?????????????? */
char* Get_Current_Time_String(void) {
    static char time_buf[32];
    Get_Time_String_To_Buffer(time_buf, sizeof(time_buf));
    return time_buf;
}

/* ????????????????????????????????????????? */
void Get_Time_String_To_Buffer(char* buf, size_t size) {
    if (buf == NULL || size == 0) return;
    
    if (!(time_manager.flags & TIME_FLAG_INITIALIZED)) {
        snprintf(buf, size, "[Uninitialized]");
        return;
    }
    
    uint32_t unix_time = Get_Current_Unix_Time();
    if (unix_time == 0) {
        snprintf(buf, size, "[Not Synced]");
        return;
    }
    
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    Unix_Time_To_DateTime(unix_time, &year, &month, &day, &hour, &minute, &second);
    
    snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u",
             year, month, day, hour, minute, second);
}

void Unix_Time_To_Str(uint32_t unix_time, char *buf, int len) {
    if (buf == NULL || len <= 0) return;
    if (unix_time == 0) {
        snprintf(buf, len, "No-Sync");
        return;
    }
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    Unix_Time_To_DateTime(unix_time, &year, &month, &day, &hour, &minute, &second);
    snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u", year, month, day, hour, minute, second);
}

/* ????API?????????? */
void Time_Manager_Sync_Async(uint32_t timestamp, int8_t timezone) {
    time_manager.pending_sync_time = timestamp;
    time_manager.pending_timezone = timezone;
    time_manager.flags |= TIME_FLAG_SYNC_PENDING;
}

/* ???????????? */
uint8_t Is_Time_Synchronized(void) {
    return (time_manager.flags & TIME_FLAG_SYNCHRONIZED) ? 1 : 0;
}

/* ??????????HTTP???????????????? */
void Handle_Time_Sync_Request(struct tcp_pcb *pcb, const char *request, uint16_t len) {
    (void)len;
    
    uint32_t timestamp = 0;
    int8_t timezone = time_manager.timezone;
    
    // ?????JSON Body
    const char *json_body = NULL;
    if (request != NULL) {
        json_body = strstr(request, "\r\n\r\n");
        if (json_body != NULL) {
            json_body += 4;
        }
    }
    if (json_body == NULL || *json_body == '\0') {
        const char *error_json = "{\"status\":\"error\",\"message\":\"No valid JSON body\"}";
        send_http_response(pcb, "application/json", error_json, strlen(error_json), true);
        return;
    }

    // ????timestamp???????????????/?????
    char *timestamp_ptr = strstr(json_body, "timestamp");
    if (timestamp_ptr != NULL) {
        char *colon = strchr(timestamp_ptr, ':');
        if (colon != NULL && (colon + 1) < (json_body + strlen(json_body))) {
            char *num_ptr = colon + 1;
            // ????????????????????????????
            while (*num_ptr == ' ' || *num_ptr == ',' || *num_ptr == '\t' || *num_ptr == '\n' || *num_ptr == '\r') {
                num_ptr++;
                if (num_ptr >= (json_body + strlen(json_body))) {
                    break;
                }
            }
            if (*num_ptr >= '0' && *num_ptr <= '9') {
                timestamp = strtoul(num_ptr, NULL, 10);
            }
        }
    }

    // ????timezone???????????????/?????
    char *timezone_ptr = strstr(json_body, "timezone");
    if (timezone_ptr != NULL) {
        char *colon = strchr(timezone_ptr, ':');
        if (colon != NULL && (colon + 1) < (json_body + strlen(json_body))) {
            char *num_ptr = colon + 1;
            // ????????????????????????????
            while (*num_ptr == ' ' || *num_ptr == ',' || *num_ptr == '\t' || *num_ptr == '\n' || *num_ptr == '\r') {
                num_ptr++;
                if (num_ptr >= (json_body + strlen(json_body))) {
                    break;
                }
            }
            // ??????????
            if ((*num_ptr >= '0' && *num_ptr <= '9') || *num_ptr == '-') {
                timezone = (int8_t)strtol(num_ptr, NULL, 10);
            }
        }
    }

    // ????????????????????????????
    if (timestamp > 1577836800) {
        // ?????????????
        
        time_manager.last_sync_time = timestamp;
        time_manager.last_sync_systick = g_sys_tick;
        time_manager.timezone = timezone;
        time_manager.flags |= TIME_FLAG_SYNCHRONIZED;
        
        
        // ????????????????
        const char *time_json = Web_API_Get_Time();
        send_http_response(pcb, "application/json", time_json, strlen(time_json), true);
        
        // ??????Flash???
        time_manager.pending_sync_time = timestamp;
        time_manager.pending_timezone = timezone;
        time_manager.flags |= TIME_FLAG_SYNC_PENDING;
    } else {
        const char *error_json = "{\"status\":\"error\",\"message\":\"Invalid timestamp\"}";
        send_http_response(pcb, "application/json", error_json, strlen(error_json), true);
    }
}

/* ????????????? */
void Handle_Time_Get_Request(struct tcp_pcb *pcb) {
    const char *time_json = Web_API_Get_Time();
    send_http_response(pcb, "application/json", time_json, strlen(time_json), false);
}

/* ?????????????? */
void Handle_Relay_History_Request(struct tcp_pcb *pcb) {
    const char *history_json = Web_API_Get_Relay_History();
    send_http_response(pcb, "application/json", history_json, strlen(history_json), false);
}

/* ????????????? */
void Handle_Relay_Clear_History_Request(struct tcp_pcb *pcb) {
    Clear_Relay_Operation_History();
    const char *success_json = "{\"status\":\"success\",\"message\":\"Relay history cleared\"}";
    send_http_response(pcb, "application/json", success_json, strlen(success_json), false);
}

/* ???????????? */
void Time_Manager_Task(void) {
    static uint32_t last_task_time = 0;
    static uint32_t last_flash_time = 0;
    static uint32_t flash_error_count = 0;
    
    uint32_t tick_diff = (g_sys_tick - last_task_time) & 0xFFFFFFFFU;
    if (tick_diff < 500) return;
    last_task_time = g_sys_tick;
    
    tick_diff = (g_sys_tick - last_flash_time) & 0xFFFFFFFFU;
    bool process_flash = (tick_diff >= 10000);
    
    if (process_flash) {
        last_flash_time = g_sys_tick;
        
        // ????????????????
        if (time_manager.flags & TIME_FLAG_SYNC_PENDING) {
            time_manager.flags &= ~TIME_FLAG_SYNC_PENDING;
            Web_API_Sync_From_Host(time_manager.pending_sync_time, time_manager.pending_timezone);
            flash_error_count = 0;
        }
        
        // ??????????????????????
        if (time_manager.flags & TIME_FLAG_RELAY_PENDING) {
            time_manager.flags &= ~TIME_FLAG_RELAY_PENDING;
            Write_Relay_Operation_To_Flash(time_manager.pending_relay_id, time_manager.pending_relay_operation);
            flash_error_count = 0;
        }
        
        // ????????Flash????
        if (time_manager.flags & TIME_FLAG_PENDING) {
            Execute_Flash_Save();
            flash_error_count = 0;
        }
    }
    
    static uint32_t last_status_time = 0;
    tick_diff = (g_sys_tick - last_status_time) & 0xFFFFFFFFU;
    if (tick_diff >= 60000 && (time_manager.flags & TIME_FLAG_SYNCHRONIZED)) {
        last_status_time = g_sys_tick;
    }
    
    if (flash_error_count > 3) {
        LOG_ERROR(LOG_MODULE_TIME, "Too many Flash errors, resetting flags to prevent system block");
        time_manager.flags &= ~(TIME_FLAG_SYNC_PENDING | TIME_FLAG_RELAY_PENDING | TIME_FLAG_PENDING);
        flash_error_count = 0;
    }
}
