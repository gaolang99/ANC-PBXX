#include "flash_storage.h"
#include "apm32f10x_fmc.h"
#include "apm32f10x_rcm.h" /* Added for RCM_AHB_PERIPH_FMC */
#include <string.h>
#include <stdio.h>

/* Defines */
#define FLASH_SYS_CONFIG_MAGIC   0x53595343U
#define FLASH_NET_CONFIG_MAGIC   0x4E455445U
#define FLASH_PSU_CONFIG_MAGIC   0x50535549U

/* Helper to update a specific section in the Unified Config Page */
static bool Flash_Update_Config_Page(uint32_t offset, const void *data, size_t size) {
    if (offset + size > FLASH_PAGE_SIZE) return false;

    __disable_irq();
    FMC_Unlock();
    FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
    
    /* 1. Erase Swap Page */
    if (FMC_ErasePage(FLASH_SWAP_PAGE_ADDR) != FMC_STATUS_COMPLETE) {
        FMC_Lock(); __enable_irq(); return false;
    }
    
    /* 2. Copy Target Page to Swap Page */
    uint32_t *p_src = (uint32_t *)FLASH_CONFIG_PAGE_ADDR;
    for (int i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        if (FMC_ProgramWord(FLASH_SWAP_PAGE_ADDR + (i * 4), p_src[i]) != FMC_STATUS_COMPLETE) {
            FMC_Lock(); __enable_irq(); return false;
        }
    }
    
    /* 3. Erase Target Page */
    if (FMC_ErasePage(FLASH_CONFIG_PAGE_ADDR) != FMC_STATUS_COMPLETE) {
        FMC_Lock(); __enable_irq(); return false;
    }
    
    /* 4. Copy from Swap to Target with modification */
    uint32_t *p_swap = (uint32_t *)FLASH_SWAP_PAGE_ADDR;
    uint8_t *p_new_data = (uint8_t *)data;
    
    for (int i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        uint32_t addr_offset = i * 4;
        uint32_t word_val;
        
        if (addr_offset >= offset && addr_offset < (offset + size)) {
            /* Construct new word from data buffer */
            uint32_t data_offset = addr_offset - offset;
            uint8_t b[4];
            for(int j=0; j<4; j++) {
                if (data_offset + j < size) b[j] = p_new_data[data_offset + j];
                else b[j] = ((uint8_t*)p_swap)[addr_offset + j];
            }
            word_val = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        } else {
            word_val = p_swap[i];
        }
        if (FMC_ProgramWord(FLASH_CONFIG_PAGE_ADDR + addr_offset, word_val) != FMC_STATUS_COMPLETE) {
            FMC_Lock(); __enable_irq(); return false;
        }
    }

    FMC_Lock();
    __enable_irq();
    return true;
}

/* Flash Storage Init */
void Flash_Storage_Init(void) {
    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_FMC);
}

