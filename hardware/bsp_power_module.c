#include "bsp_power_module.h"

#include "bsp_smoke_sensor.h"
#include "bsp_beeper.h"
#include "sys_arch.h"
#include <stdio.h>
#include <string.h>
#include "apm32f10x_i2c.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_rcm.h"
#include "flash_storage.h"
#include "log_system.h"

// ??????
PowerModuleStatus_T g_power_module_status;

// PSU Logical Enable State (Default ON)
static bool g_psu_enabled[2] = {true, true};
static bool g_psu_config_dirty = false;
static uint32_t g_last_psu_config_change_time = 0;
#define PSU_CONFIG_SAVE_DELAY_MS 50
// PMBus ??????
#define PSU1_ADDRESS 0x58 // ??????? 0xB0 (8-bit) -> 0x58 (7-bit)
#define PSU2_ADDRESS 0x59 // ??????? 0xB2 (8-bit) -> 0x59 (7-bit)

// PMBus Commands
#define PMBUS_CMD_STATUS_WORD   0x79
#define PMBUS_CMD_READ_VIN      0x88
#define PMBUS_CMD_READ_VOUT     0x8B
#define PMBUS_CMD_READ_IOUT     0x8C
#define PMBUS_CMD_READ_TEMP1    0x8D

// Detected PSU Addresses (Default to schematic: 0x58, 0x59)
static uint8_t g_psu_addr[2] = {0x58, 0x59};

// PMBus code removed per request (CPU will handle PMBus)

static void BSP_I2C_Init(void)
{
    GPIO_Config_T gpioConfig;
    I2C_Config_T i2cConfig;

    /* Enable Clock */
    RCM_EnableAPB1PeriphClock(RCM_APB1_PERIPH_I2C1);
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOB);

    /* GPIO Config (PB6=SCL, PB7=SDA) */
    gpioConfig.pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpioConfig.mode = GPIO_MODE_AF_OD;
    gpioConfig.speed = GPIO_SPEED_50MHz;
    GPIO_Config(GPIOB, &gpioConfig);

    /* I2C Config */
    I2C_Reset(I2C1);
    i2cConfig.mode = I2C_MODE_I2C;
    i2cConfig.dutyCycle = I2C_DUTYCYCLE_2;
    i2cConfig.ownAddress1 = 0x00; 
    i2cConfig.ack = I2C_ACK_ENABLE;
    i2cConfig.ackAddress = I2C_ACK_ADDRESS_7BIT;
    i2cConfig.clockSpeed = 100000; 
    I2C_Config(I2C1, &i2cConfig);

    /* Enable I2C */
    I2C_Enable(I2C1);
}

static bool PMBus_CheckPresence(uint8_t addr)
{
    // 1. 物理层检查：I2C 空闲时，SDA 和 SCL 必须为高电平
    if (GPIO_ReadInputBit(GPIOB, GPIO_PIN_6) == 0 || GPIO_ReadInputBit(GPIOB, GPIO_PIN_7) == 0) {
			printf("[I2C] Bus error: SDA stuck low\n");
			return false; // 总线异常，视为不在位
    }

    uint32_t timeout = 0xFFF;
    
    // Wait for Busy
    while(I2C_ReadStatusFlag(I2C1, I2C_FLAG_BUSBSY)) {
        if((timeout--) == 0) return false;
    }
    
    // --- Step 1: Send Command (Write) ---
    
    // Generate START
    I2C_EnableGenerateStart(I2C1);
    timeout = 0xFFF;
    while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) return false;
    }
    
    // Send Address (Write)
    I2C_Tx7BitAddress(I2C1, addr << 1, I2C_DIRECTION_TX);
    
    // Wait for ADDR (ACK)
    timeout = 0xFFF;
    while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        if((timeout--) == 0) {
            I2C_EnableGenerateStop(I2C1);
            return false; // No ACK or Timeout
        }
    }
    
    // Send Command (STATUS_WORD 0x79)
    I2C_TxData(I2C1, PMBUS_CMD_STATUS_WORD);
    
    // Wait for Byte Transfer Finished
    timeout = 0xFFF;
    while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        if((timeout--) == 0) {
            I2C_EnableGenerateStop(I2C1);
            return false;
        }
    }
    
    // --- Step 2: Read Data ---
    
    // Generate Repeated START
    I2C_EnableGenerateStart(I2C1);
    timeout = 0xFFF;
    while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) {
        if((timeout--) == 0) return false;
    }
    
    // Send Address (Read)
    I2C_Tx7BitAddress(I2C1, addr << 1, I2C_DIRECTION_RX);
    
    // Wait for ADDR (ACK)
    timeout = 0xFFF;
    while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)) {
        if((timeout--) == 0) {
            I2C_EnableGenerateStop(I2C1);
            return false; // No ACK (Real Check)
        }
    }
    
    // We only need to check if we can read 1 byte.
    // Prepare to NACK the next byte to stop transmission
    I2C_DisableAcknowledge(I2C1);
    
    // Wait for Byte Received
    timeout = 0xFFF;
    while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_BYTE_RECEIVED)) {
        if((timeout--) == 0) {
            I2C_EnableGenerateStop(I2C1);
            I2C_EnableAcknowledge(I2C1); // Restore ACK
            return false;
        }
    }
    
    // Read the dummy byte (Success!)
    uint8_t data = I2C_RxData(I2C1);
    (void)data;
    
    // Generate STOP
    I2C_EnableGenerateStop(I2C1);
    
    // Restore ACK for next time
    I2C_EnableAcknowledge(I2C1);
    
    // printf("[PMBus] Addr 0x%02X -> Read Success (Present)\n", addr);
    return true;
}

