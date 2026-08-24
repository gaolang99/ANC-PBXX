#ifndef __BSP_POWER_MODULE_H
#define __BSP_POWER_MODULE_H

#include "apm32f10x.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_rcm.h"
#include "bsp_relay.h"
#include "bsp_led.h"
#include "bsp_beeper.h"
#include "bsp_adc.h"

// ?????????????
typedef enum {
    POWER_OUTPUT_TYPE_BOARD,  // ??????????
    POWER_OUTPUT_TYPE_HDD     // ????????
} PowerOutputType_T;

// ??????????
typedef enum {
    BOARD_1 = 0,
    BOARD_2 = 1,
    BOARD_3 = 2,
    BOARD_4 = 3,
    BOARD_MAX
} BoardId_T;

// ????????
typedef enum {
    HDD_1 = 0,
    HDD_2 = 1,
    HDD_3 = 2,
    HDD_4 = 3,
    HDD_MAX
} HDDId_T;

// ???????????
typedef enum {
    POWER_OUTPUT_OFF = 0,
    POWER_OUTPUT_ON = 1
} PowerOutputState_T;

// ??????GPIO??????? - ???PB0~PB2
#define HDD1_ENABLE_PIN              GPIO_PIN_0
#define HDD1_ENABLE_GPIO_PORT        GPIOB
#define HDD1_ENABLE_GPIO_CLK         RCM_APB2_PERIPH_GPIOB

#define HDD2_ENABLE_PIN              GPIO_PIN_1
#define HDD2_ENABLE_GPIO_PORT        GPIOB
#define HDD2_ENABLE_GPIO_CLK         RCM_APB2_PERIPH_GPIOB

#define HDD3_ENABLE_PIN              GPIO_PIN_2
#define HDD3_ENABLE_GPIO_PORT        GPIOB
#define HDD3_ENABLE_GPIO_CLK         RCM_APB2_PERIPH_GPIOB

// PSU Alert ??? - PE14, PE15 (??????)
#define PSU1_ALERT_PIN               GPIO_PIN_14
#define PSU1_ALERT_PORT              GPIOE
#define PSU1_ALERT_CLK               RCM_APB2_PERIPH_GPIOE

#define PSU2_ALERT_PIN               GPIO_PIN_15
#define PSU2_ALERT_PORT              GPIOE
#define PSU2_ALERT_CLK               RCM_APB2_PERIPH_GPIOE

// PWROK_5V????????????? - PA10
#define PWROK_5V_PIN                 GPIO_PIN_10
#define PWROK_5V_PORT                GPIOA
#define PWROK_5V_CLK                 RCM_APB2_PERIPH_GPIOA

// ???????LED???
#define BOARD1_STATUS_LED           BSP_LED1
#define BOARD2_STATUS_LED           BSP_LED2
#define BOARD3_STATUS_LED           BSP_LED3

// ????????????
typedef struct {
    uint8_t relay_init : 1;
    uint8_t led_init : 1;
    uint8_t beeper_init : 1;
    uint8_t adc_init : 1;
    uint8_t hdd_init : 1;
} PowerModuleInitStatus_T;

// ????????????
typedef struct {
    PowerOutputState_T state;        // ?????
    float voltage;                   // ????(V)
    uint32_t last_on_time;           // ??????????(ms)
    uint32_t on_duration;            // ?????????(ms)
} PowerOutputStatus_T;

// ????????????
typedef struct {
    PowerModuleInitStatus_T init_status;               // ???????
    PowerOutputStatus_T board_status[BOARD_MAX];       // ?????????
    PowerOutputStatus_T hdd_status[HDD_MAX];           // ???????
    float input_voltage;                               // ??????(V)
    uint8_t alarm_status;                              // ??????
    uint8_t power_io_state;                            // ????????????
    uint32_t power_io_last_change_time;                // ????????

    // PSU ???? (??? PMBus ???)
    struct {
        float vin;          // ?????? (V)
        float vout;         // ?????? (V)
        float iout;         // ??????? (A)
        float temp;         // ??? (C)
        uint16_t status;    // ??
        uint8_t online;     // ?
        uint32_t on_duration; // ? (ms)
				uint32_t last_on_time; // Last Turn On Time (ms)
    } psu_data[2];          // 0: PSU1, 1: PSU2

    // PSU Alert ?? (0=??, 1=????)
    uint8_t psu1_alert;
    uint8_t psu2_alert;
    
    // PWROK_DOUT ?? (1=????, 0=??)
    uint8_t pwrok_dout[4];
    
    // PWROK_A/B ?? (1=????, 0=??????)
    uint8_t pwrok_a;
    uint8_t pwrok_b;
    
    // PWROK_5V ?? (1=????, 0=??)
    uint8_t pwrok_5v;
    
    // PSU Reset ?? (0=????????, 1=????) -> NOW PRESENCE
    uint8_t psu1_present_pin;
    uint8_t psu2_present_pin;

    // ???????? (????LED??UI???)
    uint8_t psu1_present;       // PSU1 ????
    uint8_t psu2_present;       // PSU2 ????
    uint8_t psu1_normal;        // PSU1 
    uint8_t psu2_normal;        // PSU2 
    uint8_t dc_output_normal;   // DC 
} PowerModuleStatus_T;

// ????????????
extern PowerModuleStatus_T g_power_module_status;

// ????????
void PowerModule_Init(void);                         // ??????????
void PowerModule_UpdateStatus(void);                 // ????????????

// ???????????
void PowerModule_Board_Enable(BoardId_T board_id);   // ???????????
void PowerModule_Board_Disable(BoardId_T board_id);  // ??????????
void PowerModule_Board_Toggle(BoardId_T board_id);   // ?????????????
PowerOutputState_T PowerModule_Board_GetState(BoardId_T board_id);  // ????????????

// ????????
float PowerModule_GetInputVoltage(void);             // ?????????
float PowerModule_GetBoardVoltage(uint8_t board_id);  // ??????????

// ???????????
void PowerModule_CheckAndHandleAlarms(void);         // ????????????

// ????????????????
void PowerModule_PowerIO_Init(void);                 // ???????????????
void PowerModule_UpdatePowerIOState(void);           // ????????????????

void PowerModule_SetPSUState(bool enable);           // ????????
bool PowerModule_GetPSUState(void);                  // ?????????

// void PMBus_Update_PSU(uint8_t psu_idx, uint8_t address); // PMBus  PSU ??
// void PMBus_SetOperation(uint8_t address, bool on); // PMBus ??????
void PowerModule_SyncFromPB11MCU(void);              // Sync with PB11 MCU
static bool BSP_I2C_WriteRegisters(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t len);
void PowerModule_Task(void); // Periodic task for deferred operations
void PowerModule_SetPSUEnabled(uint8_t psu_id, bool enable);
bool PowerModule_GetPSUEnabled(uint8_t psu_id);
void PowerModule_SetConfigDirty(void);
bool PowerModule_IsAny12VOutput(void);

#endif /* __BSP_POWER_MODULE_H */
