/*!
 * @file        bsp_sdio.c
 *
 * @brief       SDIO board support package body
 *
 * @version     V1.0.0
 *
 * @date        2022-11-15
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
#include "bsp_w25q16.h"
#include <stdio.h>

/** @addtogroup Board
  * @brief Board support package
  @{
  */

/** @addtogroup Board_APM32F107_EVAL
  * @brief APM32F107 EVAL board support package
  @{
*/

/** @defgroup APM32F107_EVAL_Fuctions Fuctions
  @{
  */

/*!
 * @brief       W25Q16 SPI Initialization
 *
 * @param       None
 *
 * @retval      None
 */
void W25Q16_SPI_Init(void)
{
    GPIO_Config_T gpioConfig;
    SPI_Config_T spiConfig;

    /* Enable related Clock */
    RCM_EnableAPB1PeriphClock(FLASH_SPI_BUS_CLK);
    RCM_EnableAPB2PeriphClock(FLASH_SPI_GPIO_CLK | FLASH_SPI_CS_GPIO_CLK |RCM_APB2_PERIPH_AFIO);
    
    GPIO_ConfigPinRemap(GPIO_REMAP_SWJ_JTAGDISABLE);

    /* Configure FLASH_SPI pins: SCK */
    gpioConfig.pin =  FLASH_SPI_SCK_PIN ;
    gpioConfig.mode = GPIO_MODE_AF_PP;
    gpioConfig.speed = GPIO_SPEED_50MHz;
    GPIO_Config(FLASH_SPI_GPIO_PORT, &gpioConfig);

    /* Configure FLASH_SPI pins: MOSI */
    gpioConfig.pin = FLASH_SPI_MOSI_PIN;
    GPIO_Config(FLASH_SPI_GPIO_PORT, &gpioConfig);

    /* Configure FLASH_SPI pins: MISO */
    gpioConfig.pin = FLASH_SPI_MISO_PIN;
    gpioConfig.mode = GPIO_MODE_IN_FLOATING;
    GPIO_Config(FLASH_SPI_GPIO_PORT, &gpioConfig);

    /* Configure FLASH_SPI_CS_PIN pin: sFLASH Card CS pin */
    gpioConfig.pin = FLASH_SPI_CS_PIN;
    gpioConfig.mode = GPIO_MODE_OUT_PP;
    GPIO_Config(FLASH_SPI_CS_GPIO_PORT, &gpioConfig);

    /* Deselect the FLASH: Chip Select high */
    FLASH_SPI_CS_SET();

    /* SPI configuration */
    spiConfig.direction = SPI_DIRECTION_2LINES_FULLDUPLEX;
    spiConfig.mode = SPI_MODE_MASTER;
    spiConfig.length = SPI_DATA_LENGTH_8B;
    spiConfig.polarity = SPI_CLKPOL_HIGH;
    spiConfig.phase = SPI_CLKPHA_2EDGE;
    spiConfig.nss = SPI_NSS_SOFT;
    spiConfig.baudrateDiv = SPI_BAUDRATE_DIV_4;
    spiConfig.firstBit = SPI_FIRSTBIT_MSB;
    spiConfig.crcPolynomial = 7;
    SPI_Config(FLASH_SPI_BUS, &spiConfig);

    /* Enable SPI  */
    SPI_Enable(FLASH_SPI_BUS);
}

/*!
 * @brief       Erase Falsh Sector
 *
 * @param       SectorAddr:Sector Addr
 *
 * @retval      None
 */
void W25Q16_EraseSector(uint32_t SectorAddr)
{
    /* Flash Write Enable */
    W25Q16_EnableFlashWrite();
    W25Q16_WaitFlashWriteEnd();

    /* Select the FLASH */
    FLASH_SPI_CS_CLR();

    /* send cmd */
    W25Q16_SendByte(W25Q16_SECTOR_ERASE);

    /* send Sector Addr */
    W25Q16_SendByte((SectorAddr & 0xFF0000) >> 16);
    W25Q16_SendByte((SectorAddr & 0xFF00) >> 8);
    W25Q16_SendByte(SectorAddr & 0xFF);

    /* Deselect the FLASH */
    FLASH_SPI_CS_SET();

    /* wait Falsh Erase end */
    W25Q16_WaitFlashWriteEnd();
}

/*!
 * @brief       Flash Write Page
 *
 * @param       pBuffer: pointer to the Write buffer
 *
 * @param       WriteAddr: Write the flash Address
 *
 * @param       NumToWrite: the number of byte to write
 *
 * @retval      None
 */
