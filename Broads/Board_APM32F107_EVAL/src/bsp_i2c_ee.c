/*!
 * @file        bsp_i2c_ee.c
 *
 * @brief       This file provides i2c read and write EEPROM(AT24C02) functions
 *
 * @version     V1.0.0
 *
 * @date        2022-11-07
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
#include "bsp_i2c_ee.h"
#include <stdio.h>
#include <string.h>

/** @addtogroup Board
  * @brief Board support package
  @{
  */

/** @addtogroup Board_APM32F107_EVAL
  * @brief APM32F107 EVAL board support package
  @{
  */

/** @defgroup APM32F107_EVAL_Variables Variables
  @{
  */

__IO uint16_t* EEPROM_I2C_DataReadPointer;
__IO uint8_t*  EEPROM_I2C_DataWritePointer;

I2C_EE_INFO_T eeI2CInfo;
DMA_Config_T    EEPROM_I2C_DMA_ConfigStruct;

/**@} end of group APM32F107_EVAL_Variables */

/** @defgroup APM32F107_EVAL_Functions Functions
  @{
  */

static void EEPROM_I2C_DMA_Init(void);
static void EEPROM_I2C_DMAConfig(uint32_t pBuffer, uint32_t BufferSize, uint32_t Direction);

static I2C_EE_ERROR_T I2C_EE_Callback(uint8_t errorCode);

/*!
 * @brief       EEPROM I2C Initialization
 *
 * @param       None
 *
 * @retval      None
 */
void APM_I2C_EE_Init(void)
{
    GPIO_Config_T gpioConfigStruct;
    I2C_Config_T i2cConfigStruct;

    /* Enable I2C related Clock */
    RCM_EnableAPB2PeriphClock(EEPROM_I2C_SCL_GPIO_CLK | EEPROM_I2C_SDA_GPIO_CLK);
    RCM_EnableAPB1PeriphClock(EEPROM_I2C_CLK);

    /* Connect I2C to SCL */
    gpioConfigStruct.mode = GPIO_MODE_AF_OD;
    gpioConfigStruct.speed = GPIO_SPEED_50MHz;
    gpioConfigStruct.pin = EEPROM_I2C_SCL_PIN;
    GPIO_Config(EEPROM_I2C_SCL_GPIO_PORT, &gpioConfigStruct);

    gpioConfigStruct.pin = EEPROM_I2C_SDA_PIN;
    GPIO_Config(EEPROM_I2C_SDA_GPIO_PORT, &gpioConfigStruct);

    /* Config EEPROM I2C DMA */
    EEPROM_I2C_DMA_Init();

    /* Config EEPROM_I2C */
    I2C_Reset(EEPROM_I2C);

    i2cConfigStruct.mode = I2C_MODE_I2C;
    i2cConfigStruct.dutyCycle = I2C_DUTYCYCLE_2;
    i2cConfigStruct.ackAddress = I2C_ACK_ADDRESS_7BIT;
    i2cConfigStruct.ownAddress1 = EEPROM_I2C_SLAVE_ADDRESS7;
    i2cConfigStruct.ack = I2C_ACK_ENABLE;
    i2cConfigStruct.clockSpeed = EEPROM_I2C_SPEED;
    I2C_Config(EEPROM_I2C, &i2cConfigStruct);

    /* Enable EEPROM_I2C */
    I2C_Enable(EEPROM_I2C);

    /* Enable the EEPROM_I2C peripheral DMA requests */
    I2C_EnableDMA(EEPROM_I2C);

    /* Select the EEPROM address according to the state of E0, E1, E2 pins */
    eeI2CInfo.address = EEPROM_ADDRESS;
}

/*!
 * @brief       I2C eeprom error callback
 *
 * @param       errorCode: print this error code
 *
 * @retval      I2C status code
 */
static I2C_EE_ERROR_T I2C_EE_Callback(uint8_t errorCode)
{
    I2C_EE_ERROR_T errSta = I2C_EE_ERR;

    /* Block communication and all processes */
    printf("I2C wait timeout! errorCode = %d\r\n", errorCode);

    return errSta;
}

/*!
 * @brief       Initializes DMA channel used by the I2C EEPROM driver.
 *
 * @param       None
 *
 * @retval      None
 */
