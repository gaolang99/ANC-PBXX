/*!
 * @file       main.c
 *
 * @brief      ETH Main program body
 *
 * @version    V1.0.0
 *
 * @date       2022-11-10
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
#include "main.h"
#include "board_YT8522C.h"
#include "apm32f10x_rcm.h"
#include "apm32f10x_iwdt.h"
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "http_server.h"
#include "bsp_led.h"  
#include "bsp_beeper.h"
#include "apm32f10x_fmc.h"
#include "log_system.h"
#include "sys_arch.h"
#include "web_time_handler.h"
#include "lwip/timers.h"
extern volatile uint32_t g_sys_tick;
/** @addtogroup Examples
  * @{
  */

/** @addtogroup ETH_Ping
  * @{
  */

/** @defgroup ETH_Ping_Macros Macros
  @{
*/

#include "bsp_power_module.h"

#include "bsp_smoke_sensor.h"
#include "flash_storage.h"
#include "bsp_iap.h"
#include <stdio.h>

/* Redirect fputc to J-Link RTT */
int fputc(int ch, FILE *f) {
    (void)f;
    SEGGER_RTT_Write(0, &ch, 1);
    return ch;
}

#if defined (__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
__asm(" .global __use_no_semihosting");
#elif defined(__CC_ARM)
#pragma import(__use_no_semihosting)
struct __FILE { int handle; };
FILE __stdout;
void _sys_exit(int x) { x = x; }
void _ttywrch(int ch) { ch = ch; }
#endif

/**@} end of group ETH_Ping_Macros */

/** @defgroup ETH_Ping_Variables Variables
  * @{
  */

/* Global pointers on Rx descriptor used to transmit and receive descriptors */
extern ETH_DMADescConfig_T*  DMARxDescToGet;

/* Flag from ethernetif.c */
extern volatile uint8_t g_EthPacketReceived;
extern volatile uint32_t g_EthISRCount;

/* current Ethernet LocalTime value */
volatile uint32_t ETHTimer = 0;
/* lwip network interface structure for ethernetif */
struct netif UserNetif;

/* TCP periodic Timer */
uint32_t TCPTimer = 0;
/* ARP periodic Timer */
uint32_t ARPTimer = 0;
/* MAC address */
uint8_t SetMACaddr[6] = {0x00, 0x80, 0xE1, 0x00, 0x00, 0x01};

/* Menu title string*/
const char* menuTitleStr[] =
{
    "   ETH Web Server Example   ",
};

/* Menu bottom column string*/
const char* menuBottomColStr[] =
{
    "   GEEHY SEMICONDUCTOR   ",
};

/**@} end of group ETH_Ping_Variables */

/** @defgroup ETH_Ping_Functions Functions
  * @{
  */

/* Configure SysTick */
void ConfigSysTick(void);
/* User config the different system Clock */
void UserRCMClockConfig(void);
void APM_EVAL_Init(void);
void Menu_DisplayInit(void);

/*!
 * @brief     Main program
 *
 * @param     None
 *
 * @retval    None
 */
/* Global Reset Flag */
volatile uint8_t g_system_reset_pending = 0;
static uint32_t g_reset_timestamp = 0;

