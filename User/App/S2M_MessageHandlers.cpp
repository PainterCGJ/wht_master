#include "S2M_MessageHandlers.h"

#include "elog.h"
#include "MasterServer.h"

// Announce Message Handler
std::unique_ptr<Message> AnnounceHandler::processMessage(uint32_t slaveId,
                                                         const Message &message,
                                                         MasterServer *server) {
    // Announce messages don't generate responses
    return nullptr;
}

void AnnounceHandler::executeActions(uint32_t slaveId, const Message &message,
                                     MasterServer *server) {
    const auto *announceMsg =
        dynamic_cast<const Slave2Master::AnnounceMessage *>(&message);
    if (!announceMsg) return;

    elog_i("AnnounceHandler",
           "Received announce message from device 0x%08X (v%d.%d.%d)",
           announceMsg->deviceId, announceMsg->versionMajor,
           announceMsg->versionMinor, announceMsg->versionPatch);

    // 添加或更新设备信息
    if (!server->getDeviceManager().hasDeviceInfo(announceMsg->deviceId)) {
        server->getDeviceManager().addDeviceInfo(
            announceMsg->deviceId, announceMsg->versionMajor,
            announceMsg->versionMinor, announceMsg->versionPatch);
    } else {
        server->getDeviceManager().updateDeviceAnnounce(announceMsg->deviceId);
    }

    // 检查是否需要分配短ID
    if (server->getDeviceManager().shouldAssignShortId(announceMsg->deviceId)) {
        uint8_t shortId =
            server->getDeviceManager().assignShortId(announceMsg->deviceId);
        if (shortId > 0) {
            // 发送短ID分配消息
            auto assignMsg =
                std::make_unique<Master2Slave::ShortIdAssignMessage>();
            assignMsg->shortId = shortId;

            server->sendCommandToSlaveWithRetry(announceMsg->deviceId,
                                                std::move(assignMsg), 3);
            elog_i("AnnounceHandler",
                   "Sent short ID assignment (%d) to device 0x%08X", shortId,
                   announceMsg->deviceId);
        }
    }
}

// Short ID Confirm Message Handler
std::unique_ptr<Message> ShortIdConfirmHandler::processMessage(
    uint32_t slaveId, const Message &message, MasterServer *server) {
    // Short ID confirm messages don't generate responses
    return nullptr;
}

void ShortIdConfirmHandler::executeActions(uint32_t slaveId,
                                           const Message &message,
                                           MasterServer *server) {
    const auto *confirmMsg =
        dynamic_cast<const Slave2Master::ShortIdConfirmMessage *>(&message);
    if (!confirmMsg) return;

    elog_i("ShortIdConfirmHandler",
           "Received short ID confirmation from device 0x%08X (shortId=%d, "
           "status=%d)",
           slaveId, confirmMsg->shortId, confirmMsg->status);

    if (confirmMsg->status == 0) {
        // 成功确认，将设备加入到两个管理系统
        server->getDeviceManager().confirmShortId(slaveId, confirmMsg->shortId);
        elog_i("ShortIdConfirmHandler",
               "Device 0x%08X successfully joined network with short ID %d",
               slaveId, confirmMsg->shortId);
    } else {
        elog_w("ShortIdConfirmHandler",
               "Device 0x%08X failed to confirm short ID %d (status=%d)",
               slaveId, confirmMsg->shortId, confirmMsg->status);
    } 

    // 移除相应的待处理命令，防止重试
    server->removePendingCommand(
        slaveId,
        static_cast<uint8_t>(Master2SlaveMessageId::SHORT_ID_ASSIGN_MSG));
}

// SetTime Response Message Handler
std::unique_ptr<Message> SetTimeResponseHandler::processMessage(uint32_t slaveId,
                                                               const Message &message,
                                                               MasterServer *server) {
    // SetTime response messages don't generate responses
    return nullptr;
}

