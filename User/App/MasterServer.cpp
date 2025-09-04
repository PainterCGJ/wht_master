#include "MasterServer.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "MutexCPP.h"
#include "elog.h"
#include "hptimer.hpp"
#include "udp_task.h"
#include "uwb_task.h"

// MasterServer 构造函数实现
MasterServer::MasterServer()
    : pendingCommandsMutex("PendingCommandsMutex"), lastSyncTime(0), initialTimeSyncCompleted(false)
{
    initializeMessageHandlers();
    initializeSlave2MasterHandlers();

    processor.setMTU(FRAME_LEN_MAX);

    slaveDataProcessingTask = std::make_unique<SlaveDataProcT>(*this);
    backendDataProcessingTask = std::make_unique<BackDataProcT>(*this);
    mainTask = std::make_unique<MainTask>(*this);
}

// MasterServer 析构函数实现
MasterServer::~MasterServer()
{
    elog_d(TAG, "MasterServer destroyed");
}

void MasterServer::initializeMessageHandlers()
{
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::SLAVE_CFG_MSG)] = &SlaveConfigHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::MODE_CFG_MSG)] = &ModeConfigHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::SLAVE_RST_MSG)] = &ResetHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::CTRL_MSG)] = &ControlHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::PING_CTRL_MSG)] = &PingControlHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::DEVICE_LIST_REQ_MSG)] =
        &DeviceListHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::INTERVAL_CFG_MSG)] =
        &IntervalConfigHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::CLEAR_DEVICE_LIST_MSG)] =
        &ClearDeviceListHandler::getInstance();
    messageHandlers_[static_cast<uint8_t>(Backend2MasterMessageId::SET_UWB_CHAN_MSG)] =
        &SetUwbChannelHandler::getInstance();
}

void MasterServer::initializeSlave2MasterHandlers()
{
    slave2MasterHandlers_[static_cast<uint8_t>(Slave2MasterMessageId::ANNOUNCE_MSG)] = &AnnounceHandler::getInstance();
    slave2MasterHandlers_[static_cast<uint8_t>(Slave2MasterMessageId::SHORT_ID_CONFIRM_MSG)] =
        &ShortIdConfirmHandler::getInstance();
    slave2MasterHandlers_[static_cast<uint8_t>(Slave2MasterMessageId::RST_RSP_MSG)] =
        &ResetResponseHandler::getInstance();
    slave2MasterHandlers_[static_cast<uint8_t>(Slave2MasterMessageId::PING_RSP_MSG)] =
        &PingResponseHandler::getInstance();
}

uint32_t MasterServer::getCurrentTimestamp()
{
    return hal_hptimer_get_ms();
}

uint16_t MasterServer::calculateTotalConductionNum() const
{
    uint16_t totalConductionNum = 0;
    auto connectedSlaves = deviceManager.getConnectedSlavesInConfigOrder();

    for (uint32_t slaveId : connectedSlaves)
    {
        if (deviceManager.hasSlaveConfig(slaveId))
        {
            const auto &slaveConfig = deviceManager.getSlaveConfig(slaveId);
            totalConductionNum += slaveConfig.conductionNum;
        }
    }

    return totalConductionNum;
}

void MasterServer::sendResponseToBackend(std::unique_ptr<Message> response)
{
    if (!response)
    {
        elog_w(TAG, "sendResponseToBackend called with null response");
        return;
    }

    elog_i(TAG, "Sending Master2Backend response: %s", response->getMessageTypeName());
    elog_v(TAG, "Starting message serialization...");

    auto responseData = processor.packMaster2BackendMessage(*response);
    elog_v(TAG, "Message serialization completed, %d fragments to send", static_cast<int>(responseData.size()));

    bool sendSuccess = true;
    int fragmentIndex = 0;
    for (auto &fragment : responseData)
    {
        elog_v(TAG, "Sending fragment %d/%d (%d bytes)", fragmentIndex + 1, static_cast<int>(responseData.size()),
               static_cast<int>(fragment.size()));

        if (!sendToBackend(fragment))
        {
            elog_e(TAG, "Failed to send response fragment %d/%d", fragmentIndex + 1,
                   static_cast<int>(responseData.size()));
            sendSuccess = false;
            break; // Stop sending remaining fragments on failure
        }
        else
        {
            elog_v(TAG, "Fragment %d/%d sent successfully", fragmentIndex + 1, static_cast<int>(responseData.size()));
        }
        fragmentIndex++;
    }

    if (sendSuccess)
    {
        elog_v(TAG, "Master2Backend response sent to backend successfully");
    }
    else
    {
        elog_e(TAG, "Master2Backend response sending failed");
    }
}

void MasterServer::sendCommandToSlave(uint32_t slaveId, std::unique_ptr<Message> command)
{
    if (!command)
        return;

    auto commandData = processor.packMaster2SlaveMessage(slaveId, *command);

    elog_i(TAG, "Sending Master2Slave command to 0x%08X: %s", slaveId, command->getMessageTypeName());

    bool sendSuccess = true;
    for (auto &fragment : commandData)
    {
        if (!sendToSlave(fragment))
        {
            elog_e(TAG, "Failed to send command fragment");
            sendSuccess = false;
            break; // 如果发送失败，不再尝试发送后续片段
        }
    }

    // 如果发送失败，返回失败状态
    if (!sendSuccess)
    {
        elog_e(TAG, "Command send failed, aborting");
        return;
    }

    elog_v(TAG, "Master2Slave command broadcasted to slaves");
}

