#ifndef __SYS_ARCH_H
#define __SYS_ARCH_H

#include <stdint.h>
#include "lwip/opt.h"  // 添加LwIP头文件

/* 系统滴答计数器 */
extern volatile uint32_t g_sys_tick;

/* 系统滴答初始化 */
void sys_tick_init(uint32_t sysclk_mhz);

/* 系统时间函数 */
uint32_t sys_get_tick_ms(void);
void sys_delay_ms(uint32_t ms);

/* 使用LwIP的类型声明sys_now，避免冲突 */
u32_t sys_now(void);

/* 信号量 */
typedef struct {
    uint8_t valid;
    uint8_t count;
} apm_sys_sem_t;

/* 邮箱 */
typedef struct {
    uint8_t valid;
    uint8_t len;
    uint8_t buf[4];
} apm_sys_mbox_t;

/* 互斥锁 */
typedef struct {
    uint8_t lock;
} apm_sys_mutex_t;

/* 线程句柄 */
typedef uint8_t apm_sys_thread_t;
#define APM_SYS_THREAD_NULL 0

/* 同步原语函数 */
apm_sys_sem_t apm_sys_sem_new(uint8_t count);
void apm_sys_sem_free(apm_sys_sem_t *sem);
uint32_t apm_sys_sem_wait(apm_sys_sem_t *sem, uint32_t timeout_ms);
void apm_sys_sem_signal(apm_sys_sem_t *sem);

apm_sys_mbox_t apm_sys_mbox_new(uint8_t size);
void apm_sys_mbox_free(apm_sys_mbox_t *mbox);
void apm_sys_mbox_post(apm_sys_mbox_t *mbox, void *msg, uint16_t len);
uint32_t apm_sys_mbox_fetch(apm_sys_mbox_t *mbox, void *msg, uint32_t timeout_ms);

apm_sys_mutex_t apm_sys_mutex_new(void);
void apm_sys_mutex_free(apm_sys_mutex_t *mutex);
uint32_t apm_sys_mutex_lock(apm_sys_mutex_t *mutex, uint32_t timeout_ms);
void apm_sys_mutex_unlock(apm_sys_mutex_t *mutex);

/* 线程函数 */
apm_sys_thread_t apm_sys_thread_new(const char *name, void (*func)(void*), void *arg, uint16_t stack_size, uint8_t prio);
apm_sys_thread_t apm_sys_thread_self(void);

#endif /* __SYS_ARCH_H */