static void PMBus_Scan(void)
{
    // printf("--- I2C Scan Start ---\n");
    for(uint8_t i=1; i<127; i++) {
        // Simple Ping
        uint32_t timeout = 0xFFF;
        while(I2C_ReadStatusFlag(I2C1, I2C_FLAG_BUSBSY)) { if((timeout--)==0) break; }
        
        I2C_EnableGenerateStart(I2C1);
        timeout = 0xFFF;
        while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) { if((timeout--)==0) break; }
        
        I2C_Tx7BitAddress(I2C1, i << 1, I2C_DIRECTION_TX);
        
        timeout = 0xFFF;
        bool ack = false;
        while(1) {
            if(I2C_ReadStatusFlag(I2C1, I2C_FLAG_ADDR)) {
                ack = true;
                I2C_EnableGenerateStop(I2C1);
                (void)I2C1->STS1; (void)I2C1->STS2;
                break;
            }
            if(I2C_ReadStatusFlag(I2C1, I2C_FLAG_AE)) {
                I2C_ClearStatusFlag(I2C1, I2C_FLAG_AE);
                I2C_EnableGenerateStop(I2C1);
                break;
            }
            if((timeout--) == 0) {
                I2C_EnableGenerateStop(I2C1);
                break;
            }
        }
        
        if(ack) {
            printf("Found Device at 0x%02X\n", i);
        }
    }
    printf("--- I2C Scan End ---\n");
}


/**
 * @brief  ??????????
 */
void PowerModule_Init(void)
{
    uint8_t i;
    
    // ??????????
    for (i = 0; i < BOARD_MAX; i++) {
        g_power_module_status.board_status[i].state = POWER_OUTPUT_OFF;
    }
    
    // ?????????
    Relay_Init();
    LED_Init();
    // Beeper_Init(); // Handled by buzzer_control_init()
    ADC_Init();
    BSP_I2C_Init(); // PMBus I2C Init
    
    // ????????IO???
    PowerModule_PowerIO_Init();
    
    // printf("[Power] Initialized\n");

    // Load PSU configuration from Flash
    Flash_PSU_Config_T psu_config;
    bool valid_config = false;

    // printf("[Power] Init: Checking Flash Config at 0x%08X...\n", FLASH_PSU_CONFIG_ADDR);
    
    if (Flash_Read_PSU_Config(&psu_config)) {
        // printf("[Power] Flash Read Success. Checking integrity...\n");
        valid_config = true;
        
        g_psu_enabled[0] = psu_config.psu1_enabled;
        g_psu_enabled[1] = psu_config.psu2_enabled;
        
        if (g_psu_enabled[0]) g_power_module_status.psu_data[0].last_on_time = sys_get_tick_ms();
        if (g_psu_enabled[1]) g_power_module_status.psu_data[1].last_on_time = sys_get_tick_ms();

        // Apply PSU State FIRST (Note: This function sets all relays ON by default!)
        PowerModule_SetPSUState(g_psu_enabled[0]);
        
        // Restore Relay State (Enforce OFF for those not in mask)
        // Since SetPSUState turned everything ON, we only need to turn OFF the ones that should be OFF.
        for(int i=0; i<4; i++) {
            if ((psu_config.relay_states & (1 << i)) == 0) {
                // If bit is 0, turn it OFF (overriding the default ON)
                PowerModule_Board_Disable((BoardId_T)i);
            }
        }
        
        printf("[Power] Config Restored: PSU1=%d, PSU2=%d, Relays=0x%02X\n", 
               g_psu_enabled[0], g_psu_enabled[1], psu_config.relay_states);
        
        // Clear dirty flag set by SetPSUState/Board_Enable during init
        g_psu_config_dirty = false;

    } else {
        // printf("[Power] Flash Read Failed (Bad Magic/Checksum or Empty).\n");
    }

    if (!valid_config) {
        // printf("[Power] Using Default Settings.\n");
        g_psu_enabled[0] = true;
        g_psu_enabled[1] = true;
        
        g_power_module_status.psu_data[0].last_on_time = sys_get_tick_ms();
        g_power_module_status.psu_data[1].last_on_time = sys_get_tick_ms();
        
        PowerModule_SetPSUState(true);

        // Default: Enable all 4 output channels
        for(int i=0; i<BOARD_MAX; i++) {
            PowerModule_Board_Enable((BoardId_T)i);
        }

        // Force save defaults to fix corrupted flash
        g_psu_config_dirty = true;
        g_last_psu_config_change_time = sys_get_tick_ms() - PSU_CONFIG_SAVE_DELAY_MS; // Save immediately
    }
    
    // I2C Scan and PMBus initialization removed.
}

