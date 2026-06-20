/*!
 * @file        bsp_lcd.c
 *
 * @brief       LCD board support package program body
 *
 * @version     V1.0.0
 *
 * @date        2022-11-04
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
#include "bsp_lcd.h"
#include "apm32f10x_spi.h"
#include "apm32f10x_gpio.h"
#include "bsp_lcd_font.h"
#include "bsp_delay.h"
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
 * @brief       LCD SPI init
 *
 * @param       None
 *
 * @retval      None
 *
 */
void LCD_SPIInit(void)
{
    GPIO_Config_T gpioConfig;
    SPI_Config_T spiConfig;

    /* Enable GPIO clock */
    RCM_EnableAPB2PeriphClock(LCD_SPI_GPIO_CLK);
    /* Enable SPI clock */
    RCM_EnableAPB2PeriphClock(LCD_SPI_BUS_CLK);

    /* Configure SCK and MOSI pins as Alternate Function Push Pull */
    gpioConfig.pin = LCD_SPI_MOSI_PIN | LCD_SPI_SCK_PIN;
    gpioConfig.mode = GPIO_MODE_AF_PP;
    gpioConfig.speed = GPIO_SPEED_50MHz;
    GPIO_Config(LCD_SPI_GPIO_PORT, &gpioConfig);

    gpioConfig.pin = LCD_SPI_CS_PIN;
    gpioConfig.mode = GPIO_MODE_OUT_PP;
    GPIO_Config(LCD_SPI_GPIO_PORT, &gpioConfig);

    LCD_SPI_CS_SET();

    /* SPI configuration */
    SPI_ConfigStructInit(&spiConfig);
    spiConfig.direction = SPI_DIRECTION_2LINES_FULLDUPLEX;
    spiConfig.mode = SPI_MODE_MASTER;
    spiConfig.length = SPI_DATA_LENGTH_8B;
    spiConfig.polarity = SPI_CLKPOL_HIGH;
    spiConfig.phase = SPI_CLKPHA_2EDGE;
    spiConfig.nss = SPI_NSS_SOFT;
    spiConfig.baudrateDiv = SPI_BAUDRATE_DIV_2;
    spiConfig.firstBit = SPI_FIRSTBIT_MSB;
    spiConfig.crcPolynomial = 7;
    SPI_Config(LCD_SPI_BUS, &spiConfig);

    SPI_Enable(LCD_SPI_BUS);
}

/*!
 * @brief       LCD SPI sends a byte through the SPI
 *
 * @param       data: byte send
 *
 * @retval      The error code
 *
 */
uint8_t LCD_SPIReadWriteByte(uint8_t data)
{
    uint8_t timeout = 0;

    while (SPI_I2S_ReadStatusFlag(LCD_SPI_BUS, SPI_FLAG_TXBE) == RESET)
    {
        timeout++;

        if (timeout >= 200)
        {
            return LCD_ERROR;
        }
    }

    SPI_I2S_TxData(LCD_SPI_BUS, data);

    while (SPI_I2S_ReadStatusFlag(LCD_SPI_BUS, SPI_FLAG_BSY) == SET)
    {
        timeout++;

        if (timeout >= 200)
        {
            return LCD_ERROR;
        }
    }

    return LCD_OK;
}

/*!
 * @brief       LCD GPIO init
 *
 * @param       None
 *
 * @retval      None
 *
 */
void APM_LCDGPIOInit(void)
{
    GPIO_Config_T gpioConfig;

    /* Enable GPIO clock */
    RCM_EnableAPB2PeriphClock(LCD_DC_GPIO_CLK | LCD_BLK_GPIO_CLK | LCD_RES_GPIO_CLK);

    gpioConfig.mode = GPIO_MODE_OUT_PP;
    gpioConfig.pin = LCD_DC_PIN;
    gpioConfig.speed = GPIO_SPEED_50MHz;
    GPIO_Config(LCD_DC_GPIO_PORT, &gpioConfig);

    gpioConfig.pin = LCD_RES_PIN;
    GPIO_Config(LCD_RES_GPIO_PORT, &gpioConfig);

    gpioConfig.pin = LCD_BLK_PIN;
    GPIO_Config(LCD_BLK_GPIO_PORT, &gpioConfig);

    LCD_DC_SET();
    LCD_RES_SET();
    LCD_BLK_SET();
}

