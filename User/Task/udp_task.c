#include "elog.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "cmsis_os.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "main.h"
#include "udp_task.h"

#define UDP_SERVER_PORT 8080
#define TX_QUEUE_SIZE 2 * 10
#define RX_QUEUE_SIZE 2 * 10

// 消息类型定义
typedef enum
{
    MSG_TYPE_SEND_DATA = 1, // 应用任务发送数据
    MSG_TYPE_CLOSE_CONN,    // 关闭连接
    MSG_TYPE_CONFIG         // 配置信息
} msg_type_t;

// 发送消息结构体
typedef struct
{
    msg_type_t type;
    struct sockaddr_in dest_addr; // 目标地址
    uint16_t data_len;
    uint8_t data[UDP_BUFFER_SIZE];
} tx_msg_t;

// 全局变量
static osMessageQueueId_t txQueue; // 发送队列
static osMessageQueueId_t rxQueue; // 接收队列
static osThreadId_t udpTaskHandle;

// 接收数据回调函数指针
typedef void (*udp_rx_callback_t)(const udp_rx_msg_t *msg);
static udp_rx_callback_t rx_callback = NULL;

// UDP通信任务
void udp_comm_task(void *argument)
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[2 * UDP_BUFFER_SIZE];
    socklen_t client_addr_len = sizeof(client_addr);
    int recv_len;
    tx_msg_t tx_msg;
    udp_rx_msg_t rx_msg;

    // 创建 socket
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0)
    {
        elog_e("udp_task", "Failed to create UDP socket");
        osThreadExit();
    }

    // 设置服务器地址结构
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 绑定 socket
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        elog_e("udp_task", "Failed to bind UDP socket to port %d", UDP_SERVER_PORT);
        closesocket(sockfd);
        osThreadExit();
    }

    elog_i("udp_task", "UDP server started on port %d", UDP_SERVER_PORT);

    while (1)
    {
        // 检查是否有发送消息
        if (osMessageQueueGet(txQueue, &tx_msg, NULL, 0) == osOK)
        {
            switch (tx_msg.type)
            {
            case MSG_TYPE_SEND_DATA:
                // 发送数据到指定地址
                {
                    int sent_bytes = sendto(sockfd, tx_msg.data, tx_msg.data_len, 0,
                                            (struct sockaddr *)&tx_msg.dest_addr, sizeof(tx_msg.dest_addr));
                    if (sent_bytes < 0)
                    {
                        // 发送失败，记录错误
                        const char *error_desc = "Unknown error";
                        switch (errno)
                        {
                        case ENOBUFS:
                            error_desc = "No buffer space available";
                            break;
                        case ENETDOWN:
                            error_desc = "Network is down";
                            break;
                        case ENETUNREACH:
                            error_desc = "Network unreachable";
                            break;
                        case EHOSTUNREACH:
                            error_desc = "Host unreachable";
                            break;
                        case ECONNREFUSED:
                            error_desc = "Connection refused";
                            break;
                        case EMSGSIZE:
                            error_desc = "Message too large";
                            break;
                        default:
                            error_desc = "Socket send error";
                            break;
                        }
                        elog_e("udp_task", "UDP sendto failed: %s, target=%s:%d, size=%d", error_desc,
                               inet_ntoa(tx_msg.dest_addr.sin_addr), ntohs(tx_msg.dest_addr.sin_port), tx_msg.data_len);
                    }
                    else if (sent_bytes != tx_msg.data_len)
                    {
                        // 部分发送
                        elog_w("udp_task", "UDP partial send: sent=%d, expected=%d", sent_bytes, tx_msg.data_len);
                    }
                    else
                    {
                        // 发送成功
                        elog_v("udp_task", "UDP sent %d bytes to %s:%d", sent_bytes,
                               inet_ntoa(tx_msg.dest_addr.sin_addr), ntohs(tx_msg.dest_addr.sin_port));
                    }
                }
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
        recv_len = recvfrom(sockfd, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr *)&client_addr,
                            &client_addr_len);
        if (recv_len > 0)
        {
            // 检查是否可能发生数据截断
            if (recv_len >= sizeof(buffer))
            {
                elog_w("udp_task",
                       "UDP packet size (%d bytes) >= buffer size (%d bytes), data may be truncated! "
                       "Consider increasing UDP_BUFFER_SIZE if packets exceed this size.",
                       recv_len, UDP_BUFFER_SIZE);
            }

            // 记录接收到的字节数
            elog_i("udp_task", "UDP received %d bytes from %s:%d", recv_len, inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));

            // 如果数据大于UDP_BUFFER_SIZE，需要分片处理
            if (recv_len > UDP_BUFFER_SIZE)
            {
                // 计算需要分片的数量
                int fragment_count = (recv_len + UDP_BUFFER_SIZE - 1) / UDP_BUFFER_SIZE;
                elog_i("udp_task", "Large packet detected (%d bytes), splitting into %d fragments", recv_len, fragment_count);

                int offset = 0;
                int fragments_queued = 0;
                int fragments_dropped = 0;

                // 分片并逐个入队
                for (int frag_idx = 0; frag_idx < fragment_count; frag_idx++)
                {
                    // 计算当前分片的大小
                    int fragment_size = (offset + UDP_BUFFER_SIZE <= recv_len) ? UDP_BUFFER_SIZE : (recv_len - offset);

                    // 构造接收消息
                    rx_msg.src_addr = client_addr;
                    rx_msg.data_len = fragment_size;

                    // 复制数据到rx_msg
                    for (int i = 0; i < fragment_size; i++)
                    {
                        rx_msg.data[i] = buffer[offset + i];
                    }

                    // 将分片放入接收队列
                    if (osMessageQueuePut(rxQueue, &rx_msg, 0, 0) != osOK)
                    {
                        elog_w("udp_task", "UDP RX queue full, dropping fragment %d/%d from %s:%d (%d bytes)",
                               frag_idx + 1, fragment_count, inet_ntoa(client_addr.sin_addr),
                               ntohs(client_addr.sin_port), fragment_size);
                        fragments_dropped++;
                    }
                    else
                    {
                        elog_v("udp_task", "UDP fragment %d/%d queued successfully (%d bytes)", frag_idx + 1,
                               fragment_count, fragment_size);
                        fragments_queued++;

                        // 如果有回调函数，调用它
                        if (rx_callback != NULL)
                        {
                            rx_callback(&rx_msg);
                        }
                    }

                    offset += fragment_size;
                }

                // 记录分片处理结果
                if (fragments_dropped > 0)
                {
                    elog_w("udp_task", "Packet fragmentation completed: %d fragments queued, %d fragments dropped",
                           fragments_queued, fragments_dropped);
                }
                else
                {
                    elog_v("udp_task", "Packet fragmentation completed: all %d fragments queued successfully",
                           fragments_queued);
                }
            }
            else
            {
                // 数据小于等于UDP_BUFFER_SIZE，正常处理
                rx_msg.src_addr = client_addr;
                rx_msg.data_len = recv_len;

                // 复制数据到rx_msg
                for (int i = 0; i < recv_len; i++)
                {
                    rx_msg.data[i] = buffer[i];
                }

                // 将数据放入接收队列
                if (osMessageQueuePut(rxQueue, &rx_msg, 0, 0) != osOK)
                {
                    elog_w("udp_task", "UDP RX queue full, dropping packet from %s:%d (%d bytes)",
                           inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), recv_len);
                }
                else
                {
                    elog_v("udp_task", "UDP packet queued successfully");

                    // 如果有回调函数，调用它
                    if (rx_callback != NULL)
                    {
                        rx_callback(&rx_msg);
                    }
                }
            }
        }

        osDelay(10); // 防止任务占满 CPU
    }
}

