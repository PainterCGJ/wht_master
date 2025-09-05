#include "S2M_MessageHandlers.h"

#include "MasterServer.h"
#include "elog.h"

// Announce Message Handler
std::unique_ptr<Message> AnnounceHandler::processMessage(uint32_t slaveId, const Message &message, MasterServer *server)
{
    // Announce messages don't generate responses
    return nullptr;
}

void AnnounceHandler::executeActions(uint32_t slaveId, const Message &message, MasterServer *server)
{
    const auto *announceMsg = dynamic_cast<const Slave2Master::AnnounceMessage *>(&message);
    if (!announceMsg)
        return;

    elog_i("AnnounceHandler", "Received announce message from device 0x%08X (v%d.%d.%d)", announceMsg->deviceId,
           announceMsg->versionMajor, announceMsg->versionMinor, announceMsg->versionPatch);

    // 添加或更新设备信息
    if (!server->getDeviceManager().hasDeviceInfo(announceMsg->deviceId))
    {
        server->getDeviceManager().addDeviceInfo(announceMsg->deviceId, announceMsg->versionMajor,
                                                 announceMsg->versionMinor, announceMsg->versionPatch);
    }
    else
    {
        server->getDeviceManager().updateDeviceAnnounce(announceMsg->deviceId);
    }

    // 检查是否需要分配短ID或重新发送已分配的短ID
    if (server->getDeviceManager().shouldAssignShortId(announceMsg->deviceId))
    {
        // 需要分配新的短ID
        uint8_t shortId = server->getDeviceManager().assignShortId(announceMsg->deviceId);
        if (shortId > 0)
        {
            // 发送短ID分配消息
            auto assignMsg = std::make_unique<Master2Slave::ShortIdAssignMessage>();
            assignMsg->shortId = shortId;

            server->sendCommandToSlaveWithRetry(announceMsg->deviceId, std::move(assignMsg), 3);
            elog_i("AnnounceHandler", "Sent short ID assignment (%d) to device 0x%08X", shortId, announceMsg->deviceId);
        }
    }
    else if (server->getDeviceManager().hasDeviceInfo(announceMsg->deviceId))
    {
        // 设备已存在，检查是否已分配短ID，如果已分配则重新发送
        DeviceInfo deviceInfo = server->getDeviceManager().getDeviceInfo(announceMsg->deviceId);
        if (deviceInfo.shortIdAssigned && deviceInfo.shortId > 0)
        {
            // 重新发送已分配的短ID
            auto assignMsg = std::make_unique<Master2Slave::ShortIdAssignMessage>();
            assignMsg->shortId = deviceInfo.shortId;

            server->sendCommandToSlaveWithRetry(announceMsg->deviceId, std::move(assignMsg), 3);
            elog_i("AnnounceHandler", "Re-sent existing short ID assignment (%d) to device 0x%08X", deviceInfo.shortId,
                   announceMsg->deviceId);
        }
    }
}

// Short ID Confirm Message Handler
std::unique_ptr<Message> ShortIdConfirmHandler::processMessage(uint32_t slaveId, const Message &message,
                                                               MasterServer *server)
{
    // Short ID confirm messages don't generate responses
    return nullptr;
}

void ShortIdConfirmHandler::executeActions(uint32_t slaveId, const Message &message, MasterServer *server)
{
    const auto *confirmMsg = dynamic_cast<const Slave2Master::ShortIdConfirmMessage *>(&message);
    if (!confirmMsg)
        return;

    elog_i("ShortIdConfirmHandler",
           "Received short ID confirmation from device 0x%08X (shortId=%d, "
           "status=%d)",
           slaveId, confirmMsg->shortId, confirmMsg->status);

    if (confirmMsg->status == 0)
    {
        // 成功确认，将设备加入到两个管理系统
        server->getDeviceManager().confirmShortId(slaveId, confirmMsg->shortId);
        elog_i("ShortIdConfirmHandler", "Device 0x%08X successfully joined network with short ID %d", slaveId,
               confirmMsg->shortId);
    }
    else
    {
        elog_w("ShortIdConfirmHandler", "Device 0x%08X failed to confirm short ID %d (status=%d)", slaveId,
               confirmMsg->shortId, confirmMsg->status);
    }

    // 移除相应的待处理命令，防止重试
    server->removePendingCommand(slaveId, static_cast<uint8_t>(Master2SlaveMessageId::SHORT_ID_ASSIGN_MSG));
}

// Reset Response Handler
std::unique_ptr<Message> ResetResponseHandler::processMessage(uint32_t slaveId, const Message &message,
                                                              MasterServer *server)
{
    // Reset response messages don't generate responses
    return nullptr;
}

void ResetResponseHandler::executeActions(uint32_t slaveId, const Message &message, MasterServer *server)
{
    const auto *rspMsg = dynamic_cast<const Slave2Master::RstResponseMessage *>(&message);
    if (!rspMsg)
        return;

    elog_v("ResetResponseHandler", "Received reset response from slave 0x%08X, status: %d", slaveId, rspMsg->status);

    // 注意：根据新的心跳包机制，只有心跳包才更新lastSeen时间
    // server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // Clear the reset flag for this slave since it has responded
    server->getDeviceManager().clearSlaveResetFlag(slaveId);

    // Handle slave config response for backend tracking
    server->handleSlaveConfigResponse(slaveId, message.getMessageId(), rspMsg->status);

    // 移除相应的待处理命令（注意：现在不再有单独的RST_MSG命令）
    // server->removePendingCommand(slaveId, static_cast<uint8_t>(Master2SlaveMessageId::RST_MSG));
}

// Ping Response Handler
std::unique_ptr<Message> PingResponseHandler::processMessage(uint32_t slaveId, const Message &message,
                                                             MasterServer *server)
{
    // Ping response messages don't generate responses
    return nullptr;
}

void PingResponseHandler::executeActions(uint32_t slaveId, const Message &message, MasterServer *server)
{
    const auto *pingRsp = dynamic_cast<const Slave2Master::PingRspMessage *>(&message);
    if (!pingRsp)
        return;

    elog_v("PingResponseHandler", "Received ping response from slave 0x%08X (seq=%d)", slaveId,
           pingRsp->sequenceNumber);

    // 注意：根据新的心跳包机制，只有心跳包才更新lastSeen时间
    // server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // Update ping session success count
    for (auto &session : server->activePingSessions)
    {
        if (session.targetId == slaveId)
        {
            session.successCount++;
            break;
        }
    }

    // 移除相应的待处理命令
    server->removePendingCommand(slaveId, static_cast<uint8_t>(Master2SlaveMessageId::PING_REQ_MSG));
}

// Heartbeat Message Handler
std::unique_ptr<Message> HeartbeatHandler::processMessage(uint32_t slaveId, const Message &message,
                                                          MasterServer *server)
{
    // Heartbeat messages don't generate responses
    return nullptr;
}

void HeartbeatHandler::executeActions(uint32_t slaveId, const Message &message, MasterServer *server)
{
    const auto *heartbeatMsg = dynamic_cast<const Slave2Master::HeartbeatMessage *>(&message);
    if (!heartbeatMsg)
        return;

    elog_v("HeartbeatHandler", "Received heartbeat from slave 0x%08X (reserve=%d)", slaveId, heartbeatMsg->reserve);

    // 心跳包是唯一更新设备最后通信时间的消息
    server->getDeviceManager().updateDeviceLastSeen(slaveId);
}
