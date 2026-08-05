/**
 * @file
 * Ethernet Interface Skeleton
 *
 */

/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

/*
 * This file is a skeleton for developing Ethernet network interface
 * drivers for lwIP. Add code to the low_level functions and do a
 * search-and-replace for the word "ethernetif" to replace it with
 * something that better describes your network interface.
 */

#include "ethernetif.h"
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "netif/etharp.h"
#include "netif/ppp_oe.h"
#include <string.h>
#include <stdio.h>

/* Define those to better describe your network interface. */
#define IFNAME0 'a'
#define IFNAME1 'p'

/** @addtogroup Examples
  * @{
  */

/** @addtogroup ETH_Ping
  * @{
  */ 

/** @defgroup ETH_Ping_Structures Structures
  @{
*/

/**
 * Helper struct to hold private data used to operate your ethernet interface.
 * Keeping the ethernet address of the MAC in this struct is not necessary
 * as it is already kept in the struct netif.
 * But this is only an example, anyway...
 */
struct ethernetif {
  struct eth_addr *ethaddr;
  /* Add whatever per-interface state that is needed here. */
};

/**@} end of group ETH_Ping_Structures */

/** @defgroup ETH_Ping_Functions Functions
  @{
*/

/* Forward declarations. */
err_t  ethernetif_input(struct netif *netif);
ETH_Frame_T ETH_RxPacket(void);
int ETH_TxPacket(u16 Length);

/**@} end of group ETH_Ping_Functions */

/** @defgroup ETH_Ping_Variables Variables
  @{
*/

/* MAC ADDRESS */
extern uint8_t SetMACaddr[6];
/* Ethernet Rx & Tx DMA Descriptors */
extern ETH_DMADescConfig_T  DMARxDscrTab[ETH_RXBUFNB], DMATxDscrTab[ETH_TXBUFNB];
/* Ethernet buffers */
extern uint8_t Rx_Buff[ETH_RXBUFNB][ETH_MAX_PACKET_SIZE], Tx_Buff[ETH_TXBUFNB][ETH_MAX_PACKET_SIZE];

extern ETH_DMADescConfig_T  *DMATxDescToSet;
extern ETH_DMADescConfig_T  *DMARxDescToGet;

/* Flag to signal packet reception from ISR to Main Loop */
volatile uint8_t g_EthPacketReceived = 0;
volatile uint32_t g_EthISRCount = 0;

/**@} end of group ETH_Ping_Variables */

/** @defgroup ETH_Ping_Functions Functions
  @{
*/

/**
 * In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void
low_level_init(struct netif *netif)
{
  int i;
  /* set MAC hardware address length */
  netif->hwaddr_len = ETHARP_HWADDR_LEN;

  /* set MAC hardware address */
  netif->hwaddr[0] =  SetMACaddr[0];
  netif->hwaddr[1] =  SetMACaddr[1];
  netif->hwaddr[2] =  SetMACaddr[2];
  netif->hwaddr[3] =  SetMACaddr[3];
  netif->hwaddr[4] =  SetMACaddr[4];
  netif->hwaddr[5] =  SetMACaddr[5];

  /* maximum transfer unit */
  netif->mtu = 1500;

  /* device capabilities */
  /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

  /* Initialize Tx Descriptors list: Chain Mode */
  ETH_ConfigDMATxDescChain(DMATxDscrTab, &Tx_Buff[0][0], ETH_TXBUFNB);
  /* Initialize Rx Descriptors list: Chain Mode  */
  ETH_ConfigDMARxDescChain(DMARxDscrTab, &Rx_Buff[0][0], ETH_RXBUFNB);
  
  /* Initialize DMA descriptor pointers to the start of the arrays */
  DMARxDescToGet = DMARxDscrTab;
  DMATxDescToSet = DMATxDscrTab;

  /* Enable Ethernet Rx interrrupt */
  for(i=0; i<ETH_RXBUFNB; i++)
  {
    ETH_EnableDMARxDescReceiveInterrupt(&DMARxDscrTab[i]);
  }

#ifdef HARDWARE_CHECKSUM
  /* Enable the checksum insertion for the Tx frames */
  /* Disable hardware checksum insertion to avoid conflict with LwIP software checksum */
  /*
  for(int i=0; i<ETH_TXBUFNB; i++)
  {
    ETH_ConfigDMATxDescChecksumInsertion(&DMATxDscrTab[i], ETH_DMATXDESC_CHECKSUMTCPUDPICMPFULL);
  }
  */
