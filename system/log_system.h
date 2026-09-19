#ifndef LOG_SYSTEM_H
#define LOG_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>

/* ?????????? */
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} Log_Level_T;

/* ????????? */
typedef enum {
    LOG_MODULE_SYSTEM,
    LOG_MODULE_USER,
    LOG_MODULE_NETWORK,
    LOG_MODULE_TIME
} Log_Module_T;

/* ??????????? - ?��????????????????????? */
typedef enum {
    LOG_MSG_CUSTOM = 0,       // 0: ?????????? (??????)
    LOG_MSG_SYSTEM_START,     // 1: "System Started"
    LOG_MSG_RELAY1_ON,        // 2: "Relay1 ON"
    LOG_MSG_RELAY1_OFF,       // 3: "Relay1 OFF"
    LOG_MSG_RELAY2_ON,        // 4: "Relay2 ON"
    LOG_MSG_RELAY2_OFF,       // 5: "Relay2 OFF"
    LOG_MSG_RELAY3_ON,        // 6: "Relay3 ON"
    LOG_MSG_RELAY3_OFF,       // 7: "Relay3 OFF"
    LOG_MSG_RELAY4_ON,        // 8: "Relay4 ON"
    LOG_MSG_RELAY4_OFF,       // 9: "Relay4 OFF"
    LOG_MSG_LOGIN_SUCCESS,    // 10: "Login Success"
    LOG_MSG_LOGIN_FAIL,       // 11: "Login Failed"
    LOG_MSG_TIME_SYNC,        // 12: "Time Synchronized"
    LOG_MSG_SMOKE_ALARM,      // 13: "Smoke Alarm Triggered"
    LOG_MSG_POWER_ALARM,      // 14: "Power Alarm Triggered"
    LOG_MSG_MAX
} Log_Msg_Code_T;

/* ?????????? (RAM) */
typedef struct {
    uint32_t timestamp;      // ??????? SysTick
    char time_str[24];       // ??????????????
    Log_Level_T level;
    Log_Module_T module;
    char message[64];        // ???????
} Log_Entry_T;

/* ???????? */
void Log_System_Init(void);
void Log_Write_With_Time(Log_Level_T level, Log_Module_T module, 
                        const char *time_str, const char *format, ...);
void Log_Write(Log_Level_T level, Log_Module_T module, const char *format, ...);
const char *Log_GetEntriesJSON(int page);
int Log_GetCount(void);
void Log_SaveToFlash(void);
void Log_LoadFromFlash(void);
bool Log_IsDirty(void);
void Log_Clear(void);
bool Log_GetEntry_ByIndex(int index, Log_Entry_T *entry);
int Log_TestFlashWrite(void);
/* Flash Access Helpers */
int Log_GetFlashCount(void);
bool Log_GetFlashEntry_ByIndex(int index, Log_Entry_T *entry);
void Log_Decode_Entry(uint16_t msg_code, uint32_t param1, uint32_t param2, char *buffer, int max_len);
bool Log_IsStaticBuffer(const void *ptr);

/* ???????????????????????? */
#define LOG_WITH_TIME(level, module, time_str, fmt, ...) \
    Log_Write_With_Time(level, module, time_str, fmt, ##__VA_ARGS__)
    
#define LOG_INFO(module, fmt, ...)  Log_Write(LOG_LEVEL_INFO, module, fmt, ##__VA_ARGS__)
#define LOG_WARN(module, fmt, ...)  Log_Write(LOG_LEVEL_WARN, module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...) Log_Write(LOG_LEVEL_ERROR, module, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(module, fmt, ...) Log_Write(LOG_LEVEL_DEBUG, module, fmt, ##__VA_ARGS__)

#define EVENT_START 0x0001
#define EVENT_LOGIN 0x0002
#define EVENT_RELAY 0x0003
#define EVENT_ALARM 0x0004
#define EVENT_TEST  0x0005
#define EVENT_PSU   0x0006
#define EVENT_SMOKE 0x0007
#define EVENT_TIME  0x0008
#define EVENT_RESET 0x0009

#endif /* LOG_SYSTEM_H */

