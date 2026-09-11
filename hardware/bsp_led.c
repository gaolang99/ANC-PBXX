#include "bsp_led.h"

// LED GPIO???/????/????????????????????
GPIO_T* LED_GPIO_PORT[LED_NUM] = {BSP_LED1_GPIO_PORT, BSP_LED2_GPIO_PORT, BSP_LED3_GPIO_PORT};
const uint16_t LED_PIN[LED_NUM] = {BSP_LED1_PIN, BSP_LED2_PIN, BSP_LED3_PIN};
const uint32_t LED_GPIO_CLK[LED_NUM] = {BSP_LED1_GPIO_CLK, BSP_LED2_GPIO_CLK, BSP_LED3_GPIO_CLK};

// LED??????
static LED_State_T led_states[LED_NUM] = {LED_OFF};

/**
 * @brief  ?????LED????GPIO?????PE7/PE8/PE9??
 */
void LED_Init(void)
{
    GPIO_Config_T gpio_config;
    uint8_t i;
    
    // ???????LED????GPIO?????
    for (i = 0; i < LED_NUM; i++) {
        RCM_EnableAPB2PeriphClock(LED_GPIO_CLK[i]);
    }
    
    // ????GPIO????????
    gpio_config.mode = GPIO_MODE_OUT_PP;
    gpio_config.speed = GPIO_SPEED_50MHz;
    
    // ?????????LED????GPIO????????????
    for (i = 0; i < LED_NUM; i++) {
        gpio_config.pin = LED_PIN[i];
        GPIO_Config(LED_GPIO_PORT[i], &gpio_config);
        
        GPIO_ResetBit(LED_GPIO_PORT[i], LED_PIN[i]);
        led_states[i] = LED_OFF;
    }
}

/**
 * @brief  ?????LED
 */
void LED_On(LED_Id_T led_id)
{
    if (led_id < LED_NUM) {
        GPIO_SetBit(LED_GPIO_PORT[led_id], LED_PIN[led_id]);
        led_states[led_id] = LED_ON;
    }
}

/**
 * @brief  ??????LED
 */
void LED_Off(LED_Id_T led_id)
{
    if (led_id < LED_NUM) {
        GPIO_ResetBit(LED_GPIO_PORT[led_id], LED_PIN[led_id]);
        led_states[led_id] = LED_OFF;
    }
}

/**
 * @brief  ?????LED??
 */
void LED_Toggle(LED_Id_T led_id)
{
    if (led_id < LED_NUM) {
        (led_states[led_id] == LED_ON) ? LED_Off(led_id) : LED_On(led_id);
    }
}

/**
 * @brief  ??????LED????
 */
LED_State_T LED_GetState(LED_Id_T led_id)
{
    return (led_id < LED_NUM) ? led_states[led_id] : LED_OFF;
}

/**
 * @brief  ??????LED
 */
void LED_All_On(void)
{
    for (uint8_t i = 0; i < LED_NUM; i++) {
        LED_On(i);
    }
}

/**
 * @brief  ???????LED
 */
void LED_All_Off(void)
{
    for (uint8_t i = 0; i < LED_NUM; i++) {
        LED_Off(i);
    }
}

/**
 * @brief  LED?? (??????)
 *         state 0: OFF
 *         state 1: ON
 */
void LED_SetState(LED_State_T state)
{
    if (state == LED_ON) {
        LED_All_On();
    } else {
        LED_All_Off();
    }
}

/**
 * @brief  LED Control Task (Should be called periodically, e.g., every 50ms)
 * @param  system_normal: true = Blink 1Hz, false = Steady Off (Fault)
 * @param  link_up: true = Link UP, false = Link DOWN
 * @param  activity: true = Data TX/RX (Toggle LED), false = Steady (if Link UP)
 * @param  output_12v_ok: true = LED3 ON, false = LED3 OFF
 */
void LED_Task(bool system_normal, bool link_up, bool activity, bool output_12v_ok)
{
    static uint8_t sys_led_tick = 0;
    
    // --- LED1: System Indicator (R1) ---
    // Normal: 1Hz Blink (500ms ON, 500ms OFF)
    // Fault: Not blink (Assume OFF)
    if (system_normal) {
        sys_led_tick++;
        // Assuming 50ms interval: 10 ticks = 500ms
        if (sys_led_tick >= 10) {
            sys_led_tick = 0;
            LED_Toggle(BSP_LED1);
        }
    } else {
        // Fault: Not blink
        LED_Off(BSP_LED1);
        sys_led_tick = 0; // Reset tick so it restarts cleanly if normal again
    }
    
    // --- LED2: Link Status (R2) ---
    // Flash: Data Activity
    // Steady ON: Link Normal (No Activity)
    // OFF: No Link
    if (link_up) {
        if (activity) {
            // Activity detected: Toggle
            LED_Toggle(BSP_LED2);
        } else {
            // No Activity: Steady ON
            LED_On(BSP_LED2);
        }
    } else {
        // Link Down: OFF
        LED_Off(BSP_LED2);
    }
    
    // --- LED3: 12V Output Indicator (R3) ---
    // ON: Output Normal
    // OFF: Output Abnormal
    if (output_12v_ok) {
        LED_On(BSP_LED3);
    } else {
        LED_Off(BSP_LED3);
    }
}
