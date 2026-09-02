#include "bsp_rs485.h"
#include "apm32f10x_misc.h"
#include <string.h>

// RS485 ���ջ�����
static uint8_t rs485_rx_buffer[RS485_BUFFER_SIZE];
static uint16_t rs485_rx_write_index = 0;
static uint16_t rs485_rx_read_index = 0;

/**
 * @brief  ��ʼ�� RS485 ͨ��
 * @param  ��
 * @retval ��
 */
void RS485_Init(void)
{
    GPIO_Config_T gpio_config;
    USART_Config_T usart_config;
    
    // 1. ���� GPIO �� UART5 ʱ��
    RCM_EnableAPB2PeriphClock(RS485_UART_GPIO_CLK);
    RCM_EnableAPB1PeriphClock(RS485_USART_CLK);
    
    // 2. ���� UART5_TX (PC12)
    gpio_config.pin = RS485_UART_TX_PIN;
    gpio_config.mode = GPIO_MODE_AF_PP;
    gpio_config.speed = GPIO_SPEED_50MHz;
    GPIO_Config(RS485_UART_TX_PORT, &gpio_config);
    
    // 3. ���� UART5_RX (PD2)
    gpio_config.pin = RS485_UART_RX_PIN;
    gpio_config.mode = GPIO_MODE_IN_FLOATING;
    gpio_config.speed = GPIO_SPEED_50MHz;
    GPIO_Config(RS485_UART_RX_PORT, &gpio_config);
    
    // 4. ���� UART5
    usart_config.baudRate = 9600;
    usart_config.wordLength = USART_WORD_LEN_8B;
    usart_config.stopBits = USART_STOP_BIT_1;
    usart_config.parity = USART_PARITY_NONE;
    usart_config.mode = USART_MODE_TX_RX;
    usart_config.hardwareFlow = USART_HARDWARE_FLOW_NONE;
    USART_Config(RS485_USART, &usart_config);
    
    // 5. ���� UART5 �����ж�
    USART_EnableInterrupt(RS485_USART, USART_INT_RXBNE);
    NVIC_EnableIRQRequest(UART5_IRQn, 2, 0);
    
    // 6. ʹ�� UART5
    USART_Enable(RS485_USART);
    
    // Ĭ������Ϊ����ģʽ
    RS485_SetToReceiveMode();
    
    // ��ջ�����
    RS485_ClearBuffer();
}

/**
 * @brief  ����һ���ֽ� (����ʱ����)
 * @param  data: Ҫ���͵�����
 * @retval ��
 */
void RS485_SendByte(uint8_t data)
{
    uint32_t timeout = 0xFFFF;
    USART_TxData(RS485_USART, data);
    while (USART_ReadStatusFlag(RS485_USART, USART_FLAG_TXBE) == RESET && timeout--);
}

/**
 * @brief  �������� (����ʱ����)
 * @param  data: Ҫ���͵����ݻ�����
 * @param  len: ���ݳ���
 * @retval ��
 */
void RS485_SendData(uint8_t *data, uint16_t len)
{
    uint32_t timeout = 0xFFFF;
    RS485_SetToSendMode();
    
    for (uint16_t i = 0; i < len; i++) {
        RS485_SendByte(data[i]);
    }
    
    // �ȴ��������
    while (USART_ReadStatusFlag(RS485_USART, USART_FLAG_TXC) == RESET && timeout--);
    
    RS485_SetToReceiveMode();
}

/**
 * @brief  ����һ���ֽ� (����ģʽ������ʱ����)
 * @param  ��
 * @retval ���յ�������
 */
uint8_t RS485_ReceiveByte(void)
{
    uint32_t timeout = 0xFFFF;
    while (USART_ReadStatusFlag(RS485_USART, USART_FLAG_RXBNE) == RESET && timeout--);
    if (timeout == 0) return 0;
    return USART_RxData(RS485_USART);
}

/**
 * @brief  �������� (������)
 * @param  buffer: �������ݻ�����
 * @param  len: ���ճ���
 * @retval ʵ�ʽ��յ������ݳ���
 */
uint16_t RS485_ReceiveData(uint8_t *buffer, uint16_t len)
{
    uint16_t count = 0;
    while (count < len && RS485_GetAvailableData() > 0) {
        buffer[count++] = RS485_ReadFromBuffer();
    }
    return count;
}

/**
 * @brief  ��ս��ջ�����
 */
void RS485_ClearBuffer(void)
{
    rs485_rx_write_index = 0;
    rs485_rx_read_index = 0;
}

/**
 * @brief  ��ȡ�������п��õ����ݳ���
 * @retval ���ݳ���
 */
uint16_t RS485_GetAvailableData(void)
{
    if (rs485_rx_write_index >= rs485_rx_read_index) {
        return rs485_rx_write_index - rs485_rx_read_index;
    } else {
        return RS485_BUFFER_SIZE - rs485_rx_read_index + rs485_rx_write_index;
    }
}

/**
 * @brief  �ӻ�������ȡһ���ֽ�
 * @retval ����
 */
uint8_t RS485_ReadFromBuffer(void)
{
    uint8_t data = 0;
    if (rs485_rx_read_index != rs485_rx_write_index) {
        data = rs485_rx_buffer[rs485_rx_read_index];
        rs485_rx_read_index = (rs485_rx_read_index + 1) % RS485_BUFFER_SIZE;
    }
    return data;
}

/**
 * @brief  ����Ϊ����ģʽ
 */
void RS485_SetToSendMode(void)
{
    // ���Ӳ������ DE ���ſ��ƣ����ڴ����Ӵ���
    // GPIO_WriteBit(RS485_RDE_GPIO_PORT, RS485_RDE_PIN, BIT_SET);
}

/**
 * @brief  ����Ϊ����ģʽ
 */
void RS485_SetToReceiveMode(void)
{
    // GPIO_WriteBit(RS485_RDE_GPIO_PORT, RS485_RDE_PIN, BIT_RESET);
}

/**
 * @brief  UART5 �жϴ�������
 */
void UART5_IRQHandler(void)
{
    uint8_t data;
    
    if (USART_ReadIntFlag(RS485_USART, USART_INT_RXBNE) != RESET) {
        data = USART_RxData(RS485_USART);
        
        uint16_t next_write = (rs485_rx_write_index + 1) % RS485_BUFFER_SIZE;
        if (next_write != rs485_rx_read_index) {
            rs485_rx_buffer[rs485_rx_write_index] = data;
            rs485_rx_write_index = next_write;
        }
        
        USART_ClearIntFlag(RS485_USART, USART_INT_RXBNE);
    }
}