// 初始化UDP通信任务
void UDP_Task_Init(void)
{
    // 创建消息队列
    txQueue = osMessageQueueNew(TX_QUEUE_SIZE, sizeof(tx_msg_t), NULL);
    if (txQueue == NULL)
    {
        return;
    }

    rxQueue = osMessageQueueNew(RX_QUEUE_SIZE, sizeof(udp_rx_msg_t), NULL);
    if (rxQueue == NULL)
    {
        return;
    }

    // 创建UDP通信任务
    const osThreadAttr_t udpTask_attributes = {
        .name = "udpCommTask",
        .stack_size = 1024 * 16,
        .priority = (osPriority_t)osPriorityNormal,
    };
    udpTaskHandle = osThreadNew(udp_comm_task, NULL, &udpTask_attributes);
}

// API函数：发送UDP数据
int UDP_SendData(const uint8_t *data, uint16_t len, const char *ip_addr, uint16_t port)
{
    if (data == NULL || len == 0 || len > UDP_BUFFER_SIZE || ip_addr == NULL)
    {
        return -1;
    }

    tx_msg_t msg;
    msg.type = MSG_TYPE_SEND_DATA;
    msg.data_len = len;

    // use memcpy to copy data
    memcpy(msg.data, data, len);

    // 设置目标地址
    msg.dest_addr.sin_family = AF_INET;
    msg.dest_addr.sin_port = htons(port);
    if (inet_aton(ip_addr, &msg.dest_addr.sin_addr) == 0)
    {
        return -2; // 无效的IP地址
    }

    // 发送到队列
    if (osMessageQueuePut(txQueue, &msg, 0, 100) != osOK)
    {
        return -3; // 队列满或超时
    }

    return 0; // 成功
}

// API函数：接收UDP数据（非阻塞）
int UDP_ReceiveData(udp_rx_msg_t *msg, uint32_t timeout_ms)
{
    if (msg == NULL)
    {
        return -1;
    }

    if (osMessageQueueGet(rxQueue, msg, NULL, timeout_ms) == osOK)
    {
        return 0; // 成功
    }

    return -1; // 超时或错误
}

// API函数：设置接收回调函数
void UDP_SetRxCallback(udp_rx_callback_t callback)
{
    rx_callback = callback;
}

// API函数：获取队列状态
int UDP_GetTxQueueCount(void)
{
    return (int)osMessageQueueGetCount(txQueue);
}

int UDP_GetRxQueueCount(void)
{
    return (int)osMessageQueueGetCount(rxQueue);
}

// API函数：清空队列
void UDP_ClearTxQueue(void)
{
    tx_msg_t msg;
    while (osMessageQueueGet(txQueue, &msg, NULL, 0) == osOK)
    {
        // 清空队列
    }
}

void UDP_ClearRxQueue(void)
{
    udp_rx_msg_t msg;
    while (osMessageQueueGet(rxQueue, &msg, NULL, 0) == osOK)
    {
        // 清空队列
    }
}