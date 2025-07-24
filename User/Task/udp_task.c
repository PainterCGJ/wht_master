#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include "cmsis_os.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

#define UDP_SERVER_PORT     8000
#define UDP_BUFFER_SIZE     512
#define TX_QUEUE_SIZE       10
#define RX_QUEUE_SIZE       10

// 消息类型定义
typedef enum {
    MSG_TYPE_SEND_DATA = 1,     // 应用任务发送数据
    MSG_TYPE_CLOSE_CONN,        // 关闭连接
    MSG_TYPE_CONFIG             // 配置信息
} msg_type_t;

// 发送消息结构体
typedef struct {
    msg_type_t type;
    struct sockaddr_in dest_addr;  // 目标地址
    uint16_t data_len;
    uint8_t data[UDP_BUFFER_SIZE];
} tx_msg_t;

// 接收消息结构体
typedef struct {
    struct sockaddr_in src_addr;   // 源地址
    uint16_t data_len;
    uint8_t data[UDP_BUFFER_SIZE];
} rx_msg_t;

// 全局变量
static osMessageQueueId_t txQueue;    // 发送队列
static osMessageQueueId_t rxQueue;    // 接收队列
static osThreadId_t udpTaskHandle;

// 接收数据回调函数指针
typedef void (*udp_rx_callback_t)(const rx_msg_t *msg);
static udp_rx_callback_t rx_callback = NULL;

// UDP通信任务
void udp_comm_task(void *argument)
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[UDP_BUFFER_SIZE];
    socklen_t client_addr_len = sizeof(client_addr);
    int recv_len;
    tx_msg_t tx_msg;
    rx_msg_t rx_msg;

    // 创建 socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        osThreadExit();
    }

    // 设置服务器地址结构
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 绑定 socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        closesocket(sockfd);
        osThreadExit();
    }

    while (1) {
        // 检查是否有发送消息
        if (osMessageQueueGet(txQueue, &tx_msg, NULL, 0) == osOK) {
            switch (tx_msg.type) {
                case MSG_TYPE_SEND_DATA:
                    // 发送数据到指定地址
                    sendto(sockfd, tx_msg.data, tx_msg.data_len, 0,
                           (struct sockaddr *)&tx_msg.dest_addr, sizeof(tx_msg.dest_addr));
                    break;
                
                case MSG_TYPE_CLOSE_CONN:
                    break;
                
                case MSG_TYPE_CONFIG:
                    break;
                
                default:
                    break;
            }
        }

        // 非阻塞接收数据
        recv_len = recvfrom(sockfd, buffer, UDP_BUFFER_SIZE, MSG_DONTWAIT,
                            (struct sockaddr *)&client_addr, &client_addr_len);
        if (recv_len > 0) {
            // 构造接收消息 - 使用简单赋值避免memcpy
            rx_msg.src_addr = client_addr;
            rx_msg.data_len = recv_len;
            for (int i = 0; i < recv_len && i < UDP_BUFFER_SIZE; i++) {
                rx_msg.data[i] = buffer[i];
            }

            // 将数据放入接收队列
            osMessageQueuePut(rxQueue, &rx_msg, 0, 0);

            // 如果有回调函数，调用它
            if (rx_callback != NULL) {
                rx_callback(&rx_msg);
            }
        }

        osDelay(10); // 防止任务占满 CPU
    }
}

// 初始化UDP通信任务
void UDP_Task_Init(void) {
    // 创建消息队列
    txQueue = osMessageQueueNew(TX_QUEUE_SIZE, sizeof(tx_msg_t), NULL);
    if (txQueue == NULL) {
        return;
    }

    rxQueue = osMessageQueueNew(RX_QUEUE_SIZE, sizeof(rx_msg_t), NULL);
    if (rxQueue == NULL) {
        return;
    }

    // 创建UDP通信任务
    const osThreadAttr_t udpTask_attributes = {
        .name = "udpCommTask",
        .stack_size = 512 * 4,
        .priority = (osPriority_t)osPriorityNormal,
    };
    udpTaskHandle = osThreadNew(udp_comm_task, NULL, &udpTask_attributes);
}

// API函数：发送UDP数据
int UDP_SendData(const uint8_t *data, uint16_t len, const char *ip_addr, uint16_t port) {
    if (data == NULL || len == 0 || len > UDP_BUFFER_SIZE || ip_addr == NULL) {
        return -1;
    }

    tx_msg_t msg;
    msg.type = MSG_TYPE_SEND_DATA;
    msg.data_len = len;
    
    // 手动复制数据
    for (int i = 0; i < len; i++) {
        msg.data[i] = data[i];
    }

    // 设置目标地址
    msg.dest_addr.sin_family = AF_INET;
    msg.dest_addr.sin_port = htons(port);
    if (inet_aton(ip_addr, &msg.dest_addr.sin_addr) == 0) {
        return -2; // 无效的IP地址
    }

    // 发送到队列
    if (osMessageQueuePut(txQueue, &msg, 0, 100) != osOK) {
        return -3; // 队列满或超时
    }

    return 0; // 成功
}

// API函数：接收UDP数据（非阻塞）
int UDP_ReceiveData(rx_msg_t *msg, uint32_t timeout_ms) {
    if (msg == NULL) {
        return -1;
    }

    if (osMessageQueueGet(rxQueue, msg, NULL, timeout_ms) == osOK) {
        return 0; // 成功
    }

    return -1; // 超时或错误
}

// API函数：设置接收回调函数
void UDP_SetRxCallback(udp_rx_callback_t callback) {
    rx_callback = callback;
}

// API函数：获取队列状态
int UDP_GetTxQueueCount(void) {
    return (int)osMessageQueueGetCount(txQueue);
}

int UDP_GetRxQueueCount(void) {
    return (int)osMessageQueueGetCount(rxQueue);
}

// API函数：清空队列
void UDP_ClearTxQueue(void) {
    tx_msg_t msg;
    while (osMessageQueueGet(txQueue, &msg, NULL, 0) == osOK) {
        // 清空队列
    }
}

void UDP_ClearRxQueue(void) {
    rx_msg_t msg;
    while (osMessageQueueGet(rxQueue, &msg, NULL, 0) == osOK) {
        // 清空队列
    }
}