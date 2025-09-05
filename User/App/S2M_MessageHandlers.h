#pragma once

#include <memory>

#include "WhtsProtocol.h"

using namespace WhtsProtocol;

// Forward declarations
class MasterServer;

// Message handler interface for Slave2Master messages
class ISlave2MasterMessageHandler
{
  public:
    virtual ~ISlave2MasterMessageHandler() = default;
    virtual std::unique_ptr<Message> processMessage(uint32_t slaveId, const Message &message, MasterServer *server) = 0;
    virtual void executeActions(uint32_t slaveId, const Message &message, MasterServer *server) = 0;
};

// Announce Message Handler
class AnnounceHandler : public ISlave2MasterMessageHandler
{
  public:
    static AnnounceHandler &getInstance()
    {
        static AnnounceHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(uint32_t slaveId, const Message &message, MasterServer *server) override;
    void executeActions(uint32_t slaveId, const Message &message, MasterServer *server) override;

  private:
    AnnounceHandler() = default;
    AnnounceHandler(const AnnounceHandler &) = delete;
    AnnounceHandler &operator=(const AnnounceHandler &) = delete;
};

// Short ID Confirm Message Handler
class ShortIdConfirmHandler : public ISlave2MasterMessageHandler
{
  public:
    static ShortIdConfirmHandler &getInstance()
    {
        static ShortIdConfirmHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(uint32_t slaveId, const Message &message, MasterServer *server) override;
    void executeActions(uint32_t slaveId, const Message &message, MasterServer *server) override;

  private:
    ShortIdConfirmHandler() = default;
    ShortIdConfirmHandler(const ShortIdConfirmHandler &) = delete;
    ShortIdConfirmHandler &operator=(const ShortIdConfirmHandler &) = delete;
};

class ResetResponseHandler : public ISlave2MasterMessageHandler
{
  public:
    static ResetResponseHandler &getInstance()
    {
        static ResetResponseHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(uint32_t slaveId, const Message &message, MasterServer *server) override;
    void executeActions(uint32_t slaveId, const Message &message, MasterServer *server) override;

  private:
    ResetResponseHandler() = default;
    ResetResponseHandler(const ResetResponseHandler &) = delete;
    ResetResponseHandler &operator=(const ResetResponseHandler &) = delete;
};

// Ping Response Handler
class PingResponseHandler : public ISlave2MasterMessageHandler
{
  public:
    static PingResponseHandler &getInstance()
    {
        static PingResponseHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(uint32_t slaveId, const Message &message, MasterServer *server) override;
    void executeActions(uint32_t slaveId, const Message &message, MasterServer *server) override;

  private:
    PingResponseHandler() = default;
    PingResponseHandler(const PingResponseHandler &) = delete;
    PingResponseHandler &operator=(const PingResponseHandler &) = delete;
};

// Heartbeat Message Handler
class HeartbeatHandler : public ISlave2MasterMessageHandler
{
  public:
    static HeartbeatHandler &getInstance()
    {
        static HeartbeatHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(uint32_t slaveId, const Message &message, MasterServer *server) override;
    void executeActions(uint32_t slaveId, const Message &message, MasterServer *server) override;

  private:
    HeartbeatHandler() = default;
    HeartbeatHandler(const HeartbeatHandler &) = delete;
    HeartbeatHandler &operator=(const HeartbeatHandler &) = delete;
};