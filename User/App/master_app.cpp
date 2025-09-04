// #include "slave_app.h"
#include "MasterServer.h"
#include "cmsis_os2.h"
#include "elog.h"
#include "udp_task.h"
#include "uwb_task.h"

using namespace WhtsProtocol;

extern void UWB_Task_Init(void);
extern void UDP_Task_Init(void);

extern "C" int main_app(void) {
    UWB_Task_Init();    // 初始化UWB通信任务
    UDP_Task_Init();    // 初始化UDP通信任务

    MasterServer masterServer;
    masterServer.run();

    for (;;) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_0);
        elog_v("master_app", "Hello World");
        osDelay(MAIN_LOOP_DELAY_MS);
    }
    return 0;
}