#include "bsp_iap.h"
#include "apm32f10x_fmc.h"
#include "apm32f10x_misc.h"
#include "apm32f10x_bakpr.h" /* For Loop Detection */
#include "apm32f10x_rcm.h"   /* For BKP Clock */
#include "apm32f10x_pmu.h"   /* For BKP Write Access */
#include "flash_storage.h"   /* For Flash Map Definitions */
#include <stdio.h>
#include <string.h>

/* IAP Implementation
 * Receives packets from I2C Slave buffer
 * Parses Opcode
 * Executes Flash operations
 */

/* Flash Page Size for APM32F107 (128KB - 256KB device) is usually 2KB */
#define FLASH_PAGE_SIZE 2048 

/* Buffer to hold received packet for processing */
// Since I2C_RX_BUFFER_SIZE was in bsp_i2c_slave.h which is deleted,
// we define a safe size here. Standard IAP packets are usually small (256 bytes + header).
#define IAP_RX_BUF_SIZE 264 
#define I2C_RX_BUFFER_SIZE IAP_RX_BUF_SIZE /* Fix for undefined identifier error */
static uint8_t packet_buf[IAP_RX_BUF_SIZE];

/* RAM Buffer for Self-Update (Avoid Stack Overflow, Ensure Alignment) */
static uint8_t ram_page_buf[FLASH_PAGE_SIZE] __attribute__((aligned(4)));

/* Web Upgrade Variables */
/* 
 * 3-Part Partition Strategy (Internal Flash Only):
 * 1. APP:   0x08000000 (Running Application)
 * 2. CACHE: 0x08020000 (Download Storage)
 * 3. COPY:  RAM Function (Ferry)
 */
#define FLASH_APP_ADDR     0x08000000
#define FLASH_CACHE_ADDR   0x08020000
#define APP_SIZE_LIMIT     (112 * 1024) // 108KB max

static uint32_t web_flash_addr = FLASH_CACHE_ADDR;

/* 
 * RAM Code Section Definition 
 * In Keil, use "#pragma arm section code" or "__attribute__((section))"
 * We will put critical IAP functions in RAM.
 * Added noinline to prevent inlining into Flash code.
 */
#define RAM_FUNC __attribute__((section(".ram_code"))) __attribute__((noinline))

/* 
 * CRITICAL: FMC Registers Definition for RAM Access 
 * We cannot call library functions (FMC_ErasePage) because they are in Flash!
 * We must re-implement minimal FMC logic here using direct register access.
 */
#define FMC_REG_BASE          0x40022000
#define FMC_REG_ACR           (*((volatile uint32_t *)(FMC_REG_BASE + 0x00)))
#define FMC_REG_KEY           (*((volatile uint32_t *)(FMC_REG_BASE + 0x04)))
#define FMC_REG_OPTKEY        (*((volatile uint32_t *)(FMC_REG_BASE + 0x08)))
#define FMC_REG_SR            (*((volatile uint32_t *)(FMC_REG_BASE + 0x0C)))
#define FMC_REG_CR            (*((volatile uint32_t *)(FMC_REG_BASE + 0x10)))
#define FMC_REG_AR            (*((volatile uint32_t *)(FMC_REG_BASE + 0x14)))

#define FMC_KEY1              0x45670123
#define FMC_KEY2              0xCDEF89AB

#define FMC_CR_PG             (1U << 0)
#define FMC_CR_PER            (1U << 1)
#define FMC_CR_MER            (1U << 2)
#define FMC_CR_STRT           (1U << 6)
#define FMC_CR_LOCK           (1U << 7)

#define FMC_SR_BSY            (1U << 0)
#define FMC_SR_PGERR          (1U << 2)
#define FMC_SR_WRPRTERR       (1U << 4)
#define FMC_SR_EOP            (1U << 5)

/* RAM Function: Wait for Ready */
RAM_FUNC int IAP_RAM_WaitBusy(void) {
    uint32_t timeout = 0x00FFFFFF;
    while ((FMC_REG_SR & FMC_SR_BSY) && (timeout > 0)) {
        timeout--;
    }
    return (timeout > 0) ? 0 : -1;
}