int main(void)
{
	PowerStatus pwrsta={0,0};
    buzzer_control_init();
    
    /* 
     * CRITICAL: Check for Pending Firmware Update (Self-Update Mode)
     * This must run BEFORE anything else, especially interrupts or network.
     * If valid firmware is found at 0x08020000, it will copy it to 0x08000000 and RESET.
     */
    IAP_CheckAndSelfUpdate();
    
    APM_EVAL_Init();
    
    /* User config the different system Clock */
    UserRCMClockConfig();

    /* MCO Output Disabled as per User Pinout (PHY uses external crystal) */
    /* Clock_OutputHseToMco(); */

    /* Initialize J-Link RTT */
    SEGGER_RTT_Init();
    
    /* Disable buffering for printf */
    setvbuf(stdout, NULL, _IONBF, 0);
    
    /* Direct RTT write to verify connection */
    SEGGER_RTT_WriteString(0, "\r\n\r\n[System] RTT Initialized (Direct Write)\r\n");
    
    /* Configure SysTick for 1ms interrupts */
    if (SysTick_Config(SystemCoreClock / 1000))
    {
        /* Capture error */
        while (1);
    }
    /* Initialize Power Module (includes Relay, LED, Beeper, ADC, and Power IO) */
    PowerModule_Init();
    
    
    
    /* Ensure PSUs are powered ON by default to match g_psu_enabled state */
    /* buzzer_control_init -> PWROPEN_Init sets PSON High (OFF), so we must pull it Low (ON) */
    // PowerModule_SetPSUState(true); // Handled in PowerModule_Init now to avoid resetting relays

    /* Initialize Smoke Sensor (includes ADC, GPIO, and RS485/UART5) */
    SmokeSensor_Init();
    
    /* Initialize Ethernet GPIO/MAC/DMA FIRST to avoid Flash interference */
    Ethernet_Config();
		
    /* Initialize LwIP Stack */
    LwIP_Init();
		
    /* Initialize Log System (moved after ETH init) */
    Log_System_Init();
    
    /* Print System Info */
    // printf("\n[System] Startup. HCLK: %d Hz\n", RCM_ReadHCLKFreq());
    
    /* Initialize Time Manager (moved after ETH init) */
    Time_Manager_Init();
    
    /* Check PHY Link Status */
    uint32_t timeout = 0;
    uint16_t phy_bsr = 0;

    /* Check PHY Link Status - Non-blocking / Quick Check */
    phy_bsr = ETH_ReadPHYRegister(PHY_ADDRESS, PHY_BSR);
    if ((phy_bsr & PHY_LINKED_STATUS) && (phy_bsr != 0xFFFF)) {
        printf("[System] PHY Linked! BSR: 0x%04X\n", phy_bsr);
    } else {
        printf("[System] PHY Link Not Ready Yet. BSR: 0x%04X. Continuing...\n", phy_bsr);
    }

    /* If PHY Linked, set Netif UP */
    if ((phy_bsr & PHY_LINKED_STATUS) && (phy_bsr != 0xFFFF))
    {
        netif_set_link_up(&UserNetif);
        // printf("[System] Netif Link UP set.\n");
    } else {
        /* This is normal if cable is not plugged in or auto-neg is still in progress */
        // printf("[System] PHY Link Not Ready (BSR: 0x%04X). Will retry in background.\n", phy_bsr);
        /* Do NOT set Link DOWN explicitly here as LwIP defaults to DOWN and needs polling or interrupt to go UP */
        /* netif_set_link_down(&UserNetif); */ 
    }
    
    /* Initialize HTTP Server */
    Http_Server_Init();

    /* Initialize Watchdog (IWDT) */
    /* LSI ~40kHz. Prescaler 256. Reload 1000. */
    /* Timeout = (256 * 1000) / 40000 = 6.4 seconds */
    /* This should be enough for any long tasks but catch system hangs. */
    // IWDT_EnableWriteAccess();
    // IWDT_ConfigDivider(IWDT_DIVIDER_256);
    // IWDT_ConfigReload(1000); 
    // IWDT_Enable();
    
    /* System Startup Complete */
    /* Removed verbose startup logs per user request */
    // LOG_INFO(LOG_MODULE_SYSTEM, "System startup complete");
    // LOG_INFO(LOG_MODULE_SYSTEM, "IP address: %s", ETH_IP_ADDR);
    
    /* Check Network Interface Status */
    if (netif_is_up(&UserNetif)) {
        // LOG_INFO(LOG_MODULE_SYSTEM, "Network interface is UP");
    } else {
        LOG_INFO(LOG_MODULE_SYSTEM, "Network interface is DOWN");
    }
    
    if (netif_is_link_up(&UserNetif)) {
        // LOG_INFO(LOG_MODULE_SYSTEM, "Network link is UP");
    } else {
        LOG_INFO(LOG_MODULE_SYSTEM, "Network link is DOWN");
    }
    
    LED_SetState(LED_OFF);
    uint32_t last_time_task = 0;
    // uint32_t last_http_task = 0; // Removed: HTTP task now runs every loop
    uint32_t last_led_toggle = 0;
    uint8_t led_state = 0;
    uint32_t last_power_update = 0;
    uint32_t last_smoke_update = 0; // Added for interleaved scheduling
    
    while (1)
    {		
        /* Check Reset Pending */
        if (g_system_reset_pending) {
            if (g_reset_timestamp == 0) {
                 g_reset_timestamp = sys_now();
            }
            // Wait 2 seconds for HTTP response to flush
            if (sys_now() - g_reset_timestamp > 2000) {
                 printf("[System] RESETTING...\n");
                 NVIC_SystemReset();
            }
        }

        /* Check for IAP Packets */
        IAP_MainLoop();

        /* Feed the Watchdog - Disabled */
        // IWDT_Refresh();
				
        /* --- 1. Network Processing (Highest Priority) --- */
        /* Poll multiple times to drain Rx buffer quickly */
        LwIP_Pkt_Handle();
        
        /* --- 2. HTTP Server (High Priority) --- */
        Http_Server_Task();
        
        /* If Upgrade is in Progress, SKIP all other tasks to prevent resource contention/interrupts */
        if (g_upgrade_in_progress) {
             /* Still need to process timers for LwIP retransmission if needed */
             sys_check_timeouts();
             LwIP_Periodic_Handle(ETHTimer);
             continue; 
        }

        /* --- 3. LwIP Periodic Tasks --- */
        sys_check_timeouts();
        if (ETHTimer < last_led_toggle) last_led_toggle = 0; // Timer wrap protection
        LwIP_Periodic_Handle(ETHTimer);

        /* --- 4. Interleaved Peripheral Tasks (Lower Priority) --- */
        /* Split heavy tasks to different time slots to avoid long blocking */
        
        /* Execute Deferred Power Tasks (Flash Write) */
        PowerModule_Task();
        
        /* Update Beeper State (Non-blocking beep) */
        Beeper_Update();
        
        /* --- LED Control Task (Every 50ms) --- */
        static uint32_t last_led_control = 0;
        if (ETHTimer - last_led_control >= 50) {
            last_led_control = ETHTimer;
            
            /* Read PHY Status for Link LED */
            static uint32_t last_eth_isr_count = 0;
            uint16_t phy_bsr = ETH_ReadPHYRegister(PHY_ADDRESS, PHY_BSR);
            bool link_up = (phy_bsr & PHY_LINKED_STATUS) && (phy_bsr != 0xFFFF);
            
            /* Check Activity */
            bool activity = false;
            if (g_EthISRCount != last_eth_isr_count) {
                activity = true;
                last_eth_isr_count = g_EthISRCount;
            }
            
            /* Check System Normal (No Alarms) */
            bool system_normal = (g_power_module_status.alarm_status == 0);
            
            /* Check 12V Output (Any channel > 8V) */
            bool output_12v_ok = PowerModule_IsAny12VOutput();
            
            /* Call centralized LED Task defined in bsp_led.c */
            LED_Task(system_normal, link_up, activity, output_12v_ok);
        }
        
        /* Check power signal and control buzzer */
        /* Only check every 1000ms to avoid excessive I/O reading and log spam */
        static uint32_t last_pwr_check = 0;
        if (ETHTimer - last_pwr_check >= 500) {
            last_pwr_check = ETHTimer;
            pwrsta = pwr_sig_detect();
            
            // Note: buzzer_SetByPwrState now handles ALL alarm conditions internally:
            // 1. PSU Presence & OK
            // 2. DOUT 1-4
            // 3. 5V
            // 4. Smoke Alarm
            // 5. Mute Logic (Global)
            buzzer_SetByPwrState(pwrsta);
        }
        if (ETHTimer - last_power_update >= 1000) { // Increase interval to 500ms
            last_power_update = ETHTimer;
            PowerModule_UpdateStatus();
            
            /* Give network a chance between peripheral tasks */
            LwIP_Pkt_Handle(); 
        }
        else if (ETHTimer - last_smoke_update >= 500) { // Increase interval to 500ms
            /* Offset smoke update to run in a different cycle than power update */
            last_smoke_update = ETHTimer;
            // SmokeSensor_Update();   // Update smoke sensor ADC/GPIO/RS485
            // I2C_Slave_UpdateData(); // Update I2C registers (Disabled, see I2C_Slave_Init)
            
            /* Give network a chance between peripheral tasks */
            LwIP_Pkt_Handle();
        }

        
        
        /* --- 5. Slow Periodic Tasks --- */
        if (ETHTimer - last_time_task >= 1000) {
            last_time_task = ETHTimer;
            Time_Manager_Task();
           // printf(".\n"); // Heartbeat to confirm loop is running
        }
        
        /* HTTP Server Periodic Task (Connection Management & Dynamic IP) */
        Http_Server_Task();

        /* Extra poll at end of loop */
        LwIP_Pkt_Handle();
    }

}

