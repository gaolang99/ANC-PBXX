/*!
 * @file        bsp_delay.c
 *
 * @brief       Delay board support package program body
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

/* Includes */
#include "bsp_delay.h"

/** @addtogroup Board
  * @brief Board support package
  @{
  */

/** @addtogroup Board_APM32F107_EVAL
  * @brief APM32F107 EVAL board support package
  @{
*/

/** @defgroup APM32F107_EVAL_Variables Variables
  @{
  */

/* The count of microseconds */
static uint32_t cntUs = 0;
/* The count of milliseconds */
static uint32_t cntMs = 0;

/**@} end of group APM32F107_EVAL_Variables */

/** @defgroup APM32F107_EVAL_Fuctions Functions
  @{
  */

/*!
 * @brief       Configures Delay.
 *
 * @param       None
 *
 * @retval      None
 */
void APM_EVAL_DelayInit(void)
{
    SysTick_ConfigCLKSource(SYSTICK_CLK_SOURCE_HCLK_DIV8);
    
    cntUs = SystemCoreClock / 8000000;
    cntMs = (uint16_t)cntUs * 1000;
}

/*!
 * @brief       Configures Delay ms.
 *
 * @param       nms: Specifies the delay to be configured.
 *              This parameter can be one of following parameters:
 *              @arg nms
 *
 * @retval      None
 */
void APM_EVAL_DelayMs(__IO uint32_t nms)
{
    uint32_t temp;
    
    SysTick->LOAD = (uint32_t)nms * cntMs;
    SysTick->VAL = 0x00;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    do
    {
        temp = SysTick->CTRL;
    }
    while((temp & 0x01) && !(temp & (1 << 16)));
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SysTick->VAL = 0x00;
}

/*!
 * @brief       Configures Delay us.
 *
 * @param       nus: Specifies the delay to be configured.
 *              This parameter can be one of following parameters:
 *              @arg nus
 *
 * @retval      None
 */
void APM_EVAL_DelayUs(__IO uint32_t nus)
{
    uint32_t temp;
    
    SysTick->LOAD = (uint32_t)nus * cntUs;
    SysTick->VAL = 0x00;
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
    do
    {
        temp = SysTick->CTRL;
    }
    while((temp & 0x01) && !(temp & (1 << 16)));
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    SysTick->VAL = 0x00;
}

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