/**
 * @brief  Sync power status from PB11 MCU (I2C Slave 0x1F)
 *         PB11 MCU collects status from physical pins and provides them via I2C registers.
 */
void PowerModule_SyncFromPB11MCU(void)
{
    // ... (This function remains but might be redundant if we use local GPIO detection)
    // For now, let's keep it but also update status from local GPIOs in UpdatePowerIOState
}

// PMBus_SetOperation removed

/**
 * @brief  ????????????
 */
void PowerModule_UpdateStatus(void)
{
    uint8_t i;
    uint32_t current_time = sys_get_tick_ms();
    
    // 1. Sync with PB11 MCU (Slave 0x1f) - This updates Voltages and IO status
    // PowerModule_SyncFromPB11MCU(); // Temporary disabled to prevent I2C blocking web server

    // 2. ???? PSU Data via PMBus (Every 500ms)
    static uint32_t last_pmbus_time = 0;
    if (current_time - last_pmbus_time >= 500) {
        last_pmbus_time = current_time;
        
        // Debug: I2C Scan (Run periodically if PSU is OFF)
        static uint8_t scan_counter = 0;
        
        // If either PSU is logically OFF, scan every 10 cycles (approx 5 seconds)
        if ((!g_psu_enabled[0] || !g_psu_enabled[1])) {
            if (++scan_counter >= 10) {
                scan_counter = 0;
                PMBus_Scan();
            }
        }

        // Sync with PB11 MCU (Slave 0x1f)
        PowerModule_SyncFromPB11MCU();

        // Read PMBus Data directly (Using auto-detected addresses)
        // PMBus_Update_PSU(0, g_psu_addr[0]); // Removed
        // PMBus_Update_PSU(1, g_psu_addr[1]); // Removed
    }
    
    // 3. Update ADC Values (Voltage) FIRST
    float max_bus_voltage = 0.0f;
    for (i = 0; i < BOARD_MAX; i++) {
        // Map Board ID to ADC Channel (ADC_CH_12V_OUT1 corresponds to BOARD_1)
        // ADC_CH_12V_OUT1 is 2, BOARD_1 is 0. So Channel = 2 + i.
        g_power_module_status.board_status[i].voltage = ADC_ReadActualVoltage(ADC_CH_12V_OUT1 + i);
        if (g_power_module_status.board_status[i].voltage > max_bus_voltage) {
            max_bus_voltage = g_power_module_status.board_status[i].voltage;
        }
    }

    // 4. Update GPIO Status (Present/PWROK)
    PowerModule_UpdatePowerIOState();
    
    // Logic Optimization: Online/Offline determination logic (dependent on GPIO/I2C PMBus detection)
    // User Requirement:
    // When PSU Enabled: Check BOTH PWROK and PRESENT signals - logical AND. If either is missing, it's Offline.
    // When PSU Disabled: Check ONLY PRESENT pin to determine physical presence.
    
    // First read hardware Presence pin state (PD7/PD6 - Active Low)
    uint8_t psu1_present_pin = GPIO_ReadInputBit(GPIOD, GPIO_PIN_7);
    uint8_t psu2_present_pin = GPIO_ReadInputBit(GPIOD, GPIO_PIN_6);
    
    // Read PWROK state (via pwr_sig_detect function)
    PowerStatus sta = pwr_sig_detect();
    uint8_t psu1_pwrok = (sta.Pwrok & MODULE1_MASK) ? 1 : 0;
    uint8_t psu2_pwrok = (sta.Pwrok & MODULE2_MASK) ? 1 : 0;
    
    // PSU1 Detection Logic
    if (g_psu_enabled[0]) {
        // When PSU Enabled: PWROK && PRESENT (Active Low)
        uint8_t psu1_present = !psu1_present_pin; // Invert: Low level = Present
        g_power_module_status.psu1_present = psu1_present && psu1_pwrok;
    } else {
        // When PSU Disabled: Check only PRESENT pin
        g_power_module_status.psu1_present = !psu1_present_pin; // Invert: Low level = Present
    }

    // PSU2 Detection Logic
    if (g_psu_enabled[1]) {
        // When PSU Enabled: PWROK && PRESENT (Active Low)
        uint8_t psu2_present = !psu2_present_pin; // Invert: Low level = Present
        g_power_module_status.psu2_present = psu2_present && psu2_pwrok;
    } else {
        // When PSU Disabled: Check only PRESENT pin
        g_power_module_status.psu2_present = !psu2_present_pin; // Invert: Low level = Present
    }
    
    // 5. Update Online Status based on Hardware Status (Present only)
    for(i=0; i<2; i++) {
        // User Requirement: Online/Offline status is determined ONLY by whether PSUs are inserted.
        bool is_present = (i==0) ? g_power_module_status.psu1_present : g_power_module_status.psu2_present;
        
        g_power_module_status.psu_data[i].online = is_present ? 1 : 0;
    }

    // --- Presence Change Logging & Timer Reset ---
    static bool prev_psu1_present = false;
    static bool prev_psu2_present = false;
    static bool presence_init = false;

    if (!presence_init) {
        prev_psu1_present = g_power_module_status.psu1_present;
        prev_psu2_present = g_power_module_status.psu2_present;
        presence_init = true;
    } else {
        // PSU 1 Change
        if (g_power_module_status.psu1_present != prev_psu1_present) {
            if (g_power_module_status.psu1_present) {
                g_power_module_status.psu_data[0].last_on_time = sys_get_tick_ms(); // Reset timer on connect
                Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Power Supply Unit 1 Connected");
            } else {
                Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Power Supply Unit 1 Disconnected");
            }
            prev_psu1_present = g_power_module_status.psu1_present;
        }
        
        // PSU 2 Change
        if (g_power_module_status.psu2_present != prev_psu2_present) {
            if (g_power_module_status.psu2_present) {
                g_power_module_status.psu_data[1].last_on_time = sys_get_tick_ms(); // Reset timer on connect
                Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Power Supply Unit 2 Connected");
            } else {
                Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Power Supply Unit 2 Disconnected");
            }
            prev_psu2_present = g_power_module_status.psu2_present;
        }
    }
    
    // Update Input Voltage (for protection logic)
    // Note: We don't have a direct Input Voltage ADC channel in the provided macros?
    // Looking at bsp_adc.h: ADC_CH_12V_OUT1-4 are output voltages.
    // Is there an input voltage channel?
    // Let's assume one of them or use PSU PMBus voltage as reference if ADC is missing.
    // For now, let's update it from ADC_CH_12V_OUT1 (Channel 2) as a proxy if input_voltage is 0
    // Or better, update it from PSU PMBus reading if available.
    
    if (g_power_module_status.psu_data[0].online) {
         g_power_module_status.input_voltage = g_power_module_status.psu_data[0].vout;
    } else if (g_power_module_status.psu_data[1].online) {
         g_power_module_status.input_voltage = g_power_module_status.psu_data[1].vout;
    } else {
         // Fallback to ADC if PMBus is offline (unlikely if we have power)
         // But wait, if PMBus is offline, we might not have 12V either.
         g_power_module_status.input_voltage = 0.0f;
    }

    // ???????IO?? (Moved up)
    // PowerModule_UpdatePowerIOState(); 
    
    // ???????????? (Only LED and status, Beeper handled by main loop)
    PowerModule_CheckAndHandleAlarms();
}

