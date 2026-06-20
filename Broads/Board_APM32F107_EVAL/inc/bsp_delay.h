/*!
 * @file        bsp_delay.h
 *
 * @brief       Header for bsp_delay.c module
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
#ifndef __BSP_DELAY_H
#define __BSP_DELAY_H

/* Includes */
#include "main.h"

/** @addtogroup Board
  * @brief Board support package
  @{
*/

/** @addtogroup Board_APM32F107_EVAL
  * @brief APM32F107 EVAL board support package
  @{
*/

/** @defgroup APM32F107_EVAL_Variables
  @{
  */

/**@} end of group APM32F107_EVAL_Variables */

/** @defgroup APM32F107_EVAL_Fuctions
  @{
  */

/* function declaration */
void APM_EVAL_DelayInit(void);

/* Delay */
void APM_EVAL_DelayMs(__IO uint32_t nms);
void APM_EVAL_DelayUs(__IO uint32_t nus);

#endif

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
