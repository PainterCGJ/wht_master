#include "B2M_MessageHandlers.h"

#include "MasterServer.h"
#include "elog.h"
#include "master_app.h"
#include "uwb_task.h"

// Slave Configuration Message Handler
std::unique_ptr<Message> SlaveConfigHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *configMsg = dynamic_cast<const Backend2Master::SlaveConfigMessage *>(&message);
    if (!configMsg)
        return nullptr;

    elog_v("SlaveConfigHandler", "Processing slave config message");

    auto response = std::make_unique<Master2Backend::SlaveConfigResponseMessage>();
    response->status = RESPONSE_STATUS_SUCCESS; // Success
    response->slaveNum = configMsg->slaveNum;

    // Copy slaves info
    for (const auto &slave : configMsg->slaves)
    {
        Master2Backend::SlaveConfigResponseMessage::SlaveInfo slaveInfo;
        slaveInfo.id = slave.id;
        slaveInfo.conductionNum = slave.conductionNum;
        slaveInfo.resistanceNum = slave.resistanceNum;
        slaveInfo.clipMode = slave.clipMode;
        slaveInfo.clipStatus = slave.clipStatus;
        response->slaves.push_back(slaveInfo);
    }

    return std::move(response);
}

void SlaveConfigHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *configMsg = dynamic_cast<const Backend2Master::SlaveConfigMessage *>(&message);
    if (!configMsg)
        return;

    // Clear existing configurations to ensure proper order
    server->getDeviceManager().clearSlaveConfigs();

    // Store slave configurations in device manager
    for (const auto &slave : configMsg->slaves)
    {
        server->getDeviceManager().addSlave(slave.id);
        server->getDeviceManager().setSlaveConfig(slave.id, slave);
        elog_v("SlaveConfigHandler",
               "Stored config for slave 0x%08X: Conduction=%d, Resistance=%d, "
               "ClipMode=%d",
               slave.id, static_cast<int>(slave.conductionNum), static_cast<int>(slave.resistanceNum),
               static_cast<int>(slave.clipMode));
    }

    elog_v("SlaveConfigHandler", "Configuration actions executed for %d slaves", static_cast<int>(configMsg->slaveNum));
}

// Mode Configuration Message Handler
std::unique_ptr<Message> ModeConfigHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *modeMsg = dynamic_cast<const Backend2Master::ModeConfigMessage *>(&message);
    if (!modeMsg)
        return nullptr;

    elog_v("ModeConfigHandler", "Processing mode config message - Mode: %d", static_cast<int>(modeMsg->mode));

    // Don't return response immediately - wait for slave responses
    // The response will be sent by the configuration tracking mechanism
    return nullptr;
}

void ModeConfigHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *modeMsg = dynamic_cast<const Backend2Master::ModeConfigMessage *>(&message);
    if (!modeMsg)
        return;

    // Set the mode in device manager
    server->getDeviceManager().setCurrentMode(modeMsg->mode);

    elog_i("ModeConfigHandler", "Mode set to %d - configuration will be distributed via TDMA sync messages",
           static_cast<int>(modeMsg->mode));

    // With TDMA unified sync messages, we no longer send individual config messages
    // The configuration will be distributed automatically through periodic sync messages

    // Send immediate success response since configuration is handled by sync messages
    auto response = std::make_unique<Master2Backend::ModeConfigResponseMessage>();
    response->status = RESPONSE_STATUS_SUCCESS; // Success
    response->mode = modeMsg->mode;
    server->sendResponseToBackend(std::move(response));

    elog_v("ModeConfigHandler", "Mode configuration completed - slaves will receive config via next sync message");
}

// Reset Message Handler
std::unique_ptr<Message> ResetHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *rstMsg = dynamic_cast<const Backend2Master::RstMessage *>(&message);
    if (!rstMsg)
        return nullptr;

    elog_v("ResetHandler", "Processing reset message - Slave count: %d", static_cast<int>(rstMsg->slaveNum));

    for (const auto &slave : rstMsg->slaves)
    {
        elog_v("ResetHandler", "  Reset Slave ID: 0x%08X, Lock: %d, Clip status: 0x%04X", slave.id,
               static_cast<int>(slave.lock), slave.clipStatus);
    }

    // Don't return response immediately - wait for slave responses
    // The response will be sent by the configuration tracking mechanism
    return nullptr;
}

void ResetHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *rstMsg = dynamic_cast<const Backend2Master::RstMessage *>(&message);
    if (!rstMsg)
        return;

    elog_i("ResetHandler", "Processing reset message for %d slaves", static_cast<int>(rstMsg->slaveNum));

    // Mark slaves for reset - they will be reset via the next sync message
    std::vector<uint32_t> targetSlaves;
    for (const auto &slave : rstMsg->slaves)
    {
        if (server->getDeviceManager().isSlaveConnected(slave.id))
        {
            server->getDeviceManager().markSlaveForReset(slave.id);
            targetSlaves.push_back(slave.id);
            elog_v("ResetHandler", "Marked slave 0x%08X for reset via sync message", slave.id);
        }
        else
        {
            elog_w("ResetHandler", "Slave 0x%08X is not connected, skipping reset", slave.id);
        }
    }

    if (targetSlaves.empty())
    {
        elog_w("ResetHandler", "No connected slaves found for reset, sending immediate success response");
        // If no connected slaves, send immediate success response
        auto response = std::make_unique<Master2Backend::RstResponseMessage>();
        response->status = RESPONSE_STATUS_SUCCESS; // Success
        response->slaveNum = rstMsg->slaveNum;

        // Copy slave reset info
        for (const auto &slave : rstMsg->slaves)
        {
            Master2Backend::RstResponseMessage::SlaveRstInfo slaveRstInfo;
            slaveRstInfo.id = slave.id;
            slaveRstInfo.lock = slave.lock;
            slaveRstInfo.clipStatus = slave.clipStatus;
            response->slaves.push_back(slaveRstInfo);
        }
        server->sendResponseToBackend(std::move(response));
        return;
    }

    // Create a copy of the original message for tracking
    auto originalMessageCopy = std::make_unique<Backend2Master::RstMessage>();
    originalMessageCopy->slaveNum = rstMsg->slaveNum;
    originalMessageCopy->slaves = rstMsg->slaves;

    // Start configuration tracking - response will be sent when all slaves respond with reset responses
    server->addPendingBackendResponse(static_cast<uint8_t>(Backend2MasterMessageId::SLAVE_RST_MSG),
                                      std::move(originalMessageCopy), targetSlaves);

    elog_i("ResetHandler", "Reset flags set for %d slaves, they will be reset via next sync message",
           static_cast<int>(targetSlaves.size()));
}

// Control Message Handler
std::unique_ptr<Message> ControlHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *controlMsg = dynamic_cast<const Backend2Master::CtrlMessage *>(&message);
    if (!controlMsg)
        return nullptr;

    elog_v("ControlHandler", "Processing control message - Running status: %d",
           static_cast<int>(controlMsg->runningStatus));

    auto response = std::make_unique<Master2Backend::CtrlResponseMessage>();
    response->status = RESPONSE_STATUS_SUCCESS; // Success
    response->runningStatus = controlMsg->runningStatus;

    return std::move(response);
}

void ControlHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *controlMsg = dynamic_cast<const Backend2Master::CtrlMessage *>(&message);
    if (!controlMsg)
        return;

    auto &deviceManager = server->getDeviceManager();

    // 保存当前运行状态
    deviceManager.setSystemRunningStatus(controlMsg->runningStatus);

    elog_v("ControlHandler", "Setting system running status to %d", static_cast<int>(controlMsg->runningStatus));

    // 根据运行状态执行操作
    switch (controlMsg->runningStatus)
    {
    case SYSTEM_STATUS_STOP: // 停止
        elog_v("ControlHandler", "Stopping all operations");

        // 清除所有待处理的命令，避免无意义的重试
        server->clearAllPendingCommands();

        // 停止所有数据采集
        deviceManager.resetDataCollection();

        elog_v("ControlHandler", "Data collection stopped, stop commands sent to slaves");
        break;

    case SYSTEM_STATUS_RUN: // 启动
        elog_v("ControlHandler", "Starting operations in mode %d", static_cast<int>(deviceManager.getCurrentMode()));

        // 根据当前模式启动相应操作
        if (deviceManager.getCurrentMode() <= MODE_CLIP)
        { // 导通、阻值、卡钉检测模式
            // 启动数据采集模式
            deviceManager.startDataCollection();

            // 通过TDMA同步消息向所有从机启动采集
            server->startSlaveDataCollection();

            elog_v("ControlHandler",
                   "Started data collection in mode %d, slave control "
                   "commands sent",
                   deviceManager.getCurrentMode());
        }
        else
        {
            elog_w("ControlHandler", "Unsupported mode: %d", static_cast<int>(deviceManager.getCurrentMode()));
        }
        break;

    case SYSTEM_STATUS_RESET: // 重置
        elog_v("ControlHandler", "Resetting all devices via TDMA sync messages");

        // 标记所有从机需要复位，复位将通过下一次同步消息发送
        for (uint32_t slaveId : deviceManager.getConnectedSlaves())
        {
            if (deviceManager.hasSlaveConfig(slaveId))
            {
                deviceManager.markSlaveForReset(slaveId);
                elog_v("ControlHandler", "Marked slave 0x%08X for reset", slaveId);
            }
        }

        // 重置设备管理器状态
        deviceManager.resetDataCollection();
        break;

    default:
        elog_w("ControlHandler", "Unknown running status: %d", static_cast<int>(controlMsg->runningStatus));
        break;
    }
}

// Ping Control Message Handler
std::unique_ptr<Message> PingControlHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *pingMsg = dynamic_cast<const Backend2Master::PingCtrlMessage *>(&message);
    if (!pingMsg)
        return nullptr;

    elog_v("PingControlHandler",
           "Processing ping control message - Mode: %d, Count: %d, Interval: "
           "%d, Target: 0x%08X",
           static_cast<int>(pingMsg->pingMode), pingMsg->pingCount, pingMsg->interval, pingMsg->destinationId);

    // Don't return response immediately - wait for ping session to complete
    // The response will be sent when the ping session finishes
    return nullptr;
}

void PingControlHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *pingMsg = dynamic_cast<const Backend2Master::PingCtrlMessage *>(&message);
    if (!pingMsg)
        return;

    // Create a copy of the original message for tracking
    auto originalMessageCopy = std::make_unique<Backend2Master::PingCtrlMessage>();
    originalMessageCopy->pingMode = pingMsg->pingMode;
    originalMessageCopy->pingCount = pingMsg->pingCount;
    originalMessageCopy->interval = pingMsg->interval;
    originalMessageCopy->destinationId = pingMsg->destinationId;

    // Add ping session to the server with original message
    server->addPingSession(pingMsg->destinationId, pingMsg->pingMode, pingMsg->pingCount, pingMsg->interval,
                           std::move(originalMessageCopy));

    elog_v("PingControlHandler",
           "Added ping session for target 0x%08X (mode=%d, count=%d, "
           "interval=%d), response will be sent when session completes",
           pingMsg->destinationId, static_cast<int>(pingMsg->pingMode), pingMsg->pingCount, pingMsg->interval);
}

