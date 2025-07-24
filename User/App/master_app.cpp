// #include "slave_app.h"
#include "master_app.hpp"

#include "TaskCPP.h"
#include "cmsis_os2.h"
#include "elog.h"
#include "udp_task.h"
#include "uwb_task.h"

extern void UWB_Task_Init(void);
extern void UDP_Task_Init(void);

// void Udp2uwb_Task_Init(void);

BackendDataTransferTask backendDataTransferTask;
extern "C" int main_app(void) {
    UWB_Task_Init();    // 初始化UWB通信任务
    UDP_Task_Init();    // 初始化UDP通信任务
    // Udp2uwb_Task_Init();    // 初始化UDP到UWB的转换任务
    backendDataTransferTask.give();

    for (;;) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_0);
        osDelay(500);
    }
    return 0;
}

// BackendDataTransferTask 实现
BackendDataTransferTask::BackendDataTransferTask()
    : TaskClassS("BackendDataTransfer", TaskPrio_High) {}

// BackendDataTransferTask 任务实现
void BackendDataTransferTask::task() {
    elog_i(TAG, "BackendDataTransferTask task start");

    // 从uwb接受数据并转发到udp
    uwb_rx_msg_t uwb_rx_msg;
    while (1) {
        if (UWB_ReceiveData(&uwb_rx_msg, 1000) == 0) {
            elog_i(TAG, "uwb_rx_msg.data_len: %d", uwb_rx_msg.data_len);
            UDP_SendData(uwb_rx_msg.data, uwb_rx_msg.data_len, "192.168.0.103",
                         9000);
        }
        osDelay(10);
    }
}