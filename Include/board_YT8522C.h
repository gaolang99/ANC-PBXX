/*!
 * @file       BOARD_YT8522C.h
 *
 * @brief      This file provides all the config YT8522C PHY functions
 *
 * @version     V1.0.0
 *
 * @date        2022-11-10
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

/** jumpers setting
*    ==========================================================================================+
*      Jumper |       MII mode configuration            |      RMII mode configuration         +
*    ==========================================================================================+
*      JP6    | 2-3 provide 25MHz clock by MCO(PA8)     |  2-3 provide 25MHz clock by MCO(PA8) +
*             | 1-2 provide 25MHz clock by ext. Crystal |                                      +
*     -----------------------------------------------------------------------------------------+
*      JP5    |          2-3                            |  1-2                                 +
*     -----------------------------------------------------------------------------------------+
*      JP4    |          Open                           |  Close                               +
*    ==========================================================================================+
*/

/* Define to prevent recursive inclusion */
#ifndef __BOARD_YT8522C_H
#define __BOARD_YT8522C_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include "main.h"

/** @addtogroup Examples
  * @{
  */

/** @addtogroup ETH_Ping
  * @{
  */

/** @defgroup ETH_Ping_Macros Macros
  @{
*/

/* PHY ADDRESS - YT8522C hardware set to 5'b00011 (Decimal 3) */
#define PHY_ADDRESS       0x03

/* PHY Clock Source: External Crystal (YT8522C has 25MHz) */
/* Disable MCO output as PHY uses external crystal */
//#define PHY_CLOCK_MCO

/**@} end of group ETH_Ping_Macros*/

/* MII and RMII mode selection */
//#define RMII_MODE
#define MII_MODE

/** @defgroup ETH_Ping_Macros Macros
  @{
*/

/* Reset delay */
#define YT8522C_RESET_DELAY        PHY_RESET_DELAY

/**@} end of group ETH_Ping_Macros*/

/** @defgroup ETH_Ping_Macros Macros
  @{
*/

/* 
   Pin Mapping based on User Hardware (Remapped MII Mode):
   PHY_RStn     : PE11
   MCU_ETH_MDIO : PA2
   MCU_ETH_MDC  : PC1
   MCU_ETH_CRS  : PA0
   MCU_ETH_RXCLK: PA1
   MCU_ETH_COL  : PA3
   MCU_ETH_RXD3 : PD12
   MCU_ETH_RXD2 : PD11
   MCU_ETH_RXD1 : PD10
   MCU_ETH_RXD0 : PD9
   MCU_ETH_RXDV : PD8
   MCU_ETH_RX_ER: PB10
   MCU_ETH_TXEN : PB11
   MCU_ETH_TXD0 : PB12
   MCU_ETH_TXD1 : PB13
   MCU_ETH_TXD2 : PC2
   MCU_ETH_TXD3 : PB8
   MCU_ETH_TXCLK: PC3
*/

/* ETH_RESET# PE11 */
#define ETH_RESET_GPIO_CLK              RCM_APB2_PERIPH_GPIOE
#define ETH_RESET_PORT                  GPIOE
#define ETH_RESET_PIN                   GPIO_PIN_11
#define ETH_RESET_PIN_HIGH()            GPIO_SetBit(ETH_RESET_PORT,ETH_RESET_PIN)
#define ETH_RESET_PIN_LOW()             GPIO_ResetBit(ETH_RESET_PORT,ETH_RESET_PIN)

/* ETH_MDIO PA2 */      
#define ETH_MDIO_GPIO_CLK               RCM_APB2_PERIPH_GPIOA
#define ETH_MDIO_PORT                   GPIOA
#define ETH_MDIO_PIN                    GPIO_PIN_2

/* ETH_MDC PC1 */       
#define ETH_MDC_GPIO_CLK                RCM_APB2_PERIPH_GPIOC
#define ETH_MDC_PORT                    GPIOC
#define ETH_MDC_PIN                     GPIO_PIN_1

/* ETH_CRS PA0 */
#define ETH_CRS_GPIO_CLK                RCM_APB2_PERIPH_GPIOA
#define ETH_CRS_PORT                    GPIOA
#define ETH_CRS_PIN                     GPIO_PIN_0

