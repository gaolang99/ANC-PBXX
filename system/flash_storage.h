#ifndef FLASH_STORAGE_H
#define FLASH_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

/* APM32F107VC Flash total 256KB: 0x08000000~0x0803FFFF */
/* Flash Page Size is 2KB (2048 Bytes) for High Density Devices */
#define FLASH_PAGE_SIZE          2048U

/* =========================================================================
   Flash Layout Optimization
   ========================================================================= */

/* System Log: 2KB (Page 124) [0x0803E000 - 0x0803E7FF] */
#define FLASH_LOG_START_ADDR     0x0803E000U
#define FLASH_LOG_END_ADDR       0x0803E7FFU

/* LED/Relay Log: 2KB (Page 125) [0x0803E800 - 0x0803EFFF] */
#define FLASH_LED_LOG_ADDR       0x0803E800U

/* Time Storage: 2KB (Page 126) [0x0803F000 - 0x0803F7FF] */
#define FLASH_TIME_ADDR          0x0803F000U

/* Unified Config Page: 2KB (Page 127) [0x0803F800 - 0x0803FFFF] */
#define FLASH_CONFIG_PAGE_ADDR   0x0803F800U

/* Temporary Swap Page: 2KB (Page 123) [0x0803D800 - 0x0803DFFF] */
#define FLASH_SWAP_PAGE_ADDR     0x0803D800U

/* Offsets within the Unified Config Page */
#define FLASH_SYS_CONFIG_ADDR    (FLASH_CONFIG_PAGE_ADDR + 0U)
#define FLASH_NET_CONFIG_ADDR    (FLASH_CONFIG_PAGE_ADDR + 512U)
#define FLASH_PSU_CONFIG_ADDR    (FLASH_CONFIG_PAGE_ADDR + 1024U)

/* 
   Compact Log Entry for Flash Storage (16 Bytes)
   Capacity: 2048 / 16 = 128 Entries
*/
typedef struct {
    uint32_t timestamp;     /* Unix Timestamp (4B) */
    uint16_t msg_code;      /* Event ID (2B) */
    uint8_t  level;         /* Log Level (Was reserved1) */
    uint8_t  module;        /* Module ID (Was reserved2) */
    uint32_t param1;        /* Parameter 1 (4B) */
    uint32_t param2;        /* Parameter 2 (4B) */
} __attribute__((packed, aligned(4))) Flash_Log_Entry_T;

/* System Configuration Structure */
typedef struct {
    uint32_t magic;
    char password[32];
    char device_name[64];
    char fw_version[32];
    char channel_names[4][32];
    uint32_t checksum;
} Flash_System_Config_T;

/* Network Configuration Structure */
typedef struct {
    uint32_t ip;
    uint32_t mask;
    uint8_t enabled;
    uint8_t reserved[3];
} Whitelist_Rule_T;

typedef struct {
    uint32_t magic;
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t session_timeout;
    uint16_t web_port;
    uint16_t reserved;
    Whitelist_Rule_T whitelist_rules[5];
    uint32_t checksum;
} Flash_Network_Config_T;

/* PSU Configuration Structure */
typedef struct {
    uint32_t magic;
    uint8_t psu1_enabled;
    uint8_t psu2_enabled;
    uint8_t relay_states;
    uint8_t smoke_enabled;
    uint32_t checksum;
} Flash_PSU_Config_T;

/* 
   Optimized Compact PSU Config for Append-Only Storage (8 Bytes aligned) 
   We will store multiple copies of this structure in the Flash Page.
   The latest valid entry is the one we use.
*/
typedef struct {
    uint8_t relay_states;  // 1B
    uint8_t psu1_enabled;  // 1B
    uint8_t psu2_enabled;  // 1B
    uint8_t smoke_enabled; // 1B
    uint32_t magic_seq;    // 4B (Use this as valid flag, e.g., 0xA5A5A5A5)
} __attribute__((packed, aligned(4))) Flash_PSU_Config_Compact_T;

/* Function Prototypes */
void Flash_Storage_Init(void);
int Flash_Write_Log(const Flash_Log_Entry_T *entry);
int Flash_Read_Logs(Flash_Log_Entry_T *entries, int max_count);
int Flash_Get_Log_Count(void);
void Flash_Erase_Logs(void);
bool Flash_Get_Log_Entry_ByIndex(int index, Flash_Log_Entry_T *entry);

/* Config Functions */
bool Flash_Read_System_Config(Flash_System_Config_T *config);
bool Flash_Write_System_Config(const Flash_System_Config_T *config);
bool Flash_Verify_Password(const char *password);
bool Flash_Update_Password(const char *new_password);

bool Flash_Read_Network_Config(Flash_Network_Config_T *config);
bool Flash_Write_Network_Config(const Flash_Network_Config_T *config);

bool Flash_Read_PSU_Config(Flash_PSU_Config_T *config);
bool Flash_Write_PSU_Config(Flash_PSU_Config_T *config);

#endif /* FLASH_STORAGE_H */