/*!
 * @brief       Init EVAL board
 *
 * @param       None
 *
 * @retval      None
 *
 * @note
 */
void APM_EVAL_Init(void)
{
    USART_Config_T usartConfigStruct;
    
    /* UART4 Config */
    usartConfigStruct.baudRate = 115200;
    usartConfigStruct.hardwareFlow = USART_HARDWARE_FLOW_NONE;
    usartConfigStruct.mode = USART_MODE_TX_RX;
    usartConfigStruct.parity = USART_PARITY_NONE;
    usartConfigStruct.stopBits = USART_STOP_BIT_1;
    usartConfigStruct.wordLength = USART_WORD_LEN_8B;
    APM_EVAL_COMInit(COM4, &usartConfigStruct);

    APM_LCDInit();
    LCD_Clear(0, 0, LCD_WIDTH, LCD_HEIGHT, LCD_BCOLOR);

    Menu_DisplayInit();
}

/*!
 * @brief       Initialize the Menu UI
 *
 * @param       None
 *
 * @retval      None
 *
 * @note
 */
void Menu_DisplayInit(void)
{
    char ipStr[24] = "IP: ";
    char maskStr[24] = "Mask: ";
    char gwStr[24] = "Gw: ";
    
    strcat(ipStr, ETH_IP_ADDR);
    strcat(maskStr, ETH_NET_MASK);
    strcat(gwStr, ETH_GATEWAY);
    
    /* Menu title */
    LCD_Clear(0, 0, LCD_WIDTH, 40, LCD_FCOLOR);
    LCD_DisplayString(20, LCD_LINE_TITLE, menuTitleStr[0], LCD_TEXT_SELECT_FC, LCD_TEXT_SELECT_BC, 16, 0);
    
    /* Menu item */
    LCD_DisplayString(10, LCD_LINE_2, "   Static IP address:   ", LCD_TEXT_FC, LCD_TEXT_BC, 16, 0);
    LCD_DisplayString(40, LCD_LINE_3, ipStr, LCD_TEXT_FC, LCD_TEXT_BC, 16, 0);
    LCD_DisplayString(40, LCD_LINE_4, maskStr, LCD_TEXT_FC, LCD_TEXT_BC, 16, 0);
    LCD_DisplayString(40, LCD_LINE_5, gwStr, LCD_TEXT_FC, LCD_TEXT_BC, 16, 0);
    
    /* Web Server Info */
    LCD_DisplayString(10, LCD_LINE_6, "   Web Server:          ", LCD_TEXT_FC, LCD_TEXT_BC, 16, 0);
    LCD_DisplayString(40, LCD_LINE_7, "http://192.168.1.199   ", LCD_TEXT_FC, LCD_TEXT_BC, 16, 0);
    
    /* Menu bottom */
    LCD_Clear(0, LCD_LINE_BOTTOM, LCD_WIDTH, LCD_HEIGHT, LCD_FCOLOR);
    LCD_DisplayString(20, LCD_LINE_BOTTOM, menuBottomColStr[0], LCD_TEXT_SELECT_FC, LCD_TEXT_SELECT_BC, 16, 0);
}

