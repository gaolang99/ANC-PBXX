#ifndef WEB_TIME_HANDLER_H
#define WEB_TIME_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "tcp.h"
#include "bsp_relay.h"  // ????????BSP????
#include <stddef.h>
/* ??????????????? */
void Time_Manager_Init(void);
uint32_t Get_Current_Unix_Time(void);
const char* Get_Formatted_Time(void);
void Web_API_Sync_From_Host(uint32_t unix_time, int8_t timezone);
const char* Web_API_Get_Time(void);
char* Get_Current_Time_String(void);
void Get_Time_String_To_Buffer(char* buf, size_t size);
uint8_t Is_Time_Synchronized(void);
/* ?????????????????? */
void Record_Relay_Operation(Relay_Id_T relay_id, Relay_State_T operation);
const char* Web_API_Get_Relay_History(void);
void Clear_Relay_Operation_History(void);

/* HTTP?????????? */
void Handle_Time_Sync_Request(struct tcp_pcb *pcb, const char *request, uint16_t len);
void Handle_Time_Get_Request(struct tcp_pcb *pcb);
void Handle_Relay_History_Request(struct tcp_pcb *pcb);
void Handle_Relay_Clear_History_Request(struct tcp_pcb *pcb);

/* ???????????????????????Flash?????? */
void Time_Manager_Task(void);

/* ????API?????????? */
void Time_Manager_Sync_Async(uint32_t timestamp, int8_t timezone);

#endif /* WEB_TIME_HANDLER_H */