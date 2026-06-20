/*!
 * @file        bsp_key.h
 *
 * @brief       Header for bsp_key.c module
 *
 * @version     V1.0.0
 *
 * @date        2022-11-04
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
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#include "Board.h"
#include "main.h"

/** @addtogroup Board
  * @brief Board support package
  @{
*/

/** @addtogroup Board_APM32F107_EVAL
  * @brief APM32F107 EVAL board support package
  @{
*/

/** @defgroup APM32F107_EVAL_Macros Macros
  @{
*/

#define KEY1                    GPIO_ReadInputBit(KEY1_BUTTON_GPIO_PORT,KEY1_BUTTON_PIN)
#define KEY2                    GPIO_ReadInputBit(KEY2_BUTTON_GPIO_PORT,KEY2_BUTTON_PIN)
#define KEY3                    GPIO_ReadInputBit(KEY3_BUTTON_GPIO_PORT,KEY3_BUTTON_PIN)

/**@} end of group APM32F107_EVAL_Macros*/

/** @defgroup APM32F107_EVAL_Enumerates Enumerates
  @{
  */

/**
 * @brief    Key value
 */
typedef enum
{
    KEY_ALL_REALEASED,          /*!< All key is realeased */
    KEY1_PRESSED,               /*!< KEY1 is pressed */
    KEY2_PRESSED,               /*!< KEY2 is pressed */
    KEY3_PRESSED,               /*!< KEY3 is pressed */
} KEY_VALUE_T;

/**
 * @brief    Key mode
 */
typedef enum
{
    KEY_MODE_SINGLE,                /*!< Single detect mode */
    KEY_MODE_CONTINUOUS,            /*!< Continuous detect mode */
} KEY_MODE_T;

/**@} end of group APM32F107_EVAL_Enumerates*/

/** @defgroup APM32F107_EVAL_Fuctions
  @{
  */

uint8_t Key_Scan(KEY_MODE_T mode);

#endif

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
