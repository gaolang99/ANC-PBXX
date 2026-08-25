#ifndef __BSP_ADC_H
#define __BSP_ADC_H

#include "apm32f10x.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_adc.h"
#include "apm32f10x_rcm.h"
#include "apm32f10x_tmr.h"
#include "apm32f10x_dma.h"

// ADC Channels
#define ADC_CH_VCC5     0   // ADC1 - PA4 - ADC_CHANNEL_4
#define ADC_CH_VCC3_DL  1   // ADC2 - PA5 - ADC_CHANNEL_5
#define ADC_CH_12V_OUT1 2   // ADC3 - PA6 - ADC_CHANNEL_6
#define ADC_CH_12V_OUT2 3   // ADC4 - PA7 - ADC_CHANNEL_7
#define ADC_CH_12V_OUT3 4   // ADC5 - PC4 - ADC_CHANNEL_14
#define ADC_CH_12V_OUT4 5   // ADC6 - PC5 - ADC_CHANNEL_15
#define ADC_CH_SMOKE    6   // ADC7 - PC0 - ADC_CHANNEL_10

// Voltage divider resistors
#define R_TOP_VCC       4990.0f
#define R_BOTTOM_VCC    10000.0f
#define R_TOP_12V       61900.0f
#define R_BOTTOM_12V    10000.0f

#define V_REF           3.3f    // ADC Reference Voltage
#define ADC_MAX         4095    // 12-bit ADC Max Value
#define ADC_CHANNEL_NUM 7       // Total Channels

// Global Buffer
extern uint16_t g_adc_dma_buffer[ADC_CHANNEL_NUM];

// ADC Status
extern volatile uint8_t g_adc_conversion_complete;

typedef enum {
    ADC_STATUS_IDLE,
    ADC_STATUS_CONVERTING,
    ADC_STATUS_COMPLETE
} ADC_STATUS_T;

// Functions
void ADC_Init(void);
void TMR_ADC_Init(void);
uint16_t ADC_ReadRaw(uint8_t ch);
float ADC_ReadActualVoltage(uint8_t ch);
ADC_STATUS_T ADC_GetConversionStatus(void);

#endif /* __BSP_ADC_H */
