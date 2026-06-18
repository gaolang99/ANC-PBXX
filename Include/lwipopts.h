#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#include <stdint.h>
#include "apm32f10x.h"

/* -------------------------- 裸机系统核心配置 -------------------------- */
#define NO_SYS                      1       
#define LWIP_TCP                    1       
#define LWIP_UDP                    0       
#define LWIP_ICMP                   1       
#define LWIP_RAW                    0       
#define LWIP_TCP_KEEPALIVE          0       
#define LWIP_NETIF_LOOPBACK         1       

/* -------------------------- 内存配置（针对 APM32F107 64KB RAM 极限优化） -------------------------- */
#define MEM_ALIGNMENT               4
// 核心内存池大小：5KB (适度增加，平衡 TCP 缓冲需求与 RAM 限制)
#define MEM_SIZE                    (5*1024)   

// PBUF配置
#define PBUF_POOL_SIZE              8           // [调整] 16 -> 8 以解决 L6406E 内存溢出，同时满足 4KB 发送窗口需求
#define PBUF_POOL_BUFSIZE           1536        
#define PBUF_LINK_HLEN              14           

// 内存池配置
#define MEMP_NUM_PBUF               10          
#define MEMP_NUM_TCP_PCB            10          
#define MEMP_NUM_TCP_PCB_LISTEN     4           
#define MEMP_NUM_TCP_SEG            40          // [增加] 24 -> 40 以支持更大的 TCP_SND_QUEUELEN
#define MEMP_NUM_ARP_QUEUE          2           
#define MEMP_NUM_SYS_TIMEOUT        5           

/* -------------------------- TCP配置（低内存模式） -------------------------- */
#define TCP_MSS                     1024         
// TCP发送缓冲区：回退到 2*MSS 以避免 5KB RAM 下的内存耗尽 (4096 -> 2048)
#define TCP_SND_BUF                 (2*TCP_MSS)  
#define TCP_WND                     (2*TCP_MSS)  
#define TCP_SND_QUEUELEN            16          // 【关键】增加队列长度适配 4KB 缓冲区
#define TCP_SNDQUEUELOWAT           TCP_SND_QUEUELEN/2           
#define TCP_LISTEN_BACKLOG          5           
#define TCP_NODELAY                 1           
#define TCP_DEFAULT_LISTEN_BACKLOG  5           
#define TCP_MAXRTX                  12          // 恢复默认值12，增加重传次数
#define TCP_SYNMAXRTX               6           // 恢复默认值6，增加连接建立稳定性
#define TCP_TMR_INTERVAL            250         // 恢复标准250ms
#define TCP_FAST_INTERVAL           250         // 恢复标准250ms
#define TCP_SLOW_INTERVAL           500         // 恢复标准500ms
#define TCP_DELAYED_ACK_TIMEOUT     50          // 增加到50ms以缓解定时器压力

/* -------------------------- ARP配置 -------------------------- */
#define LWIP_ARP                    1
#define ARP_TABLE_SIZE              2           

/* -------------------------- 协议栈功能精简 -------------------------- */
#define LWIP_DHCP                   0       
#define LWIP_AUTOIP                 0
#define LWIP_NETIF_HOSTNAME         0       
#define LWIP_NETCONN                0       
#define LWIP_SOCKET                 0       
#define LWIP_STATS                  0       
#define LWIP_STATS_DISPLAY          0
#define LWIP_PROVIDE_ERRNO          1
#define LWIP_DISABLE_TCP_SANITY_CHECKS 1    

/* -------------------------- IP配置 -------------------------- */
#define IP_FORWARD                  0
#define IP_REASSEMBLY               0       
#define IP_FRAG                     0       
#define IP_REASS_MAX_PBUFS          0
#define IP_DEFAULT_TTL              64
#define IP_SOF_BROADCAST            1
#define IP_SOF_BROADCAST_RECV       1

/* -------------------------- 系统配置（裸机适配） -------------------------- */
#define LWIP_SINGLE_NETIF           1       
#define TCPIP_THREAD_STACKSIZE      0       
#define TCPIP_THREAD_PRIO           0
#define TCPIP_MBOX_SIZE             0
#define DEFAULT_TCP_RECVMBOX_SIZE   0
#define DEFAULT_ACCEPTMBOX_SIZE     0
#define DEFAULT_UDP_RECVMBOX_SIZE   0
#define DEFAULT_RAW_RECVMBOX_SIZE   0

/* -------------------------- 定时器配置 -------------------------- */
#define LWIP_TCPIP_TIMEOUT          1
#define ARP_TMR_INTERVAL            5000    // 修正宏名为标准 LwIP 宏名，恢复为 5000ms 周期

/* -------------------------- 调试配置（保持禁用） -------------------------- */
#define LWIP_DEBUG                  0       
#define LWIP_DBG_MIN_LEVEL          LWIP_DBG_LEVEL_OFF
#define LWIP_PLATFORM_DIAG(x)       do {} while(0)

/* -------------------------- 互斥保护 -------------------------- */
#define SYS_LIGHTWEIGHT_PROT        1       // 启用轻量级保护以避免裸机下的并发冲突
#define LWIP_COMPAT_MUTEX           0
#define LWIP_ALLOW_MEM_FREE_FROM_OTHER_CONTEXT 1

/* -------------------------- 校验和配置 -------------------------- */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1

/* -------------------------- 零拷贝配置 -------------------------- */
#define LWIP_TCPIP_CORE_LOCKING     0       
#define TCP_OVERSIZE                TCP_MSS 
#define IP_FRAG_MAX_MTU             1500    

#endif /* __LWIPOPTS_H__ */