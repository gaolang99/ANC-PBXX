/*!
 * @file        Board_APM32F107_EVAL.h
 *
 * @brief       This file contains definitions for APM32F107_EVAL's Leds, push-buttons hardware resources
 *
 * @version     V1.0.0
 *
 * @date        2022-11-01
 *
 * @attention
 *
 *  Copyright (C) 2022 Geehy Semiconductor
 *
 *  You may not use this file except in compliance with the
 *  GEEHY COPYRIGHT NOTICE (GEEHY SOFTWARE PACKAGE LICENSE).
 *
 *  The program is only for reference, which is distributed in the hope
 *  that it will be useful and instructional for customers to develop
 *  their software. Unless required by applicable law or agreed to in
 *  writing, the program is distributed on an "AS IS" BASIS, WITHOUT
 *  ANY WARRANTY OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the GEEHY SOFTWARE PACKAGE LICENSE for the governing permissions
 *  and limitations under the License.
 */

/* Define to prevent recursive inclusion */
#ifndef __BOARD_APM32F107_EVAL_H
#define __BOARD_APM32F107_EVAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "apm32f10x.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_rcm.h"
#include "apm32f10x_eint.h"
#include "apm32f10x_usart.h"
#include "apm32f10x_misc.h"

/** @addtogroup Board
  @{
*/

/** @addtogroup Board_APM32F107_EVAL
  @{
*/

/** @defgroup APM32F107_EVAL_Enumerations
  * @{
  */

typedef enum
{
    LED1 = 0,
    LED2 = 1,
    LED3 = 2
} Led_TypeDef;

typedef enum
{
    BUTTON_KEY1 = 0,
    BUTTON_KEY2 = 1,
    BUTTON_KEY3 = 2
} Button_TypeDef;

typedef enum
{
    BUTTON_MODE_GPIO = 0,
    BUTTON_MODE_EINT = 1
} ButtonMode_TypeDef;

typedef enum
{
    COM1 = 0,
    COM2 = 1,
    COM3 = 2,
    COM4 = 3
} COM_TypeDef;

/**@} end of group APM32F107_EVAL_Enumerations */

/**
  * @}
  */

/** @addtogroup APM32F107_EVAL_LED
  * @{
  */
#define LEDn                             3

#define LED1_PIN                         GPIO_PIN_7
#define LED1_GPIO_PORT                   GPIOE
#define LED1_GPIO_CLK                    RCM_APB2_PERIPH_GPIOE

#define LED2_PIN                         GPIO_PIN_8
#define LED2_GPIO_PORT                   GPIOE
#define LED2_GPIO_CLK                    RCM_APB2_PERIPH_GPIOE

#define LED3_PIN                         GPIO_PIN_9
#define LED3_GPIO_PORT                   GPIOE
#define LED3_GPIO_CLK                    RCM_APB2_PERIPH_GPIOE

/**@} end of group APM32F107_EVAL_LED */

/** @addtogroup APM32F107_EVAL_BUTTON
  * @{
  */
#define BUTTONn                          3

/**
 * @brief Key1 push-button
 */
#define KEY1_BUTTON_PIN                   GPIO_PIN_10
#define KEY1_BUTTON_GPIO_PORT             GPIOE
#define KEY1_BUTTON_GPIO_CLK              RCM_APB2_PERIPH_GPIOE
#define KEY1_BUTTON_EINT_LINE             EINT_LINE_10
#define KEY1_BUTTON_EINT_PORT_SOURCE      GPIO_PORT_SOURCE_E
#define KEY1_BUTTON_EINT_PIN_SOURCE       GPIO_PIN_SOURCE_10
#define KEY1_BUTTON_EINT_IRQn             EINT15_10_IRQn

/**
 * @brief Key2 push-button
 */
#define KEY2_BUTTON_PIN                   GPIO_PIN_12
#define KEY2_BUTTON_GPIO_PORT             GPIOE
#define KEY2_BUTTON_GPIO_CLK              RCM_APB2_PERIPH_GPIOE
#define KEY2_BUTTON_EINT_LINE             EINT_LINE_12
#define KEY2_BUTTON_EINT_PORT_SOURCE      GPIO_PORT_SOURCE_E
#define KEY2_BUTTON_EINT_PIN_SOURCE       GPIO_PIN_SOURCE_12
#define KEY2_BUTTON_EINT_IRQn             EINT15_10_IRQn

/**
 * @brief Key3 push-button
 */