void W25Q16_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumToWrite)
{
    uint16_t i = 0;

    /* Enable Flash Write */
    W25Q16_EnableFlashWrite();

    /* Select the FLASH */
    FLASH_SPI_CS_CLR();

    /* W25Q16 Page Program */
    W25Q16_SendByte(W25Q16_PAGE_PROGRAM);
    W25Q16_SendByte((WriteAddr & 0xFF0000) >> 16);
    W25Q16_SendByte((WriteAddr & 0xFF00) >> 8);
    W25Q16_SendByte(WriteAddr & 0xFF);

    if (NumToWrite > W25Q16_FLASH_PAGE_SIZE)
    {
        NumToWrite = W25Q16_FLASH_PAGE_SIZE;
    }

    /* Write data to flash */
    for (i = 0; i < NumToWrite; i++)
    {
        W25Q16_SendByte(*pBuffer);
        pBuffer++;
    }

    /* Deselect the FLASH */
    FLASH_SPI_CS_SET();

    /* Wait Write End */
    W25Q16_WaitFlashWriteEnd();
}

/*!
 * @brief       Flash Write Buffer
 *
 * @param       pBuffer: pointer to the Write buffer
 *
 * @param       WriteAddr: Write the flash Address
 *
 * @param       NumToWrite: the number of byte to write
 *
 * @retval      None
 */
void W25Q16_WriteBuffer(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumToWrite)
{
    uint8_t i = 0;
    uint8_t PageNumber = 0;
    uint8_t RemainNumber = 0;
    uint8_t PageRemainNumber = 0;

    /* complete Page need to write */
    PageNumber =  NumToWrite / W25Q16_FLASH_PAGE_SIZE;

    /* Remain byte */
    RemainNumber = NumToWrite % W25Q16_FLASH_PAGE_SIZE;

    /* if WriteAddr align */
    if ((WriteAddr % W25Q16_FLASH_PAGE_SIZE) == 0)
    {
        /* NumToWrite < W25Q16_FLASH_PAGE_SIZE */
        if (PageNumber == 0)
        {
            W25Q16_WritePage(pBuffer, WriteAddr, NumToWrite);
        }
        else
        {
            /* write complete Page */
            for (i = 0; i < PageNumber; i++)
            {
                W25Q16_WritePage(pBuffer, WriteAddr, W25Q16_FLASH_PAGE_SIZE);
                WriteAddr +=  W25Q16_FLASH_PAGE_SIZE;
                pBuffer += W25Q16_FLASH_PAGE_SIZE;
            }

            /* write remain data */
            if (RemainNumber > 0)
            {
                W25Q16_WritePage(pBuffer, WriteAddr, RemainNumber);
            }
        }
    }
    else
    {
        /* Write Address Page Remain Number */
        PageRemainNumber = W25Q16_FLASH_PAGE_SIZE - (WriteAddr % W25Q16_FLASH_PAGE_SIZE);

        /* NumToWrite < W25Q16_FLASH_PAGE_SIZE */
        if (PageNumber == 0)
        {
            if (RemainNumber > PageRemainNumber)
            {
                /* write all over the current page */
                W25Q16_WritePage(pBuffer, WriteAddr, PageRemainNumber);
                WriteAddr +=  PageRemainNumber;
                pBuffer += PageRemainNumber;

                RemainNumber = RemainNumber - PageRemainNumber;;

                /* write remain data */
                W25Q16_WritePage(pBuffer, WriteAddr, RemainNumber);
            }
            else
            {
                W25Q16_WritePage(pBuffer, WriteAddr, RemainNumber);
            }
        }
        else
        {
            /* write all over the current page */
            W25Q16_WritePage(pBuffer, WriteAddr, PageRemainNumber);
            WriteAddr +=  PageRemainNumber;
            pBuffer += PageRemainNumber;

            NumToWrite -= PageRemainNumber;
            PageNumber =  NumToWrite / W25Q16_FLASH_PAGE_SIZE;
            RemainNumber = NumToWrite % W25Q16_FLASH_PAGE_SIZE;

            /* write complete Page */
            for (i = 0; i < PageNumber; i++)
            {
                W25Q16_WritePage(pBuffer, WriteAddr, W25Q16_FLASH_PAGE_SIZE);
                WriteAddr +=  W25Q16_FLASH_PAGE_SIZE;
                pBuffer += W25Q16_FLASH_PAGE_SIZE;
            }

            /* write remain data */
            if (RemainNumber > 0)
            {
                W25Q16_WritePage(pBuffer, WriteAddr, RemainNumber);
            }
        }
    }
}

/*!
 * @brief       Flash Read Buffer
 *
 * @param       pBuffer: pointer to the read buffer
 *
 * @param       ReadAddr: read the flash Address
 *
 * @param       NumToWrite: the number of byte to read
 *
 * @retval      None
 */