/*!
 * @brief       LCD write data
 *
 * @param       data
 *
 * @retval      None
 *
 */
void LCD_WriteData(uint8_t data)
{
    LCD_SPI_CS_CLR();
    LCD_SPIReadWriteByte(data);
    LCD_SPI_CS_SET();
}

/*!
 * @brief       LCD write byte
 *
 * @param       data
 *
 * @retval      None
 *
 */
void LCD_WriteByte(uint8_t data)
{
    LCD_WriteData(data);
}

/*!
 * @brief       LCD write half-word
 *
 * @param       data
 *
 * @retval      None
 *
 */
void LCD_WriteHalfword(uint16_t data)
{
    LCD_WriteData(data >> 8);
    LCD_WriteData(data);
}

/*!
 * @brief       LCD write register
 *
 * @param       data
 *
 * @retval      None
 *
 */
void LCD_WriteReg(uint8_t data)
{
    LCD_DC_CLR();
    LCD_WriteData(data);
    LCD_DC_SET();
}

/*!
 * @brief       LCD set cursor address
 *
 * @param       x1
 *
 * @param       y1
 *
 * @param       x2
 *
 * @param       y1
 *
 * @retval      None
 *
 */
void LCD_Address_Set(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    /* Column address */
    LCD_WriteReg(0x2A);
    LCD_WriteHalfword(x1);
    LCD_WriteHalfword(x2);

    /* Row address */
    LCD_WriteReg(0x2B);
    LCD_WriteHalfword(y1);
    LCD_WriteHalfword(y2);

    /* Write to register */
    LCD_WriteReg(0x2C);
}

/*!
 * @brief       LCD calculate math power
 *
 * @param       m
 *
 * @param       n
 *
 * @retval      Calculate result
 *
 */
uint32_t LCD_CalPow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n--)
    {
        result *= m;
    }

    return result;
}

/*!
 * @brief       LCD draw point
 *
 * @param       x: row lable
 *
 * @param       y: column lable
 *
 * @param       color: color to be filled
 *
 * @retval      None
 *
 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color)
{
    LCD_Address_Set(x, y, x, y);
    LCD_WriteHalfword(color);
}

/*!
 * @brief       LCD draw corss
 *
 * @param       x: row lable
 *
 * @param       y: column lable
 *
 * @param       color: color to be filled
 *
 * @retval      None
 *
 */
void LCD_DrawCorss(uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t i;

    for (i = 0; i < 10; i++)
    {
        LCD_DrawPoint(x + i, y, color);
    }

    for (i = 0; i < 5; i++)
    {
        LCD_DrawPoint(x + 5, y + i, color);
        LCD_DrawPoint(x + 5, y - i, color);
    }
}

/*!
 * @brief       LCD draw line
 *
 * @param       x1: row start lable
 *
 * @param       y1: column start lable
 *
 * @param       x1: row end lable
 *
 * @param       y1: column end lable
 *
 * @param       color: color to be filled
 *
 * @retval      None
 *
 */