// PMBus_Update_PSU removed

/**
 * @brief  ????????IO???????
 */
void PowerModule_PowerIO_Init(void)
{
    // GPIO Init is now handled by buzzer_control_init() in bsp_beeper.c
    // We do NOT re-initialize here to avoid conflicts.
    
    // Ensure GPIOD clock is enabled for PD7/PD6 access
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOD);
    
    // GPIO_Config_T gpioConfig;
    // ...
}

/**
 * @brief  ????????????????IO??????
 */
void PowerModule_UpdatePowerIOState(void)
{
    // Read Status using functions from bsp_beeper.c (which now owns the hardware config)
    PowerStatus sta = pwr_sig_detect();
    
    // Map PowerStatus to PowerModuleStatus_T
    // PowerStatus: PwrPresent (Bit0=Mod1, Bit1=Mod2), Pwrok (Bit0=Mod1, Bit1=Mod2)
    
    g_power_module_status.psu1_present = (sta.PwrPresent & MODULE1_PRSNT_MASK) ? 1 : 0;
    g_power_module_status.psu2_present = (sta.PwrPresent & MODULE2_PRSNT_MASK) ? 1 : 0;
    
    g_power_module_status.pwrok_a = (sta.Pwrok & MODULE1_MASK) ? 1 : 0; // Assuming Pwrok maps to A/B
    g_power_module_status.pwrok_b = (sta.Pwrok & MODULE2_MASK) ? 1 : 0;
    
    // Note: The collogue code logic for "Present" and "Ok" seems inverted or specific.
    // pwr_sig_detect: 
    // sta.PwrPresent = (~PinStatus >> 0) & PWR_STATUS_MASK;  (Inverted?)
    // sta.Pwrok = (PinStatus >> 2) & PWR_STATUS_MASK;
    
    // Let's trust pwr_sig_detect return values.
    
    // Also read specific pins if needed for other status
    g_power_module_status.pwrok_dout[0] = GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT1);
    g_power_module_status.pwrok_dout[1] = GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT2);
    g_power_module_status.pwrok_dout[2] = GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT3);
    g_power_module_status.pwrok_dout[3] = GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT4);
    
    // 5V OK
    g_power_module_status.pwrok_5v = GPIO_ReadInputBit(PWROK_5V_PORT, PWROK_5V_PIN);
    
    // Alert Pins
    g_power_module_status.psu1_alert = GPIO_ReadInputBit(PSU1_ALERT_PORT, PSU1_ALERT_PIN);
    g_power_module_status.psu2_alert = GPIO_ReadInputBit(PSU2_ALERT_PORT, PSU2_ALERT_PIN);
}

