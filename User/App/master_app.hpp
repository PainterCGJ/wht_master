#include "TaskCPP.h"


class BackendDataTransferTask : public TaskClassS<1024> {
    public:
     BackendDataTransferTask();

    private:
     void task() override;
     static constexpr const char TAG[] = "BackendDataTransferTask";
 };