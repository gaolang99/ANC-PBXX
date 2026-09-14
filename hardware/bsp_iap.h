#ifndef __BSP_IAP_H
#define __BSP_IAP_H

#include "apm32f10x.h"
#include <stdbool.h>

/* IAP Opcodes */
#define OPC_READ        0x00 // Not used in update, but present in screenshot
#define OPC_WREN        0x06 // Write Enable / Start Packet
#define OPC_JUMP        0x07 // Jump to App
#define OPC_ERPG        0x08 // Erase Page (Optional)
#define OPC_ERUSM       0x09 // Erase User Space (Optional)
#define OPC_USRCD       0x0A // User Code?

/* IAP Packet Constants */
#define IAP_HEADER_SIZE 8
#define IAP_DATA_SIZE   32
#define IAP_PACKET_SIZE (IAP_HEADER_SIZE + IAP_DATA_SIZE)

/* Functions */
void IAP_Init(void);
void IAP_MainLoop(void); /* The iap_i2c function */
void IAP_CheckAndSelfUpdate(void);

/* Web Upgrade Interface */
void IAP_Web_Init(uint32_t total_len);
int IAP_Web_Write(uint8_t *data, uint16_t len);
int IAP_Web_Finish(void);

#endif /* __BSP_IAP_H */
