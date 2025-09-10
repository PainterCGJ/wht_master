#pragma once

#include <memory>

#include "B2M_MessageHandlers.h"
#include "CommandTracking.h"
#include "DeviceManager.h"
#include "MutexCPP.h"
#include "S2M_MessageHandlers.h"
#include "TaskCPP.h"
#include "master_app.h"

class MasterServer
{
  public:
    MasterServer();
    ~MasterServer();

    constexpr static const char TAG[] = "MasterServer";
    constexpr static const uint32_t DataSend_TX_QUEUE_TIMEOUT = DATA_SEND_TX_QUEUE_TIMEOUT_MS;

    ProtocolProcessor processor;
    std::unordered_map<uint8_t, std::unique_ptr<IMessageHandler>> messageHandlers;
    std::vector<PendingCommand> pendingCommands;
    std::vector<PingSession> activePingSessions;
    std::vector<PendingBackendResponse> pendingBackendResponses;
    DeviceManager deviceManager;

    // Mutex to protect pendingCommands from race conditions
    Mutex pendingCommandsMutex;

    // 时间同步相关
    uint32_t lastSyncTime;
    bool initialTimeSyncCompleted; // 标记是否已完成初始时间同步

    /**
     * 运行主循环
     */
    void run();

    /**
     * 发送到从机
     * @param frame 要发送的数据帧
     * @return 是否发送成功
     */
    bool sendToSlave(std::vector<uint8_t> &frame);

    /**
     * 发送到后端
     * @param frame 要发送的数据帧
     * @return 是否发送成功
     */
    bool sendToBackend(std::vector<uint8_t> &frame);

    /**
     * 后端到主机数据处理任务类 (处理从后端接收到的数据)
     */
    class SlaveDataProcT : public TaskClassS<2048>
    {
      public:
        SlaveDataProcT(MasterServer &parent);

      private:
        MasterServer &parent;
        std::vector<uint8_t> recvData;
        void task() override;
        static constexpr const char TAG[] = "SlaveDataProcT";
    };

    /**
     * 从机到主机数据处理任务类 (处理从从机接收到的数据)
     */
    class BackDataProcT : public TaskClassS<2048>
    {
      public:
        BackDataProcT(MasterServer &parent);

      private:
        MasterServer &parent;
        std::vector<uint8_t> recvData;
        void task() override;
        static constexpr const char TAG[] = "BackDataProcT";
    };

    class MainTask : public TaskClassS<1024>
    {
      public:
        MainTask(MasterServer &parent);

      private:
        MasterServer &parent;
        void task() override;
        static constexpr const char TAG[] = "MainTask";
    };

    // 数据传输任务实例
    std::unique_ptr<SlaveDataProcT> slaveDataProcessingTask;
    std::unique_ptr<BackDataProcT> backendDataProcessingTask;
    std::unique_ptr<MainTask> mainTask;

    // Utility methods
    uint32_t getCurrentTimestamp();

    // Core processing methods
    void processBackend2MasterMessage(const Message &message);
    void processSlave2MasterMessage(uint32_t slaveId, const Message &message);
    void processFrame(Frame &frame);

    // Message sending methods
    void sendResponseToBackend(std::unique_ptr<Message> response);
    void sendCommandToSlave(uint32_t slaveId, std::unique_ptr<Message> command);
    void sendCommandToSlaveWithRetry(uint32_t slaveId, std::unique_ptr<Message> command,

                                     uint8_t maxRetries = 3);

    // Command management
    void processPendingCommands();
    void removePendingCommand(uint32_t slaveId, uint8_t commandMessageId);
    void clearAllPendingCommands();
    void addPingSession(uint32_t targetId, uint8_t pingMode, uint16_t totalCount, uint16_t interval,
                        std::unique_ptr<Message> originalMessage = nullptr);
    void processPingSessions();

    // Configuration response tracking
    void addPendingBackendResponse(uint8_t messageType, std::unique_ptr<Message> originalMessage,
                                   const std::vector<uint32_t> &targetSlaves);
    void processPendingBackendResponses();
    void handleSlaveConfigResponse(uint32_t slaveId, uint8_t messageType, uint8_t status);

    // 数据采集管理
    void startSlaveDataCollection();
    void processTimeSync();

    // Device management
    DeviceManager &getDeviceManager()
    {
        return deviceManager;
    }
    ProtocolProcessor &getProcessor()
    {
        return processor;
    }

    // Calculate total conduction number for sync interval
    uint16_t calculateTotalConductionNum() const;

    // Build slave configurations for unified TDMA sync message
    void buildSlaveConfigsForSync(Master2Slave::SyncMessage &syncMsg, const DeviceManager &dm);

    // Register message handlers
    void registerMessageHandler(uint8_t messageId, std::unique_ptr<IMessageHandler> handler);

  private:
    // O(1) lookup, no heap allocation for Backend2Master
    IMessageHandler *messageHandlers_[256] = {};
    // O(1) lookup, no heap allocation for Slave2Master
    ISlave2MasterMessageHandler *slave2MasterHandlers_[256] = {};
    void initializeMessageHandlers();
    void initializeSlave2MasterHandlers();
};