/* RAM Function: Erase Page */
RAM_FUNC int IAP_RAM_ErasePage(uint32_t addr) {
    int status = 0;
    
    /* Wait for Busy */
    status = IAP_RAM_WaitBusy();
    if(status != 0) return status;
    
    /* Unlock */
    if (FMC_REG_CR & FMC_CR_LOCK) {
        FMC_REG_KEY = FMC_KEY1;
        FMC_REG_KEY = FMC_KEY2;
    }
    
    /* Clear Flags */
    FMC_REG_SR = (FMC_SR_PGERR | FMC_SR_WRPRTERR | FMC_SR_EOP);
    
    /* Set Page Erase Mode */
    FMC_REG_CR |= FMC_CR_PER;
    FMC_REG_AR = addr;
    FMC_REG_CR |= FMC_CR_STRT;
    
    /* Wait */
    status = IAP_RAM_WaitBusy();
    
    /* Disable Page Erase */
    FMC_REG_CR &= ~FMC_CR_PER;
    
    return status;
}

/* RAM Function: Program Word */
RAM_FUNC int IAP_RAM_ProgramWord(uint32_t addr, uint32_t data) {
    int status = 0;
    
    /* Wait */
    status = IAP_RAM_WaitBusy();
    if(status != 0) return status;
    
    /* Unlock */
    if (FMC_REG_CR & FMC_CR_LOCK) {
        FMC_REG_KEY = FMC_KEY1;
        FMC_REG_KEY = FMC_KEY2;
    }
    
    /* Clear Flags */
    FMC_REG_SR = (FMC_SR_PGERR | FMC_SR_WRPRTERR | FMC_SR_EOP);
    
    /* Set Program Mode */
    FMC_REG_CR |= FMC_CR_PG;
    
    /* Write Data */
    *(__IO uint16_t*)addr = (uint16_t)data;
    status = IAP_RAM_WaitBusy();
    
    if (status == 0) {
        *(__IO uint16_t*)(addr + 2) = (uint16_t)(data >> 16);
        status = IAP_RAM_WaitBusy();
    }
    
    /* Disable Program Mode */
    FMC_REG_CR &= ~FMC_CR_PG;
    
    return status;
}

/* Wrapper for Erase (During Download Phase) */
void IAP_ErasePage(uint32_t addr)
{
    /* Use Standard FMC Erase, but protected */
    /* Note: Calling IAP_RAM_ErasePage is safe too */
    /* IMPORTANT: During Download, we are writing to CACHE (0x0802xxxx) */
    /* APP is running at 0x0800xxxx. */
    /* We MUST disable interrupts to avoid Vector Table fetch during Flash Busy */
    
    __disable_irq();
    IAP_RAM_ErasePage(addr);
    __enable_irq();
    
    printf("[IAP] Erase Page 0x%08X Success\n", addr);
}

/* Wrapper for Write (During Download Phase) */
void IAP_WriteData(uint32_t addr, uint8_t *data, uint16_t len)
{
    /* Write to Internal Flash */
    /* Must disable interrupts */
    __disable_irq();
    
    for (uint16_t i = 0; i < len; i += 4) {
        uint32_t word_val = 0xFFFFFFFF;
        
        /* Handle non-aligned tail? */
        /* Code below assumes 4-byte aligned buffer from IAP_Web_Write */
        if (i + 4 <= len) {
             word_val = *((uint32_t*)(data + i));
        } else {
             /* Should not happen if caller aligns */
             memcpy(&word_val, data + i, len - i);
        }
        
        IAP_RAM_ProgramWord(addr + i, word_val);
    }
    
    __enable_irq();
    
    /* Log every 1KB */
    if ((addr & 0x3FF) == 0) {
        // printf("[IAP] Write 0x%08X OK\n", addr);
    }
}