// Device List Request Handler
std::unique_ptr<Message> DeviceListHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *deviceListMsg = dynamic_cast<const Backend2Master::DeviceListReqMessage *>(&message);
    if (!deviceListMsg)
        return nullptr;

    elog_v("DeviceListHandler", "Processing device list request");

    // 获取所有设备信息
    auto allDevices = server->getDeviceManager().getAllDeviceInfos();

    auto response = std::make_unique<Master2Backend::DeviceListResponseMessage>();
    response->deviceCount = static_cast<uint8_t>(allDevices.size());

    // 添加所有设备到响应中
    for (const auto &deviceInfo : allDevices)
    {
        Master2Backend::DeviceListResponseMessage::DeviceInfo responseDevice;
        responseDevice.deviceId = deviceInfo.deviceId;
        responseDevice.shortId = deviceInfo.shortId;
        responseDevice.online = deviceInfo.online;
        responseDevice.versionMajor = deviceInfo.versionMajor;
        responseDevice.versionMinor = deviceInfo.versionMinor;
        responseDevice.versionPatch = deviceInfo.versionPatch;
        response->devices.push_back(responseDevice);
    }

    elog_v("DeviceListHandler", "Returning %d devices (including offline)", response->deviceCount);

    return std::move(response);
}

void DeviceListHandler::executeActions(const Message &message, MasterServer *server)
{
    // No additional actions needed for device list request
    elog_d("DeviceListHandler", "Device list request processed");
}

std::unique_ptr<Message> IntervalConfigHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *intervalMsg = dynamic_cast<const Backend2Master::IntervalConfigMessage *>(&message);
    if (!intervalMsg)
        return nullptr;

    elog_v("IntervalConfigHandler", "Processing interval config message - Interval: %d", intervalMsg->intervalMs);

    auto response = std::make_unique<Master2Backend::IntervalConfigResponseMessage>();
    response->status = RESPONSE_STATUS_SUCCESS; // Success
    response->intervalMs = intervalMsg->intervalMs;

    return std::move(response);
}

void IntervalConfigHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *intervalMsg = dynamic_cast<const Backend2Master::IntervalConfigMessage *>(&message);
    if (!intervalMsg)
        return;

    server->getDeviceManager().setConfiguredInterval(intervalMsg->intervalMs);

    elog_v("IntervalConfigHandler", "Configured interval set to %d", intervalMsg->intervalMs);
}

// Clear Device List Handler
std::unique_ptr<Message> ClearDeviceListHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *clearMsg = dynamic_cast<const Backend2Master::ClearDeviceListMessage *>(&message);
    if (!clearMsg)
        return nullptr;

    elog_i("ClearDeviceListHandler", "Processing clear device list request");

    // 此消息不需要回复，后端可通过设备列表查询来确认是否已清空
    return nullptr;
}

void ClearDeviceListHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *clearMsg = dynamic_cast<const Backend2Master::ClearDeviceListMessage *>(&message);
    if (!clearMsg)
        return;

    // 清除所有设备信息
    server->getDeviceManager().clearAllDevices();

    elog_i("ClearDeviceListHandler", "All device information cleared successfully");
}

// Set UWB Channel Handler
std::unique_ptr<Message> SetUwbChannelHandler::processMessage(const Message &message, MasterServer *server)
{
    const auto *channelMsg = dynamic_cast<const Backend2Master::SetUwbChannelMessage *>(&message);
    if (!channelMsg)
        return nullptr;

    elog_i("SetUwbChannelHandler", "Processing set UWB channel request - Channel: %d",
           static_cast<int>(channelMsg->channel));

    // 创建响应消息
    auto response = std::make_unique<Master2Backend::SetUwbChannelResponseMessage>();
    response->channel = channelMsg->channel;

    // 尝试设置UWB信道
    int result = UWB_SetChannel(channelMsg->channel);

    if (result == 0)
    {
        response->status = 0; // 成功
        elog_i("SetUwbChannelHandler", "UWB channel set to %d successfully", static_cast<int>(channelMsg->channel));
    }
    else
    {
        response->status = 1; // 失败
        elog_e("SetUwbChannelHandler", "Failed to set UWB channel to %d, error: %d",
               static_cast<int>(channelMsg->channel), result);
    }

    return std::move(response);
}

void SetUwbChannelHandler::executeActions(const Message &message, MasterServer *server)
{
    const auto *channelMsg = dynamic_cast<const Backend2Master::SetUwbChannelMessage *>(&message);
    if (!channelMsg)
        return;

    elog_d("SetUwbChannelHandler", "UWB channel setting action completed for channel %d",
           static_cast<int>(channelMsg->channel));
}