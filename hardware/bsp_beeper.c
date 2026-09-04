#include "bsp_beeper.h"
#include "sys_arch.h" 
#include "bsp_power_module.h" // Needed for PWROK_5V_PIN definitions
#include "bsp_smoke_sensor.h"
#include "log_system.h"

static Beeper_State_T beeper_state = BEEPER_OFF;


// 非阻塞蜂鸣器控制变量
static uint32_t beeper_start_time = 0;
static uint32_t beeper_last_toggle_time = 0;
static uint8_t beeper_beep_count = 0;
static uint8_t beeper_current_beep = 0;
static bool beeper_beeping = false;
static bool beeper_alarm_active = false;

// 复位状态变量
static uint32_t reset_btn_start_time = 0;
static bool reset_btn_pressed = false;
extern void Restore_Network_Defaults(void); // Defined in http_server.c

void Beeper_Update(void)
{
    uint32_t now = sys_get_tick_ms();

    // --- Network Reset Logic (Long Press Mute Key 10s) ---
    // BEEPER_RESET_PIN (PA11) is Active Low (Input Floating)
    if (GPIO_ReadInputBit(BEEPER_RESET_GPIO_PORT, BEEPER_RESET_PIN) == 0) {
        if (!reset_btn_pressed) {
            reset_btn_pressed = true;
            reset_btn_start_time = now;
        } else {
            // Check for 10 seconds (10000 ms)
            if (now - reset_btn_start_time > 10000) {
                // Trigger Reset
                // Beep 3 times to indicate success
                Beeper_Beep(3);
                
                // Log the event before reset
                Log_Write(LOG_LEVEL_WARN, LOG_MODULE_SYSTEM, "System Reset to Defaults (192.168.1.199) by User");
                
                // Execute Reset
                Restore_Network_Defaults();
                
                // Reset state
                reset_btn_pressed = false;
                reset_btn_start_time = now;
            }
        }
    } else {
        reset_btn_pressed = false;
    }
    // -----------------------------------------------------

    // 优先处理报警模式 (200ms翻转)
    if (beeper_alarm_active) {
        if (now - beeper_last_toggle_time >= 200) {
            Beeper_Toggle();
            beeper_last_toggle_time = now;
        }
        return;
    }

    // 处理有限次数蜂鸣
    if (beeper_beeping) {
        if (now - beeper_start_time >= 200) {
            beeper_start_time = now;
            if (beeper_state == BEEPER_ON) {
                Beeper_Off();
                beeper_current_beep++;
                if (beeper_current_beep >= beeper_beep_count) {
                    beeper_beeping = false;
                }
            } else {
                Beeper_On();
            }
        }
        return;
    }
    
    // 默认关闭
    if (beeper_state == BEEPER_ON) {
        Beeper_Off();
    }
}

void Beeper_Init(void)
{
    GPIO_Config_T gpio_config;

    RCM_EnableAPB2PeriphClock(BEEPER_GPIO_CLK);
    RCM_EnableAPB2PeriphClock(BEEPER_RESET_GPIO_CLK);

    gpio_config.mode = GPIO_MODE_OUT_PP;
    gpio_config.speed = GPIO_SPEED_50MHz;
    gpio_config.pin = BEEPER_PIN;
    GPIO_Config(BEEPER_GPIO_PORT, &gpio_config);
    
    GPIO_ResetBit(BEEPER_GPIO_PORT, BEEPER_PIN);
    beeper_state = BEEPER_OFF;

    gpio_config.mode = GPIO_MODE_IN_PU; // Enable Pull-Up to prevent floating
    gpio_config.pin = BEEPER_RESET_PIN;
    gpio_config.speed = GPIO_SPEED_50MHz; 
    GPIO_Config(BEEPER_RESET_GPIO_PORT, &gpio_config);

}

void PWROPEN_Init(void)
{
    GPIO_Config_T gpioConfig;

    RCM_EnableAPB2PeriphClock(PWROK_ATX_PSONn_PORT_CLK);

    gpioConfig.pin = ATX_PSONn;
    gpioConfig.mode = GPIO_MODE_OUT_PP; 
    gpioConfig.speed = GPIO_SPEED_50MHz;      

    GPIO_Config(PWROK_ATX_PSONn_PORT, &gpioConfig);
    //GPIO_ResetBit GPIO_SetBit
    GPIO_SetBit(PWROK_ATX_PSONn_PORT,ATX_PSONn);
}

