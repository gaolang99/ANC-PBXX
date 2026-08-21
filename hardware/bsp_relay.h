#ifndef __BSP_RELAY_H
#define __BSP_RELAY_H

#include "apm32f10x.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_rcm.h"
#include <stdbool.h>
// 继电器数量
#define RELAY_NUM 4

// 继电器控制GPIO引脚定义 - Gate Driver控制端
// 根据提供的信息使用PE2~PE5
#define RELAY1_PIN                  GPIO_PIN_2
#define RELAY1_GPIO_PORT            GPIOE
#define RELAY1_GPIO_CLK             RCM_APB2_PERIPH_GPIOE

#define RELAY2_PIN                  GPIO_PIN_3
#define RELAY2_GPIO_PORT            GPIOE
#define RELAY2_GPIO_CLK             RCM_APB2_PERIPH_GPIOE

#define RELAY3_PIN                  GPIO_PIN_4
#define RELAY3_GPIO_PORT            GPIOE
#define RELAY3_GPIO_CLK             RCM_APB2_PERIPH_GPIOE

#define RELAY4_PIN                  GPIO_PIN_5
#define RELAY4_GPIO_PORT            GPIOE
#define RELAY4_GPIO_CLK             RCM_APB2_PERIPH_GPIOE

// ATX电源控制引脚 - MCU_PE10
#define ATX_PSONN_PIN               GPIO_PIN_10
#define ATX_PSONN_GPIO_PORT         GPIOE
#define ATX_PSONN_GPIO_CLK          RCM_APB2_PERIPH_GPIOE

// 继电器状态枚举
typedef enum {
    RELAY_OFF = 0,
    RELAY_ON = 1,
    RELAY_FAULT = 2  // 继电器故障（闭合但无反馈）
} Relay_State_T;

// 继电器编号枚举
typedef enum {
    RELAY1 = 0,
    RELAY2 = 1,
    RELAY3 = 2,
    RELAY4 = 3
} Relay_Id_T;

// 函数声明
void Relay_Init(void);
void Relay_On(Relay_Id_T relay_id);
void Relay_Off(Relay_Id_T relay_id);
void Relay_Toggle(Relay_Id_T relay_id);
Relay_State_T Relay_GetState(Relay_Id_T relay_id);
void Relay_All_On(void);
void Relay_All_Off(void);
void Relay_Apply_State(bool enable_output);

// ATX电源控制函数
//void ATX_Power_Enable(uint8_t enable);  // 1=使能电源，0=关闭电源

#endif /* __BSP_RELAY_H */