#endif

  /* Enable MAC and DMA transmission and reception */
  ETH_Start();
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become availale since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */
static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  struct pbuf *q;
  int l = 0;
  u8 *buffer =  (u8 *)(DMATxDescToSet->Buffer1Addr);
  int tx_result;
  
  for(q = p; q != NULL; q = q->next) 
  {
    memcpy((u8_t*)&buffer[l], q->payload, q->len);
    l = l + q->len;
  }

  // printf("[ETH] Send: %d bytes\n", l);
  tx_result = ETH_TxPacket(l);
  
  if(tx_result == ETH_ERROR)
  {
    return ERR_BUF;
  }

  return ERR_OK;
}

/**
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
 */
static struct pbuf *
low_level_input(struct netif *netif, int *dropped)
{
  struct pbuf *p, *q;
  u16_t len;
  int l =0;
  ETH_Frame_T frame;
  u8 *buffer;
  
  if (dropped) *dropped = 0;

  p = NULL;
  frame = ETH_RxPacket();
  
  if (frame.length > 0 && frame.length != (u16_t)ETH_ERROR)
  {
    // printf("[ETH] Recv: %d bytes\n", frame.length);
    /* Obtain the size of the packet and put it into the "len"
       variable. */
    len = frame.length;
    
    buffer = (u8 *)frame.buffer;

    /* We allocate a pbuf chain of pbufs from the pool. */
    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

    if (p != NULL)
    {
      for (q = p; q != NULL; q = q->next)
      {
        memcpy((u8_t*)q->payload, (u8_t*)&buffer[l], q->len);
        l = l + q->len;
      }    
    }
    else 
    {
        // OOM: Packet exists but cannot allocate pbuf.
        // We must drop it to free the descriptor and avoid blocking the ring.
        if (dropped) *dropped = 1;
        // printf("[ETH] Drop packet (OOM)\n");
    }
  }

  /* Release descriptor back to DMA */
  if (frame.descriptor != NULL) 
  {
      /** Set Own bit of the Rx descriptor Status: gives the buffer back to ETHERNET DMA */
      frame.descriptor->Status = ETH_DMARXDESC_OWN; 
  }
 
  /** When Rx Buffer unavailable flag is set: clear it and resume reception */
  if ((ETH->DMASTS & BIT7) != (u32)RESET)   
  {
    /** Clear RBUS ETHERNET DMA flag */
    ETH->DMASTS = BIT7;
    /** Resume DMA reception */
    ETH->DMARXPD = 0;
  }

  return p;
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
err_t
ethernetif_input(struct netif *netif)
{
  err_t err;
  struct pbuf *p;
  int dropped = 0;

  /* move received packet into a new pbuf */
  p = low_level_input(netif, &dropped);

  /* no packet could be read */
  if (p == NULL) {
      if (dropped) {
          // Packet was received but dropped due to OOM.
          // Return ERR_OK to signal "Handled" so the polling loop continues to drain the ring.
          return ERR_OK;
      }
      // No packet at all. Return ERR_MEM (or specific error) to stop polling.
      return ERR_MEM;
  }

  err = netif->input(p, netif);
  if (err != ERR_OK)
  {
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
    pbuf_free(p);
    p = NULL;
  }

  return err;
}

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t
ethernetif_init(struct netif *netif)
{
  struct ethernetif *ethernetif;

  LWIP_ASSERT("netif != NULL", (netif != NULL));

  ethernetif = (struct ethernetif *)mem_malloc(sizeof(struct ethernetif));
  if (ethernetif == NULL)
  {
    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_init: out of memory\n"));
    return ERR_MEM;
  }

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  /*
   * Initialize the snmp variables and counters inside the struct netif.
   * The last argument should be replaced with your link speed, in units
   * of bits per second.
   */
  NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, 100000000);

  netif->state = ethernetif;
  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  /* We directly use etharp_output() here to save a function call.
   * You can instead declare your own function an call etharp_output()
   * from it if you have to do some checks before sending (e.g. if link
   * is available...) */
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;

  ethernetif->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}

/*!
 * @brief   received ethernet packet   
 *
 * @param   None
 *
 * @retval  None
 *
 * @note       
 */  
