#ifndef UDP_TASK_H
#define UDP_TASK_H

#include <stdint.h>
#include "lwip/sockets.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDP_BUFFER_SIZE     512

// 接收消息结构体
typedef struct {
    struct sockaddr_in src_addr;   // 源地址
    uint16_t data_len;
    uint8_t data[UDP_BUFFER_SIZE];
} rx_msg_t;

// 接收数据回调函数指针
typedef void (*udp_rx_callback_t)(const rx_msg_t *msg);

// 初始化UDP通信任务
void UDP_Task_Init(void);

// API函数：发送UDP数据
// 参数：data - 要发送的数据, len - 数据长度, ip_addr - 目标IP地址, port - 目标端口
// 返回：0 - 成功, -1 - 参数错误, -2 - 无效IP地址, -3 - 队列满或超时
int UDP_SendData(const uint8_t *data, uint16_t len, const char *ip_addr, uint16_t port);

// API函数：接收UDP数据（非阻塞）
// 参数：msg - 接收消息缓冲区, timeout_ms - 超时时间（毫秒）
// 返回：0 - 成功, -1 - 超时或错误
int UDP_ReceiveData(rx_msg_t *msg, uint32_t timeout_ms);

// API函数：设置接收回调函数
// 参数：callback - 回调函数指针，当接收到数据时自动调用
void UDP_SetRxCallback(udp_rx_callback_t callback);

// API函数：获取队列状态
int UDP_GetTxQueueCount(void);  // 获取发送队列中的消息数量
int UDP_GetRxQueueCount(void);  // 获取接收队列中的消息数量

// API函数：清空队列
void UDP_ClearTxQueue(void);    // 清空发送队列
void UDP_ClearRxQueue(void);    // 清空接收队列

#ifdef __cplusplus
}
#endif

#endif /* UDP_TASK_H */ 