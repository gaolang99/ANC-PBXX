/*!
 * @file       apm32f10x_int.h
 *
 * @brief      This file contains the headers of the interrupt handlers
 *
 * @version     V1.0.0
 *
 * @date        2022-11-15
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
#ifndef __APM32F4XX_INT_H
#define __APM32F4XX_INT_H

#ifdef __cplusplus
  extern "C" {
#endif

/* Includes */
#include "apm32f10x.h"

/** @addtogroup Examples
  * @{
  */

/** @addtogroup ETH_Ping
  * @{
  */ 

/** @defgroup ETH_Ping_INT_Functions Functions
  @{
*/

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);


#ifdef __cplusplus
}
#endif

#endif /*__APM32F4XX_INT_H */

/**@} end of group ETH_Ping_INT_Functions */
/**@} end of group ETH_Ping */
/**@} end of group Examples */
