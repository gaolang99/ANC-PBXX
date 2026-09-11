#ifndef __BSP_LED_H
#define __BSP_LED_H

#include "apm32f10x.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_rcm.h"
#include <stdbool.h>

// LED?????????3¡¤MOS??????LED??
#define LED_NUM 3

// LED??????????‰Í????????LED_R1=PE7??LED_R2=PE8??LED_R3=PE9??
#define BSP_LED1_PIN                GPIO_PIN_7   // ???LED_R1
#define BSP_LED1_GPIO_PORT          GPIOE
#define BSP_LED1_GPIO_CLK           RCM_APB2_PERIPH_GPIOE

#define BSP_LED2_PIN                GPIO_PIN_8   // ???LED_R2
#define BSP_LED2_GPIO_PORT          GPIOE
#define BSP_LED2_GPIO_CLK           RCM_APB2_PERIPH_GPIOE

#define BSP_LED3_PIN                GPIO_PIN_9   // ???LED_R3
#define BSP_LED3_GPIO_PORT          GPIOE
#define BSP_LED3_GPIO_CLK           RCM_APB2_PERIPH_GPIOE

// LED?????
typedef enum {
    LED_OFF = 0,
    LED_ON = 1
} LED_State_T;

// LED??????????3¡¤?????
typedef enum {
    BSP_LED1 = 0,  // ???LED_R1??PE7??
    BSP_LED2 = 1,  // ???LED_R2??PE8??
    BSP_LED3 = 2   // ???LED_R3??PE9??
} LED_Id_T;

// ????????
void LED_Init(void);                // ?????LED????GPIO
void LED_On(LED_Id_T led_id);       // ?????LED
void LED_Off(LED_Id_T led_id);      // ??????LED
void LED_Toggle(LED_Id_T led_id);   // ?§Ý????LED??
LED_State_T LED_GetState(LED_Id_T led_id);  // ??????LED??
void LED_All_On(void);              // ??????LED
void LED_All_Off(void);             // ???????LED
void LED_SetState(LED_State_T state);  // LED?????
void LED_Task(bool system_normal, bool link_up, bool activity, bool output_12v_ok);

#endif /* __BSP_LED_H */