void MasterServer::sendCommandToSlaveWithRetry(uint32_t slaveId, std::unique_ptr<Message> command,

                                               uint8_t maxRetries)
{
    // Create a pending command for retry management
    PendingCommand pendingCmd(slaveId, std::move(command), maxRetries);
    pendingCmd.timestamp = getCurrentTimestamp();

    // Send the command immediately - create a copy by serializing and
    // deserializing
    auto serialized = pendingCmd.command->serialize();
    auto messageCopy = processor.createMessage(PacketId::MASTER_TO_SLAVE, pendingCmd.command->getMessageId());
    if (messageCopy && messageCopy->deserialize(serialized))
    {
        sendCommandToSlave(slaveId, std::move(messageCopy));
    }

    // Add to pending commands list for retry management (with mutex protection)
    {
        Lock lock(pendingCommandsMutex);
        pendingCommands.push_back(std::move(pendingCmd));
    }

    elog_v(TAG, "Command sent to slave 0x%08X with retry support (max retries: %d)", slaveId, maxRetries);
}

void MasterServer::processPendingCommands()
{
    // Lock the mutex to prevent race conditions with removePendingCommand
    Lock lock(pendingCommandsMutex);

    uint32_t currentTime = getCurrentTimestampMs();
    constexpr uint32_t BASE_RETRY_TIMEOUT = BASE_RETRY_TIMEOUT_MS; // 基础重试超时时间

    auto it = pendingCommands.begin();
    while (it != pendingCommands.end())
    {
        // 使用指数退避算法计算重试超时时间
        uint32_t retryTimeout = BASE_RETRY_TIMEOUT * (1 << it->retryCount); // 2^retryCount
        constexpr uint32_t MAX_RETRY_TIMEOUT = MAX_RETRY_TIMEOUT_MS;
        retryTimeout = std::min(retryTimeout, MAX_RETRY_TIMEOUT);

        if (currentTime - it->timestamp > retryTimeout)
        {
            if (it->retryCount < it->maxRetries)
            {
                // Retry the command
                it->retryCount++;
                it->timestamp = currentTime;

                // Create a copy of the command by serializing and deserializing
                auto serialized = it->command->serialize();
                auto messageCopy = processor.createMessage(PacketId::MASTER_TO_SLAVE, it->command->getMessageId());
                if (messageCopy && messageCopy->deserialize(serialized))
                {
                    bool sendSuccess = false;
                    // 尝试发送命令，如果UWB连续失败会返回false
                    auto commandData = processor.packMaster2SlaveMessage(it->slaveId, *messageCopy);
                    elog_v(TAG, "Retrying command to slave 0x%08X (attempt %d/%d)", it->slaveId, it->retryCount,
                           it->maxRetries);

                    sendSuccess = true;
                    for (auto &fragment : commandData)
                    {
                        if (!sendToSlave(fragment))
                        {
                            elog_e(TAG, "Failed to send command fragment during retry");
                            sendSuccess = false;
                            break;
                        }
                    }

                    if (sendSuccess)
                    {
                        elog_v(TAG, "Command retry successful for slave 0x%08X", it->slaveId);
                    }

                    // 如果发送失败且已达到最大重试次数，直接移除命令
                    if (!sendSuccess && it->retryCount >= it->maxRetries)
                    {
                        elog_w(TAG,
                               "Command to slave 0x%08X failed after %d "
                               "retries due to UWB errors",
                               it->slaveId, it->maxRetries);

                        it = pendingCommands.erase(it);
                        continue;
                    }
                }
                else
                {
                    elog_e(TAG,
                           "Failed to create command copy for retry - "
                           "messageId: 0x%02X, slaveId: 0x%08X",
                           it->command->getMessageId(), it->slaveId);
                }
                ++it;
            }
            else
            {
                // Max retries reached, remove from pending list
                elog_w(TAG, "Command to slave 0x%08X failed after %d retries", it->slaveId, it->maxRetries);

                // 命令重试失败，移除待处理命令

                it = pendingCommands.erase(it);
            }
        }
        else
        {
            ++it;
        }
    }
}

void MasterServer::removePendingCommand(uint32_t slaveId, uint8_t commandMessageId)
{
    // Lock the mutex to prevent race conditions with processPendingCommands
    Lock lock(pendingCommandsMutex);

    auto it = pendingCommands.begin();
    while (it != pendingCommands.end())
    {
        if (it->slaveId == slaveId && it->command->getMessageId() == commandMessageId)
        {
            elog_v(TAG, "Removing pending command for slave 0x%08X (msgId=0x%02X)", slaveId, commandMessageId);
            it = pendingCommands.erase(it);
            break; // 只移除第一个匹配的命令
        }
        else
        {
            ++it;
        }
    }
}

void MasterServer::clearAllPendingCommands()
{
    // Lock the mutex to prevent race conditions
    Lock lock(pendingCommandsMutex);

    if (!pendingCommands.empty())
    {
        elog_v(TAG, "Clearing %d pending commands", pendingCommands.size());
        pendingCommands.clear();
    }
}