/**
 * @brief  ??????????????l??
 */
void PowerModule_CheckAndHandleAlarms(void)
{
    uint8_t has_alarm = 0;
    
    // 1. ????????????
    // PSU1 ????????: ????(1) AND Alert????(1) AND PWROK(1)
    g_power_module_status.psu1_normal = (g_power_module_status.psu1_present && 
                                         g_power_module_status.psu1_alert && 
                                         g_power_module_status.pwrok_a);
    
    // PSU2 ????????
    g_power_module_status.psu2_normal = (g_power_module_status.psu2_present && 
                                         g_power_module_status.psu2_alert && 
                                         g_power_module_status.pwrok_b);

    // DC Output Normal Condition: Check only ENABLED channels
    // If a channel is manually disabled, it shouldn't cause an alarm.
    bool dc_ok = true;
    
    // Check 5V first (Global)
    if (!g_power_module_status.pwrok_5v) {
        dc_ok = false;
    }
    
    // Check each channel
    for(int i=0; i<BOARD_MAX; i++) {
        // If channel is logically ON (Relay ON), it MUST have PWROK High
        if (g_power_module_status.board_status[i].state == POWER_OUTPUT_ON) {
             if (!g_power_module_status.pwrok_dout[i]) {
                 dc_ok = false;
                 break;
             }
        }
    }
    g_power_module_status.dc_output_normal = dc_ok ? 1 : 0;

    // 2. LED Control
    //if (g_power_module_status.psu1_normal) LED_On(BSP_LED1); else LED_Off(BSP_LED1);
   // if (g_power_module_status.psu2_normal) LED_On(BSP_LED2); else LED_Off(BSP_LED2);
    //if (g_power_module_status.dc_output_normal) LED_On(BSP_LED3); else LED_Off(BSP_LED3);

    // 3. ??????? (???????????????)
    // ???????????????????????????????????????????
    // 3. Power Alarm Logic
    // Only alarm for PSUs that are logically ENABLED.
    bool power_alarm = false;
    
    // Check PSU1
    if (g_psu_enabled[0]) {
        if (!g_power_module_status.psu1_normal) power_alarm = true;
    }
    
    // Check PSU2
    if (g_psu_enabled[1]) {
        if (!g_power_module_status.psu2_normal) power_alarm = true;
    }

    // Check DC Output (Only if at least one PSU is enabled)
    if (g_psu_enabled[0] || g_psu_enabled[1]) {
        if (!g_power_module_status.dc_output_normal) {
            power_alarm = true;
        }
    }
    
    // ????????????????????????(??????????????)???????
    // ???? PSU ?????????????????Dout ?????????
    bool smoke_alarm = false;
    if (g_smoke_sensor_status.aout_voltage > 0.1f) {
        if (g_smoke_sensor_status.dout_state == 1) {
            smoke_alarm = true;
        }
    }

    if (power_alarm || smoke_alarm) {
        has_alarm = 1;
    }

    // ????????????
    g_power_module_status.alarm_status = has_alarm;

    // --- Alarm Logging ---
    static bool prev_psu1_normal = true; 
    static bool prev_psu2_normal = true;
    static bool prev_dc_normal = true;
    static bool prev_smoke_alarm = false;
    static bool alarm_init = false;

    if (!alarm_init) {
        // Initialize with current state to avoid startup log flood
        prev_psu1_normal = g_power_module_status.psu1_normal;
        prev_psu2_normal = g_power_module_status.psu2_normal;
        prev_dc_normal = g_power_module_status.dc_output_normal;
        prev_smoke_alarm = smoke_alarm;
        alarm_init = true;
    } else {
        // PSU 1 Alarm Log
        if (g_psu_enabled[0] && g_power_module_status.psu1_normal != prev_psu1_normal) {
             if (!g_power_module_status.psu1_normal) {
                 Log_Write(LOG_LEVEL_ERROR, LOG_MODULE_SYSTEM, "Power Supply Unit 1 Failure Detected");
             } else {
                 Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Power Supply Unit 1 Recovered");
             }
             prev_psu1_normal = g_power_module_status.psu1_normal;
        }

        // PSU 2 Alarm Log
        if (g_psu_enabled[1] && g_power_module_status.psu2_normal != prev_psu2_normal) {
             if (!g_power_module_status.psu2_normal) {
                 Log_Write(LOG_LEVEL_ERROR, LOG_MODULE_SYSTEM, "Power Supply Unit 2 Failure Detected");
             } else {
                 Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Power Supply Unit 2 Recovered");
             }
             prev_psu2_normal = g_power_module_status.psu2_normal;
        }
        
        // DC Output Alarm Log
        if (g_power_module_status.dc_output_normal != prev_dc_normal) {
             if (!g_power_module_status.dc_output_normal) {
                 Log_Write(LOG_LEVEL_ERROR, LOG_MODULE_SYSTEM, "DC Output Failure (Channel Offline)");
             } else {
                 Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "DC Output Recovered");
             }
             prev_dc_normal = g_power_module_status.dc_output_normal;
        }
        
        // Smoke Alarm Log
        if (smoke_alarm != prev_smoke_alarm) {
             if (smoke_alarm) {
                 Log_Write(LOG_LEVEL_ERROR, LOG_MODULE_SYSTEM, "Smoke Alarm Triggered");
             } else {
                 Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Smoke Alarm Cleared");
             }
             prev_smoke_alarm = smoke_alarm;
        }
    }

    // 4. ?????????? -> MOVED TO MAIN LOOP via buzzer_SetByPwrState
    // However, if Smoke Alarm is active, we might want to force Beeper On.
    // The colleague's buzzer_SetByPwrState only checks power.
    // We should probably update buzzer_SetByPwrState to check Smoke too, 
    // OR just handle Smoke alarm here if buzzer_SetByPwrState is not running (but it is).
    // For now, let's assume colleague's code handles Power alarm, and we rely on `Beeper_Beep` for others?
    // But `buzzer_SetByPwrState` turns OFF beeper if power is OK.
    
    // Workaround: If Smoke Alarm, Force Beeper On via Beeper_On(), 
    // and hope buzzer_SetByPwrState doesn't turn it off immediately.
    // Actually, we modified buzzer_SetByPwrState to NOT turn off if `beeper_beeping` is true.
    // So we can use Beeper_Beep(255) for smoke alarm?
    
    // Commented out to prevent intermittent beeping and allow Mute logic to work
    // if (smoke_alarm) {
    //     // Force beep if not already beeping
    //     // Note: Beeper_IsMuted is not available in read-only bsp_beeper.c
    //     // We assume if mute_flag is set in main loop, we shouldn't beep, but we can't read it.
    //     // For now, just call Beeper_On() and let main loop handle mute if needed, 
    //     // OR rely on user to press mute button which sets mute_flag in bsp_beeper.c (but we can't read it).
    //     
    //     // Since we cannot read mute state, we just beep.
    //     // Beeper_On(); 
    // }
}

