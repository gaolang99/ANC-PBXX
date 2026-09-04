#ifndef __BSP_BEEPER_H
#define __BSP_BEEPER_H

#include "apm32f10x.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_rcm.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp_power_module.h"

#define BEEPER_PIN                      GPIO_PIN_12
#define BEEPER_GPIO_PORT                GPIOA
#define BEEPER_GPIO_CLK                 RCM_APB2_PERIPH_GPIOA

#define BEEPER_RESET_PIN                GPIO_PIN_11
#define BEEPER_RESET_GPIO_PORT          GPIOA
#define BEEPER_RESET_GPIO_CLK           RCM_APB2_PERIPH_GPIOE


#define PWROK_PORT_CLK           		RCM_APB2_PERIPH_GPIOC
#define PWROK_PORT					    GPIOC
#define PWROK_B							GPIO_PIN_7								
#define PWROK_A							GPIO_PIN_6

#define PWROK_ATX_PSONn_PORT_CLK        RCM_APB2_PERIPH_GPIOE
#define PWROK_ATX_PSONn_PORT			GPIOE
#define ATX_PSONn						GPIO_PIN_10

#define PWROK_DOUT_PORT_CLK             RCM_APB2_PERIPH_GPIOD
#define PWROK_DOUT_PORT					GPIOD
#define PWROK_DOUT4						GPIO_PIN_15								
#define PWROK_DOUT3						GPIO_PIN_14
#define PWROK_DOUT2						GPIO_PIN_1
#define PWROK_DOUT1						GPIO_PIN_0

#define PWR_PRSNTN_PORT_CLK             RCM_APB2_PERIPH_GPIOD
#define PWR_PRSNTN_PORT					GPIOD
#define PWR_PRSNTN1						GPIO_PIN_7
#define PWR_PRSNTN2						GPIO_PIN_6

#define PWR_STATUS_MASK     0x3

#define MODULE1_MASK  0x1
#define MODULE2_MASK  0x2

#define MODULE1_PRSNT_MASK  0x1
#define MODULE2_PRSNT_MASK  0x2

typedef enum {
    BEEPER_OFF = 0,
    BEEPER_ON = 1
} Beeper_State_T;

typedef struct {
    uint8_t PwrPresent;
    uint8_t Pwrok;
} PowerStatus;

void buzzer_control_init(void);
PowerStatus pwr_sig_detect(void);
void buzzer_SetByPwrState(PowerStatus sta);
void Beeper_Init(void);
void Beeper_On(void);
void Beeper_Off(void);
void Beeper_Toggle(void);
void Beeper_Beep(uint8_t times);      
void Beeper_BeepForRelay(uint8_t relay_id);  
void Beeper_SetMute(bool mute); // ���þ���
void Beeper_Update(void);
#endif /* __BSP_BEEPER_H */