// Configuration response tracking methods
void MasterServer::addPendingBackendResponse(uint8_t messageType, std::unique_ptr<Message> originalMessage,
                                             const std::vector<uint32_t> &targetSlaves)
{
    if (targetSlaves.empty())
    {
        elog_w(TAG, "No target slaves for backend response tracking");
        return;
    }

    PendingBackendResponse pendingResponse(messageType, std::move(originalMessage), targetSlaves);
    pendingResponse.timestamp = getCurrentTimestampMs();

    pendingBackendResponses.push_back(std::move(pendingResponse));

    elog_v(TAG,
           "Added pending backend response tracking for message type 0x%02X, "
           "%d slaves",
           messageType, static_cast<int>(targetSlaves.size()));
}

void MasterServer::processPendingBackendResponses()
{
    static bool processing = false;
    static uint32_t lastProcessTime = 0;

    // Prevent re-entry
    if (processing)
    {
        elog_v(TAG, "processPendingBackendResponses already in progress, skipping");
        return;
    }

    processing = true;

    uint32_t currentTime = getCurrentTimestampMs();

    // Add safety check to prevent getting stuck in this method
    const uint32_t MAX_PROCESS_TIME = MAX_BACKEND_PROCESS_TIME_MS; // 最大处理时间
    if (lastProcessTime > 0 && (currentTime - lastProcessTime) > MAX_PROCESS_TIME)
    {
        elog_w(TAG, "processPendingBackendResponses taking too long, forcing exit");
        processing = false;
        return;
    }
    lastProcessTime = currentTime;

    // if (!pendingBackendResponses.empty()) {
    //     elog_v(TAG, "Processing %d pending backend responses",
    //            static_cast<int>(pendingBackendResponses.size()));
    // }

    // Add protection against infinite loops
    const int MAX_ITERATIONS = MAX_BACKEND_PROCESS_ITERATIONS;
    int iterationCount = 0;

    auto it = pendingBackendResponses.begin();
    while (it != pendingBackendResponses.end() && iterationCount < MAX_ITERATIONS)
    {
        iterationCount++;
        // elog_v(TAG, "Checking pending response: messageType=0x%02X,
        // isComplete=%s, pendingSlaves=%d (iteration %d)",
        //        it->messageType, it->isComplete() ? "true" : "false",
        //        static_cast<int>(it->pendingSlaves.size()), iterationCount);

        if (it->isComplete())
        {
            elog_i(TAG,
                   "All slaves responded for message type 0x%02X, preparing "
                   "response",
                   it->messageType);

            // All slaves have responded, send response to backend
            std::unique_ptr<Message> response = nullptr;

            switch (it->messageType)
            {
            case static_cast<uint8_t>(Backend2MasterMessageId::MODE_CFG_MSG): {
                elog_v(TAG, "Processing MODE_CFG_MSG completion");
                elog_v(TAG, "Attempting to cast original message to "
                            "ModeConfigMessage...");

                const auto *originalMsg =
                    dynamic_cast<const Backend2Master::ModeConfigMessage *>(it->originalMessage.get());
                elog_v(TAG, "Dynamic cast completed, originalMsg = %p", originalMsg);

                if (originalMsg)
                {
                    elog_v(TAG, "Original message cast successful, creating "
                                "response...");

                    auto modeResponse = std::make_unique<Master2Backend::ModeConfigResponseMessage>();
                    elog_v(TAG, "Response object created, setting status and "
                                "mode...");

                    modeResponse->status = it->getOverallStatus();
                    elog_v(TAG, "Status set to %d", modeResponse->status);

                    modeResponse->mode = originalMsg->mode;
                    elog_v(TAG, "Mode set to %d", modeResponse->mode);

                    response = std::move(modeResponse);
                    elog_v(TAG, "Response moved to response variable");

                    elog_i(TAG,
                           "Mode configuration completed for all slaves, "
                           "status: %s",
                           response->getMessageTypeName());
                }
                else
                {
                    elog_e(TAG, "Failed to cast original message to "
                                "ModeConfigMessage");
                }
                break;
            }
            case static_cast<uint8_t>(Backend2MasterMessageId::SLAVE_RST_MSG): {
                elog_v(TAG, "Processing SLAVE_RST_MSG completion");
                elog_v(TAG, "Attempting to cast original message to RstMessage...");

                const auto *originalMsg = dynamic_cast<const Backend2Master::RstMessage *>(it->originalMessage.get());
                elog_v(TAG, "Dynamic cast completed, originalMsg = %p", originalMsg);

                if (originalMsg)
                {
                    elog_v(TAG, "Original message cast successful, creating "
                                "response...");

                    auto resetResponse = std::make_unique<Master2Backend::RstResponseMessage>();
                    elog_v(TAG, "Response object created, setting status and "
                                "slave info...");

                    resetResponse->status = it->getOverallStatus();
                    elog_v(TAG, "Overall status set to %d", resetResponse->status);

                    resetResponse->slaveNum = originalMsg->slaveNum;
                    elog_v(TAG, "Slave number set to %d", resetResponse->slaveNum);

                    // Copy slave reset info with actual response status
                    // from slaves
                    elog_v(TAG, "Processing %d slaves in original message",
                           static_cast<int>(originalMsg->slaves.size()));
                    for (const auto &slave : originalMsg->slaves)
                    {
                        Master2Backend::RstResponseMessage::SlaveRstInfo slaveRstInfo;
                        slaveRstInfo.id = slave.id;
                        slaveRstInfo.lock = slave.lock;
                        slaveRstInfo.clipStatus = slave.clipStatus;

                        // Check if this slave actually responded
                        auto statusIt = it->slaveStatuses.find(slave.id);
                        if (statusIt != it->slaveStatuses.end())
                        {
                            // Slave responded, use actual status
                            elog_v(TAG, "Slave 0x%08X responded with status %d", slave.id, statusIt->second);
                            // Note: The slave's individual status is
                            // already included in the overall status
                            // calculation The actual reset response from
                            // slave contains the real status
                        }
                        else
                        {
                            // Slave didn't respond, mark as failed
                            elog_w(TAG,
                                   "Slave 0x%08X did not respond, marking "
                                   "as failed",
                                   slave.id);
                            // For slaves that didn't respond, we keep the
                            // original info but the overall status will be
                            // error
                        }

                        resetResponse->slaves.push_back(slaveRstInfo);
                        elog_v(TAG,
                               "Added slave info for 0x%08X (lock=%d, "
                               "clipStatus=0x%04X)",
                               slave.id, slave.lock, slave.clipStatus);
                    }

                    response = std::move(resetResponse);
                    elog_v(TAG, "Response moved to response variable");

                    elog_i(TAG,
                           "Reset configuration completed for all slaves, "
                           "status: %s",
                           response->getMessageTypeName());
                }
                else
                {
                    elog_e(TAG, "Failed to cast original message to RstMessage");
                }
                break;
            }
            // Add other message types as needed
            default:
                elog_w(TAG, "Unknown message type 0x%02X in pending response", it->messageType);
                break;
            }

            if (response)
            {
                elog_v(TAG, "Sending response to backend for message type 0x%02X", it->messageType);
                sendResponseToBackend(std::move(response));
                elog_v(TAG, "Response sent successfully");
            }
            else
            {
                elog_e(TAG, "Failed to create response for message type 0x%02X", it->messageType);
            }

            elog_v(TAG, "Removing completed pending response");
            it = pendingBackendResponses.erase(it);
            elog_v(TAG, "Pending response removed, %d remaining", static_cast<int>(pendingBackendResponses.size()));
        }
        else if (it->isTimedOut(currentTime))
        {
            elog_w(TAG,
                   "Backend response timeout for message type 0x%02X, %d "
                   "slaves still pending",
                   it->messageType, static_cast<int>(it->pendingSlaves.size()));

            // Timeout, send error response
            std::unique_ptr<Message> response = nullptr;

            switch (it->messageType)
            {
            case static_cast<uint8_t>(Backend2MasterMessageId::MODE_CFG_MSG): {
                const auto *originalMsg =
                    dynamic_cast<const Backend2Master::ModeConfigMessage *>(it->originalMessage.get());
                if (originalMsg)
                {
                    auto modeResponse = std::make_unique<Master2Backend::ModeConfigResponseMessage>();
                    modeResponse->status = 1; // Error due to timeout
                    modeResponse->mode = originalMsg->mode;
                    response = std::move(modeResponse);
                }
                break;
            }
            case static_cast<uint8_t>(Backend2MasterMessageId::SLAVE_RST_MSG): {
                elog_v(TAG, "Processing SLAVE_RST_MSG timeout");
                const auto *originalMsg = dynamic_cast<const Backend2Master::RstMessage *>(it->originalMessage.get());
                if (originalMsg)
                {
                    elog_v(TAG, "Creating timeout response for reset command");
                    auto resetResponse = std::make_unique<Master2Backend::RstResponseMessage>();
                    resetResponse->status = 1; // Error due to timeout
                    resetResponse->slaveNum = originalMsg->slaveNum;

                    // Copy slave reset info with timeout status
                    elog_v(TAG, "Processing %d slaves for timeout response",
                           static_cast<int>(originalMsg->slaves.size()));
                    for (const auto &slave : originalMsg->slaves)
                    {
                        Master2Backend::RstResponseMessage::SlaveRstInfo slaveRstInfo;
                        slaveRstInfo.id = slave.id;
                        slaveRstInfo.lock = slave.lock;
                        slaveRstInfo.clipStatus = slave.clipStatus;

                        // Check if this slave responded before timeout
                        auto statusIt = it->slaveStatuses.find(slave.id);
                        if (statusIt != it->slaveStatuses.end())
                        {
                            elog_v(TAG,
                                   "Slave 0x%08X responded before timeout "
                                   "with status %d",
                                   slave.id, statusIt->second);
                        }
                        else
                        {
                            elog_w(TAG, "Slave 0x%08X did not respond (timeout)", slave.id);
                        }

                        resetResponse->slaves.push_back(slaveRstInfo);
                    }
                    response = std::move(resetResponse);
                    elog_v(TAG, "Timeout response created successfully");
                }
                else
                {
                    elog_e(TAG, "Failed to cast original message for timeout "
                                "response");
                }
                break;
            }
            }

            if (response)
            {
                sendResponseToBackend(std::move(response));
            }

            it = pendingBackendResponses.erase(it);
        }
        else
        {
            ++it;
        }
    }

    if (iterationCount >= MAX_ITERATIONS)
    {
        elog_w(TAG,
               "processPendingBackendResponses reached maximum iterations "
               "(%d), exiting to prevent infinite loop",
               MAX_ITERATIONS);
    }

    // Reset the processing time when we successfully complete
    lastProcessTime = 0;
    processing = false;
}

