#ifndef __SYSTEM_CONFIG_H
#define __SYSTEM_CONFIG_H

// 系统时钟频率（基于内部8MHz HSI）
#define SYSTEM_CLOCK_HZ     72000000UL  // 72MHz

// 电压监控阈值（单位：V）
#define VOLTAGE_NORMAL_MIN  11.0f      // 最低正常电压
#define VOLTAGE_NORMAL_MAX  13.0f      // 最高正常电压
#define VOLTAGE_ALARM_HIGH  13.5f      // 过压报警阈值
#define VOLTAGE_ALARM_LOW   10.5f      // 欠压报警阈值

// 安全控制（0=关闭，1=启用）
#define SAFETY_RELAY_CONTROL 0         // 禁用继电器控制
#define SAFETY_BEEP_CONTROL  0         // 禁用蜂鸣器控制

// ADC采样频率
#define ADC_SAMPLE_INTERVAL_MS 1000    // 1秒采样一次

#endif