/**
 * @brief  ????????? (12V)
 */
float PowerModule_GetInputVoltage(void)
{
    // Return cached value from I2C sync
    return g_power_module_status.input_voltage;
}

/**
 * @brief  ????????????
 */
float PowerModule_GetBoardVoltage(uint8_t board_index)
{
    if (board_index < BOARD_MAX) {
        return g_power_module_status.board_status[board_index].voltage;
    }
    return 0.0f;
}

/**
 * @brief  ???????? (ATX PSON#)
 * @param  enable: true=ON(Low), false=OFF(High)
 */
void PowerModule_SetPSUState(bool enable)
{
    // ????????????? PSU???????????????????????
    // ?????
    // 1. ?? PSU ?????????????????????????????????????/?????
    // 2. ??? PSU ???????????????????????????????????
    
    if (enable) {
        // ?????? (ATX PSON# ????)
        GPIO_ResetBit(ATX_PSONN_GPIO_PORT, ATX_PSONN_PIN);
        
        // Restore Relay Physical State (Logic state remains unchanged)
        // User Requirement: Open All Relays when PSU turns ON
        Relay_All_On();
        Relay_Apply_State(true);
        
        // User Requirement: Restart timer for all channels when PSU turns ON
        for (int i = 0; i < BOARD_MAX; i++) {
             // Explicitly set upper-level state to ON and reset timer
             g_power_module_status.board_status[i].state = POWER_OUTPUT_ON;
             g_power_module_status.board_status[i].last_on_time = sys_get_tick_ms();
        }
    } else {
        // ??????? (ATX PSON# ????)
        GPIO_SetBit(ATX_PSONN_GPIO_PORT, ATX_PSONN_PIN);
        
        // Turn off relays physically
        Relay_Apply_State(false);
        
        // User Requirement: Sync upper-level state to OFF
        for (int i = 0; i < BOARD_MAX; i++) {
             g_power_module_status.board_status[i].state = POWER_OUTPUT_OFF;
        }
    }
    
    // Mark config as dirty to save new state
    g_psu_config_dirty = true;
    g_last_psu_config_change_time = sys_get_tick_ms();
}

