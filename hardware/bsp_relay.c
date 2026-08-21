#include "bsp_relay.h"
#include "bsp_beeper.h"
#include "bsp_power_module.h"

// 继电器控制GPIO端口数组
GPIO_T* RELAY_GPIO_PORT[RELAY_NUM] = {RELAY1_GPIO_PORT, RELAY2_GPIO_PORT, RELAY3_GPIO_PORT, RELAY4_GPIO_PORT};
const uint16_t RELAY_PIN[RELAY_NUM] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};
const uint32_t RELAY_GPIO_CLK[RELAY_NUM] = {RELAY1_GPIO_CLK, RELAY2_GPIO_CLK, RELAY3_GPIO_CLK, RELAY4_GPIO_CLK};



// 继电器状态数组
static Relay_State_T relay_states[RELAY_NUM] = {RELAY_OFF};

/**
 * @brief  初始化继电器控制GPIO和状态检测GPIO
 * @param  无
 * @retval 无
 */
void Relay_Init(void)
{
    GPIO_Config_T gpio_config;
    uint8_t i;
    
    // 使能所有继电器控制GPIO时钟
    for (i = 0; i < RELAY_NUM; i++) {
        RCM_EnableAPB2PeriphClock(RELAY_GPIO_CLK[i]);
    }
    
    // 配置继电器控制GPIO为推挽输出
    gpio_config.mode = GPIO_MODE_OUT_PP;
    gpio_config.speed = GPIO_SPEED_50MHz;
    
    // 初始化所有继电器控制GPIO（初始状态为关闭）
    for (i = 0; i < RELAY_NUM; i++) {
        gpio_config.pin = RELAY_PIN[i];
        GPIO_Config(RELAY_GPIO_PORT[i], &gpio_config);
        GPIO_ResetBit(RELAY_GPIO_PORT[i], RELAY_PIN[i]);  // 初始关
        relay_states[i] = RELAY_OFF;
    }

    // 初始化 ATX_PSONn (MCU_PE10) - 拉低以开启电源
    RCM_EnableAPB2PeriphClock(ATX_PSONN_GPIO_CLK);
    gpio_config.pin = ATX_PSONN_PIN;
    gpio_config.mode = GPIO_MODE_OUT_PP;
    gpio_config.speed = GPIO_SPEED_50MHz;
    GPIO_Config(ATX_PSONN_GPIO_PORT, &gpio_config);
    GPIO_ResetBit(ATX_PSONN_GPIO_PORT, ATX_PSONN_PIN); // 拉低开启电源
}

/**
 * @brief  打开指定继电器 - 优化版本，移除蜂鸣器调用
 * @param  relay_id: 继电器编号
 * @retval 无
 */
void Relay_On(Relay_Id_T relay_id)
{
    if (relay_id < RELAY_NUM) {
        relay_states[relay_id] = RELAY_ON;
        
        // 只有当PSU开启时，才实际闭合继电器
        // 如果PSU关闭，只更新逻辑状态，等PSU开启时再恢复
        if (PowerModule_GetPSUState()) {
            GPIO_SetBit(RELAY_GPIO_PORT[relay_id], RELAY_PIN[relay_id]);
        }
        
        PowerModule_SetConfigDirty();

        // 移除蜂鸣器调用，减少延迟
        Beeper_BeepForRelay(relay_id);
    }
}

/**
 * @brief  关闭指定继电器 - 优化版本，移除蜂鸣器调用
 * @param  relay_id: 继电器编号
 * @retval 无
 */
void Relay_Off(Relay_Id_T relay_id)
{
    if (relay_id < RELAY_NUM) {
        GPIO_ResetBit(RELAY_GPIO_PORT[relay_id], RELAY_PIN[relay_id]);
        relay_states[relay_id] = RELAY_OFF;
        
        PowerModule_SetConfigDirty();

        // 移除蜂鸣器调用，减少延迟
				Beeper_BeepForRelay(relay_id);
    }
}

/**
 * @brief  切换指定继电器状态
 * @param  relay_id: 继电器编号
 * @retval 无
 */
void Relay_Toggle(Relay_Id_T relay_id)
{
    if (relay_id < RELAY_NUM) {
        if (relay_states[relay_id] == RELAY_ON) {
            Relay_Off(relay_id);
        } else {
            Relay_On(relay_id);
        }
    }
}

/**
 * @brief  获取指定继电器设置状态
 * @param  relay_id: 继电器编号
 * @retval 继电器状态
 */
Relay_State_T Relay_GetState(Relay_Id_T relay_id)
{
    if (relay_id < RELAY_NUM) {
        return relay_states[relay_id];
    }
    return RELAY_OFF;
}

/**
 * @brief  打开所有继电器
 * @param  无
 * @retval 无
 */
void Relay_All_On(void)
{
    uint8_t i;
    for (i = 0; i < RELAY_NUM; i++) {
        Relay_On(i);
    }
}

/**
 * @brief  关闭所有继电器
 * @param  无
 * @retval 无
 */
void Relay_All_Off(void)
{
    uint8_t i;
    for (i = 0; i < RELAY_NUM; i++) {
        Relay_Off(i);
    }
}

/**
 * @brief  应用继电器状态到硬件 (不修改逻辑状态)
 * @param  enable_output: true=根据状态输出, false=强制关闭输出
 * @retval 无
 */
void Relay_Apply_State(bool enable_output)
{
    uint8_t i;
    for (i = 0; i < RELAY_NUM; i++) {
        if (enable_output && relay_states[i] == RELAY_ON) {
            GPIO_SetBit(RELAY_GPIO_PORT[i], RELAY_PIN[i]);
        } else {
            GPIO_ResetBit(RELAY_GPIO_PORT[i], RELAY_PIN[i]);
        }
    }
}

