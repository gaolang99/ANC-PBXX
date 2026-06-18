/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2021 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#ifdef __IAR_SYSTEMS_ICC__
  #include <intrinsics.h>
#endif

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#define SEGGER_RTT_MAX_NUM_UP_BUFFERS             (3)     // Max. number of up-buffers (T->H) available on this target
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS           (3)     // Max. number of down-buffers (H->T) available on this target

#define BUFFER_SIZE_UP                            (1024)  // Size of the buffer for terminal output of target, up to host
#define BUFFER_SIZE_DOWN                          (16)    // Size of the buffer for terminal input to target from host

#define SEGGER_RTT_PRINTF_BUFFER_SIZE             (64u)    // Size of buffer for RTT printf to bulk-send chars via RTT

#define SEGGER_RTT_MODE_DEFAULT                   SEGGER_RTT_MODE_NO_BLOCK_SKIP

//
// Target is not allowed to perform other RTT operations while string is output to prevent data mixing
//
#define SEGGER_RTT_LOCK()
#define SEGGER_RTT_UNLOCK()

//
// Define for "do not block" mode
//
#define SEGGER_RTT_IN_RAM                         (0)

#endif
/*********************************************************************
*                    SEGGER Microcontroller GmbH                     *
*                        The Embedded Experts                        *
**********************************************************************
*                                                                    *
*            (c) 1995 - 2021 SEGGER Microcontroller GmbH             *
*                                                                    *
*       www.segger.com     Support: support@segger.com               *
*                                                                    *
**********************************************************************
*/

#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

#ifdef __IAR_SYSTEMS_ICC__
  #include <intrinsics.h>
#endif

/*********************************************************************
*
*       Defines, configurable
*
**********************************************************************
*/

#define SEGGER_RTT_MAX_NUM_UP_BUFFERS             (3)     // Max. number of up-buffers (T->H) available on this target
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS           (3)     // Max. number of down-buffers (H->T) available on this target

#define BUFFER_SIZE_UP                            (1024)  // Size of the buffer for terminal output of target, up to host
#define BUFFER_SIZE_DOWN                          (16)    // Size of the buffer for terminal input to target from host

#define SEGGER_RTT_PRINTF_BUFFER_SIZE             (64u)    // Size of buffer for RTT printf to bulk-send chars via RTT

#define SEGGER_RTT_MODE_DEFAULT                   SEGGER_RTT_MODE_NO_BLOCK_SKIP

//
// Target is not allowed to perform other RTT operations while string is output to prevent data mixing
//
#define SEGGER_RTT_LOCK()
#define SEGGER_RTT_UNLOCK()

//
// Define for "do not block" mode
//
#define SEGGER_RTT_IN_RAM                         (0)

#endif