void SetTimeResponseHandler::executeActions(uint32_t slaveId,
                                           const Message &message,
                                           MasterServer *server) {
    const auto *setTimeRspMsg =
        dynamic_cast<const Slave2Master::SetTimeResponseMessage *>(&message);
    if (!setTimeRspMsg) return;

    elog_i("SetTimeResponseHandler",
           "Received set time response from device 0x%08X (status=%d, timestamp=%lu)",
           slaveId, setTimeRspMsg->status, (unsigned long)setTimeRspMsg->timestamp);

    // 标记时间同步响应
    bool success = (setTimeRspMsg->status == 0);
    server->markTimeSyncResponse(slaveId, success);

    if (success) {
        elog_i("SetTimeResponseHandler",
               "Device 0x%08X time synchronization successful", slaveId);
    } else {
        elog_w("SetTimeResponseHandler",
               "Device 0x%08X time synchronization failed (status=%d)",
               slaveId, setTimeRspMsg->status);
    }

    // 移除相应的待处理命令，防止重试
    server->removePendingCommand(
        slaveId,
        static_cast<uint8_t>(Master2SlaveMessageId::SET_TIME_MSG));
}

// Conduction Config Response Handler
std::unique_ptr<Message> ConductionConfigResponseHandler::processMessage(
    uint32_t slaveId, const Message &message, MasterServer *server) {
    // Config response messages don't generate responses
    return nullptr;
}

void ConductionConfigResponseHandler::executeActions(uint32_t slaveId,
                                                     const Message &message,
                                                     MasterServer *server) {
    const auto *rspMsg =
        dynamic_cast<const Slave2Master::ConductionConfigResponseMessage *>(
            &message);
    if (!rspMsg) return;

    elog_v("ConductionConfigResponseHandler",
           "Received conduction config response from slave 0x%08X, status: %d",
           slaveId, rspMsg->status);

    server->getDeviceManager().addSlave(slaveId);
    server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // Handle slave config response for backend tracking
    server->handleSlaveConfigResponse(slaveId, message.getMessageId(),
                                      rspMsg->status);

    // 移除相应的待处理命令
    server->removePendingCommand(
        slaveId,
        static_cast<uint8_t>(Master2SlaveMessageId::CONDUCTION_CFG_MSG));
}

// Resistance Config Response Handler
std::unique_ptr<Message> ResistanceConfigResponseHandler::processMessage(
    uint32_t slaveId, const Message &message, MasterServer *server) {
    // Config response messages don't generate responses
    return nullptr;
}

void ResistanceConfigResponseHandler::executeActions(uint32_t slaveId,
                                                     const Message &message,
                                                     MasterServer *server) {
    const auto *rspMsg =
        dynamic_cast<const Slave2Master::ResistanceConfigResponseMessage *>(
            &message);
    if (!rspMsg) return;

    elog_v("ResistanceConfigResponseHandler",
           "Received resistance config response from slave 0x%08X, status: %d",
           slaveId, rspMsg->status);

    server->getDeviceManager().addSlave(slaveId);
    server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // Handle slave config response for backend tracking
    server->handleSlaveConfigResponse(slaveId, message.getMessageId(),
                                      rspMsg->status);

    // 移除相应的待处理命令
    server->removePendingCommand(
        slaveId,
        static_cast<uint8_t>(Master2SlaveMessageId::RESISTANCE_CFG_MSG));
}

// Clip Config Response Handler
std::unique_ptr<Message> ClipConfigResponseHandler::processMessage(
    uint32_t slaveId, const Message &message, MasterServer *server) {
    // Config response messages don't generate responses
    return nullptr;
}

void ClipConfigResponseHandler::executeActions(uint32_t slaveId,
                                               const Message &message,
                                               MasterServer *server) {
    const auto *rspMsg =
        dynamic_cast<const Slave2Master::ClipConfigResponseMessage *>(&message);
    if (!rspMsg) return;

    elog_v("ClipConfigResponseHandler",
           "Received clip config response from slave 0x%08X, status: %d",
           slaveId, rspMsg->status);

    server->getDeviceManager().addSlave(slaveId);
    server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // Handle slave config response for backend tracking
    server->handleSlaveConfigResponse(slaveId, message.getMessageId(),
                                      rspMsg->status);

    // 移除相应的待处理命令
    server->removePendingCommand(
        slaveId, static_cast<uint8_t>(Master2SlaveMessageId::CLIP_CFG_MSG));
}

