/*!
 * @file       BOARD_YT8522C.c
 *
 * @brief      This file provides all the config YT8522C PHY functions
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
#include "board_YT8522C.h"
#include "apm32f10x_rcm.h"
#include "apm32f10x_eth.h"
#include "apm32f10x_gpio.h"
#include "apm32f10x_misc.h"
#include "opt.h"
#include "netif.h"
#include <stdint.h>
#include <stdio.h>

extern void Ethernet_ResetDelay(uint32_t nCount);

/** @addtogroup Examples
  * @{
  */

/** @addtogroup ETH_Ping
  * @{
  */

/** @defgroup ETH_Ping_Variables Variables
  @{
*/

/* ETH Init Structure */
ETH_Config_T ETH_InitStructure;
/* lwip network interface structure for ethernetif */
extern struct netif UserNetif;

/**@} end of group ETH_Ping_Variables */

/** @defgroup ETH_Ping_Functions Functions
  @{
*/

/* Configure the GPIO ports for ethernet pins */
static void Ethernet_GPIOConfig(void);
/* Configure the Ethernet MAC/DMA */
static void Ethernet_MACDMAConfig(uint16_t addr);
/* Ethernet Reset Delay */
void Ethernet_ResetDelay(__IO uint32_t count);
/* PHY Debug Dump */
void Ethernet_PHY_DebugDump(uint16_t addr);

/*!
 * @brief  configurate Ethernet.
 *
 * @param  None
 *
 * @retval None
 *
 * @note
 */
void Ethernet_Config(void)
{
    /* 确保全局时钟变量准确反映 100MHz */
    SystemCoreClockUpdate();

    /* --- Clock & Status Diagnostics --- */
    printf("[ETH] System Clock: %u Hz\n", SystemCoreClock);
    
    if (RCM_ReadStatusFlag(RCM_FLAG_HSERDY)) {
        printf("[ETH] HSE Status: READY\n");
    } else {
        printf("[ETH] HSE Status: NOT READY (Warning: PHY Clock issues if MCO used)\n");
    }

#ifdef PHY_CLOCK_MCO
    printf("[ETH] MCO Output: ENABLED (Source: HSE on PA8)\n");
    /* 1. 强制输出 MCO 时钟 (PA8) 为 25MHz (HSE 直接输出) */
    /* 必须在 PHY 复位前开启，确保 PHY 启动时有时钟 */
    RCM_EnableAPB2PeriphClock(RCM_APB2_PERIPH_GPIOA);
    GPIO_Config_T gpioConfig;
    gpioConfig.pin = GPIO_PIN_8;
    gpioConfig.mode = GPIO_MODE_AF_PP;
    gpioConfig.speed = GPIO_SPEED_50MHz;
    GPIO_Config(GPIOA, &gpioConfig);
    RCM_ConfigMCO(RCM_MCOCLK_HSE); 
    Ethernet_ResetDelay(0x100000);
#else
    printf("[ETH] MCO Output: DISABLED (Assuming external Crystal for PHY)\n");
#endif

    /* 2. 执行 MAC 硬件复位 */
    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_ETH_MAC | RCM_AHB_PERIPH_ETH_MAC_TX |
                              RCM_AHB_PERIPH_ETH_MAC_RX);
    RCM_EnableAHBPeriphReset(RCM_AHB_PERIPH_ETH_MAC);
    Ethernet_ResetDelay(0x200000);
    RCM_DisableAHBPeriphReset(RCM_AHB_PERIPH_ETH_MAC);
    Ethernet_ResetDelay(0x20000);

    /* 3. 配置 GPIO (包含 PHY 硬件复位) */
    Ethernet_GPIOConfig();
    
    /* 4. 等待 PHY 硬件复位后稳定 */
    /* 减少等待时间 */
    Ethernet_ResetDelay(0x100000);

    /* 5. 设置 MDC 分频并验证 (针对 100MHz HCLK 使用 DIV62) */
    ETH->ADDR = (ETH->ADDR & ~0x1C) | (ETH_MACMIIAR_CR_DIV62 << 2);
    Ethernet_ResetDelay(0x10000); 
    
    printf("[ETH] MAC Init. ADDR: 0x%08X, REMAP1: 0x%08X, MACCR: 0x%08X\n", 
           ETH->ADDR, AFIO->REMAP1, ETH->CTRL);

    /* Perform PHY Software Reset */
    printf("[ETH] PHY Software Reset...\n");
    ETH_WritePHYRegister(PHY_ADDRESS, PHY_BCR, PHY_RESET);
    Ethernet_ResetDelay(0x50000); /* Wait for reset to complete */

    
    /* 
       Enable Auto-Negotiation.
       Reverting from forced 100M/FD to solve Link Down issues with modern switches.
    */
    printf("[ETH] Configuring PHY: Enable Auto-Negotiation...\n");
    /* 0x0200 is Restart Auto-Negotiation bit */
    ETH_WritePHYRegister(PHY_ADDRESS, PHY_BCR, PHY_AUTONEGOTIATION | 0x0200);
    
    /* Link UP Wait Loop Removed for speed */


    /* 
       锁定使用头文件定义的 PHY_ADDRESS。
       虽然扫描可能会发现多个地址（如 0x00），但应以硬件设计定义的 0x03 为准。
    */
    uint16_t foundAddr = PHY_ADDRESS;
    uint16_t id1 = ETH_ReadPHYRegister(foundAddr, 2);
    
    if (id1 == 0xFFFF || id1 == 0x0000) {
        printf("[ETH] PHY at Addr 0x%02X not responding! Scanning for alternatives...\n", foundAddr);
        for (int i = 0; i < 32; i++) {
            id1 = ETH_ReadPHYRegister(i, 2);
            uint16_t id2 = ETH_ReadPHYRegister(i, 3);
            if (id1 != 0xFFFF && id1 != 0x0000) {
                printf("[ETH] Alternative PHY found at Addr: 0x%02X, ID1: 0x%04X, ID2: 0x%04X\n", i, id1, id2);
                foundAddr = i;
                break;
            }
        }
    } else {
        uint16_t id2 = ETH_ReadPHYRegister(foundAddr, 3);
        printf("[ETH] Confirmed PHY at Addr: 0x%02X, ID1: 0x%04X, ID2: 0x%04X\n", foundAddr, id1, id2);
        
        /* Optional: Verify YT8522C ID2 (0xE928 or similar) */
        if (id2 == 0xE928) {
            printf("[ETH] PHY ID Match: YT8522C detected.\n");
        } else {
            printf("[ETH] PHY ID Mismatch? Expected 0xE928 for YT8522C.\n");
        }
    }

    if (id1 == 0xFFFF || id1 == 0x0000) {
        printf("[ETH] No PHY detected on MDIO bus! Please check Hardware (Power/Clock/Reset/MDIO).\n");
        Ethernet_PHY_DebugDump(PHY_ADDRESS);
        Ethernet_MACDMAConfig(PHY_ADDRESS);
    } else {
        /* Ethernet_PHY_DebugDump(foundAddr); */ /* Disabled for speed */
        Ethernet_MACDMAConfig(foundAddr);
    }
}