static void EEPROM_I2C_DMA_Init(void)
{
    /* Configure and enable I2C DMA TX Channel interrupt */
    NVIC_EnableIRQRequest(EEPROM_I2C_DMA_TX_IRQn, EEPROM_I2C_DMA_PREPRIO, EEPROM_I2C_DMA_SUBPRIO);

    /* Configure and enable I2C DMA RX Channel interrupt */
    NVIC_EnableIRQRequest(EEPROM_I2C_DMA_RX_IRQn, EEPROM_I2C_DMA_PREPRIO, EEPROM_I2C_DMA_SUBPRIO);

    /* I2C DMA TX and RX channels configuration */
    /* Enable the DMA clock */
    RCM_EnableAHBPeriphClock(EEPROM_I2C_DMA_CLK);

    /* I2C TX DMA Channel configuration */
    DMA_Reset(EEPROM_I2C_DMA_CHANNEL_TX);
    EEPROM_I2C_DMA_ConfigStruct.peripheralBaseAddr = (uint32_t)EEPROM_I2C_DR_Address;

    /* This parameter will be configured durig communication */
    EEPROM_I2C_DMA_ConfigStruct.memoryBaseAddr = (uint32_t)0;

    /* This parameter will be configured durig communication */
    EEPROM_I2C_DMA_ConfigStruct.dir = DMA_DIR_PERIPHERAL_SRC;

    /* This parameter will be configured durig communication */
    EEPROM_I2C_DMA_ConfigStruct.bufferSize = 0xFFFF;

    EEPROM_I2C_DMA_ConfigStruct.peripheralInc = DMA_PERIPHERAL_INC_DISABLE;
    EEPROM_I2C_DMA_ConfigStruct.memoryInc = DMA_MEMORY_INC_ENABLE;
    EEPROM_I2C_DMA_ConfigStruct.peripheralDataSize = DMA_PERIPHERAL_DATA_SIZE_BYTE;
    EEPROM_I2C_DMA_ConfigStruct.memoryDataSize = DMA_MEMORY_DATA_SIZE_BYTE;
    EEPROM_I2C_DMA_ConfigStruct.loopMode = DMA_MODE_NORMAL;
    EEPROM_I2C_DMA_ConfigStruct.priority = DMA_PRIORITY_VERYHIGH;
    EEPROM_I2C_DMA_ConfigStruct.M2M = DMA_M2MEN_DISABLE;
    DMA_Config(EEPROM_I2C_DMA_CHANNEL_TX, &EEPROM_I2C_DMA_ConfigStruct);

    /* I2C RX DMA Channel configuration */
    DMA_Reset(EEPROM_I2C_DMA_CHANNEL_RX);
    DMA_Config(EEPROM_I2C_DMA_CHANNEL_RX, &EEPROM_I2C_DMA_ConfigStruct);

    /* Enable the DMA Channels Interrupts */
    DMA_EnableInterrupt(EEPROM_I2C_DMA_CHANNEL_TX, DMA_INT_TC);
    DMA_EnableInterrupt(EEPROM_I2C_DMA_CHANNEL_RX, DMA_INT_TC);
}

/*!
 * @brief       Initializes DMA channel used by the I2C EEPROM driver.
 *
 * @param       None
 *
 * @retval      None
 */
static void EEPROM_I2C_DMAConfig(uint32_t pBuffer, uint32_t BufferSize, uint32_t Direction)
{
    /* Initialize the DMA with the new parameters */
    if (Direction == EEPROM_I2C_DIRECTION_TX)
    {
        /* Configure the DMA Tx Channel with the buffer address and the buffer size */
        EEPROM_I2C_DMA_ConfigStruct.memoryBaseAddr = (uint32_t)pBuffer;
        EEPROM_I2C_DMA_ConfigStruct.dir = DMA_DIR_PERIPHERAL_DST;
        EEPROM_I2C_DMA_ConfigStruct.bufferSize = (uint32_t)BufferSize;
        DMA_Config(EEPROM_I2C_DMA_CHANNEL_TX, &EEPROM_I2C_DMA_ConfigStruct);
    }
    else
    {
        /* Configure the DMA Rx Channel with the buffer address and the buffer size */
        EEPROM_I2C_DMA_ConfigStruct.memoryBaseAddr = (uint32_t)pBuffer;
        EEPROM_I2C_DMA_ConfigStruct.dir = DMA_DIR_PERIPHERAL_SRC;
        EEPROM_I2C_DMA_ConfigStruct.bufferSize = (uint32_t)BufferSize;
        DMA_Config(EEPROM_I2C_DMA_CHANNEL_RX, &EEPROM_I2C_DMA_ConfigStruct);

    }
}

