/*!
 * @file        main.h
 *
 * @brief       Header for main.c module
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

/* Define to prevent recursive inclusion */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
  extern "C" {
#endif

/* Includes */
#include "Board.h"
#include "bsp_lcd.h"

/* ?????? HSE ? 25MHz???????????????????? */
#ifdef HSE_VALUE
#undef HSE_VALUE
#endif
#define HSE_VALUE    ((uint32_t)25000000)

#include "apm32f10x.h"
#include "apm32f10x_eth.h"
#include "apm32f10x_usart.h"
#include "apm32f10x_gpio.h"
#include "tcp.h"
#include "mem.h"
#include "memp.h"
#include "tcp.h"
#include "tcp_impl.h"
#include "udp.h"
#include "etharp.h"
#include "dhcp.h"
#include "ethernetif.h"

/* Configurate local ETH information */
#define ETH_IP_ADDR         "192.168.1.199"
#define ETH_NET_MASK        "255.255.255.0"
#define ETH_GATEWAY         "192.168.1.1"

/** @addtogroup Examples
  * @{
  */

/** @addtogroup ETH_Ping
  * @{
  */

/** @defgroup ETH_Ping_Variables Variables
  * @{
  */

extern volatile uint32_t ETHTimer;
extern volatile uint8_t g_system_reset_pending;

/**@} end of group ETH_Ping_Variables */

/** @defgroup ETH_Ping_Functions Functions
  * @{
  */

void LwIP_Init(void);
void LwIP_Pkt_Handle(void);
void LwIP_Periodic_Handle(__IO uint32_t ETHTimer);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