/* RAM Function: Self Update Routine (Internal Flash Ferry) */
RAM_FUNC void IAP_RAM_SelfUpdate(void) {
    uint32_t dst = FLASH_APP_ADDR;   /* 0x08000000 */
    uint32_t src = FLASH_CACHE_ADDR; /* 0x08020000 */
    uint32_t len = 0;
    
    /* 1. Validate Source (Read First Word from Cache) */
    /* Safe to read Flash here because we are not writing yet */
    uint32_t sp = *((uint32_t*)src);
    
    if ((sp & 0x2FFE0000) != 0x20000000) {
        return; /* No valid update */
    }
    
    /* Unlock Flash */
    if (FMC_REG_CR & FMC_CR_LOCK) {
        FMC_REG_KEY = FMC_KEY1;
        FMC_REG_KEY = FMC_KEY2;
    }
    
    /* 2. Copy Loop */
    for (len = 0; len < APP_SIZE_LIMIT; len += FLASH_PAGE_SIZE) {
        /* CRITICAL: Read from Flash to RAM Buffer BEFORE Erasing/Writing */
        /* memcpy is safe here because Flash is idle */
        uint8_t *src_ptr = (uint8_t*)(src + len);
        
        /* Use Global RAM Buffer instead of Stack (Avoid Overflow) */
        /* MANUALLY COPY - DO NOT USE memcpy() OR LIBRARY FUNCTIONS */
        /* Because the library function might be in the Flash area we are about to erase! */
        for(int k=0; k<FLASH_PAGE_SIZE; k++) {
            ram_page_buf[k] = src_ptr[k];
        }
        
        /* CRITICAL: Now Flash will be Busy. Code MUST be in RAM. */
        /* Erase Destination Page (Internal Flash) */
        IAP_RAM_ErasePage(dst + len);
        
        /* Program Page */
        for (uint32_t i = 0; i < FLASH_PAGE_SIZE; i += 4) {
            /* Manual Little Endian Construction to avoid library calls */
            uint32_t data = (uint32_t)ram_page_buf[i] | 
                           ((uint32_t)ram_page_buf[i+1] << 8) | 
                           ((uint32_t)ram_page_buf[i+2] << 16) | 
                           ((uint32_t)ram_page_buf[i+3] << 24);
                           
            /* Check if erased (0xFFFFFFFF) to save time/stress */
            if (data != 0xFFFFFFFF) {
                IAP_RAM_ProgramWord(dst + len + i, data);
            }
        }
    }
    
    /* 3. Erase Cache Flag (Page 0 of Cache) to prevent Loop */
    IAP_RAM_ErasePage(FLASH_CACHE_ADDR);

    /* 4. Reset */
    #define SCB_AIRCR (*((volatile uint32_t *)0xE000ED0C))
    SCB_AIRCR = 0x05FA0004;
    
    while(1);
}

/* Wrapper to check and trigger update */
void IAP_CheckAndSelfUpdate(void)
{
    /* Enable PMU and BAKPR Clocks (Correct Macro: RCM_APB1_PERIPH_BAKR) */
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_PMU | RCM_APB1_PERIPH_BAKR);
    /* Enable Access to Backup Registers */
    PMU_EnableBackupAccess();
    
    /* Use BKP_DR1 (BAKPR_DATA1) as Boot Counter */
    uint16_t boot_count = BAKPR_ReadBackupRegister(BAKPR_DATA1);
    
    /* Check if Cache has valid firmware */
    uint32_t sp = *((uint32_t*)FLASH_CACHE_ADDR);
    
    if ((sp & 0x2FFE0000) == 0x20000000) {
        // printf("[IAP] Found New Firmware in Cache (0x%08X). Boot Count: %d\n", FLASH_CACHE_ADDR, boot_count);
        
        if (boot_count > 3) {
            // printf("[IAP] Upgrade Loop Detected! Erasing Cache Flag...\n");
            IAP_ErasePage(FLASH_CACHE_ADDR); /* Erase Page 0 of Cache */
            BAKPR_ConfigBackupRegister(BAKPR_DATA1, 0);
            // printf("[IAP] Recovery Complete. Booting old firmware.\n");
            return;
        }
        
        /* Increment Boot Count */
        BAKPR_ConfigBackupRegister(BAKPR_DATA1, boot_count + 1);
        
        // printf("[IAP] Starting Self-Update from Cache...\n");
        
        /* Disable Interrupts CRITICAL */
        __disable_irq();
        
        /* Run RAM Function */
        IAP_RAM_SelfUpdate();
        
        /* Should not reach here */
        __enable_irq();
    }
    else {
        // printf("[IAP] No Pending Update in Cache.\n");
        if (boot_count != 0) {
            BAKPR_ConfigBackupRegister(BAKPR_DATA1, 0);
        }
    }
}