void MasterServer::handleSlaveConfigResponse(uint32_t slaveId, uint8_t messageType, uint8_t status)
{
    // Find the corresponding pending backend response
    for (auto &pendingResponse : pendingBackendResponses)
    {
        // Check if this slave response matches any pending backend response
        bool isMatch = false;

        switch (pendingResponse.messageType)
        {
        case static_cast<uint8_t>(Backend2MasterMessageId::MODE_CFG_MSG):
            // Mode config is now handled via TDMA sync messages - no specific response expected
            // This case is kept for compatibility but should not receive responses anymore
            break;
        case static_cast<uint8_t>(Backend2MasterMessageId::SLAVE_RST_MSG):
            if (messageType == static_cast<uint8_t>(Slave2MasterMessageId::RST_RSP_MSG))
            {
                isMatch = true;
            }
            break;
        }

        if (isMatch && pendingResponse.pendingSlaves.count(slaveId) > 0)
        {
            pendingResponse.markSlaveResponse(slaveId, status);
            elog_v(TAG,
                   "Marked slave 0x%08X response for backend message type "
                   "0x%02X, status: %d, %d slaves remaining",
                   slaveId, pendingResponse.messageType, status,
                   static_cast<int>(pendingResponse.pendingSlaves.size()));
            break;
        }
    }
}