/* ETH_RX_CLK PA1 */
#define ETH_RXCLK_GPIO_CLK              RCM_APB2_PERIPH_GPIOA
#define ETH_RXCLK_PORT                  GPIOA
#define ETH_RXCLK_PIN                   GPIO_PIN_1

/* ETH_COL PA3 */
#define ETH_COL_GPIO_CLK                RCM_APB2_PERIPH_GPIOA
#define ETH_COL_PORT                    GPIOA
#define ETH_COL_PIN                     GPIO_PIN_3

/* ETH_RXD3 PD12 */
#define ETH_RXD3_GPIO_CLK               RCM_APB2_PERIPH_GPIOD
#define ETH_RXD3_PORT                   GPIOD
#define ETH_RXD3_PIN                    GPIO_PIN_12

/* ETH_RXD2 PD11 */
#define ETH_RXD2_GPIO_CLK               RCM_APB2_PERIPH_GPIOD
#define ETH_RXD2_PORT                   GPIOD
#define ETH_RXD2_PIN                    GPIO_PIN_11

/* ETH_RXD1 PD10 */
#define ETH_RXD1_GPIO_CLK               RCM_APB2_PERIPH_GPIOD
#define ETH_RXD1_PORT                   GPIOD
#define ETH_RXD1_PIN                    GPIO_PIN_10

/* ETH_RXD0 PD9 */
#define ETH_RXD0_GPIO_CLK               RCM_APB2_PERIPH_GPIOD
#define ETH_RXD0_PORT                   GPIOD
#define ETH_RXD0_PIN                    GPIO_PIN_9

/* ETH_RX_DV PD8 */
#define ETH_RXDV_GPIO_CLK               RCM_APB2_PERIPH_GPIOD
#define ETH_RXDV_PORT                   GPIOD
#define ETH_RXDV_PIN                    GPIO_PIN_8

/* ETH_RX_ER PB10 */
#define ETH_RXER_GPIO_CLK               RCM_APB2_PERIPH_GPIOB
#define ETH_RXER_PORT                   GPIOB
#define ETH_RXER_PIN                    GPIO_PIN_10

/* ETH_TX_EN PB11 */
#define ETH_TXEN_GPIO_CLK               RCM_APB2_PERIPH_GPIOB
#define ETH_TXEN_PORT                   GPIOB
#define ETH_TXEN_PIN                    GPIO_PIN_11

/* ETH_TXD0 PB12 */
#define ETH_TXD0_GPIO_CLK               RCM_APB2_PERIPH_GPIOB
#define ETH_TXD0_PORT                   GPIOB
#define ETH_TXD0_PIN                    GPIO_PIN_12

/* ETH_TXD1 PB13 */
#define ETH_TXD1_GPIO_CLK               RCM_APB2_PERIPH_GPIOB
#define ETH_TXD1_PORT                   GPIOB
#define ETH_TXD1_PIN                    GPIO_PIN_13

/* ETH_TXD2 PC2 */
#define ETH_TXD2_GPIO_CLK               RCM_APB2_PERIPH_GPIOC
#define ETH_TXD2_PORT                   GPIOC
#define ETH_TXD2_PIN                    GPIO_PIN_2

/* ETH_TXD3 PB8 */
#define ETH_TXD3_GPIO_CLK               RCM_APB2_PERIPH_GPIOB
#define ETH_TXD3_PORT                   GPIOB
#define ETH_TXD3_PIN                    GPIO_PIN_8

/* ETH_TX_CLK PC3 */
#define ETH_TXCLK_GPIO_CLK              RCM_APB2_PERIPH_GPIOC
#define ETH_TXCLK_PORT                  GPIOC
#define ETH_TXCLK_PIN                   GPIO_PIN_3

/**@} end of group ETH_Ping_Macros*/

/** @defgroup ETH_Ping_Functions Functions
  @{
*/

void Ethernet_Config(void);
void Ethernet_ResetDelay(__IO uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /** __BOARD_YT8522C_H */

/**@} end of group ETH_Ping_Functions */
/**@} end of group ETH_Ping */
/**@} end of group Examples */