/*!
* @brief     Configure SysTick
*
* @param     None
*
* @retval    None
*/
void ConfigSysTick(void)
{
    /* Configured in main() */
    if (SysTick_Config(SystemCoreClock / 1000))
    {
        /* Capture error */
        while (1);
    }
}

/*!
 * @brief     This function initializes the lwIP stack
 *
 * @param     None
 *
 * @retval    None
 */
void LwIP_Init(void)
{
    struct ip_addr ipaddr;
    struct ip_addr netmask;
    struct ip_addr gw;

    int ipAddrTemp[4];
    int netMaskTemp[4];
    int gatewayTemp[4];

    /* Initializes the dynamic memory heap */
    mem_init();

    /* Initializes the memory pools */
    memp_init();

    /* Try to read IP config from Flash */
    Flash_Network_Config_T net_config;
    bool use_flash_config = false;

    if (Flash_Read_Network_Config(&net_config)) {
        printf("[System] Network Config loaded from Flash (Binary).\n");
        // Note: LwIP ip_addr struct has 'addr' field which is uint32_t
        ipaddr.addr = net_config.ip_addr;
        netmask.addr = net_config.netmask;
        gw.addr = net_config.gateway;
        
        printf("  IP: 0x%08X\n", ipaddr.addr);
        use_flash_config = true;
    }

    if (!use_flash_config) {
        printf("[System] Using Default Network Config.\n");
        sscanf(ETH_IP_ADDR, "%d.%d.%d.%d", &ipAddrTemp[0], &ipAddrTemp[1], &ipAddrTemp[2], &ipAddrTemp[3]);
        sscanf(ETH_NET_MASK, "%d.%d.%d.%d", &netMaskTemp[0], &netMaskTemp[1], &netMaskTemp[2], &netMaskTemp[3]);
        sscanf(ETH_GATEWAY, "%d.%d.%d.%d", &gatewayTemp[0], &gatewayTemp[1], &gatewayTemp[2], &gatewayTemp[3]);
        
        IP4_ADDR(&ipaddr, ipAddrTemp[0], ipAddrTemp[1], ipAddrTemp[2], ipAddrTemp[3]);
        IP4_ADDR(&netmask, netMaskTemp[0], netMaskTemp[1], netMaskTemp[2], netMaskTemp[3]);
        IP4_ADDR(&gw, gatewayTemp[0], gatewayTemp[1], gatewayTemp[2], gatewayTemp[3]);
    }

    /* Config MAC Address */
    uint8_t mac_addr[6] = {0x00, 0x80, 0xE1, 0x00, 0x00, 0x01};
    ETH_ConfigMACAddress(ETH_MAC_ADDRESS0, mac_addr);

    /* Add a network interface to the list of lwIP netifs */
    netif_add(&UserNetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);

    /* Registers the default network interface */
    netif_set_default(&UserNetif);

    /* When the netif is fully configured this function must be called */
    netif_set_up(&UserNetif);
    
    // printf("[LwIP] Init Success!\n");
    // // [FIX] Use ip4_addrX macros to print the actual configured IP from the netif/ipaddr struct
    // // This fixes the "Garbled IP" log issue when loading from Flash (where ipAddrTemp was uninitialized)
    // printf("[LwIP] IP: %d.%d.%d.%d\n", 
    //        ip4_addr1(&ipaddr), ip4_addr2(&ipaddr), ip4_addr3(&ipaddr), ip4_addr4(&ipaddr));
    // printf("[LwIP] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);


}