/**
 * @brief  ?????????
 */
bool PowerModule_GetPSUState(void)
{
    // ????????????? (0=ON, 1=OFF)
    return (GPIO_ReadOutputBit(ATX_PSONN_GPIO_PORT, ATX_PSONN_PIN) == RESET);
}

/**
 * @brief  ??????????? (??? Relay 1-4)
 */
void PowerModule_Board_Enable(BoardId_T board_id)
{
    if (board_id < BOARD_MAX) {
        // ???????: ???PSU?????
        if (!PowerModule_GetPSUState()) {
            // printf("[Power] Warning: Enable Board %d while PSU is OFF\n", board_id);
            // return; // Removed to allow manual relay control even if PSU is OFF
        }
        
        // ???????: ???12V?????? (??????? 10V)
        // Only warn if voltage is valid (>0) but low. If 0, it might be sensor error or startup.
        // Also check if PSU is ON. If PSU is OFF, input voltage will be 0, so no need to warn about Low Voltage.
        static uint32_t last_warn_time = 0;
        if (PowerModule_GetPSUState() && g_power_module_status.input_voltage > 0.1f && g_power_module_status.input_voltage < 10.0f) {
             if (sys_get_tick_ms() - last_warn_time > 5000) {
                 // printf("[Power] Warning: Board %d Input Low (%.2fV)\n", board_id, g_power_module_status.input_voltage);
                 last_warn_time = sys_get_tick_ms();
             }
        }

        Relay_On((Relay_Id_T)board_id);
        g_power_module_status.board_status[board_id].state = POWER_OUTPUT_ON;
        g_power_module_status.board_status[board_id].last_on_time = sys_get_tick_ms();
        
        g_psu_config_dirty = true;
        g_last_psu_config_change_time = sys_get_tick_ms();
    }
}

/**
 * @brief  ??????????
 */
void PowerModule_Board_Disable(BoardId_T board_id)
{
    if (board_id < BOARD_MAX) {
        Relay_Off((Relay_Id_T)board_id);
        g_power_module_status.board_status[board_id].state = POWER_OUTPUT_OFF;
        
        g_psu_config_dirty = true;
        g_last_psu_config_change_time = sys_get_tick_ms();
        // ???????????????????????
    }
}

/**
 * @brief  ?????????????
 */
void PowerModule_Board_Toggle(BoardId_T board_id)
{
    if (board_id < BOARD_MAX) {
        Relay_Toggle((Relay_Id_T)board_id);
        if (g_power_module_status.board_status[board_id].state == POWER_OUTPUT_ON) {
             g_power_module_status.board_status[board_id].state = POWER_OUTPUT_OFF;
        } else {
             g_power_module_status.board_status[board_id].state = POWER_OUTPUT_ON;
             g_power_module_status.board_status[board_id].last_on_time = sys_get_tick_ms();
        }
        
        g_psu_config_dirty = true;
        g_last_psu_config_change_time = sys_get_tick_ms();
    }
}

/**
 * @brief  ????????????
 */
PowerOutputState_T PowerModule_Board_GetState(BoardId_T board_id)
{
    if (board_id < BOARD_MAX) {
        return g_power_module_status.board_status[board_id].state;
    }
    return POWER_OUTPUT_OFF;
}