void MasterServer::addPingSession(uint32_t targetId, uint8_t pingMode, uint16_t totalCount, uint16_t interval,
                                  std::unique_ptr<Message> originalMessage)
{
    // Create a new ping session
    PingSession session(targetId, pingMode, totalCount, interval, std::move(originalMessage));
    session.lastPingTime = getCurrentTimestampMs();

    activePingSessions.push_back(std::move(session));

    elog_v(TAG,
           "Added ping session for target 0x%08X (mode=%d, count=%d, "
           "interval=%dms)",
           targetId, pingMode, totalCount, interval);
}

void MasterServer::processPingSessions()
{
    uint32_t currentTime = getCurrentTimestampMs();

    auto it = activePingSessions.begin();
    while (it != activePingSessions.end())
    {
        if (currentTime - it->lastPingTime >= it->interval)
        {
            if (it->currentCount < it->totalCount)
            {
                // Send ping command
                auto pingCmd = std::make_unique<Master2Slave::PingReqMessage>();
                pingCmd->sequenceNumber = it->currentCount + 1;
                pingCmd->timestamp = currentTime;

                sendCommandToSlave(it->targetId, std::move(pingCmd));

                it->currentCount++;
                it->lastPingTime = currentTime;

                elog_v(TAG, "Sent ping %d/%d to target 0x%08X", it->currentCount, it->totalCount, it->targetId);
                ++it;
            }
            else
            {
                // Ping session completed
                elog_i(TAG,
                       "Ping session completed for target 0x%08X (%d/%d "
                       "successful)",
                       it->targetId, it->successCount, it->totalCount);

                // Send response to backend if we have the original message
                if (it->originalMessage)
                {
                    const auto *originalPingMsg =
                        dynamic_cast<const Backend2Master::PingCtrlMessage *>(it->originalMessage.get());
                    if (originalPingMsg)
                    {
                        auto response = std::make_unique<Master2Backend::PingResponseMessage>();
                        response->pingMode = it->pingMode;
                        response->totalCount = it->totalCount;
                        response->successCount = it->successCount; // Use actual success count
                        response->destinationId = it->targetId;

                        sendResponseToBackend(std::move(response));
                        elog_i(TAG,
                               "Sent ping response to backend for target "
                               "0x%08X (%d/%d successful)",
                               it->targetId, it->successCount, it->totalCount);
                    }
                }

                it = activePingSessions.erase(it);
            }
        }
        else
        {
            ++it;
        }
    }
}

void MasterServer::processBackend2MasterMessage(const Message &message)
{
    elog_i(TAG, "Received Backend2Master message: %s", message.getMessageTypeName());

    uint8_t messageId = message.getMessageId();
    elog_v(TAG, "Processing Backend2Master message, ID: 0x%02X", static_cast<int>(messageId));

    IMessageHandler *handler = messageHandlers_[messageId];
    if (handler)
    {
        // Process message and generate response
        auto response = handler->processMessage(message, this);

        // Execute associated actions
        handler->executeActions(message, this);

        // Send response if generated
        if (response)
        {
            sendResponseToBackend(std::move(response));
        }
        else
        {
            elog_v(TAG, "No response needed for this Backend2Master message");
        }
    }
    else
    {
        elog_w(TAG, "Unknown Backend2Master message type: 0x%02X", static_cast<int>(messageId));
    }
}

void MasterServer::processSlave2MasterMessage(uint32_t slaveId, const Message &message)
{
    elog_i(TAG, "Received Slave2Master message from slave 0x%08X: %s", slaveId, message.getMessageTypeName());

    uint8_t messageId = message.getMessageId();
    elog_v(TAG, "Processing Slave2Master message from slave 0x%08X, ID: 0x%02X", slaveId, static_cast<int>(messageId));

    ISlave2MasterMessageHandler *handler = slave2MasterHandlers_[messageId];
    if (handler)
    {
        // Process message and generate response
        auto response = handler->processMessage(slaveId, message, this);

        // Execute associated actions
        handler->executeActions(slaveId, message, this);

        // Send response if generated (currently no Slave2Master messages
        // generate responses)
        if (response)
        {
            // If in the future we need to send responses back to slaves
            // sendCommandToSlave(slaveId, std::move(response));
            elog_v(TAG, "Response generated for Slave2Master message");
        }
        else
        {
            elog_v(TAG, "No response needed for this Slave2Master message");
        }
    }
    else
    {
        elog_w(TAG, "Unknown Slave2Master message type: 0x%02X", static_cast<int>(messageId));
    }
}