/*!
 * @brief       I2C write page data to eeprom
 *
 * @param       buffer: pointer to the read buffer
 *
 * @param       writeAddr: write the flash address
 *
 * @param       number: the number of byte to write
 *
 * @retval      I2C status code
 */
I2C_EE_ERROR_T I2C_EE_PageWrite(uint8_t* buffer, uint16_t writeAddr, uint8_t* number)
{
    /* Set the pointer to the Number of data to be written. This pointer will be used
      by the DMA Transfer Completer interrupt Handler in order to reset the
      variable to 0. User should check on this variable in order to know if the
      DMA transfer has been complete or not. */
    EEPROM_I2C_DataWritePointer = number;

    I2C_EE_ERROR_T errSta = I2C_EE_OK;

    /* While the bus is busy */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (I2C_ReadStatusFlag(EEPROM_I2C, I2C_FLAG_BUSBSY) == SET)
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(1);
        }
    }

    /* Send START condition */
    I2C_EnableGenerateStart(EEPROM_I2C);

    /* Test on EV5 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(2);
        }
    }

    /* Send EEPROM address for write */
    I2C_Tx7BitAddress(EEPROM_I2C, eeI2CInfo.address, I2C_DIRECTION_TX);

    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(3);
        }
    }

#ifdef EEPROM_ADDRESS_SIZE_8Bit
    I2C_TxData(EEPROM_I2C, writeAddr);

#elif defined (EEPROM_ADDRESS_SIZE_16Bit)

    /* Send the EEPROM's internal address to write to : MSB of the address first */
    I2C_TxData(EEPROM_I2C, (uint8_t)((writeAddr & 0xFF00) >> 8));

    /* Test on EV8 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTING))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(4);
        }
    }

    /* Send the EEPROM's internal address to write to : LSB of the address */
    I2C_TxData(EEPROM_I2C, (uint8_t)(writeAddr & 0xFF00));

#endif

    /* Test on EV8 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTING))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(5);
        }
    }

    /* Configure the DMA Tx Channel with the buffer address and the buffer size */
    EEPROM_I2C_DMAConfig((uint32_t)buffer, (uint8_t)(*number), EEPROM_I2C_DIRECTION_TX);

    /* Enable the DMA Tx Channel */
    DMA_Enable(EEPROM_I2C_DMA_CHANNEL_TX);

    return errSta;
}

/*!
 * @brief       Wait for EEPROM standby
 *
 * @param       None
 *
 * @retval      I2C status code
 */