void LCD_DrawLine(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    uint16_t i;
    int xErr = 0, yErr = 0, xDelta, yDelta, distance;
    int xInc, yInc, uRow, uCol;

    /* Calculate the axis lable increasement */
    xDelta = x2-x1;
    yDelta = y2-y1;

    /* Draw the begin point*/
    uRow = x1;
    uCol = y1;

    if (xDelta > 0)
    {
        xInc = 1;
    }
    else if (xDelta == 0)
    {
        xInc = 0;
    }
    else
    {
        xInc = -1;
        xDelta = -xDelta;
    }

    if (yDelta > 0)
    {
        yInc = 1;
    }
    else if (yDelta == 0)
    {
        yInc = 0;
    }
    else
    {
        yInc = -1;
        yDelta = -yDelta;
    }

    if (xDelta > yDelta)
    {
        distance = xDelta;
    }
    else
    {
        distance = yDelta;
    }

    for (i = 0; i < distance + 1; i++)
    {
        LCD_DrawPoint(uRow, uCol, color);

        xErr += xDelta;
        yErr += yDelta;

        if (xErr > distance)
        {
            xErr -= distance;
            uRow += xInc;
        }

        if (yErr > distance)
        {
            yErr -= distance;
            uCol += yInc;
        }
    }
}

/*!
 * @brief       LCD draw circle
 *
 * @param       x0: center point lable
 *
 * @param       y0: center point lable
 *
 * @param       radius: circle radius
 *
 * @param       color: color to be filled
 *
 * @retval      None
 *
 */
void LCD_DrawCircle(uint16_t x0, uint16_t y0, uint8_t radius, uint16_t color)
{
    int a, b;

    a = 0;
    b = radius;

    while (a <= b)
    {
        LCD_DrawPoint(x0 - b, y0 - a, color);
        LCD_DrawPoint(x0 + b, y0 - a, color);
        LCD_DrawPoint(x0 - a, y0 + b, color);
        LCD_DrawPoint(x0 - a, y0 - b, color);
        LCD_DrawPoint(x0 + b, y0 + a, color);
        LCD_DrawPoint(x0 + a, y0 - b, color);
        LCD_DrawPoint(x0 + a, y0 + b, color);
        LCD_DrawPoint(x0 - b, y0 + a, color);
        a++;

        if ((a * a + b * b) > (radius * radius))
        {
            b--;
        }
    }
}

/*!
 * @brief       LCD clear content
 *
 * @param       xStart: row start lable
 *
 * @param       yStart: column start lable
 *
 * @param       xEnd: row end lable
 *
 * @param       yEnd: column end lable
 *
 * @param       color: color to be filled
 *
 * @retval      None
 *
 */
void LCD_Clear(uint16_t xStart, uint16_t yStart, uint16_t xEnd, uint16_t yEnd, uint16_t color)
{
    uint16_t i, j;

    LCD_Address_Set(xStart, yStart, xEnd - 1, yEnd - 1);

    for (i = yStart; i < yEnd; i++)
    {
        for (j = xStart; j < xEnd; j++)
        {
            LCD_WriteHalfword(color);
        }
    }
}

/*!
 * @brief       LCD display character
 *
 * @param       x: row lable
 *
 * @param       y: column lable
 *
 * @param       fc: foreground color
 *
 * @param       bc: background color
 *
 * @param       fontSize: font size
 *
 * @param       mode: superposition mode
 *
 * @retval      None
 *
 */
void LCD_DisplayChar(uint16_t x, uint16_t y, uint8_t num, uint16_t fc, uint16_t bc, uint8_t fontSize, uint8_t mode)
{
    uint8_t temp, xSize, t, m = 0;
    uint16_t i, characterSize;
    uint16_t x0 = x;

    xSize = fontSize / 2;
    characterSize = (xSize / 8 + ((xSize % 8) ? 1 : 0)) * fontSize;
    num = num - ' ';

    LCD_Address_Set(x, y, x + xSize - 1, y + fontSize - 1);

    for (i = 0; i < characterSize; i++)
    {
        /* Select ascii font*/
        if (fontSize == 12)
        {
            temp = asciiFont_1206[num][i];
        }
        else if (fontSize == 16)
        {
            temp = asciiFont_1608[num][i];
        }
        else if (fontSize == 24)
        {
            temp = asciiFont_2412[num][i];
        }
        else if (fontSize == 32)
        {
            temp = asciiFont_3216[num][i];
        }
        else
        {
            return;
        }

        for (t = 0; t < 8; t++)
        {
            if (!mode)
            {
                if (temp & (0x01 << t))
                {
                    LCD_WriteHalfword(fc);
                }
                else
                {
                    LCD_WriteHalfword(bc);
                }

                m++;

                if (m % xSize == 0)
                {
                    m = 0;
                    break;
                }
            }
            else
            {
                if (temp & (0x01 << t))
                {
                    LCD_DrawPoint(x, y, fc);
                }

                x++;

                if ((x - x0) == xSize)
                {
                    x = x0;
                    y++;
                    break;
                }
            }
        }
    }
}