/*!
 * @brief     This function received ethernet packet
 *
 * @param     None
 *
 * @retval    None
 */
void LwIP_Pkt_Handle(void)
{
    /* Read a received packet from the Ethernet buffers and send it to the lwIP for handling */
    /* Poll multiple times to drain Rx buffer quickly, but limit to avoid starving other tasks */
    int count = 0;
    while (ethernetif_input(&UserNetif) == ERR_OK)
    {
        count++;
        if (count >= 10) break;
    }
}

/*!
 * @brief     This function LwIP periodic tasks
 *
 * @param     ETHTimer the current Ethernet Timer value
 *
 * @retval    None
 */
void LwIP_Periodic_Handle(__IO uint32_t local_ETHTimer)
{
#if LWIP_TCP
    /* TCP periodic process every 250 ms */
    if (local_ETHTimer - TCPTimer >= TCP_TMR_INTERVAL)
    {
        TCPTimer = local_ETHTimer;
        tcp_tmr();
    }
#endif

    /* ARP periodic process every 5s */
    if ((local_ETHTimer - ARPTimer) >= ARP_TMR_INTERVAL)
    {
        ARPTimer = local_ETHTimer;
        etharp_tmr();
    }
}

/*!
 * @brief     User config the different system Clock
 *
 * @param     None
 *
 * @retval    None
 */