/*!
 * @brief  Dump important PHY registers for debugging
 */
void Ethernet_PHY_DebugDump(uint16_t addr)
{
    printf("\n--- PHY 0x%02X Diagnostic Dump ---\n", addr);
    
    uint16_t regs[] = {0, 1, 2, 3, 4, 5, 9, 10, 15, 17, 18};
    const char* names[] = {"BCR", "BSR", "ID1", "ID2", "AN_ADV", "AN_LPA", "GBCR", "GBSR", "EXT_STAT", "PHY_STAT", "PHY_INT"};
    
    for(int i=0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        /* 确保分频系数 (DIV62 适用于 100MHz) */
        ETH->ADDR = (uint32_t)((ETH->ADDR & ~0x1C) | (ETH_MACMIIAR_CR_DIV62 << 2));
        
        uint16_t val = ETH_ReadPHYRegister(addr, regs[i]);
        printf("[PHY] Reg %02d (%-8s): 0x%04X\n", regs[i], names[i], val);
        Ethernet_ResetDelay(0x10000);
    }
    
    /* 特别检查 YT8522C 可能的特定寄存器 (如果手册已知) */
    /* 通常寄存器 17 或 31 包含扩展状态 */
    
    if (ETH_ReadPHYRegister(addr, 2) == 0xFFFF) {
        printf("[PHY] ERROR: MDIO Read returning 0xFFFF. Possible causes:\n");
        printf("      1. PHY not powered\n");
        printf("      2. MDIO/MDC pins not connected or misconfigured\n");
        printf("      3. PHY Reset pin (PE11) is active LOW and not released\n");
        printf("      4. PHY Address is not 0x%02X (Hardware strapped differently)\n", addr);
    }
    printf("----------------------------------\n\n");
}