I2C_EE_ERROR_T I2C_EE_WaitEEStandby(void)
{
    __IO uint16_t tmpSTS1 = 0;
    uint32_t retryTemp = 0;

    I2C_EE_ERROR_T errSta = I2C_EE_OK;

    /* While the bus is busy */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (I2C_ReadStatusFlag(EEPROM_I2C, I2C_FLAG_BUSBSY) == SET)
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(6);
        }
    }

    /* Keep looping till the slave acknowledge his address or maximum number
    of trials is reached (this number is defined by I2C_EE_RETRY_NUMBER_MAX define
    in bsp_i2c_ee.h file) */
    while (1)
    {
        /* Send START condition */
        I2C_EnableGenerateStart(EEPROM_I2C);

        /* Test on EV5 and clear it */
        eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

        while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_MODE_SELECT))  //EV5
        {
            if ((eeI2CInfo.timeout--) == 0)
            {
                errSta = I2C_EE_TIMEOUT;
                return I2C_EE_Callback(7);
            }
        }

        /* Send EEPROM address for write */
        I2C_Tx7BitAddress(EEPROM_I2C, eeI2CInfo.address, I2C_DIRECTION_TX);

        /* Wait for ADDR flag to be set (Slave acknowledged his address) */
        eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

        do
        {
            /* Get the current value of the SR1 register */
            tmpSTS1 = EEPROM_I2C->STS1;

            /* Update the timeout value and exit if it reach 0 */
            if ((eeI2CInfo.timeout--) == 0)
            {
                errSta = I2C_EE_TIMEOUT;
                return I2C_EE_Callback(8);
            }
        }

        /* Keep looping till the Address is acknowledged or the AF flag is
           set (address not acknowledged at time) */
        while ((tmpSTS1 & ((uint16_t)0x0002 | (uint16_t)0x0400)) == 0);

        /* Check if the ADDR flag has been set */
        if (tmpSTS1 & (uint16_t)0x0002)
        {
            /* Clear ADDR Flag by reading SR1 then SR2 registers (SR1 have already
               been read) */
            (void)EEPROM_I2C->STS2;

            /* STOP condition */
            I2C_EnableGenerateStop(EEPROM_I2C);

            /* Exit the function */
            return I2C_EE_OK;
        }
        else
        {
            /* Clear AF flag */
            I2C_ClearStatusFlag(EEPROM_I2C, I2C_FLAG_AE);
        }

        /* Check if the maximum allowed numbe of trials has bee reached */
        if (retryTemp++ == I2C_EE_RETRY_NUMBER_MAX)
        {
            /* If the maximum number of trials has been reached, exit the function */
            errSta = I2C_EE_ERR;
            return errSta;
        }
    }
}

/*!
 * @brief       EEPROM Write Buffer
 *
 * @param       buffer: pointer to the Write buffer
 *
 * @param       writeAddr: Write the flash Address
 *
 * @param       number: the number of byte to write
 *
 * @retval      I2C status code
 */
I2C_EE_ERROR_T I2C_EE_BufferWrite(uint8_t* buffer, uint16_t writeAddr, uint16_t number)
{
    I2C_EE_ERROR_T errSta = I2C_EE_OK;

    uint16_t pageNumber = 0;
    uint16_t remainNumber = 0;
    uint16_t addr = 0;
    uint16_t pageCnt = 0;

    /* complete Page need to write */
    pageNumber =  number / EEPROM_PAGE_SIZE;

    /* Remain byte */
    remainNumber = number % EEPROM_PAGE_SIZE;
    addr = writeAddr % EEPROM_PAGE_SIZE;
    pageCnt = EEPROM_PAGE_SIZE - addr;

    if (addr == 0)
    {
        if (pageNumber == 0)
        {
            eeI2CInfo.dataNum = remainNumber;
            errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
            I2C_EE_WaitEEStandby();

            if (errSta != I2C_EE_OK)
            {
                return I2C_EE_ERR;
            }

        }
        else
        {
            while (pageNumber--)
            {
                eeI2CInfo.dataNum = EEPROM_PAGE_SIZE;
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
                I2C_EE_WaitEEStandby();

                /* Need add some delay in here*/
                APM_EVAL_DelayMs(1);

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }

                writeAddr += EEPROM_PAGE_SIZE;
                buffer += EEPROM_PAGE_SIZE;
            }

            if (remainNumber != 0)
            {
                eeI2CInfo.dataNum = remainNumber;
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
                I2C_EE_WaitEEStandby();

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }
            }
        }
    }
    else
    {
        if (pageNumber == 0)
        {
            if (number > pageCnt)
            {
                eeI2CInfo.dataNum = pageCnt;
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
                I2C_EE_WaitEEStandby();

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }

                eeI2CInfo.dataNum = (number - pageCnt);
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
                I2C_EE_WaitEEStandby();

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }
            }
            else
            {
                eeI2CInfo.dataNum = remainNumber;
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
                I2C_EE_WaitEEStandby();

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }
            }
        }
        else
        {
            number -= pageCnt;
            /* complete Page need to write */
            pageNumber =  number / EEPROM_PAGE_SIZE;

            /* Remain byte */
            remainNumber = number % EEPROM_PAGE_SIZE;

            if (pageCnt != 0)
            {
                eeI2CInfo.dataNum = pageCnt;
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
                I2C_EE_WaitEEStandby();

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }

                writeAddr += pageCnt;
                buffer += pageCnt;
            }

            while (remainNumber--)
            {
                eeI2CInfo.dataNum = EEPROM_PAGE_SIZE;
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));

                eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;
                I2C_EE_WaitEEStandby();

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }

                writeAddr += EEPROM_PAGE_SIZE;
                buffer += EEPROM_PAGE_SIZE;
            }

            if (remainNumber != 0)
            {
                eeI2CInfo.dataNum = remainNumber;
                errSta = I2C_EE_PageWrite(buffer, writeAddr, (uint8_t*)(&eeI2CInfo.dataNum));
                I2C_EE_WaitEEStandby();

                if (errSta != I2C_EE_OK)
                {
                    return I2C_EE_ERR;
                }
            }
        }
    }

    return errSta;
}