void PWROK_Init(void)
{
    GPIO_Config_T gpioConfig;

    RCM_EnableAPB2PeriphClock(PWROK_PORT_CLK);

    gpioConfig.pin = PWROK_A | PWROK_B;
    gpioConfig.mode = GPIO_MODE_IN_FLOATING; 
    gpioConfig.speed = GPIO_SPEED_2MHz;      

    GPIO_Config(PWROK_PORT, &gpioConfig);
}

void PWRDOUT_Init(void)
{
    GPIO_Config_T gpioConfig;

    RCM_EnableAPB2PeriphClock(PWROK_DOUT_PORT_CLK);

    gpioConfig.pin = PWROK_DOUT3 | PWROK_DOUT4 | PWROK_DOUT2 | PWROK_DOUT1;
    gpioConfig.mode = GPIO_MODE_IN_FLOATING;   
    gpioConfig.speed = GPIO_SPEED_50MHz; 

    GPIO_Config(PWROK_DOUT_PORT, &gpioConfig);
}

void PWR_PRSNTN_Init(void)
{
    GPIO_Config_T gpioConfig;

    RCM_EnableAPB2PeriphClock(PWR_PRSNTN_PORT_CLK);

    gpioConfig.pin = PWR_PRSNTN1 | PWR_PRSNTN2;
    gpioConfig.mode = GPIO_MODE_IN_FLOATING;   // Configure as input with pull-up for presence detection
    gpioConfig.speed = GPIO_SPEED_50MHz; 

    GPIO_Config(PWR_PRSNTN_PORT, &gpioConfig);
    
    // No need to set output value for input pins
    // The pull-up will ensure proper logic levels when PSU is not present
}

void PWROK_5V_Init(void)
{
    GPIO_Config_T gpioConfig;

    RCM_EnableAPB2PeriphClock(PWROK_5V_CLK);

    gpioConfig.pin = PWROK_5V_PIN;
    gpioConfig.mode = GPIO_MODE_IN_FLOATING; // Or GPIO_MODE_IN_PU depending on circuit, assume floating/external pull
    gpioConfig.speed = GPIO_SPEED_50MHz;

    GPIO_Config(PWROK_5V_PORT, &gpioConfig);
}


void Beeper_On(void)
{
    GPIO_SetBit(BEEPER_GPIO_PORT, BEEPER_PIN);
    beeper_state = BEEPER_ON;
}

void Beeper_Off(void)
{
    GPIO_ResetBit(BEEPER_GPIO_PORT, BEEPER_PIN);
    beeper_state = BEEPER_OFF;
}

void Beeper_Toggle(void)
{
    if (beeper_state == BEEPER_ON) {
        Beeper_Off();
    } else {
        Beeper_On();
    }
}


void Beeper_Beep(uint8_t times)
{
    if (times == 0) return;
    
    beeper_beep_count = times;
    beeper_current_beep = 0;
    beeper_beeping = true;
    beeper_start_time = sys_get_tick_ms();  

    Beeper_On();
}

/**
 * @brief  根据继电器ID蜂鸣
 * @param  relay_id: 继电器编号
 * @retval 无
 */
void Beeper_BeepForRelay(uint8_t relay_id)
{
    // 根据继电器ID蜂鸣对应次数
    // 继电器ID从0开始，所以需要+1
    Beeper_Beep(relay_id + 1);
}

void buzzer_control_init(void)
{
    Beeper_Init();
    PWROPEN_Init();
    PWROK_Init();
    PWRDOUT_Init();
    PWR_PRSNTN_Init();
    PWROK_5V_Init();
}

PowerStatus pwr_sig_detect(void)
{
    PowerStatus sta = {0,0};
    uint8_t pwrok_a = GPIO_ReadInputBit(PWROK_PORT,PWROK_A);
    uint8_t pwrok_b = GPIO_ReadInputBit(PWROK_PORT,PWROK_B);
    
    // PWROK pins are High Active for "In Position" (Present)
    sta.PwrPresent = (pwrok_a << 0) | (pwrok_b << 1);
    
    // PWROK pins are also High Active for "Power OK"
    sta.Pwrok = (pwrok_a << 0) | (pwrok_b << 1);

    return sta;
}

uint32_t g_muted_alarms = 0;
uint32_t g_active_alarms = 0;
bool g_mute_request = false;
uint8_t mute_key = 0;

// Alarm Bitmasks
#define ALARM_PSU1_ABSENT   (1<<0)
#define ALARM_PSU2_ABSENT   (1<<1)
#define ALARM_PSU1_FAIL     (1<<2)
#define ALARM_PSU2_FAIL     (1<<3)
#define ALARM_5V_FAIL       (1<<4)
#define ALARM_DOUT_FAIL     (1<<5)
#define ALARM_SMOKE         (1<<6)