/**
 * @brief  Configure ETH Init Structure with default values
 */
static void Ethernet_DefaultConfigInit(ETH_Config_T* ETH_ConfigStruct)
{
    ETH_ConfigStructInit(ETH_ConfigStruct);

    /* 
       为了防止初始化时因网线未插或 Link Down 导致 ETH_Config 超时失败，
       此处暂时关闭阻塞式的自动协商等待。MAC 将使用默认的 100M/FullDuplex 配置启动。
       实际的 Link 状态同步应在后续通过轮询 PHY 状态寄存器来实现。
    */
    ETH_ConfigStruct->autoNegotiation = ETH_AUTONEGOTIATION_DISABLE;
    ETH_ConfigStruct->speed = ETH_SPEED_100M;
    ETH_ConfigStruct->mode = ETH_MODE_FULLDUPLEX;
    ETH_ConfigStruct->loopbackMode = ETH_LOOPBACKMODE_DISABLE;
    ETH_ConfigStruct->retryTransmission = ETH_RETRYTRANSMISSION_DISABLE;
    ETH_ConfigStruct->automaticPadCRCStrip = ETH_AUTOMATICPADCRCSTRIP_DISABLE;
    ETH_ConfigStruct->receiveAll = ETH_RECEIVEAll_DISABLE;
    ETH_ConfigStruct->broadcastFramesReception = ETH_BROADCASTFRAMESRECEPTION_ENABLE;
    ETH_ConfigStruct->promiscuousMode = ETH_PROMISCUOUS_MODE_DISABLE;
    ETH_ConfigStruct->multicastFramesFilter = ETH_MULTICASTFRAMESFILTER_PERFECT;
    ETH_ConfigStruct->unicastFramesFilter = ETH_UNICASTFRAMESFILTER_PERFECT;

#ifdef HARDWARE_CHECKSUM
    /* Disable hardware checksum offload to avoid conflict with LwIP software checksum */
    ETH_ConfigStruct->checksumOffload = ETH_CHECKSUMOFFLAOD_DISABLE;
#endif

    /* ETH Config DMA */
    ETH_ConfigStruct->dropTCPIPChecksumErrorFrame = ETH_DROPTCPIPCHECKSUMERRORFRAME_DISABLE;
    ETH_ConfigStruct->receiveStoreForward = ETH_RECEIVESTOREFORWARD_ENABLE;
    ETH_ConfigStruct->flushReceivedFrame = ETH_FLUSHRECEIVEDFRAME_DISABLE;
    ETH_ConfigStruct->transmitStoreForward = ETH_TRANSMITSTOREFORWARD_ENABLE;

    ETH_ConfigStruct->forwardErrorFrames = ETH_FORWARDERRORFRAMES_DISABLE;
    ETH_ConfigStruct->forwardUndersizedGoodFrames = ETH_FORWARDUNDERSIZEDGOODFRAMES_DISABLE;
    ETH_ConfigStruct->secondFrameOperate = ETH_SECONDFRAMEOPERARTE_DISABLE;
    ETH_ConfigStruct->addressAlignedBeats = ETH_ADDRESSALIGNEDBEATS_ENABLE;
    ETH_ConfigStruct->fixedBurst = ETH_FIXEDBURST_ENABLE;
    ETH_ConfigStruct->rxDMABurstLength = ETH_RXDMABURSTLENGTH_16BEAT;
    ETH_ConfigStruct->txDMABurstLength = ETH_TXDMABURSTLENGTH_16BEAT;
    ETH_ConfigStruct->DMAArbitration = ETH_DMAARBITRATION_ROUNDROBIN_RXTX_1_1;
}

/*!
 * @brief  configurate Ethernet Interface.
 *
 * @param  addr PHY address
 *
 * @retval None
 *
 * @note
 */