void W25Q16_ReadBuffer(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumToRead)
{
    uint16_t i = 0;

    /* Select the FLASH */
    FLASH_SPI_CS_CLR();

    /* send read cmd */
    W25Q16_SendByte(W25Q16_READ_DATA);
    W25Q16_SendByte((ReadAddr & 0xFF0000) >> 16);
    W25Q16_SendByte((ReadAddr& 0xFF00) >> 8);
    W25Q16_SendByte(ReadAddr & 0xFF);

    /* read data to flash*/
    for (i = 0; i < NumToRead; i++)
    {
        *pBuffer = W25Q16_SendByte(W25Q16_DUMMY_BYTE);
        pBuffer++;
    }

    /* Deselect the FLASH */
    FLASH_SPI_CS_SET();
}

/*!
 * @brief       Flash Read ID
 *
 * @param       None
 *
 * @retval      None
 */
uint32_t W25Q16_ReadFlashID(void)
{
    uint32_t TempBuffer[3] = {0};
    uint32_t FlashID = 0;

    /* Select the FLASH */
    FLASH_SPI_CS_CLR();

    /* send read ID cmd */
    W25Q16_SendByte(W25Q16_JEDEC_DEVICE_ID);
    TempBuffer[0] = W25Q16_SendByte(W25Q16_DUMMY_BYTE);
    TempBuffer[1] = W25Q16_SendByte(W25Q16_DUMMY_BYTE);
    TempBuffer[2] = W25Q16_SendByte(W25Q16_DUMMY_BYTE);

    /* Deselect the FLASH */
    FLASH_SPI_CS_SET();

    FlashID = (TempBuffer[0] << 16) | (TempBuffer[1] << 8) | TempBuffer[2];

    return FlashID;
}

/*!
 * @brief       Read Flash DeviceID
 *
 * @param       None
 *
 * @retval      None
 */
uint32_t W25Q16_ReadFlashDeviceID(void)
{
    uint32_t DeviceID = 0;

    /* Select the FLASH */
    FLASH_SPI_CS_CLR();

    /* Send W25Q16 DeviceID cmd */
    W25Q16_SendByte(W25Q16_DEVICE_ID);
    W25Q16_SendByte(W25Q16_DUMMY_BYTE);
    W25Q16_SendByte(W25Q16_DUMMY_BYTE);
    W25Q16_SendByte(W25Q16_DUMMY_BYTE);

    /* Read DeviceID from the FLASH */
    DeviceID = W25Q16_SendByte(W25Q16_DUMMY_BYTE);

    /* Deselect the FLASH */
    FLASH_SPI_CS_SET();

    return DeviceID;
}

/*!
 * @brief       SPI read Byte from flash
 *
 * @param       None
 *
 * @retval      None
 */
uint8_t W25Q16_ReadByte(void)
{
    uint8_t temp = 0;

    temp = W25Q16_SendByte(W25Q16_DUMMY_BYTE);

    return temp;
}

/*!
 * @brief       SPI Send Byte to flash
 *
 * @param       None
 *
 * @retval      None
 */
uint8_t W25Q16_SendByte(uint8_t data)
{
    /* SPI master send data */
    while (SPI_I2S_ReadStatusFlag(FLASH_SPI_BUS, SPI_FLAG_TXBE) == RESET);

    SPI_I2S_TxData(FLASH_SPI_BUS, data);

    /* SPI slave receive data */
    while (SPI_I2S_ReadStatusFlag(FLASH_SPI_BUS, SPI_FLAG_RXBNE) == RESET);

    return SPI_I2S_RxData(FLASH_SPI_BUS);
}

/*!
 * @brief       Enable Flash Write
 *
 * @param       None
 *
 * @retval      None
 */
void W25Q16_EnableFlashWrite(void)
{
    FLASH_SPI_CS_CLR();

    /* send W25Q16 Write Enable cmd */
    W25Q16_SendByte(W25Q16_WRITE_ENABLE);

    FLASH_SPI_CS_SET();
}

/*!
 * @brief       Wait Flash Write End
 *
 * @param       None
 *
 * @retval      None
 */
void W25Q16_WaitFlashWriteEnd(void)
{
    uint8_t RegStatus = 0;

    FLASH_SPI_CS_CLR();

    /* send Read W25Q16 Status cmd */
    W25Q16_SendByte(W25Q16_READ_STATUS_REG);

    do
    {
        /* Read W25Q16 Status Reg */
        RegStatus = W25Q16_SendByte(W25Q16_DUMMY_BYTE);
    }
    while ((RegStatus & W25Q16_WIP_FLAG) != 0);

    FLASH_SPI_CS_SET();
}

/*!
 * @brief       Flash Into PowerDown
 *
 * @param       None
 *
 * @retval      None
 */
void W25Q16_IntoPowerDown(void)
{
    FLASH_SPI_CS_CLR();

    /* send PowerDown cmd*/
    W25Q16_SendByte(W25Q16_POWER_DOWN);

    FLASH_SPI_CS_SET();
}

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