/*!
 * @brief       LCD display string
 *
 * @param       x: row lable
 *
 * @param       y: column lable
 *
 * @param       fc: foreground color
 *
 * @param       bc: background color
 *
 * @param       p: point to string buffer
 *
 * @param       fontSize: font size
 *
 * @param       mode: superposition mode
 *
 * @retval      None
 *
 */
void LCD_DisplayString(uint16_t x, uint16_t y, const char* p, uint16_t fc, uint16_t bc, uint8_t fontSize, uint8_t mode)
{
    while (*p != '\0')
    {
        LCD_DisplayChar(x, y, *p, fc, bc, fontSize, mode);
        x += fontSize / 2;
        p++;
    }
}

/*!
 * @brief       LCD display integer type number
 *
 * @param       x: row lable
 *
 * @param       y: column lable
 *
 * @param       fc: foreground color
 *
 * @param       bc: background color
 *
 * @param       num: input integer type number
 *
 * @param       len: number length
 *
 * @param       fontSize: font size
 *
 * @param       mode: superposition mode
 *
 * @retval      None
 *
 */
void LCD_DisplayIntNum(uint16_t x, uint16_t y, uint16_t num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t fontSize)
{
    uint8_t t, temp;
    uint8_t enshow = 0;
    uint8_t xSize = fontSize / 2;

    for (t = 0; t < len; t++)
    {
        temp = (num / LCD_CalPow(10, len - t - 1)) % 10;

        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                LCD_DisplayChar(x + t * xSize, y, ' ', fc, bc, fontSize, 0);
                continue;
            }
            else
            {
                enshow = 1;
            }

        }

        LCD_DisplayChar(x + t * xSize, y, temp + 48, fc, bc, fontSize, 0);
    }
}

/*!
 * @brief       LCD display float type number
 *
 * @param       x: row lable
 *
 * @param       y: column lable
 *
 * @param       fc: foreground color
 *
 * @param       bc: background color
 *
 * @param       num: input float type number
 *
 * @param       len: number length
 *
 * @param       fontSize: font size
 *
 * @param       mode: superposition mode
 *
 * @retval      None
 *
 */
void LCD_DisplayFloatNum(uint16_t x, uint16_t y, float num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t fontSize)
{
    uint8_t t, temp, xSize;
    uint16_t numTemp;

    xSize = fontSize / 2;
    numTemp = (uint16_t)(num * 100);

    for (t = 0; t < len; t++)
    {
        temp = (numTemp / LCD_CalPow(10, len - t - 1)) % 10;

        if (t == (len - 2))
        {
            LCD_DisplayChar(x + (len - 2) * xSize, y, '.', fc, bc, fontSize, 0);
            t++;
            len += 1;
        }

        LCD_DisplayChar(x + t * xSize, y, temp + 48, fc, bc, fontSize, 0);
    }
}

/*!
 * @brief       Simple millisecond delay
 *
 * @param       ms: Specifies the delay time
 *
 * @retval      None
 *
 * @note
 */
static void LCD_DelayMs(unsigned int ms)
{
    unsigned short i = 0;
    
    while (ms--)
    {
        i = 7995;
        while (i--);
    }
}

/*!
 * @brief       LCD init
 *
 * @param       None
 *
 * @retval      None
 *
 * @note
 */
