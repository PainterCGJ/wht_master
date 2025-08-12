// #include "slave_app.h"
#include "master_app.hpp"

#include "MasterServer.h"
#include "cmsis_os2.h"
#include "udp_task.h"
#include "uwb_task.h"
#include "elog.h"

using namespace WhtsProtocol;

extern void UWB_Task_Init(void);
extern void UDP_Task_Init(void);

extern "C" int main_app(void) {
    UWB_Task_Init();    // 初始化UWB通信任务
    UDP_Task_Init();    // 初始化UDP通信任务

    MasterServer masterServer;
    masterServer.run();

    // uint8_t data[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (;;) {

        // UDP_SendData(data, 10, "192.168.0.107", 8080);
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_0);
        elog_v("master_app", "Hello World");
        osDelay(500);
    }
    return 0;
}