void PowerModule_SetPSUEnabled(uint8_t psu_id, bool enable)
{
    if (psu_id < 2) {
        if (g_psu_enabled[psu_id] != enable) {
            g_psu_enabled[psu_id] = enable;
            
            if (enable) {
                g_power_module_status.psu_data[psu_id].last_on_time = sys_get_tick_ms();
            }

            // --- Independent PMBus Control Logic ---
            // Send PMBus OPERATION command to the specific PSU to Turn ON/OFF
            // 0x58 for PSU1, 0x59 for PSU2
            uint8_t target_addr = (psu_id == 0) ? PSU1_ADDRESS : PSU2_ADDRESS;
            
            // Try to send command via I2C PMBus
            // Note: PMBus_SetOperation was removed but we need it here.
            // Let's implement a simple direct I2C write here or re-enable the helper.
            // Command 0x01 (OPERATION): 0x80 = ON, 0x00 = OFF (Immediate)
            
            // Re-implementing simple I2C write for PMBus OPERATION with TIMEOUT PROTECTION
            bool i2c_err = false;
            uint32_t timeout = 0xFFF;
            
            // Step 1: Wait Busy
            while(I2C_ReadStatusFlag(I2C1, I2C_FLAG_BUSBSY)) { if((timeout--)==0) { i2c_err=true; break; } }
            
            if (!i2c_err) {
                I2C_EnableGenerateStart(I2C1);
                timeout = 0xFFF;
                while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_MODE_SELECT)) { if((timeout--)==0) { i2c_err=true; break; } }
            }
            
            if (!i2c_err) {
                I2C_Tx7BitAddress(I2C1, target_addr << 1, I2C_DIRECTION_TX);
                
                timeout = 0xFFF;
                while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) { 
                    if((timeout--)==0) { 
                        I2C_EnableGenerateStop(I2C1); 
                        i2c_err = true; // Address NACK or Timeout
                        break; 
                    }
                }
            }
            
            if (!i2c_err) {
                // Send CMD 0x01 (OPERATION)
                I2C_TxData(I2C1, 0x01); 
                timeout = 0xFFF;
                while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
                    if((timeout--)==0) { I2C_EnableGenerateStop(I2C1); i2c_err=true; break; }
                }
            }
            
            if (!i2c_err) {
                // Send Data (0x80 ON, 0x00 OFF)
                I2C_TxData(I2C1, enable ? 0x80 : 0x00);
                timeout = 0xFFF;
                while(!I2C_ReadEventStatus(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
                    if((timeout--)==0) { I2C_EnableGenerateStop(I2C1); i2c_err=true; break; }
                }
            }
            
            I2C_EnableGenerateStop(I2C1);
            
            if (i2c_err) {
                 // printf("[Power] PMBus I2C Error (Addr 0x%02X)\n", target_addr);
            } else {
                 // printf("[Power] PMBus Addr 0x%02X Operation -> %s\n", target_addr, enable ? "ON" : "OFF");
            }

            // Apply to hardware (ATX PSON#)
            // Dual PSU Logic: If ANY PSU is enabled, turn ON ATX.
            bool should_be_on = g_psu_enabled[0] || g_psu_enabled[1];
            bool currently_on = PowerModule_GetPSUState();

            if (should_be_on != currently_on) {
                PowerModule_SetPSUState(should_be_on);
            }

            // User Requirement: When manually disabling PSU (all off), reset relay states to ALL OFF.
            // This ensures next start (manual or reboot) defaults to ALL OFF.
            if (!should_be_on) {
                Relay_All_Off(); // Sets logic state to OFF.
            }
            
            // Mark config as dirty and set timestamp
            g_psu_config_dirty = true;
            g_last_psu_config_change_time = sys_get_tick_ms();
            // printf("[Power] PSU%d State Changed to %d. Config Save Pending...\n", psu_id + 1, enable);
            Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "PSU%d State: %d (Save Pending)", psu_id + 1, enable);
        }
    }
}

void PowerModule_SetConfigDirty(void)
{
    g_psu_config_dirty = true;
    g_last_psu_config_change_time = sys_get_tick_ms();
}

void PowerModule_Task(void)
{
    if (g_psu_config_dirty) {
        if ((sys_get_tick_ms() - g_last_psu_config_change_time) >= PSU_CONFIG_SAVE_DELAY_MS) {
            // Save to Flash
            Flash_PSU_Config_T config;
            config.psu1_enabled = g_psu_enabled[0];
            config.psu2_enabled = g_psu_enabled[1];
            
            config.relay_states = 0;
            for(int i=0; i<4; i++) {
                // Use Relay_GetState to capture the actual hardware state,
                // regardless of whether it was set via PowerModule_Board_Enable or Relay_On.
                if(Relay_GetState((Relay_Id_T)i) == RELAY_ON) {
                    config.relay_states |= (1 << i);
                }
            }

            // printf("[Power] Saving Config: Relays=0x%02X, PSU1=%d, PSU2=%d\n", 
            //        config.relay_states, config.psu1_enabled, config.psu2_enabled);

            if (Flash_Write_PSU_Config(&config)) {
                // printf("[Power] Saved PSU Config to Flash (Deferred)\n");
                // Log_Write(LOG_LEVEL_INFO, LOG_MODULE_SYSTEM, "Saved PSU Config (Relays:0x%02X)", config.relay_states); // Reduced noise
            } else {
                // printf("[Power] Failed to save PSU Config to Flash\n");
                Log_Write(LOG_LEVEL_ERROR, LOG_MODULE_SYSTEM, "Failed to Save PSU Config");
            }
            g_psu_config_dirty = false;
        }
    }
}

bool PowerModule_GetPSUEnabled(uint8_t psu_id)
{
    if (psu_id < 2) {
        return g_psu_enabled[psu_id];
    }
    return false;
}

bool PowerModule_IsAny12VOutput(void)
{
    for (int i = 0; i < BOARD_MAX; i++) {
        // Threshold 8.0V to indicate 12V output presence
        if (g_power_module_status.board_status[i].voltage > 8.0f) {
            return true;
        }
    }
    return false;
}