/*!
 * @brief       EEPROM read buffer
 *
 * @param       buffer: pointer to the Write buffer
 *
 * @param       readAddr: read the flash Address
 *
 * @param       number: the number of byte to read
 *
 * @retval      I2C status code
 */
I2C_EE_ERROR_T I2C_EE_BufferRead(uint8_t* buffer, uint16_t readAddr, uint16_t* number)
{
    EEPROM_I2C_DataReadPointer = number;
    I2C_EE_ERROR_T errSta = I2C_EE_OK;

    /* While the bus is busy */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (I2C_ReadStatusFlag(EEPROM_I2C, I2C_FLAG_BUSBSY) == SET)
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(9);
        }
    }

    /* Send START condition */
    I2C_EnableGenerateStart(EEPROM_I2C);

    /* Test on EV5 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(10);
        }
    }

    /* Send EEPROM address for write */
    I2C_Tx7BitAddress(EEPROM_I2C, eeI2CInfo.address, I2C_DIRECTION_TX);

    /* Test on EV6 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(11);
        }
    }

#ifdef EEPROM_ADDRESS_SIZE_8Bit
    I2C_TxData(EEPROM_I2C, readAddr);

#elif defined (EEPROM_ADDRESS_SIZE_16Bit)
    /* Send the EEPROM's internal address to read from : MSB of the address first */
    I2C_TxData(EEPROM_I2C, (uint8_t)((readAddr & 0xFF00) >> 8));

    /* Test on EV8 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTING))   //EV8
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(12);
        }
    }

    /* Send the EEPROM's internal address to read from : LSB of the address */
    I2C_TxData(EEPROM_I2C, (uint8_t)(readAddr & 0xFF00));