/* 
   Write Log with Shift Strategy (Ring Buffer Simulation)
   - Capacity: 128 entries (2KB / 16B)
   - When full: Move last 100 entries to Swap, Erase, Move back to Start.
*/
int Flash_Write_Log(const Flash_Log_Entry_T *entry) {
    if (entry == NULL) return -1;
    
    const uint32_t TOTAL_SIZE = FLASH_LOG_END_ADDR - FLASH_LOG_START_ADDR + 1;
    const int MAX_ENTRIES = TOTAL_SIZE / sizeof(Flash_Log_Entry_T); // 128
    const int KEEP_ENTRIES = 100; // Keep last 100 when full
    
    // 1. Find write index
    int write_idx = -1;
    Flash_Log_Entry_T *flash_ptr = (Flash_Log_Entry_T *)FLASH_LOG_START_ADDR;
    
    // Safety check for corruption
    if (flash_ptr[0].timestamp != 0xFFFFFFFF && flash_ptr[0].timestamp == 0) {
         // Timestamp 0 is suspicious if it's the very first entry after erase, unless time is not synced.
         // But 0 usually means "programmed but zeroed".
    }
    
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (flash_ptr[i].timestamp == 0xFFFFFFFF) {
            write_idx = i;
            break;
        }
    }
    
    __disable_irq();
    FMC_Unlock();
    FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
    
    // 2. Handle Full State
    if (write_idx == -1) {
        // Full! Perform Shift Operation.
        // printf("[Flash] Log Full! Shifting...\n");
        
        // Step A: Erase Swap
        if (FMC_ErasePage(FLASH_SWAP_PAGE_ADDR) != FMC_STATUS_COMPLETE) {
            // printf("[Flash] Swap Erase Failed!\n");
            FMC_Lock(); __enable_irq(); return -3;
        }
        
        // Step B: Copy last KEEP_ENTRIES to Swap
        // Source start index: MAX_ENTRIES - KEEP_ENTRIES = 28
        int src_idx = MAX_ENTRIES - KEEP_ENTRIES;
        Flash_Log_Entry_T *p_src_struct = &flash_ptr[src_idx]; // Use struct pointer
        
        // Copy only valid entries that are not 0xFFFFFFFF
        int valid_entries_to_copy = 0;
        for (int i = 0; i < KEEP_ENTRIES; i++) {
             if (p_src_struct[i].timestamp != 0xFFFFFFFF) {
                 uint32_t *p_entry_words = (uint32_t *)&p_src_struct[i];
                 FMC_ProgramWord(FLASH_SWAP_PAGE_ADDR + (valid_entries_to_copy * sizeof(Flash_Log_Entry_T)), p_entry_words[0]);
                 FMC_ProgramWord(FLASH_SWAP_PAGE_ADDR + (valid_entries_to_copy * sizeof(Flash_Log_Entry_T)) + 4, p_entry_words[1]);
                 FMC_ProgramWord(FLASH_SWAP_PAGE_ADDR + (valid_entries_to_copy * sizeof(Flash_Log_Entry_T)) + 8, p_entry_words[2]);
                 FMC_ProgramWord(FLASH_SWAP_PAGE_ADDR + (valid_entries_to_copy * sizeof(Flash_Log_Entry_T)) + 12, p_entry_words[3]);
                 valid_entries_to_copy++;
             }
        }
        
        // Step C: Erase Log Page
        for (uint32_t addr = FLASH_LOG_START_ADDR; addr <= FLASH_LOG_END_ADDR; addr += FLASH_PAGE_SIZE) {
            if (FMC_ErasePage(addr) != FMC_STATUS_COMPLETE) {
                 // printf("[Flash] Log Erase Failed at %08X!\n", addr);
                 FMC_Lock(); __enable_irq(); return -4;
            }
        }
        
        // Step D: Copy back from Swap to Start of Log Page
        uint32_t *p_swap = (uint32_t *)FLASH_SWAP_PAGE_ADDR;
        for (int i = 0; i < valid_entries_to_copy * sizeof(Flash_Log_Entry_T) / 4; i++) {
            FMC_ProgramWord(FLASH_LOG_START_ADDR + (i * 4), p_swap[i]);
        }
        
        write_idx = valid_entries_to_copy; // Next write after the copied entries
    }
    
    // 3. Write New Entry
    uint32_t *src = (uint32_t *)entry;
    uint32_t dest_addr = FLASH_LOG_START_ADDR + (write_idx * sizeof(Flash_Log_Entry_T));
    
    for (int i = 0; i < sizeof(Flash_Log_Entry_T) / 4; i++) {
        FMC_ProgramWord(dest_addr + (i * 4), src[i]);
    }
    
    // Verify Write
    bool verify_ok = true;
    uint32_t *check_ptr = (uint32_t *)dest_addr;
    for (int i = 0; i < sizeof(Flash_Log_Entry_T) / 4; i++) {
        if (check_ptr[i] != src[i]) {
            verify_ok = false;
            break;
        }
    }
    
    FMC_Lock();
    __enable_irq();
    
    if (!verify_ok) {
        // printf("[Flash] Write Verify Failed at index %d!\n", write_idx);
        return -2; // Error code
    }
    
    // printf("[Flash] Write Success at index %d. Total: %d\n", write_idx, Flash_Get_Log_Count());
    return write_idx;
}