void UserRCMClockConfig(void)
{
    /* Reset RCM clock configuration */
    RCM_Reset();
    
    /* Enable HSE oscillator */
    RCM_ConfigHSE(RCM_HSE_OPEN);
    
    /* Wait for HSE ready */
    while(RCM_ReadStatusFlag(RCM_FLAG_HSERDY) == RESET);
    
    /* Configure AHB, APB1, APB2 prescalers */
    RCM_ConfigAHB(RCM_AHB_DIV_1);   /* AHB = SYSCLK */
    RCM_ConfigAPB1(RCM_APB_DIV_2);  /* APB1 = HCLK/2 */
    RCM_ConfigAPB2(RCM_APB_DIV_1);  /* APB2 = HCLK */
    
    /* Configure PLL input and output clocks */
    /* For CL series (APM32F107), use RCM_PLLSEL_PREDIV1 as PLL source */
    /* HSE is 25MHz, use PLL prescaler 1 to divide by 1, then multiply by 4 to get 100MHz system clock */
    /* APM32F107 maximum system clock is 120MHz */
    /* First configure PLL prescaler 1 */
    RCM_ConfigPLLPSC1(RCM_PLLPSC1_SRC_HSE, RCM_PLLPSC1_DIV_1);
    /* Then configure PLL1 with multiplier 4 (25MHz * 4 = 100MHz) */
    RCM_ConfigPLL(RCM_PLLSEL_PREDIV1, RCM_PLLMF_4);
    
    /* Enable PLL */
    RCM_EnablePLL();
    
    /* Wait for PLL ready */
    while(RCM_ReadStatusFlag(RCM_FLAG_PLLRDY) == RESET);
    
    /* Select PLL as system clock */
    RCM_ConfigSYSCLK(RCM_SYSCLK_SEL_PLL);
    
    /* Wait for system clock switch to complete */
    while(RCM_ReadSYSCLKSource() != RCM_SYSCLK_SEL_PLL);

    /* Update SystemCoreClock variable to reflect 100MHz */
    SystemCoreClockUpdate();
}

/**@} end of group ETH_Ping_Functions */
/**@} end of group ETH_Ping */
/**@} end of group Examples */