void APM_LCDInit(void)
{
    LCD_SPIInit();

    APM_LCDGPIOInit();
   
    LCD_RES_CLR();
    LCD_DelayMs(100);
    LCD_RES_SET();
    LCD_DelayMs(100);

    LCD_BLK_SET();
    LCD_DelayMs(100);

    LCD_WriteReg(0x11);
    LCD_DelayMs(120);

    LCD_WriteReg(0xCF);
    LCD_WriteByte(0x00);
    LCD_WriteByte(0xD9);
    LCD_WriteByte(0X30);

    LCD_WriteReg(0xED);
    LCD_WriteByte(0x64);
    LCD_WriteByte(0x03);
    LCD_WriteByte(0X12);
    LCD_WriteByte(0X81);

    LCD_WriteReg(0xE8);
    LCD_WriteByte(0x85);
    LCD_WriteByte(0x10);
    LCD_WriteByte(0x78);

    LCD_WriteReg(0xCB);
    LCD_WriteByte(0x39);
    LCD_WriteByte(0x2C);
    LCD_WriteByte(0x00);
    LCD_WriteByte(0x34);
    LCD_WriteByte(0x02);

    LCD_WriteReg(0xF7);
    LCD_WriteByte(0x20);

    LCD_WriteReg(0xEA);
    LCD_WriteByte(0x00);
    LCD_WriteByte(0x00);

    LCD_WriteReg(0xC0);
    LCD_WriteByte(0x21);

    LCD_WriteReg(0xC1);
    LCD_WriteByte(0x12);

    LCD_WriteReg(0xC5);
    LCD_WriteByte(0x32);
    LCD_WriteByte(0x3C);

    LCD_WriteReg(0xC7);
    LCD_WriteByte(0XC1);

    LCD_WriteReg(0x36);

    if (LCD_HORIZONTAL == 0)
    {
        LCD_WriteByte(0x08);
    }
    else if (LCD_HORIZONTAL == 1)
    {
        LCD_WriteByte(0xC8);
    }
    else if (LCD_HORIZONTAL == 2)
    {
        LCD_WriteByte(0x78);
    }
    else
    {
        LCD_WriteByte(0xA8);
    }

    LCD_WriteReg(0x3A);
    LCD_WriteByte(0x55);

    LCD_WriteReg(0xB1);
    LCD_WriteByte(0x00);
    LCD_WriteByte(0x18);

    LCD_WriteReg(0xB6);
    LCD_WriteByte(0x0A);
    LCD_WriteByte(0xA2);

    LCD_WriteReg(0xF2);
    LCD_WriteByte(0x00);

    LCD_WriteReg(0x26);
    LCD_WriteByte(0x01);

    LCD_WriteReg(0xE0);
    LCD_WriteByte(0x0F);
    LCD_WriteByte(0x20);
    LCD_WriteByte(0x1E);
    LCD_WriteByte(0x09);
    LCD_WriteByte(0x12);
    LCD_WriteByte(0x0B);
    LCD_WriteByte(0x50);
    LCD_WriteByte(0XBA);
    LCD_WriteByte(0x44);
    LCD_WriteByte(0x09);
    LCD_WriteByte(0x14);
    LCD_WriteByte(0x05);
    LCD_WriteByte(0x23);
    LCD_WriteByte(0x21);
    LCD_WriteByte(0x00);

    LCD_WriteReg(0XE1);
    LCD_WriteByte(0x00);
    LCD_WriteByte(0x19);
    LCD_WriteByte(0x19);
    LCD_WriteByte(0x00);
    LCD_WriteByte(0x12);
    LCD_WriteByte(0x07);
    LCD_WriteByte(0x2D);
    LCD_WriteByte(0x28);
    LCD_WriteByte(0x3F);
    LCD_WriteByte(0x02);
    LCD_WriteByte(0x0A);
    LCD_WriteByte(0x08);
    LCD_WriteByte(0x25);
    LCD_WriteByte(0x2D);
    LCD_WriteByte(0x0F);
    LCD_WriteReg(0x29);
}

/**@} end of group APM32F107_EVAL_Functions */
/**@} end of group Board_APM32F107_EVAL */
/**@} end of group Board */