void MasterServer::processFrame(Frame &frame)
{
    elog_v(TAG, "Processing frame - PacketId: 0x%02X, payload size: %d", static_cast<int>(frame.packetId),
           frame.payload.size());

    if (frame.packetId == static_cast<uint8_t>(PacketId::BACKEND_TO_MASTER))
    {
        std::unique_ptr<Message> backendMessage;
        if (processor.parseBackend2MasterPacket(frame.payload, backendMessage))
        {
            processBackend2MasterMessage(*backendMessage);
        }
        else
        {
            elog_e(TAG, "Failed to parse Backend2Master packet");
        }
    }
    else if (frame.packetId == static_cast<uint8_t>(PacketId::SLAVE_TO_MASTER))
    {
        uint32_t slaveId;
        std::unique_ptr<Message> slaveMessage;
        if (processor.parseSlave2MasterPacket(frame.payload, slaveId, slaveMessage))
        {
            processSlave2MasterMessage(slaveId, *slaveMessage);
        }
        else
        {
            elog_e(TAG, "Failed to parse Slave2Master packet");
        }
    }
    else if (frame.packetId == static_cast<uint8_t>(PacketId::SLAVE_TO_BACKEND))
    {
        // SLAVE_TO_BACKEND帧现在在SlaveDataProcT中直接透传，这里不再处理
        elog_v(TAG, "SLAVE_TO_BACKEND frame ignored in processFrame (handled in "
                    "SlaveDataProcT)");
    }
    else
    {
        elog_w(TAG, "Unsupported packet type for Master: 0x%02X", static_cast<int>(frame.packetId));
    }
}

// 数据采集管理
void MasterServer::startSlaveDataCollection()
{
    // Enable TDMA sync message broadcasting by marking initial time sync as
    // completed In the new unified design, time synchronization is handled by
    // TDMA sync messages
    if (!initialTimeSyncCompleted)
    {
        initialTimeSyncCompleted = true;
        elog_i(TAG, "Enabled TDMA sync message broadcasting - time sync and control "
                    "will be handled automatically");
    }
}

void MasterServer::processTimeSync()
{
    DeviceManager &dm = getDeviceManager();

    // 只有在系统运行时且已完成初始时间同步后才发送定时同步消息
    if (dm.getSystemRunningStatus() != SYSTEM_STATUS_RUN || !initialTimeSyncCompleted)
    {
        return;
    }

    uint32_t currentTime = getCurrentTimestampMs();

    // 计算TDMA周期长度: 延迟启动时间 + 总时隙数量 × interval + 额外延迟
    uint32_t startupDelayMs = TDMA_STARTUP_DELAY_MS;
    uint32_t extraDelayMs = TDMA_EXTRA_DELAY_MS;

    // totalTimeSlots = totalConductionNum * CONDUCTION_INTERVAL
    uint32_t totalConductionNum = calculateTotalConductionNum();

    // 获取有效的采集间隔
    uint32_t intervalMs = static_cast<uint32_t>(dm.getEffectiveInterval());

    // 计算完整的TDMA周期时间D
    uint32_t tdmaCycleMs = startupDelayMs + (totalConductionNum * intervalMs) + extraDelayMs;

    // 最小周期保护，避免过于频繁的同步
    if (tdmaCycleMs < TDMA_MIN_CYCLE_MS)
    {
        tdmaCycleMs = TDMA_MIN_CYCLE_MS;
    }

    // 检查是否需要发送TDMA同步消息
    if (currentTime - lastSyncTime >= tdmaCycleMs)
    {
        // 创建统一的TDMA同步消息
        auto syncCmd = std::make_unique<Master2Slave::SyncMessage>();

        // 设置基本字段
        syncCmd->mode = dm.getCurrentMode();
        syncCmd->interval = dm.getEffectiveInterval();

        // 当前时间和启动时间（微秒）
        uint64_t timestampUs = hal_hptimer_get_us();
        syncCmd->currentTime = timestampUs;

        // 启动时间设置为当前时间加上启动延迟时间
        syncCmd->startTime = timestampUs + (startupDelayMs * 1000);

        // 构建从机配置列表
        buildSlaveConfigsForSync(*syncCmd, dm);

        // 广播发送统一同步消息（使用广播地址）
        sendCommandToSlave(BROADCAST_SLAVE_ID, std::move(syncCmd));

        lastSyncTime = currentTime;

        elog_v(TAG,
               "Broadcasted TDMA sync message (mode=%d, interval=%d ms, "
               "current_time=%lu us, start_time=%lu us, slaves=%d, cycle=%lu ms)",
               dm.getCurrentMode(), dm.getEffectiveInterval(), (unsigned long)timestampUs,
               (unsigned long)(timestampUs + startupDelayMs * 1000), static_cast<int>(totalTimeSlots),
               (unsigned long)tdmaCycleMs);
    }
}