#define KEY3_BUTTON_PIN                   GPIO_PIN_14
#define KEY3_BUTTON_GPIO_PORT             GPIOE
#define KEY3_BUTTON_GPIO_CLK              RCM_APB2_PERIPH_GPIOE
#define KEY3_BUTTON_EINT_LINE             EINT_LINE_14
#define KEY3_BUTTON_EINT_PORT_SOURCE      GPIO_PORT_SOURCE_E
#define KEY3_BUTTON_EINT_PIN_SOURCE       GPIO_PIN_SOURCE_14
#define KEY3_BUTTON_EINT_IRQn             EINT15_10_IRQn

/**@} end of group APM32F107_EVAL_BUTTON */

/** @addtogroup APM32F107_EVAL_COM
  * @{
  */

#define COMn                            4

/**
 * @brief Definition for COM port1, connected to USART1
 */
#define EVAL_COM1                        USART1
#define EVAL_COM1_CLK                    RCM_APB2_PERIPH_USART1
#define EVAL_COM1_TX_PIN                 GPIO_PIN_9
#define EVAL_COM1_TX_GPIO_PORT           GPIOA
#define EVAL_COM1_TX_GPIO_CLK            RCM_APB2_PERIPH_GPIOA
#define EVAL_COM1_RX_PIN                 GPIO_PIN_10
#define EVAL_COM1_RX_GPIO_PORT           GPIOA
#define EVAL_COM1_RX_GPIO_CLK            RCM_APB2_PERIPH_GPIOA
#define EVAL_COM1_IRQn                   USART1_IRQn

/**
 * @brief Definition for COM port2, connected to USART2
 */
#define EVAL_COM2                        USART2
#define EVAL_COM2_CLK                    RCM_APB1_PERIPH_USART2
#define EVAL_COM2_TX_PIN                 GPIO_PIN_5
#define EVAL_COM2_TX_GPIO_PORT           GPIOD
#define EVAL_COM2_TX_GPIO_CLK            RCM_APB2_PERIPH_GPIOD
#define EVAL_COM2_RX_PIN                 GPIO_PIN_6
#define EVAL_COM2_RX_GPIO_PORT           GPIOD
#define EVAL_COM2_RX_GPIO_CLK            RCM_APB2_PERIPH_GPIOD
#define EVAL_COM2_IRQn                   USART2_IRQn

/**
 * @brief Definition for COM port3, connected to USART3
 */
#define EVAL_COM3                        USART3
#define EVAL_COM3_CLK                    RCM_APB1_PERIPH_USART3
#define EVAL_COM3_TX_PIN                 GPIO_PIN_10
#define EVAL_COM3_TX_GPIO_PORT           GPIOB
#define EVAL_COM3_TX_GPIO_CLK            RCM_APB2_PERIPH_GPIOB
#define EVAL_COM3_RX_PIN                 GPIO_PIN_11
#define EVAL_COM3_RX_GPIO_PORT           GPIOB
#define EVAL_COM3_RX_GPIO_CLK            RCM_APB2_PERIPH_GPIOB
#define EVAL_COM3_IRQn                   USART3_IRQn

/**
 * @brief Definition for COM port4, connected to UART4
 */
#define EVAL_COM4                        UART4
#define EVAL_COM4_CLK                    RCM_APB1_PERIPH_UART4
#define EVAL_COM4_TX_PIN                 GPIO_PIN_10
#define EVAL_COM4_TX_GPIO_PORT           GPIOC
#define EVAL_COM4_TX_GPIO_CLK            RCM_APB2_PERIPH_GPIOC
#define EVAL_COM4_RX_PIN                 GPIO_PIN_11
#define EVAL_COM4_RX_GPIO_PORT           GPIOC
#define EVAL_COM4_RX_GPIO_CLK            RCM_APB2_PERIPH_GPIOC
#define EVAL_COM4_IRQn                   UART4_IRQn

/**@} end of group APM32F107_EVAL_COM */

/**
  * @}
  */

/** @defgroup APM32F107_EVAL_Functions
  * @{
  */

void APM_EVAL_LEDInit(Led_TypeDef Led);
void APM_EVAL_LEDOn(Led_TypeDef Led);
void APM_EVAL_LEDOff(Led_TypeDef Led);
void APM_EVAL_LEDToggle(Led_TypeDef Led);

void APM_EVAL_PBInit(Button_TypeDef Button, ButtonMode_TypeDef Button_Mode);
uint32_t APM_EVAL_PBGetState(Button_TypeDef Button);

void APM_EVAL_COMInit(COM_TypeDef COM, USART_Config_T* configStruct);

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_APM32F107_EVAL_H */

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
