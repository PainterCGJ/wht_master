#pragma once

#include <memory>
#include <string>
#include <vector>

#include "WhtsProtocol.h"

using namespace WhtsProtocol;

// Forward declarations
class MasterServer;

// Message handler interface for extensible message processing
class IMessageHandler
{
  public:
    virtual ~IMessageHandler() = default;
    virtual std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) = 0;
    virtual void executeActions(const Message &message, MasterServer *server) = 0;
};

// Action result structure
struct ActionResult
{
    bool success;
    std::string errorMessage;
    std::vector<uint32_t> affectedSlaves;

    ActionResult(bool s = true, const std::string &err = "") : success(s), errorMessage(err)
    {
    }
};

// Slave Configuration Message Handler
class SlaveConfigHandler : public IMessageHandler
{
  public:
    static SlaveConfigHandler &getInstance()
    {
        static SlaveConfigHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    SlaveConfigHandler() = default;
    SlaveConfigHandler(const SlaveConfigHandler &) = delete;
    SlaveConfigHandler &operator=(const SlaveConfigHandler &) = delete;
};

// Mode Configuration Message Handler
class ModeConfigHandler : public IMessageHandler
{
  public:
    static ModeConfigHandler &getInstance()
    {
        static ModeConfigHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    ModeConfigHandler() = default;
    ModeConfigHandler(const ModeConfigHandler &) = delete;
    ModeConfigHandler &operator=(const ModeConfigHandler &) = delete;
};

// Reset Message Handler
class ResetHandler : public IMessageHandler
{
  public:
    static ResetHandler &getInstance()
    {
        static ResetHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    ResetHandler() = default;
    ResetHandler(const ResetHandler &) = delete;
    ResetHandler &operator=(const ResetHandler &) = delete;
};

// Control Message Handler
class ControlHandler : public IMessageHandler
{
  public:
    static ControlHandler &getInstance()
    {
        static ControlHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    ControlHandler() = default;
    ControlHandler(const ControlHandler &) = delete;
    ControlHandler &operator=(const ControlHandler &) = delete;
};

// Ping Control Message Handler
class PingControlHandler : public IMessageHandler
{
  public:
    static PingControlHandler &getInstance()
    {
        static PingControlHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    PingControlHandler() = default;
    PingControlHandler(const PingControlHandler &) = delete;
    PingControlHandler &operator=(const PingControlHandler &) = delete;
};

// Device List Request Handler
class DeviceListHandler : public IMessageHandler
{
  public:
    static DeviceListHandler &getInstance()
    {
        static DeviceListHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    DeviceListHandler() = default;
    DeviceListHandler(const DeviceListHandler &) = delete;
    DeviceListHandler &operator=(const DeviceListHandler &) = delete;
};

class IntervalConfigHandler : public IMessageHandler
{
  public:
    static IntervalConfigHandler &getInstance()
    {
        static IntervalConfigHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    IntervalConfigHandler() = default;
    IntervalConfigHandler(const IntervalConfigHandler &) = delete;
    IntervalConfigHandler &operator=(const IntervalConfigHandler &) = delete;
};

// Clear Device List Message Handler
class ClearDeviceListHandler : public IMessageHandler
{
  public:
    static ClearDeviceListHandler &getInstance()
    {
        static ClearDeviceListHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    ClearDeviceListHandler() = default;
    ClearDeviceListHandler(const ClearDeviceListHandler &) = delete;
    ClearDeviceListHandler &operator=(const ClearDeviceListHandler &) = delete;
};

// Set UWB Channel Message Handler
class SetUwbChannelHandler : public IMessageHandler
{
  public:
    static SetUwbChannelHandler &getInstance()
    {
        static SetUwbChannelHandler instance;
        return instance;
    }
    std::unique_ptr<Message> processMessage(const Message &message, MasterServer *server) override;
    void executeActions(const Message &message, MasterServer *server) override;

  private:
    SetUwbChannelHandler() = default;
    SetUwbChannelHandler(const SetUwbChannelHandler &) = delete;
    SetUwbChannelHandler &operator=(const SetUwbChannelHandler &) = delete;
};