void MasterServer::buildSlaveConfigsForSync(Master2Slave::SyncMessage &syncMsg, const DeviceManager &dm)
{
    syncMsg.slaveConfigs.clear();

    // 获取按配置顺序排列的已连接从机
    auto connectedSlaves = dm.getConnectedSlavesInConfigOrder();

    uint8_t timeSlot = 0; // 时隙从0开始分配

    for (uint32_t slaveId : connectedSlaves)
    {
        if (!dm.hasSlaveConfig(slaveId))
        {
            elog_w(TAG, "Slave 0x%08X connected but no config found, skipping", slaveId);
            continue;
        }

        auto slaveConfig = dm.getSlaveConfig(slaveId);
        Master2Slave::SyncMessage::SlaveConfig config;

        config.id = slaveId;
        config.timeSlot = timeSlot++; // 按顺序分配时隙，从0开始

        // 检查是否需要复位该从机
        config.reset = dm.isSlaveMarkedForReset(slaveId) ? 1 : 0;
        if (config.reset == 1)
        {
            elog_v(TAG, "Slave 0x%08X marked for reset in sync message", slaveId);
        }

        // 根据当前模式设置测试数量
        switch (dm.getCurrentMode())
        {
        case MODE_CONDUCTION: // 导通检测
            config.testCount = slaveConfig.conductionNum;
            break;
        case MODE_RESISTANCE: // 阻值检测
            config.testCount = slaveConfig.resistanceNum;
            break;
        case MODE_CLIP:                              // 卡钉检测
            config.testCount = slaveConfig.clipMode; // 使用clipMode作为卡钉数量
            break;
        default:
            config.testCount = 0;
            elog_w(TAG, "Unknown mode %d for slave 0x%08X", dm.getCurrentMode(), slaveId);
            break;
        }

        syncMsg.slaveConfigs.push_back(config);

        elog_v(TAG, "Added slave 0x%08X to sync: timeSlot=%d, reset=%d, testCount=%d (mode=%d)", slaveId,
               config.timeSlot, config.reset, config.testCount, dm.getCurrentMode());
    }

    elog_v(TAG, "Built sync message with %d slave configurations", static_cast<int>(syncMsg.slaveConfigs.size()));
}

bool MasterServer::sendToBackend(std::vector<uint8_t> &frame)
{
    // 数据通过UDP_SendData发送
    if (UDP_SendData(frame.data(), frame.size(), DEFAULT_BACKEND_IP, DEFAULT_BACKEND_PORT) == 0)
    {
        elog_i(TAG, "sendToBackend success");
        return true;
    }
    else
    {
        elog_e(TAG, "sendToBackend failed");
        return false;
    }
}

bool MasterServer::sendToSlave(std::vector<uint8_t> &frame)
{
    static uint32_t consecutiveFailures = 0;
    static uint32_t lastFailureTime = 0;
    const uint32_t MAX_CONSECUTIVE_FAILURES = MAX_CONSECUTIVE_UWB_FAILURES;
    const uint32_t FAILURE_RESET_INTERVAL = UWB_FAILURE_RESET_INTERVAL_MS;

    uint32_t currentTime = getCurrentTimestampMs();

    // 如果距离上次失败超过30秒，重置失败计数
    if (currentTime - lastFailureTime > FAILURE_RESET_INTERVAL)
    {
        consecutiveFailures = 0;
    }

    // 如果连续失败次数过多，暂时停止发送
    if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES)
    {
        elog_w(TAG,
               "Too many consecutive UWB failures (%d), temporarily stopping "
               "transmission",
               consecutiveFailures);
        return false;
    }

    // send data by uwb
    if (UWB_SendData(frame.data(), frame.size(), 0) == 0)
    {
        elog_i(TAG, "sendToSlave success");
    }
    else
    {
        elog_e(TAG, "sendToSlave failed");
        return false;
    }

    // 发送成功，重置失败计数
    consecutiveFailures = 0;
    return true;
}

bool MasterServer::checkAndRecoverUWBHealth()
{
    static uint32_t lastHealthCheck = 0;
    static uint32_t uwbResetCount = 0;
    const uint32_t HEALTH_CHECK_INTERVAL = UWB_HEALTH_CHECK_INTERVAL_MS; // UWB健康检查间隔

    uint32_t currentTime = getCurrentTimestampMs();

    // 定期健康检查
    if (currentTime - lastHealthCheck < HEALTH_CHECK_INTERVAL)
    {
        return true; // 还未到检查时间
    }

    lastHealthCheck = currentTime;

    elog_i(TAG, "UWB health check completed, reset count: %d", uwbResetCount);
    return true;
}

// SlaveDataProcT 实现
MasterServer::SlaveDataProcT::SlaveDataProcT(MasterServer &parent)
    : TaskClassS("SlaveDataProcT", TaskPrio_Mid), parent(parent)
{
}