static void Ethernet_MACDMAConfig(uint16_t addr)
{
    /* Reset ETH on AHB Bus */
    ETH_Reset();

    /* Software reset */
    ETH_SoftwareReset();

    /* Wait for software reset */
    while (ETH_ReadSoftwareReset() == SET);
    
    /* Configure ETH IRQ */
    NVIC_EnableIRQRequest(ETH_IRQn, 0x01, 0x01);
    
    /* Enable ETH RX DMA interrupt */
    ETH_EnableDMAInterrupt(ETH_DMA_INT_RX);
    
    /* ETH Config Struct */
    Ethernet_DefaultConfigInit(&ETH_InitStructure);

    /* Configure Ethernet */
    uint32_t ethStatus = ETH_Config(&ETH_InitStructure, addr);
    if (ethStatus == ETH_SUCCESS) {
        printf("[ETH] Config Success! PHY Addr: 0x%02X\n", addr);
    } else {
        printf("[ETH] Config FAILED! Status: 0x%08X. Checking MDIO...\n", ethStatus);
    }
    (void)ethStatus; /* Prevent unused variable warning */
}

/**
 * @brief  Configure Ethernet with a specific PHY address
 */
uint32_t Ethernet_Config_With_Addr(uint16_t addr)
{
    /* Enable ETHERNET clock  */
    RCM_EnableAHBPeriphClock(RCM_AHB_PERIPH_ETH_MAC | RCM_AHB_PERIPH_ETH_MAC_TX |
                              RCM_AHB_PERIPH_ETH_MAC_RX);

    /* Reset ETH on AHB Bus */
    ETH_Reset();
    ETH_SoftwareReset();
    while (ETH_ReadSoftwareReset() == SET);
    
    /* Re-init structure and config */
    Ethernet_DefaultConfigInit(&ETH_InitStructure);
    
    return ETH_Config(&ETH_InitStructure, addr);
}

/*!
 * @brief  configurate Ethernet GPIO.
 *
 * @param  None
 *
 * @retval None
 *
 * @note
 */