ETH_Frame_T ETH_RxPacket(void)
{ 
  u32 ethframelength = 0;
  ETH_Frame_T ethframe = {0,0}; 

  /* Check if the descriptor is owned by the ETHERNET DMA (when set) or CPU (when reset) */
  if((DMARxDescToGet->Status & ETH_DMARXDESC_OWN) != (u32)RESET)
  {
    ethframe.length = ETH_ERROR;

    if ((ETH->DMASTS & BIT7) != (u32)RESET)  
    {
      /* Clear RBUS ETHERNET DMA flag */
      ETH->DMASTS = BIT7;
      /* Resume DMA reception */
      ETH->DMARXPD = 0;
    }

    /* Return error: OWN bit set */
    return ethframe; 
  }
  
  if(((DMARxDescToGet->Status & ETH_DMARXDESC_ERRS) == (u32)RESET) && 
     ((DMARxDescToGet->Status & ETH_DMARXDESC_LDES) != (u32)RESET) &&  
     ((DMARxDescToGet->Status & ETH_DMARXDESC_FDES) != (u32)RESET))  
  {      
    /* Get the Frame Length of the received packet: do not subtract 4 bytes (CRC) */
    ethframelength = ((DMARxDescToGet->Status & ETH_DMARXDESC_FL) >> 16);
    
    /* Get the addrees of the actual buffer */
    ethframe.buffer = DMARxDescToGet->Buffer1Addr; 
  }
  else
  {
    /* Return ERROR */
    ethframelength = ETH_ERROR;
  }
  
  ethframe.length = ethframelength;
  ethframe.descriptor = DMARxDescToGet;
  
  /* Selects the next DMA Rx descriptor list for next buffer to read */ 
  DMARxDescToGet = (ETH_DMADescConfig_T*) (DMARxDescToGet->Buffer2NextDescAddr);    
  
  /* Return Frame */
  return ethframe;  
}

/*!
 * @brief   Transmits a ethernet packet   
 *
 * @param   FrameLength: Transmits Packet size
 *
 * @retval  ETH_ERROR: in case of Tx desc owned by DMA
 *          ETH_SUCCESS: for correct transmission
 *
 * @note       
 */  
int ETH_TxPacket(u16 Length)
{   
  /** Check if the descriptor is owned by the ETHERNET DMA (when set) or CPU (when reset) */
  if((DMATxDescToSet->Status & ETH_DMATXDESC_OWN) != (u32)RESET)
  {  
    /** Return ERROR: OWN bit set */
    return ETH_ERROR;
  }
        
  /** Setting the Frame Length: bits[12:0] */
  DMATxDescToSet->ControlBufferSize = (Length & ETH_DMATXDESC_TXBS1);

  /** Setting the last segment and first segment bits */    
  DMATxDescToSet->Status |= ETH_DMATXDESC_LS | ETH_DMATXDESC_FS;

  /** Set Own bit of the Tx descriptor Status: gives the buffer back to ETHERNET DMA */
  DMATxDescToSet->Status |= ETH_DMATXDESC_OWN;

  /** When Tx Buffer unavailable flag is set: clear it and resume transmission */
  if ((ETH->DMASTS & BIT2) != (u32)RESET)
  {
    /** Clear TBUS ETHERNET DMA flag */
    ETH->DMASTS = BIT2;
    /** Resume DMA transmission*/
    ETH->DMATXPD = 0;
  }
  
  /** Update the ETHERNET DMA global Tx descriptor with next Tx decriptor */  
  /** Chained Mode */
  /** Selects the next DMA Tx descriptor list for next buffer to send */ 
  DMATxDescToSet = (ETH_DMADescConfig_T*) (DMATxDescToSet->Buffer2NextDescAddr);    

  /** Return SUCCESS */
  return ETH_SUCCESS;
}

/**
 * @brief     This function handles Ethernet interrupts
 *
 * @param     None
 *
 * @retval    None
 */
void ETH_ProcessIT(void)
{
  /* Signal main loop to process received Ethernet packets */
  g_EthPacketReceived = 1;
  g_EthISRCount++;
  
  if (g_EthISRCount % 10 == 0) {
     // Limit RTT output to avoid flooding
     // printf("[ETH] ISR: %d\n", g_EthISRCount);
  }
}

/**@} end of group ETH_Ping_Functions */
/**@} end of group ETH_Ping */
/**@} end of group Examples */