#endif

    /* Test on EV8 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_BYTE_TRANSMITTING))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(13);
        }
    }

    /* Send STRAT condition a second time */
    I2C_EnableGenerateStart(EEPROM_I2C);

    /* Test on EV5 and clear it */
    eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

    while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_MODE_SELECT))
    {
        if ((eeI2CInfo.timeout--) == 0)
        {
            errSta = I2C_EE_TIMEOUT;
            return I2C_EE_Callback(14);
        }
    }

    /* Send EEPROM address for read */
    I2C_Tx7BitAddress(EEPROM_I2C, eeI2CInfo.address, I2C_DIRECTION_RX);

    /* If number of data to be read is 1, then DMA couldn't be used */
    /* One Byte Master Reception procedure (POLLING)*/
    if ((uint16_t)(*number) < 2)
    {
        /* Wait on ADDR flag to be set (ADDR is still not cleared at this level */

        eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

        while (I2C_ReadStatusFlag(EEPROM_I2C, I2C_FLAG_ADDR) == RESET)
        {
            if ((eeI2CInfo.timeout--) == 0)
            {
                errSta = I2C_EE_TIMEOUT;
                return I2C_EE_Callback(15);
            }
        }

        /* Disable Acknowledgement */
        I2C_DisableAcknowledge(EEPROM_I2C);

        __disable_irq();

        /* Clear ADDR register by reading SR1 then SR2 register (SR1 has already been read) */
        (void)EEPROM_I2C->STS2;

        /* Send STOP condition */
        I2C_EnableGenerateStop(EEPROM_I2C);

        __enable_irq();

        /* Wait for the byte to be received */

        eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

        while (I2C_ReadStatusFlag(EEPROM_I2C, I2C_FLAG_RXBNE) == RESET)
        {
            if ((eeI2CInfo.timeout--) == 0)
            {
                errSta = I2C_EE_TIMEOUT;
                return I2C_EE_Callback(16);
            }
        }

        /* Read the byte received from the EEPROM */
        *buffer = I2C_RxData(EEPROM_I2C);


        /* Decrement the read bytes counter */
        (uint16_t)(*number)--;

        /* Wait to make sure that STOP control bit has been cleared */

        eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

        while (I2C_ReadStatusFlag(EEPROM_I2C, I2C_FLAG_STOP) == SET)
        {
            if ((eeI2CInfo.timeout--) == 0)
            {
                errSta = I2C_EE_TIMEOUT;
                return I2C_EE_Callback(17);
            }
        }

        /* Re-Enable Acknowledgement to be ready for another reception */
        I2C_EnableAcknowledge(EEPROM_I2C);

    }
    else
    {
        /* More than one Byte Master Reception procedure (DMA) */

        /* Test on EV6 and clear it */
        eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

        while (!I2C_ReadEventStatus(EEPROM_I2C, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
        {
            if ((eeI2CInfo.timeout--) == 0)
            {
                errSta = I2C_EE_TIMEOUT;
                return I2C_EE_Callback(18);
            }
        }

        /* Configure the DMA Rx Channel with the buffer address and the buffer size */
        EEPROM_I2C_DMAConfig((uint32_t)buffer, (uint16_t)(*number), EEPROM_I2C_DIRECTION_RX);

        /* Inform the DMA that the next End Of Transfer Signal will be the last one */
        I2C_EnableDMALastTransfer(EEPROM_I2C);

        /* Enable the DMA Rx Channel */
        DMA_Enable(EEPROM_I2C_DMA_CHANNEL_RX);
    }

    return errSta;
}

/*!
 * @brief       This function handles the DMA Tx Channel interrupt Handler.
 *
 * @param       None
 *
 * @retval      None
 */
void EEPROM_I2C_DMA_TX_IRQHandler(void)
{
    /* Check if the DMA transfer is complete */
    if (DMA_ReadStatusFlag(EEPROM_I2C_DMA_FLAG_TX_TC) != RESET)
    {
        /* Disable the DMA Tx Channel and Clear all its Flags */
        DMA_Disable(EEPROM_I2C_DMA_CHANNEL_TX);
        DMA_ClearStatusFlag(EEPROM_I2C_DMA_FLAG_TX_GL);

        /* Wait till all data have been physically transferred on the bus */
        eeI2CInfo.timeout = I2C_EE_LONG_TIMEOUT;

        while (!I2C_ReadStatusFlag(EEPROM_I2C, I2C_FLAG_BTC))
        {
            if ((eeI2CInfo.timeout--) == 0)
            {
                I2C_EE_Callback(19);
            }
        }

        /* Send STOP condition */
        I2C_EnableGenerateStop(EEPROM_I2C);

        /* Perform a read on SR1 and SR2 register to clear eventualaly pending flags */
        (void)EEPROM_I2C->STS1;
        (void)EEPROM_I2C->STS2;

        /* Reset the variable holding the number of data to be written */
        *EEPROM_I2C_DataWritePointer = 0;
    }
}

/*!
 * @brief       This function handles the DMA Rx Channel interrupt Handler.
 *
 * @param       None
 *
 * @retval      None
 */
void EEPROM_I2C_DMA_RX_IRQHandler(void)
{
    /* Check if the DMA transfer is complete */
    if (DMA_ReadStatusFlag(EEPROM_I2C_DMA_FLAG_RX_TC) != RESET)
    {
        /* Send STOP Condition */
        I2C_EnableGenerateStop(EEPROM_I2C);

        /* Disable the DMA Rx Channel and Clear all its Flags */
        DMA_Disable(EEPROM_I2C_DMA_CHANNEL_RX);
        DMA_ClearStatusFlag(EEPROM_I2C_DMA_FLAG_RX_GL);

        /* Reset the variable holding the number of data to be read */
        *EEPROM_I2C_DataReadPointer = 0;
    }
}

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
