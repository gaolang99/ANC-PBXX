#ifndef __BSP_RS485_H
#define __BSP_RS485_H

#include "apm32f10x.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_rcm.h"
#include "apm32f10x_usart.h"

// RS485 GPIO???????
// UART5?????????
#define RS485_UART_TX_PIN            GPIO_PIN_12  // UART5_TX (PC12) - ??????RDE????
#define RS485_UART_RX_PIN            GPIO_PIN_2   // UART5_RX (PD2) - ??????????????
#define RS485_UART_TX_PORT           GPIOC
#define RS485_UART_RX_PORT           GPIOD
#define RS485_UART_GPIO_CLK          RCM_APB2_PERIPH_GPIOC | RCM_APB2_PERIPH_GPIOD

// RS485????/????????R_DE??- ??UART5_TX???????PC12??
// ???RDE??????UART5_TX???????????????????????¡¤??????SIT3485ESA??RE/DE????
#define RS485_RDE_PIN                RS485_UART_TX_PIN
#define RS485_RDE_GPIO_PORT          RS485_UART_TX_PORT
#define RS485_RDE_GPIO_CLK           RCM_APB2_PERIPH_GPIOC

// RS485???UART5??????????????PC11=UART5_TX, PD2=UART5_RX??
#define RS485_USART                  UART5
#define RS485_USART_CLK              RCM_APB1_PERIPH_UART5

// RS485????????§³
#define RS485_BUFFER_SIZE            256

// 
void RS485_Init(void);
void RS485_SendByte(uint8_t data);
void RS485_SendData(uint8_t *data, uint16_t len);
uint8_t RS485_ReceiveByte(void);
uint16_t RS485_ReceiveData(uint8_t *buffer, uint16_t len);
void RS485_ClearBuffer(void);
uint16_t RS485_GetAvailableData(void);
void RS485_SetToSendMode(void);
void RS485_SetToReceiveMode(void);
uint8_t RS485_ReadFromBuffer(void);

#endif /* __BSP_RS485_H */
