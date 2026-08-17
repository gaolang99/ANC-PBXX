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

#include "SEGGER_RTT.h"

#include <string.h>

/*********************************************************************
*
*       Defines, non-configurable
*
**********************************************************************
*/

//
// When we do not have a preprocessor define for the compiler, we assume GCC
//
#ifndef SEGGER_RTT_MEMCPY
  #define SEGGER_RTT_MEMCPY memcpy
#endif

//
// Helper macro to calculate the minimum of two values
//
#ifndef MIN
  #define MIN(a, b)       (((a) < (b)) ? (a) : (b))
#endif

//
// Helper macro to calculate the maximum of two values
//
#ifndef MAX
  #define MAX(a, b)       (((a) > (b)) ? (a) : (b))
#endif

/*********************************************************************
*
*       Types
*
**********************************************************************
*/

//
// Description for a circular buffer (also called "ring buffer")
// which is used as up-buffer (T->H)
//


/*********************************************************************
*
*       Static data
*
**********************************************************************
*/

static char _acUpBuffer[BUFFER_SIZE_UP];
static char _acDownBuffer[BUFFER_SIZE_DOWN];

SEGGER_RTT_CB _SEGGER_RTT = {
  "SEGGER RTT",
  SEGGER_RTT_MAX_NUM_UP_BUFFERS,
  SEGGER_RTT_MAX_NUM_DOWN_BUFFERS,
  {{ "Terminal", &_acUpBuffer[0], sizeof(_acUpBuffer), 0, 0, SEGGER_RTT_MODE_NO_BLOCK_SKIP }},
  {{ "Terminal", &_acDownBuffer[0], sizeof(_acDownBuffer), 0, 0, SEGGER_RTT_MODE_NO_BLOCK_SKIP }},
};

static char _InitDone = 0;

/*********************************************************************
*
*       Public code
*
**********************************************************************
*/

/*********************************************************************
*
*       SEGGER_RTT_Init
*/
void SEGGER_RTT_Init(void) {
  if (_InitDone == 0) {
    _InitDone = 1;
  }
}

/*********************************************************************
*
*       SEGGER_RTT_Write
*/
unsigned SEGGER_RTT_Write(unsigned BufferIndex, const void* pBuffer, unsigned NumBytes) {
  unsigned Avail;
  const char* pData;
  SEGGER_RTT_BUFFER_UP* pRing;

  pData = (const char*)pBuffer;
  if (BufferIndex >= (unsigned)_SEGGER_RTT.MaxNumUpBuffers) {
    return 0;
  }
  pRing = &_SEGGER_RTT.aUp[BufferIndex];
  
  //
  // Initialize on first write if not initialized yet
  //
  if (_InitDone == 0) {
    SEGGER_RTT_Init();
  }
  
  //
  // Load volatile read offset once
  //
  unsigned RdOff = pRing->RdOff;
  unsigned WrOff = pRing->WrOff;
  
  if (RdOff > WrOff) {
    Avail = RdOff - WrOff - 1;
  } else {
    Avail = pRing->SizeOfBuffer - 1 - WrOff + RdOff;
  }
  
  if (Avail < NumBytes) {
    if (pRing->Flags == SEGGER_RTT_MODE_NO_BLOCK_SKIP) {
      return 0;
    }
    if (pRing->Flags == SEGGER_RTT_MODE_NO_BLOCK_TRIM) {
      NumBytes = Avail;
    }
    if (pRing->Flags == SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL) {
      while (Avail < NumBytes) {
        RdOff = pRing->RdOff;
        if (RdOff > WrOff) {
          Avail = RdOff - WrOff - 1;
        } else {
          Avail = pRing->SizeOfBuffer - 1 - WrOff + RdOff;
        }
      }
    }
  }

  //
  // Write data
  //
  unsigned Rem = pRing->SizeOfBuffer - WrOff;
  if (Rem > NumBytes) {
    SEGGER_RTT_MEMCPY(pRing->pBuffer + WrOff, pData, NumBytes);
    pRing->WrOff = WrOff + NumBytes;
  } else {
    SEGGER_RTT_MEMCPY(pRing->pBuffer + WrOff, pData, Rem);
    SEGGER_RTT_MEMCPY(pRing->pBuffer, pData + Rem, NumBytes - Rem);
    pRing->WrOff = NumBytes - Rem;
  }
  
  return NumBytes;
}

/*********************************************************************
*
*       SEGGER_RTT_WriteString
*/
unsigned SEGGER_RTT_WriteString(unsigned BufferIndex, const char* s) {
  return SEGGER_RTT_Write(BufferIndex, s, strlen(s));
}