int Flash_Get_Log_Count(void) {
    Flash_Log_Entry_T *flash_ptr = (Flash_Log_Entry_T *)FLASH_LOG_START_ADDR;
    int count = 0;
    const int MAX_ENTRIES = (FLASH_LOG_END_ADDR - FLASH_LOG_START_ADDR + 1) / sizeof(Flash_Log_Entry_T);
    
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (flash_ptr[i].timestamp != 0xFFFFFFFF) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

bool Flash_Get_Log_Entry_ByIndex(int index, Flash_Log_Entry_T *entry) {
    if (entry == NULL || index < 0) return false;
    
    int total = Flash_Get_Log_Count();
    if (index >= total) return false;
    
    // Index 0 = Newest. Newest is at total - 1.
    // So target physical index = total - 1 - index.
    int phys_idx = total - 1 - index;
    
    Flash_Log_Entry_T *flash_ptr = (Flash_Log_Entry_T *)FLASH_LOG_START_ADDR;
    *entry = flash_ptr[phys_idx];
    return true;
}

int Flash_Read_Logs(Flash_Log_Entry_T *entries, int max_count) {
    if (!entries) return 0;
    int total = Flash_Get_Log_Count();
    int count = 0;
    
    for (int i = 0; i < max_count && i < total; i++) {
        Flash_Get_Log_Entry_ByIndex(i, &entries[i]);
        count++;
    }
    return count;
}

void Flash_Erase_Logs(void) {
    __disable_irq();
    FMC_Unlock();
    FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
    for (uint32_t addr = FLASH_LOG_START_ADDR; addr <= FLASH_LOG_END_ADDR; addr += FLASH_PAGE_SIZE) {
        FMC_ErasePage(addr);
    }
    FMC_Lock();
    __enable_irq();
}

/* Config Implementation Helpers */
static uint32_t Calculate_SysConfig_Checksum(const Flash_System_Config_T *c) {
    uint32_t sum = 0;
    const uint8_t *p = (const uint8_t *)c->password;
    for(int i=0; i<sizeof(Flash_System_Config_T)-4; i++) sum += ((const uint8_t*)c)[i+4];
    return sum;
}

bool Flash_Read_System_Config(Flash_System_Config_T *config) {
    Flash_System_Config_T *stored = (Flash_System_Config_T *)FLASH_SYS_CONFIG_ADDR;
    if (stored->magic == FLASH_SYS_CONFIG_MAGIC) {
        *config = *stored;
        return true;
    }
    return false;
}

bool Flash_Write_System_Config(const Flash_System_Config_T *config) {
    Flash_System_Config_T c = *config;
    c.magic = FLASH_SYS_CONFIG_MAGIC;
    c.checksum = Calculate_SysConfig_Checksum(&c); // Simplification
    return Flash_Update_Config_Page(0, &c, sizeof(c));
}

bool Flash_Verify_Password(const char *password) {
    Flash_System_Config_T c;
    if (Flash_Read_System_Config(&c)) return strcmp(password, c.password) == 0;
    return strcmp(password, "admin") == 0;
}

bool Flash_Update_Password(const char *new_password) {
    Flash_System_Config_T c;
    if (!Flash_Read_System_Config(&c)) {
        memset(&c, 0, sizeof(c));
        strcpy(c.device_name, "Device"); // Default
    }
    strncpy(c.password, new_password, 31);
    return Flash_Write_System_Config(&c);
}

/* Network & PSU Config Stubs for brevity (logic same as previous) */
bool Flash_Read_Network_Config(Flash_Network_Config_T *config) {
    Flash_Network_Config_T *stored = (Flash_Network_Config_T *)FLASH_NET_CONFIG_ADDR;
    if (stored->magic == FLASH_NET_CONFIG_MAGIC) { *config = *stored; return true; }
    return false;
}

bool Flash_Write_Network_Config(const Flash_Network_Config_T *config) {
    Flash_Network_Config_T c = *config;
    c.magic = FLASH_NET_CONFIG_MAGIC;
    return Flash_Update_Config_Page(512, &c, sizeof(c));
}

/* 
   PSU Config Implementation - Optimized for Append-Only Write
   Address Range: FLASH_PSU_CONFIG_ADDR to FLASH_CONFIG_PAGE_ADDR + FLASH_PAGE_SIZE (or custom range)
   
   Actually, FLASH_PSU_CONFIG_ADDR is defined as FLASH_CONFIG_PAGE_ADDR + 1024U.
   So we have 1024 bytes (2KB - 1024) available for PSU Config.
   Each entry is 8 bytes. Capacity = 1024 / 8 = 128 entries.
   This means we can write 128 times before needing an erase!
*/

#define PSU_CONFIG_MAGIC_SEQ  0xA5A5A5A5U
#define PSU_CONFIG_SIZE       1024U  /* Remaining space in the page */

bool Flash_Read_PSU_Config(Flash_PSU_Config_T *config) {
    if (!config) return false;
    
    Flash_PSU_Config_Compact_T *ptr = (Flash_PSU_Config_Compact_T *)FLASH_PSU_CONFIG_ADDR;
    int max_entries = PSU_CONFIG_SIZE / sizeof(Flash_PSU_Config_Compact_T);
    int found_idx = -1;
    
    /* Scan forward to find the LAST valid entry */
    /* Since we write sequentially, valid entries are contiguous from start. 
       But to be safe against power loss during write, we check magic. */
       
    /* Optimization: Binary search or linear scan from end? 
       Linear scan from start is safest to find the first invalid one. */
    for (int i = 0; i < max_entries; i++) {
        if (ptr[i].magic_seq == PSU_CONFIG_MAGIC_SEQ) {
            found_idx = i;
        } else {
            /* Found empty/invalid space, so the previous one was the last valid one */
            break;
        }
    }
    
    if (found_idx != -1) {
        /* Found valid config */
        config->magic = FLASH_PSU_CONFIG_MAGIC;
        config->relay_states = ptr[found_idx].relay_states;
        config->psu1_enabled = ptr[found_idx].psu1_enabled;
        config->psu2_enabled = ptr[found_idx].psu2_enabled;
        config->smoke_enabled = ptr[found_idx].smoke_enabled;
        config->checksum = 0; /* Not used in compact mode */
        return true;
    }
    
    /* Fallback to legacy read if no compact entries found (migration) */
    Flash_PSU_Config_T *stored = (Flash_PSU_Config_T *)FLASH_PSU_CONFIG_ADDR;
    if (stored->magic == FLASH_PSU_CONFIG_MAGIC) { 
        *config = *stored; 
        return true; 
    }
    
    return false;
}

bool Flash_Write_PSU_Config(Flash_PSU_Config_T *config) {
    if (!config) return false;

    Flash_PSU_Config_Compact_T new_entry;
    new_entry.relay_states = config->relay_states;
    new_entry.psu1_enabled = config->psu1_enabled;
    new_entry.psu2_enabled = config->psu2_enabled;
    new_entry.smoke_enabled = config->smoke_enabled;
    new_entry.magic_seq = PSU_CONFIG_MAGIC_SEQ;
    
    Flash_PSU_Config_Compact_T *ptr = (Flash_PSU_Config_Compact_T *)FLASH_PSU_CONFIG_ADDR;
    int max_entries = PSU_CONFIG_SIZE / sizeof(Flash_PSU_Config_Compact_T);
    int write_idx = -1;
    
    /* Find the first empty slot */
    for (int i = 0; i < max_entries; i++) {
        /* Check for 0xFFFFFFFF in magic field (erased state) */
        if (ptr[i].magic_seq == 0xFFFFFFFF) {
            write_idx = i;
            break;
        }
    }
    
    FMC_Unlock();
    FMC_ClearStatusFlag(FMC_FLAG_OC | FMC_FLAG_PE | FMC_FLAG_WPE);
    
    if (write_idx != -1) {
        /* 1. Fast Path: Write to empty slot (No Erase!) */
        uint32_t addr = FLASH_PSU_CONFIG_ADDR + (write_idx * sizeof(Flash_PSU_Config_Compact_T));
        uint32_t *data = (uint32_t *)&new_entry;
        
        /* Write 2 words (8 bytes) */
        FMC_ProgramWord(addr, data[0]);
        FMC_ProgramWord(addr + 4, data[1]);
        
        /* Verify */
        if (ptr[write_idx].magic_seq == PSU_CONFIG_MAGIC_SEQ) {
            FMC_Lock();
            return true;
        }
    }
    
    /* 2. Slow Path: Page Full (or verify failed). Need Erase & Rewrite. */
    /* We need to preserve System Config and Network Config which are in the SAME PAGE! */
    /* This is the expensive operation we want to avoid most of the time. */
    
    /* Read existing Sys & Net config */
    /* Note: We can't use the simple struct copy because we need the RAW bytes of the page to preserve everything */
    /* But for simplicity, we rely on the Flash_Update_Config_Page function logic, 
       BUT we must adapt it to clear the PSU section and write the new entry at index 0. */
       
    /* Let's manually do the Swap-Erase-Write cycle here to be precise */
    
    /* A. Erase Swap */
    FMC_ErasePage(FLASH_SWAP_PAGE_ADDR);
    
    /* B. Copy Page to Swap */
    uint32_t *p_src = (uint32_t *)FLASH_CONFIG_PAGE_ADDR;
    for (int i = 0; i < FLASH_PAGE_SIZE / 4; i++) {
        FMC_ProgramWord(FLASH_SWAP_PAGE_ADDR + (i * 4), p_src[i]);
    }
    
    /* C. Erase Target Page */
    FMC_ErasePage(FLASH_CONFIG_PAGE_ADDR);
    
    /* D. Copy back Sys & Net Config (0 - 1024 bytes) */
    uint32_t *p_swap = (uint32_t *)FLASH_SWAP_PAGE_ADDR;
    for (int i = 0; i < 1024 / 4; i++) {
        FMC_ProgramWord(FLASH_CONFIG_PAGE_ADDR + (i * 4), p_swap[i]);
    }
    
    /* E. Write New PSU Config at Index 0 of PSU Section (Offset 1024) */
    /* All other slots (Index 1 to End) are already erased (0xFF), so they are ready for future appends! */
    uint32_t psu_start_addr = FLASH_PSU_CONFIG_ADDR;
    uint32_t *new_data = (uint32_t *)&new_entry;
    
    FMC_ProgramWord(psu_start_addr, new_data[0]);
    FMC_ProgramWord(psu_start_addr + 4, new_data[1]);
    
    FMC_Lock();
    return true;
}