void Ethernet_GPIOConfig(void)
{
    GPIO_Config_T configStruct;
    
    /* 1. 开启所有相关的 GPIO 和 AFIO 时钟 */
    /* 添加 GPIOD 时钟，因为 RXD0-RXD3/RXDV 移动到了 PD8-PD12 */
    RCM_EnableAPB2PeriphClock(ETH_MDIO_GPIO_CLK | ETH_MDC_GPIO_CLK | 
                              ETH_CRS_GPIO_CLK | ETH_RXCLK_GPIO_CLK | 
                              ETH_COL_GPIO_CLK | ETH_RXD3_GPIO_CLK | 
                              ETH_RXD2_GPIO_CLK | ETH_RXD1_GPIO_CLK | 
                              ETH_RXD0_GPIO_CLK | ETH_RXDV_GPIO_CLK | 
                              ETH_RXER_GPIO_CLK | ETH_TXEN_GPIO_CLK | 
                              ETH_TXD0_GPIO_CLK | ETH_TXD1_GPIO_CLK | 
                              ETH_TXD2_GPIO_CLK | ETH_TXD3_GPIO_CLK | 
                              ETH_TXCLK_GPIO_CLK | ETH_RESET_GPIO_CLK |
                              RCM_APB2_PERIPH_GPIOD | RCM_APB2_PERIPH_AFIO);
    
    /* 2. 配置 ETH MAC 接口模式 和 重映射 */
    /* 
       注意：用户硬件使用了 PD8-PD12 作为 RX 引脚。
       在 STM32/APM32F107 中，这属于 ETH 的重映射配置 (ETH_Remap)。
       必须调用 GPIO_ConfigPinRemap(GPIO_REMAP_ETH_MAC) 来启用此映射。
    */
    GPIO_ConfigPinRemap(GPIO_REMAP_ETH_MAC);

#if defined(MII_MODE)
    /* 强制 MII 模式 */
    GPIO_ConfigPinRemap(GPIO_REMAP_MACEISEL_MII);
#elif defined(RMII_MODE)
    /* 强制 RMII 模式 */
    GPIO_ConfigPinRemap(GPIO_REMAP_MACEISEL_RMII);
#endif

    printf("[ETH] AFIO_REMAP1 check: 0x%08X\n", AFIO->REMAP1);

    /* 3. 使用头文件定义的宏进行引脚配置 */
    configStruct.speed = GPIO_SPEED_50MHz;

    /* --- 管理接口 (MDIO/MDC) --- */
    /* MDIO (AF_PP) */
    configStruct.pin = ETH_MDIO_PIN;
    configStruct.mode = GPIO_MODE_AF_OD;
    GPIO_Config(ETH_MDIO_PORT, &configStruct);

    /* MDC (AF_PP) */
    configStruct.pin = ETH_MDC_PIN;
    configStruct.mode = GPIO_MODE_AF_OD;
    GPIO_Config(ETH_MDC_PORT, &configStruct);

    /* --- 发送组 (TX) --- */
    /* TX_EN, TXD0, TXD1, TXD2, TXD3 (AF_PP) */
    configStruct.mode = GPIO_MODE_AF_PP;
    
    configStruct.pin = ETH_TXEN_PIN;
    GPIO_Config(ETH_TXEN_PORT, &configStruct);
    
    configStruct.pin = ETH_TXD0_PIN;
    GPIO_Config(ETH_TXD0_PORT, &configStruct);
    
    configStruct.pin = ETH_TXD1_PIN;
    GPIO_Config(ETH_TXD1_PORT, &configStruct);
    
    configStruct.pin = ETH_TXD2_PIN;
    GPIO_Config(ETH_TXD2_PORT, &configStruct);
    
    configStruct.pin = ETH_TXD3_PIN;
    GPIO_Config(ETH_TXD3_PORT, &configStruct);

    /* TX_CLK (Floating Input) */
    configStruct.pin = ETH_TXCLK_PIN;
    configStruct.mode = GPIO_MODE_IN_FLOATING;
    GPIO_Config(ETH_TXCLK_PORT, &configStruct);

    /* --- 接收组 (RX) --- */
    /* RXD0, RXD1, RXD2, RXD3, RX_DV, RX_ER, RX_CLK, CRS, COL (Floating Input) */
    configStruct.mode = GPIO_MODE_IN_FLOATING;

    configStruct.pin = ETH_RXD0_PIN;
    GPIO_Config(ETH_RXD0_PORT, &configStruct);
    
    configStruct.pin = ETH_RXD1_PIN;
    GPIO_Config(ETH_RXD1_PORT, &configStruct);
    
    configStruct.pin = ETH_RXD2_PIN;
    GPIO_Config(ETH_RXD2_PORT, &configStruct);
    
    configStruct.pin = ETH_RXD3_PIN;
    GPIO_Config(ETH_RXD3_PORT, &configStruct);
    
    configStruct.pin = ETH_RXDV_PIN;
    GPIO_Config(ETH_RXDV_PORT, &configStruct);
    
    configStruct.pin = ETH_RXER_PIN;
    GPIO_Config(ETH_RXER_PORT, &configStruct);
    
    configStruct.pin = ETH_RXCLK_PIN;
    GPIO_Config(ETH_RXCLK_PORT, &configStruct);
    
    configStruct.pin = ETH_CRS_PIN;
    GPIO_Config(ETH_CRS_PORT, &configStruct);
    
    configStruct.pin = ETH_COL_PIN;
    GPIO_Config(ETH_COL_PORT, &configStruct);

    /* --- 复位引脚 --- */
    configStruct.pin = ETH_RESET_PIN;
    configStruct.mode = GPIO_MODE_OUT_PP;
    GPIO_Config(ETH_RESET_PORT, &configStruct);

    /* 4. 硬件复位 PHY (PE11) */
    printf("[ETH] PHY Hardware Reset pulse...\n");
    GPIO_ResetBit(GPIOE, GPIO_PIN_11);
    Ethernet_ResetDelay(0x20000); // Reduced delay
    GPIO_SetBit(GPIOE, GPIO_PIN_11);
    printf("[ETH] PHY Hardware Reset released.\n");
    Ethernet_ResetDelay(0x50000); // Reduced delay
    
    /* 5. 再次强制设置 MDC 分频 (针对 100MHz HCLK，使用 DIV62 得到 ~1.6MHz) */
    /* 直接操作寄存器以确保万无一失 */
    ETH->ADDR = (ETH->ADDR & ~0x1C) | (ETH_MACMIIAR_CR_DIV62 << 2);
}

/*!
 * @brief  Ethernet Reset Delay.
 *
 * @param  count  Reset Delay Time
 *
 * @retval None
 *
 * @note
 */
void Ethernet_ResetDelay(__IO uint32_t count)
{
    __IO uint32_t i = 0;
    
    for (i = count; i != 0; i--)
    {
    }
}

/**@} end of group ETH_Ping_Functions */
/**@} end of group ETH_Ping */
/**@} end of group Examples */