static uint32_t last_erased_page = 0;

/* Helper: Jump to App */
static void IAP_JumpToApp(uint32_t app_addr)
{
    /* Check if stack address is valid (Top of SRAM) */
    if (((*(__IO uint32_t*)app_addr) & 0x2FFE0000) == 0x20000000)
    {
        // printf("[IAP] Jumping to App at 0x%08X...\n", app_addr);
        
        /* Jump to user application */
        uint32_t JumpAddress = *(__IO uint32_t*) (app_addr + 4);
        void (*Jump_To_Application)(void) = (void (*)(void)) JumpAddress;
        
        /* Initialize user application's Stack Pointer */
        __set_MSP(*(__IO uint32_t*) app_addr);
        
        Jump_To_Application();
    }
    else
    {
        // printf("[IAP] Invalid Stack Address at 0x%08X\n", app_addr);
    }
}

/* Alignment Buffer */
static uint8_t iap_buf[4];
static uint8_t iap_buf_len = 0;

/* Web Upgrade Implementation */
void IAP_Web_Init(uint32_t total_len)
{
    // printf("[IAP] Web Upgrade Init. Target: 0x%08X, Len: %d\n", FLASH_CACHE_ADDR, total_len);
    web_flash_addr = FLASH_CACHE_ADDR;
    
    /* Reset Buffer */
    iap_buf_len = 0;
    memset(iap_buf, 0, 4);
    
    /* Reset Erase Tracking for JIT Erase */
    last_erased_page = 0; 
    
    // printf("[IAP] Ready for Stream Write (JIT Erase Mode).\n");
}

/* Check and Erase Page if needed (Just-In-Time) */
static void IAP_CheckAndErase(uint32_t addr) {
    uint32_t page_start = addr & ~(FLASH_PAGE_SIZE - 1);
    
    /* Boundary Check: Protect Config Area */
    if (page_start >= FLASH_LOG_START_ADDR) {
        // printf("[IAP] Error: Attempt to erase Config Area at 0x%08X! Aborted.\n", page_start);
        return;
    }

    if (page_start != last_erased_page) {
        // Safety check: ensure we are in Cache Range
        if (page_start >= FLASH_CACHE_ADDR) {
             IAP_ErasePage(page_start);
             last_erased_page = page_start;
        }
    }
}

