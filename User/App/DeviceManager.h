#pragma once

#include <unordered_map>
#include <vector>

#include "FreeRTOS.h"
#include "WhtsProtocol.h"
#include "hptimer.hpp"
#include "task.h"

using namespace WhtsProtocol;

// 采集周期状态枚举
enum class CollectionCycleState {
    IDLE,         // 空闲状态
    COLLECTING    // 正在采集 (从机自动推送数据)
};

// 设备信息结构，对应DeviceListResponseMessage::DeviceInfo
struct DeviceInfo {
    uint32_t deviceId;
    uint8_t shortId;
    uint8_t online;
    uint8_t versionMajor;
    uint8_t versionMinor;
    uint16_t versionPatch;
    uint32_t lastSeenTime;    // 最后一次通信时间
    uint32_t announceTime;    // 首次宣告时间
    uint8_t announceCount;    // 宣告次数
    bool shortIdAssigned;     // 是否已分配短ID

    DeviceInfo()
        : deviceId(0),
          shortId(0),
          online(0),
          versionMajor(0),
          versionMinor(0),
          versionPatch(0),
          lastSeenTime(0),
          announceTime(0),
          announceCount(0),
          shortIdAssigned(false) {}

    DeviceInfo(uint32_t id, uint8_t major, uint8_t minor, uint16_t patch)
        : deviceId(id),
          shortId(0),
          online(1),
          versionMajor(major),
          versionMinor(minor),
          versionPatch(patch),
          lastSeenTime(0),
          announceTime(0),
          announceCount(0),
          shortIdAssigned(false) {}
};

// Data Collection Management structure
struct DataCollectionInfo {
    uint32_t slaveId;              // 从机ID
    uint32_t startTimestamp;       // 开始采集时间戳
    uint32_t estimatedDuration;    // 估计采集时长(毫秒)
    bool dataRequested;            // 是否已发送数据请求
    bool dataReceived;             // 是否已接收数据

    DataCollectionInfo(uint32_t id, uint32_t duration)
        : slaveId(id),
          startTimestamp(0),
          estimatedDuration(duration),
          dataRequested(false),
          dataReceived(false) {}

    // 计算是否采集完成
    bool isCollectionComplete(uint32_t currentTime) const {
        return startTimestamp > 0 &&
               (currentTime - startTimestamp >= estimatedDuration);
    }
};

inline uint32_t getCurrentTimestampMs() { return hal_hptimer_get_ms(); }

inline uint32_t getCurrentTimestampUs() { return hal_hptimer_get_us(); }

// Device management for tracking connected slaves
class DeviceManager {
   private:
    // 根据当前模式选择配置参数和估算采集时间
    static constexpr uint8_t DEFAULT_INTERVAL_MS = 10;    // 默认10ms间隔
    static constexpr uint8_t BUFFER_TIME_MS = 100;        // 缓冲时间50ms

    std::unordered_map<uint32_t, bool> connectedSlaves;
    std::unordered_map<uint32_t, uint8_t> slaveShortIds;
    std::unordered_map<uint32_t, Backend2Master::SlaveConfigMessage::SlaveInfo>
        slaveConfigs;
    std::vector<uint32_t>
        slaveConfigOrder;    // Store the order of slave configurations as
                             // received from backend

    // 设备信息存储
    std::unordered_map<uint32_t, DeviceInfo>
        deviceInfos;        // deviceId -> DeviceInfo
    uint8_t nextShortId;    // 下一个可分配的短ID

    uint8_t currentMode;             // 0=Conduction, 1=Resistance, 2=Clip
    uint8_t systemRunningStatus;     // 0=Stop, 1=Run, 2=Reset
    uint8_t configuredIntervalMs;    // 配置的间隔时间，从Backend2Master消息设置

    // 数据采集管理 (简化版)
    bool dataCollectionActive;
    CollectionCycleState
        cycleState;    // 当前采集周期状态 (只有IDLE和COLLECTING)

   public:
    DeviceManager();

    void addSlave(uint32_t slaveId, uint8_t shortId = 0);
    void removeSlave(uint32_t slaveId);
    bool isSlaveConnected(uint32_t slaveId) const;
    std::vector<uint32_t> getConnectedSlaves() const;
    std::vector<uint32_t> getConnectedSlavesInConfigOrder()
        const;    // Get connected slaves in configuration order
    uint8_t getSlaveShortId(uint32_t slaveId) const;

    // 设备信息管理
    void addDeviceInfo(uint32_t deviceId, uint8_t versionMajor,
                       uint8_t versionMinor, uint16_t versionPatch);
    void updateDeviceAnnounce(uint32_t deviceId);
    void removeDeviceInfo(uint32_t deviceId);    // 从设备列表中删除设备
    bool shouldAssignShortId(uint32_t deviceId) const;
    uint8_t assignShortId(uint32_t deviceId);
    void confirmShortId(uint32_t deviceId, uint8_t shortId);
    void updateDeviceLastSeen(uint32_t deviceId);
    std::vector<DeviceInfo> getAllDeviceInfos() const;
    bool hasDeviceInfo(uint32_t deviceId) const;
    DeviceInfo getDeviceInfo(uint32_t deviceId) const;
    void updateDeviceOnlineStatus(
        uint32_t timeoutMs = 30000);    // 检查设备在线状态
    void cleanupExpiredDevices(
        uint32_t timeoutMs = 60000);    // 清理超时设备（删除而不是标记离线）

    // Configuration management
    void setSlaveConfig(
        uint32_t slaveId,
        const Backend2Master::SlaveConfigMessage::SlaveInfo &config);
    Backend2Master::SlaveConfigMessage::SlaveInfo getSlaveConfig(
        uint32_t slaveId) const;
    bool hasSlaveConfig(uint32_t slaveId) const;
    void clearSlaveConfigs();    // Clear all slave configurations and order

    // Mode management
    void setCurrentMode(uint8_t mode);
    uint8_t getCurrentMode() const;

    // System status management
    void setSystemRunningStatus(uint8_t status);
    uint8_t getSystemRunningStatus() const;

    // Interval configuration management
    void setConfiguredInterval(uint8_t intervalMs);
    uint8_t getConfiguredInterval() const;
    uint8_t getEffectiveInterval() const;    // 返回配置的间隔或默认值

    // 数据采集管理 (简化版：推送式架构)
    void startDataCollection();
    void resetDataCollection();
    void markDataReceived(uint32_t slaveId);
    void markSlaveControlResponseReceived(uint32_t slaveId);
    CollectionCycleState getCycleState() const;
    bool isDataCollectionActive() const;
};