uint32_t Beeper_GetActiveAlarmMask(void) {
    return g_active_alarms;
}

uint32_t Beeper_GetMutedAlarmMask(void) {
    return g_muted_alarms;
}

void Beeper_SetMute(bool mute)
{
    if (mute) {
        g_mute_request = true;
        // Immediate feedback
        beeper_alarm_active = false;
        Beeper_Off();
    } else {
        g_muted_alarms = 0;
        g_mute_request = false;
    }
}

void buzzer_SetByPwrState(PowerStatus sta)
{
    uint8_t is_psu1_absent = 0;
    uint8_t is_psu2_absent = 0;
    uint8_t is_psu1_not_ok = 0;
    uint8_t is_psu2_not_ok = 0;
    uint8_t is_dout_fail = 0;
    uint8_t is_5v_fail = 0;

    bool psu1_enabled = PowerModule_GetPSUEnabled(0);
    bool psu2_enabled = PowerModule_GetPSUEnabled(1);
    bool global_psu_on = PowerModule_GetPSUState();

    // Debounce counters
    static uint8_t psu1_absent_cnt = 0;
    static uint8_t psu2_absent_cnt = 0;
    static uint8_t psu1_not_ok_cnt = 0;
    static uint8_t psu2_not_ok_cnt = 0;
    static uint8_t dout_fail_cnt = 0;
    static uint8_t v5_fail_cnt = 0;

    // Hysteresis States (to prevent mute clearing on noise)
    static bool psu1_absent_state = false;
    static bool psu2_absent_state = false;
    static bool psu1_not_ok_state = false;
    static bool psu2_not_ok_state = false;
    static bool dout_fail_state = false;
    static bool v5_fail_state = false;

    // 1. Detect PSU Absent (with hysteresis)_
    if (!(sta.PwrPresent & MODULE1_PRSNT_MASK) && psu1_enabled) {
        if (psu1_absent_cnt < 5) psu1_absent_cnt++;
    } else {
        if (psu1_absent_cnt > 0) psu1_absent_cnt--;
    }
    if (psu1_absent_cnt >= 3) psu1_absent_state = true;
    else if (psu1_absent_cnt == 0) psu1_absent_state = false;
    is_psu1_absent = psu1_absent_state;

    if (!(sta.PwrPresent & MODULE2_PRSNT_MASK) && psu2_enabled) {
        if (psu2_absent_cnt < 5) psu2_absent_cnt++;
    } else {
        if (psu2_absent_cnt > 0) psu2_absent_cnt--;
    }
    if (psu2_absent_cnt >= 3) psu2_absent_state = true;
    else if (psu2_absent_cnt == 0) psu2_absent_state = false;
    is_psu2_absent = psu2_absent_state;

    // 2. Detect PSU Fail (Present but PWROK Low)
    //if ((sta.PwrPresent & MODULE1_PRSNT_MASK) && !(sta.Pwrok & MODULE1_MASK) && psu1_enabled) {
		if (!(sta.Pwrok & MODULE1_MASK) && psu1_enabled) {
        if (psu1_not_ok_cnt < 5) psu1_not_ok_cnt++;
    } else {
        if (psu1_not_ok_cnt > 0) psu1_not_ok_cnt--;
    }
    if (psu1_not_ok_cnt >= 3) psu1_not_ok_state = true;
    else if (psu1_not_ok_cnt == 0) psu1_not_ok_state = false;
    is_psu1_not_ok = psu1_not_ok_state;

    //if ((sta.PwrPresent & MODULE2_PRSNT_MASK) && !(sta.Pwrok & MODULE2_MASK) && psu2_enabled) {
		if (!(sta.Pwrok & MODULE2_MASK) && psu2_enabled) {
        if (psu2_not_ok_cnt < 5) psu2_not_ok_cnt++;
    } else {
        if (psu2_not_ok_cnt > 0) psu2_not_ok_cnt--;
    }
    if (psu2_not_ok_cnt >= 3) psu2_not_ok_state = true;
    else if (psu2_not_ok_cnt == 0) psu2_not_ok_state = false;
    is_psu2_not_ok = psu2_not_ok_state;


    // 3. Detect Output Fail
    // Check DOUT pins
    bool dout_error = false;
    if( (GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT1) == RESET && g_power_module_status.board_status[BOARD_1].state == POWER_OUTPUT_ON ) || 
        (GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT2) == RESET && g_power_module_status.board_status[BOARD_2].state == POWER_OUTPUT_ON ) ||
        (GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT3) == RESET && g_power_module_status.board_status[BOARD_3].state == POWER_OUTPUT_ON ) ||
        (GPIO_ReadInputBit(PWROK_DOUT_PORT, PWROK_DOUT4) == RESET && g_power_module_status.board_status[BOARD_4].state == POWER_OUTPUT_ON ) )
    {
        dout_error = true;
    }
    
    if (dout_error) {
        if (dout_fail_cnt < 5) dout_fail_cnt++;
    } else {
        if (dout_fail_cnt > 0) dout_fail_cnt--;
    }
    if (dout_fail_cnt >= 3) dout_fail_state = true;
    else if (dout_fail_cnt == 0) dout_fail_state = false;
    is_dout_fail = dout_fail_state;
    
    // 4. Detect 5V Fail
    if (GPIO_ReadInputBit(PWROK_5V_PORT, PWROK_5V_PIN) == RESET && global_psu_on) {
        if (v5_fail_cnt < 5) v5_fail_cnt++;
    } else {
        if (v5_fail_cnt > 0) v5_fail_cnt--;
    }
    if (v5_fail_cnt >= 3) v5_fail_state = true;
    else if (v5_fail_cnt == 0) v5_fail_state = false;
    is_5v_fail = v5_fail_state;

    // --- Logic Fix: Prevent "All Absent but System Normal" paradox ---
    // If we detect valid output voltage (>8V), it is physically impossible for ALL PSUs to be absent.
    // This handles cases where GPIO presence signals are faulty/inverted or PSUs are powered but "Absent" logic is triggered.
    float max_voltage = 0.0f;
    for(int i=0; i<4; i++) {
        if (g_power_module_status.board_status[i].voltage > max_voltage) {
            max_voltage = g_power_module_status.board_status[i].voltage;
        }
    }
    
    if (max_voltage > 8.0f) {
        // If system has power, and we are about to report ALL enabled PSUs as absent, suppress it.
        // Case 1: Both Enabled, Both Absent -> Suppress Both
        if (psu1_enabled && psu2_enabled && is_psu1_absent && is_psu2_absent) {
            is_psu1_absent = 0;
            is_psu2_absent = 0;
        }
        // Case 2: Single Enabled, It is Absent -> Suppress It
        else if (psu1_enabled && !psu2_enabled && is_psu1_absent) {
            is_psu1_absent = 0;
        }
        else if (!psu1_enabled && psu2_enabled && is_psu2_absent) {
            is_psu2_absent = 0;
        }
    }

    // 5. Check Smoke Sensor Status
    uint8_t is_smoke_alarm = 0;
    if (g_smoke_sensor_status.dout_state == 1 || g_smoke_sensor_status.aout_voltage > 4.8f) {
        is_smoke_alarm = 1;
    }

    // --- Alarm Logic Construction ---
    uint32_t current_alarms = 0;
    
    if (is_psu1_absent) current_alarms |= ALARM_PSU1_ABSENT;
    if (is_psu2_absent) current_alarms |= ALARM_PSU2_ABSENT;
    if (is_psu1_not_ok) current_alarms |= ALARM_PSU1_FAIL;
    if (is_psu2_not_ok) current_alarms |= ALARM_PSU2_FAIL;
    if (is_5v_fail)     current_alarms |= ALARM_5V_FAIL;
    if (is_dout_fail)   current_alarms |= ALARM_DOUT_FAIL;
    if (is_smoke_alarm) current_alarms |= ALARM_SMOKE;

    // Update global active alarms (before masking)
    g_active_alarms = current_alarms;

    // --- Mute Logic ---
    mute_key = GPIO_ReadInputBit(BEEPER_RESET_GPIO_PORT, BEEPER_RESET_PIN);
    
    // If button pressed (active low) OR web request
    if (!mute_key || g_mute_request)
    {
        g_muted_alarms |= current_alarms; // Add current alarms to muted mask
        g_mute_request = false; // Clear web request (button is handled continuously)
    }
    
    // Auto-reset muted alarms if the condition clears
    // This ensures that if the alarm goes away and comes back, it is treated as new.
    g_muted_alarms &= current_alarms;

    // Calculate active (unmuted) alarms
    uint32_t active_alarms = current_alarms & ~g_muted_alarms;

    if (active_alarms != 0)
    {
        beeper_alarm_active = true;
    } else
    {
        beeper_alarm_active = false;
    }

}