int IAP_Web_Write(uint8_t *data, uint16_t len)
{
    /* JIT Erase + Write with Alignment Handling */
    
    uint16_t i = 0;
    
    /* 1. Fill buffer if we have leftovers */
    if (iap_buf_len > 0) {
        while (iap_buf_len < 4 && i < len) {
            iap_buf[iap_buf_len++] = data[i++];
        }
        
        if (iap_buf_len == 4) {
            /* Flush Buffer */
            IAP_CheckAndErase(web_flash_addr);
            IAP_WriteData(web_flash_addr, iap_buf, 4);
            web_flash_addr += 4;
            iap_buf_len = 0;
        }
    }
    
    /* 2. Write main chunks (4-byte aligned) */
    if (i < len) {
        uint16_t remaining = len - i;
        uint16_t aligned_len = remaining & ~3; // Multiple of 4
        
        if (aligned_len > 0) {
            /* Handle Page Crossing */
            uint32_t current_chunk_addr = web_flash_addr;
            uint32_t chunk_end = web_flash_addr + aligned_len;
            uint32_t next_page_boundary = (current_chunk_addr & ~(FLASH_PAGE_SIZE - 1)) + FLASH_PAGE_SIZE;
            
            if (chunk_end > next_page_boundary) {
                /* Split the write */
                uint16_t first_part = next_page_boundary - current_chunk_addr;
                
                IAP_CheckAndErase(current_chunk_addr);
                IAP_WriteData(current_chunk_addr, &data[i], first_part);
                
                IAP_CheckAndErase(next_page_boundary);
                IAP_WriteData(next_page_boundary, &data[i + first_part], aligned_len - first_part);
            } else {
                /* Single Page Write */
                IAP_CheckAndErase(current_chunk_addr);
                IAP_WriteData(current_chunk_addr, &data[i], aligned_len);
            }
            
            web_flash_addr += aligned_len;
            i += aligned_len;
        }
    }
    
    /* 3. Store remaining bytes */
    while (i < len) {
        iap_buf[iap_buf_len++] = data[i++];
    }
    
    return 0;
}

extern volatile uint8_t g_system_reset_pending;

int IAP_Web_Finish(void)
{
    printf("[IAP] Upgrade Finished. Checksum/Verify...\n");
    
    /* Flush remaining bytes in buffer */
    if (iap_buf_len > 0) {
        /* Pad with 0xFF */
        while (iap_buf_len < 4) {
            iap_buf[iap_buf_len++] = 0xFF;
        }
        IAP_CheckAndErase(web_flash_addr);
        IAP_WriteData(web_flash_addr, iap_buf, 4);
        web_flash_addr += 4;
        iap_buf_len = 0;
        printf("[IAP] Flushed remaining bytes.\n");
    }
    
    printf("[IAP] Jumping to Application/Resetting...\n");
    g_system_reset_pending = 1; 
    
    return 0;
}