void MasterServer::SlaveDataProcT::task()
{
    elog_i(TAG, "SlaveDataProcT started");
    uwb_rx_msg_t msg;
    for (;;)
    {
        if (UWB_ReceiveData(&msg, 0) == 0)
        {
            elog_v(TAG, "SlaveDataProcT recvData size: %d", msg.data_len);
            // copy msg.data to recvData
            recvData.assign(msg.data, msg.data + msg.data_len);

            if (!recvData.empty())
            {
                // 检查是否为SLAVE_TO_BACKEND帧，如果是则直接透传
                bool hasSlaveToBackendFrame = false;
                size_t pos = 0;
                while (pos < recvData.size())
                {
                    // 查找帧头
                    size_t frameStart = parent.processor.findFrameHeader(recvData, pos);
                    if (frameStart == SIZE_MAX)
                    {
                        break; // 没有找到更多帧头
                    }

                    // 检查帧长度是否足够
                    if (frameStart + 7 > recvData.size())
                    {
                        break; // 帧头不完整
                    }

                    // 检查PacketId是否为SLAVE_TO_BACKEND
                    uint8_t packetId = recvData[frameStart + 2];
                    if (packetId == static_cast<uint8_t>(PacketId::SLAVE_TO_BACKEND))
                    {
                        // 找到SLAVE_TO_BACKEND帧，直接透传原始数据
                        hasSlaveToBackendFrame = true;
                        elog_v(TAG, "Found SLAVE_TO_BACKEND frame, forwarding raw "
                                    "data");

                        // 直接透传原始接收数据给后端
                        if (parent.sendToBackend(recvData))
                        {
                            elog_v(TAG,
                                   "Successfully forwarded raw SLAVE_TO_BACKEND "
                                   "data to backend (%d bytes)",
                                   recvData.size());
                        }
                        else
                        {
                            elog_e(TAG, "Failed to forward raw SLAVE_TO_BACKEND "
                                        "data to backend");
                        }
                        break; // 找到SLAVE_TO_BACKEND帧后直接透传，不再处理其他帧
                    }

                    // 移动到下一个位置继续查找
                    pos = frameStart + 1;
                }

                // 如果不是SLAVE_TO_BACKEND帧，则按原来的逻辑处理
                if (!hasSlaveToBackendFrame)
                {
                    // process recvData
                    parent.processor.processReceivedData(recvData);

                    // process complete frame
                    Frame receivedFrame;
                    while (parent.processor.getNextCompleteFrame(receivedFrame))
                    {
                        parent.processFrame(receivedFrame);
                    }
                }

                recvData.clear();
            }
        }
        TaskBase::delay(1);
    }
}

// BackDataProcT 实现
MasterServer::BackDataProcT::BackDataProcT(MasterServer &parent)
    : TaskClassS("BackDataProcT", TaskPrio_Mid), parent(parent)
{
}

void MasterServer::BackDataProcT::task()
{
    elog_i(TAG, "BackDataProcT started");
    udp_rx_msg_t msg;
    for (;;)
    {
        if (UDP_ReceiveData(&msg, 0) == 0)
        {
            // copy msg.data to recvData
            recvData.assign(msg.data, msg.data + msg.data_len);

            if (!recvData.empty())
            {
                elog_v(TAG, "Backend recvData size: %d", recvData.size());
                parent.processor.processReceivedData(recvData);
                Frame receivedFrame;
                while (parent.processor.getNextCompleteFrame(receivedFrame))
                {
                    // 只处理来自后端的消息，不处理转发的从机数据
                    if (receivedFrame.packetId == static_cast<uint8_t>(PacketId::BACKEND_TO_MASTER))
                    {
                        parent.processFrame(receivedFrame);
                    }
                    else
                    {
                        elog_w(TAG,
                               "Ignoring non-backend frame (PacketId: 0x%02X) to "
                               "prevent loopback",
                               static_cast<int>(receivedFrame.packetId));
                    }
                }
                recvData.clear();
            }
        }
        TaskBase::delay(1);
    }
}

// MainTask 实现
MasterServer::MainTask::MainTask(MasterServer &parent) : TaskClassS("MainTask", TaskPrio_High), parent(parent)
{
}

void MasterServer::MainTask::task()
{
    elog_i(TAG, "MainTask started and running");

    uint32_t lastDeviceStatusCheck = 0;
    uint32_t deviceStatusCheckInterval = DEVICE_STATUS_CHECK_INTERVAL_MS; // 设备状态检查间隔
    uint32_t lastDeviceCleanup = 0;
    uint32_t deviceCleanupInterval = DEVICE_CLEANUP_INTERVAL_MS; // 设备清理间隔

    for (;;)
    {
        uint32_t currentTime = getCurrentTimestampMs();

        // Process pending commands, ping sessions, and data collection
        parent.processPendingCommands();
        parent.processPingSessions();
        parent.processPendingBackendResponses();

        // Process data collection (simplified for push-based architecture)
        if (parent.getDeviceManager().isDataCollectionActive())
        {
            // Data collection is now handled by slaves automatically pushing
            // data No complex state management needed here
        }

        parent.processTimeSync();

        // 定期检查设备在线状态(discarded)
        if (currentTime - lastDeviceStatusCheck >= deviceStatusCheckInterval)
        {
            parent.getDeviceManager().updateDeviceOnlineStatus();
            lastDeviceStatusCheck = currentTime;
        }

        // 定期清理超时设备（删除而不是标记离线）
        if (currentTime - lastDeviceCleanup >= deviceCleanupInterval)
        {
            parent.getDeviceManager().cleanupExpiredDevices(DEVICE_TIMEOUT_MS); // 设备超时删除
            lastDeviceCleanup = currentTime;
        }

        // 定期检查UWB模块健康状态
        parent.checkAndRecoverUWBHealth();

        TaskBase::delay(TASK_DELAY_MS);
    }
}

// MasterServer run方法实现
void MasterServer::run()
{
    elog_d(TAG, "Starting MasterServer...");

    // 创建并启动从机数据处理任务
    if (slaveDataProcessingTask)
    {
        slaveDataProcessingTask->give();
        elog_d(TAG, "SlaveDataProcT started");
    }

    // 创建并启动后端数据处理任务
    if (backendDataProcessingTask)
    {
        backendDataProcessingTask->give();
        elog_d(TAG, "BackDataProcT started");
    }

    // 创建并启动主任务
    if (mainTask)
    {
        mainTask->give();
        elog_d(TAG, "MainTask started");
    }

    // 主循环
    while (1)
    {
        // elog_i(TAG, "hptimer ms: %d", getCurrentTimestampMs());
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_0);
    }
}