// Reset Response Handler
std::unique_ptr<Message> ResetResponseHandler::processMessage(
    uint32_t slaveId, const Message &message, MasterServer *server) {
    // Reset response messages don't generate responses
    return nullptr;
}

void ResetResponseHandler::executeActions(uint32_t slaveId,
                                          const Message &message,
                                          MasterServer *server) {
    const auto *rspMsg =
        dynamic_cast<const Slave2Master::RstResponseMessage *>(&message);
    if (!rspMsg) return;

    elog_v("ResetResponseHandler",
           "Received reset response from slave 0x%08X, status: %d", slaveId,
           rspMsg->status);

    server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // Handle slave config response for backend tracking
    server->handleSlaveConfigResponse(slaveId, message.getMessageId(),
                                      rspMsg->status);

    // 移除相应的待处理命令
    server->removePendingCommand(
        slaveId, static_cast<uint8_t>(Master2SlaveMessageId::RST_MSG));
}

// Ping Response Handler
std::unique_ptr<Message> PingResponseHandler::processMessage(
    uint32_t slaveId, const Message &message, MasterServer *server) {
    // Ping response messages don't generate responses
    return nullptr;
}

void PingResponseHandler::executeActions(uint32_t slaveId,
                                         const Message &message,
                                         MasterServer *server) {
    const auto *pingRsp =
        dynamic_cast<const Slave2Master::PingRspMessage *>(&message);
    if (!pingRsp) return;

    elog_v("PingResponseHandler",
           "Received ping response from slave 0x%08X (seq=%d)", slaveId,
           pingRsp->sequenceNumber);

    server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // Update ping session success count
    for (auto &session : server->activePingSessions) {
        if (session.targetId == slaveId) {
            session.successCount++;
            break;
        }
    }

    // 移除相应的待处理命令
    server->removePendingCommand(
        slaveId, static_cast<uint8_t>(Master2SlaveMessageId::PING_REQ_MSG));
}

// Slave Control Response Handler
std::unique_ptr<Message> SlaveControlResponseHandler::processMessage(
    uint32_t slaveId, const Message &message, MasterServer *server) {
    // Slave control response messages don't generate responses
    return nullptr;
}

void SlaveControlResponseHandler::executeActions(uint32_t slaveId,
                                                 const Message &message,
                                                 MasterServer *server) {
    const auto *controlRsp =
        dynamic_cast<const Slave2Master::SlaveControlResponseMessage *>(
            &message);
    if (!controlRsp) return;

    elog_v("SlaveControlResponseHandler",
           "Received slave control response from slave 0x%08X, status: %d",
           slaveId, static_cast<int>(controlRsp->status));

    server->getDeviceManager().updateDeviceLastSeen(slaveId);

    // 根据响应状态处理
    bool success = (controlRsp->status == Slave2Master::ResponseStatusCode::SUCCESS);
    
    if (success) {
        elog_i("SlaveControlResponseHandler",
               "Slave 0x%08X control command executed successfully", slaveId);

        // 标记从机控制响应已收到
        server->getDeviceManager().markSlaveControlResponseReceived(slaveId);
    } else {
        elog_w("SlaveControlResponseHandler",
               "Slave 0x%08X control command failed with status: %d", slaveId,
               static_cast<int>(controlRsp->status));
    }
    
    // 标记控制响应
    server->markControlResponse(slaveId, success);

    // 移除相应的待处理命令
    server->removePendingCommand(
        slaveId,
        static_cast<uint8_t>(Master2SlaveMessageId::SLAVE_CONTROL_MSG));
}