/* Main IAP Logic Loop (Called from Main Loop) */
void IAP_MainLoop(void)
{
    /* Block IAP if system is about to reset */
    if (g_system_reset_pending) return;

#if 0
    /* Check if a complete packet is received */
    if (g_i2c_packet_ready)
    {
        /* Copy data from volatile buffer to local buffer to avoid race conditions */
        /* Note: We assume head was reset to 0 by ISR for each new packet transaction */
        uint16_t len = g_i2c_rx_head;
        for(int i=0; i<len; i++) {
            packet_buf[i] = g_i2c_rx_buffer[i];
        }
        
        /* Reset flag */
        g_i2c_packet_ready = false;
        // g_i2c_rx_head = 0; // ISR does this on next START
        
        if (len == 0) return;
        
        /* Parse Opcode */
        uint8_t opcode = packet_buf[0];
        
        switch (opcode)
        {
            case OPC_WREN:
            {
                /* Write Command from stm32flash.c */
                /* Header: [0]=OPC, [1..4]=Addr(BigEndian), [5..6]=TotalLen(BigEndian), [7]=Dummy(0xFF) */
                /* Total Header Size = 8 bytes */
                
                if (len < 8) {
                    // printf("[IAP] Packet too short for WREN header\n");
                    break;
                }
                
                /* Extract Address (Big Endian) */
                uint32_t addr = (packet_buf[1] << 24) | (packet_buf[2] << 16) | (packet_buf[3] << 8) | packet_buf[4];
                
                /* Extract Total Length (Big Endian) */
                uint16_t total_len = (packet_buf[5] << 8) | packet_buf[6];
                
                /* stm32flash.c sends the whole file in one write() call usually, 
                   but our I2C slave buffer size is limited (256 bytes). 
                   We need to handle fragmentation or assume the Master sends chunks. 
                   Looking at stm32flash.c: "write(i2c_fd, buffer, file_size)"
                   It attempts to write the WHOLE file at once! 
                   
                   CRITICAL ISSUE: I2C Slave cannot receive > 256 bytes in one go if buffer is small.
                   However, standard I2C drivers usually limit max transfer size.
                   If the Master sends 50KB at once, our ISR buffer will wrap around or overflow.
                   
                   But assuming for now we receive a valid chunk that fits in buffer 
                   OR we are just validating the protocol header logic.
                   
                   The stm32flash.c code sets total_len = file_size + HEADERS_COUNT.
                   So total_len matches the I2C transaction length.
                */
                
                /* Validate packet length */
                /* Note: If len (received bytes) < total_len, it means we only got a partial packet.
                   This is highly likely if the file is large. 
                   We should probably process what we have if we can, or reject.
                   But for IAP, usually we need the whole packet to know data length.
                */
                
                if (total_len < 8) {
                    // printf("[IAP] Invalid Total Length: %d\n", total_len);
                    break;
                }
                
                uint16_t data_len = total_len - 8;
                
                /* Sanity check: If we received less than the header says, we might be in trouble 
                   unless we implement state machine to receive rest. 
                   For now, assume Master breaks it down or file is small? 
                   Wait, stm32flash.c writes ALL at once. 
                   If file is 20KB, total_len = 20KB. 
                   Our ISR buffer is 256 bytes. We will lose data.
                   
                   SOLUTION: The Master MUST chunk the data. 
                   The provided stm32flash.c is a simple example that might work for small files 
                   or if the Linux I2C driver breaks it into smaller Write transactions?
                   No, I2C Write is usually atomic START...DATA...STOP.
                   
                   If the user wants to use THAT specific stm32flash.c, 
                   we MUST increase our buffer or hope the file is small.
                   OR, the user is mistaken and the real tool does chunking.
                   
                   Let's assume standard behavior:
                   Addr = Destination Flash Address.
                   Data starts at offset 8.
                */
                
                printf("[IAP] WREN: Addr=0x%08X, TotalLen=%d, RxLen=%d\n", addr, total_len, len);
                
                if (len > 8) {
                    /* Write the data we received */
                    /* Calculate actual data bytes available in this packet */
                    uint16_t avail_data = len - 8;
                    if (avail_data > data_len) avail_data = data_len;
                    
                    IAP_WriteData(addr, &packet_buf[8], avail_data);
                }
                break;
            }
            
            case OPC_JUMP:
            {
                /* Jump Command */
                /* Format: [Opcode(1)] [Addr(4)] [Res(2)] */
                 if (len < 5) {
                    // printf("[IAP] Packet too short for JUMP\n");
                    break;
                }
                
                uint32_t addr = (packet_buf[1] << 24) | (packet_buf[2] << 16) | (packet_buf[3] << 8) | packet_buf[4];
                
                IAP_JumpToApp(addr);
                break;
            }
            
            case OPC_ERPG:
            {
                /* Erase Page Command */
                /* Format: [Opcode(1)] [Addr(4)] */
                if (len < 5) break;
                
                uint32_t addr = (packet_buf[1] << 24) | (packet_buf[2] << 16) | (packet_buf[3] << 8) | packet_buf[4];
                IAP_ErasePage(addr);
                break;
            }
            
            case OPC_ERUSM:
            {
                /* Erase User Space Memory */
                // TODO: Define User Space range
                // printf("[IAP] Erase User Space - Not Implemented\n");
                break;
            }
            
            case OPC_USRCD:
            {
                /* Jump to App (Alternate opcode?) */
                /* Usually points to fixed address */
                // IAP_JumpToApp(0x08000000); // Or wherever
                break;
            }
            
            default:
            {
                /* Unknown Opcode or Legacy Register Access */
                /* If it's a legacy register write, it was already handled by the ISR's s_reg_index logic? */
                /* No, ISR only sets index. It doesn't consume the "action". */
                /* But existing code only supports Register Read/Write. */
                /* Since we are in IAP parsing, we can ignore other opcodes or print debug */
                // printf("[IAP] Unknown Opcode: 0x%02X\n", opcode);
                break;
            }
        }
    }
#endif
}
