/*!
 * @file        bsp_i2c_ee.h
 *
 * @brief       This file provides i2c read and write EEPROM(AT24C32) functions
 *
 * @version     V1.0.0
 *
 * @date        2022-11-07
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
#ifndef __BSP_I2C_EE_H
#define __BSP_I2C_EE_H

/* Includes */
#include "Board.h"
#include "bsp_delay.h"

#include "apm32f10x_dma.h"
#include "apm32f10x_i2c.h"

/** @addtogroup Board
  * @brief Board support package
  @{
  */

/** @addtogroup Board_APM32F107_EVAL
  @{
  */

/** @defgroup APM32F107_EVAL_Macros Macros
  @{
*/
#define EEPROM_I2C_SPEED                    300000
#define EEPROM_I2C_SLAVE_ADDRESS7           0xA0

/* EEPROM PIN */
#define EEPROM_I2C                          I2C1
#define EEPROM_I2C_CLK                      RCM_APB1_PERIPH_I2C1

#define EEPROM_I2C_SCL_PIN                  GPIO_PIN_6
#define EEPROM_I2C_SCL_GPIO_PORT            GPIOB
#define EEPROM_I2C_SCL_GPIO_CLK             RCM_APB2_PERIPH_GPIOB

#define EEPROM_I2C_SDA_PIN                  GPIO_PIN_7
#define EEPROM_I2C_SDA_GPIO_PORT            GPIOB
#define EEPROM_I2C_SDA_GPIO_CLK             RCM_APB2_PERIPH_GPIOB

#define EEPROM_I2C_DMA                      DMA1
#define EEPROM_I2C_DMA_CHANNEL_TX           DMA1_Channel6
#define EEPROM_I2C_DMA_CHANNEL_RX           DMA1_Channel7
#define EEPROM_I2C_DMA_FLAG_TX_TC           DMA1_FLAG_TC6
#define EEPROM_I2C_DMA_FLAG_TX_GL           DMA1_FLAG_GINT6
#define EEPROM_I2C_DMA_FLAG_RX_TC           DMA1_FLAG_TC7
#define EEPROM_I2C_DMA_FLAG_RX_GL           DMA1_FLAG_GINT7
#define EEPROM_I2C_DMA_CLK                  RCM_AHB_PERIPH_DMA1
#define EEPROM_I2C_DR_Address               ((uint32_t)0x40005410)
#define EEPROM_USE_DMA

#define EEPROM_I2C_DMA_TX_IRQn              DMA1_Channel6_IRQn
#define EEPROM_I2C_DMA_RX_IRQn              DMA1_Channel7_IRQn
#define EEPROM_I2C_DMA_TX_IRQHandler        DMA1_Channel6_IRQHandler
#define EEPROM_I2C_DMA_RX_IRQHandler        DMA1_Channel7_IRQHandler
#define EEPROM_I2C_DMA_PREPRIO              0
#define EEPROM_I2C_DMA_SUBPRIO              0

#define EEPROM_I2C_DIRECTION_TX             0
#define EEPROM_I2C_DIRECTION_RX             1

#define I2C_EE_RETRY_NUMBER_MAX             300
/* define I2C timeout */
#define I2C_EE_FLAG_TIMEOUT                 ((uint32_t)0x1000)
#define I2C_EE_LONG_TIMEOUT                 ((uint32_t)(10 * I2C_EE_FLAG_TIMEOUT))

/*
* AT24C02 2Kbit (256 x 8)
*
* Device Address 0XA0
* 1 0 1 0 A2 A1 A0 R/W
* 1 0 1 0 0  0  0  0 = 0XA0
*/
#define EEPROM_ADDRESS                      0xA0
#define EEPROM_ADDRESS_SIZE_8Bit
/* #define EEPROM_ADDRESS_SIZE_16Bit */

#define EEPROM_PAGE_SIZE                    8

#define EEPROM_WRITE_ADDR                   0x00
#define EEPROM_READ_ADDR                    EEPROM_WRITE_ADDR

/**@} end of group APM32F107_EVAL_Macros */

/** @defgroup APM32F107_EVAL_Enumerates Enumerates
  @{
  */

/**
 * @brief I2C EEPROM error type
 */
typedef enum
{
    I2C_EE_OK,
    I2C_EE_ERR,
    I2C_EE_TIMEOUT,
} I2C_EE_ERROR_T;

/**@} end of group APM32F107_EVAL_Enumerates*/

/** @defgroup APM32F107_EVAL_Structures Structures
  @{
  */

/**
 * @brief I2C EEPROM information
 */
typedef struct
{
    __IO uint16_t address;
    __IO uint16_t dataNum;
    __IO uint32_t timeout;
    uint8_t startFlag;
} I2C_EE_INFO_T;

/**@} end of group APM32F107_EVAL_Structures*/

/** @defgroup APM32F107_EVAL_Functions Functions
  @{
  */
void APM_I2C_EE_Init(void);
I2C_EE_ERROR_T I2C_EE_BufferWrite(uint8_t *buffer, uint16_t writeAddr, uint16_t number);
I2C_EE_ERROR_T I2C_EE_BufferRead(uint8_t *buffer, uint16_t readAddr, uint16_t *number);

#endif

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
