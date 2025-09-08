#include "DeviceManager.h"

#include <algorithm>

#include "elog.h"

DeviceManager::DeviceManager()
    : currentMode(MODE_CONDUCTION), systemRunningStatus(SYSTEM_STATUS_STOP),
      configuredIntervalMs(0), // 0表示未配置，使用默认值
      nextShortId(SHORT_ID_START), dataCollectionActive(false), cycleState(CollectionCycleState::IDLE)
{
    // 初始化短ID池，所有短ID都可用
    for (uint8_t id = SHORT_ID_START; id <= SHORT_ID_MAX; ++id)
    {
        availableShortIds.insert(id);
    }
} // 短ID从起始值开始分配

void DeviceManager::addSlave(uint32_t slaveId, uint8_t shortId)
{
    connectedSlaves[slaveId] = true;
    if (shortId > 0)
    {
        slaveShortIds[slaveId] = shortId;
    }
}

void DeviceManager::removeSlave(uint32_t slaveId)
{
    connectedSlaves[slaveId] = false;
}

bool DeviceManager::isSlaveConnected(uint32_t slaveId) const
{
    auto it = connectedSlaves.find(slaveId);
    return it != connectedSlaves.end() && it->second;
}

std::vector<uint32_t> DeviceManager::getConnectedSlaves() const
{
    std::vector<uint32_t> result;
    for (const auto &pair : connectedSlaves)
    {
        if (pair.second)
        {
            result.push_back(pair.first);
        }
    }
    return result;
}

std::vector<uint32_t> DeviceManager::getConnectedSlavesInConfigOrder() const
{
    std::vector<uint32_t> result;
    for (uint32_t slaveId : slaveConfigOrder)
    {
        if (isSlaveConnected(slaveId))
        {
            result.push_back(slaveId);
        }
    }
    return result;
}

uint8_t DeviceManager::getSlaveShortId(uint32_t slaveId) const
{
    auto it = slaveShortIds.find(slaveId);
    return it != slaveShortIds.end() ? it->second : 0;
}

// Configuration management
void DeviceManager::setSlaveConfig(uint32_t slaveId, const Backend2Master::SlaveConfigMessage::SlaveInfo &config)
{
    slaveConfigs[slaveId] = config;

    // Add to configuration order if not already present
    if (std::find(slaveConfigOrder.begin(), slaveConfigOrder.end(), slaveId) == slaveConfigOrder.end())
    {
        slaveConfigOrder.push_back(slaveId);
    }
}

Backend2Master::SlaveConfigMessage::SlaveInfo DeviceManager::getSlaveConfig(uint32_t slaveId) const
{
    auto it = slaveConfigs.find(slaveId);
    return it != slaveConfigs.end() ? it->second : Backend2Master::SlaveConfigMessage::SlaveInfo{};
}

bool DeviceManager::hasSlaveConfig(uint32_t slaveId) const
{
    return slaveConfigs.find(slaveId) != slaveConfigs.end();
}

void DeviceManager::clearSlaveConfigs()
{
    slaveConfigs.clear();
    slaveConfigOrder.clear();
}

// Mode management
void DeviceManager::setCurrentMode(uint8_t mode)
{
    currentMode = mode;
}
uint8_t DeviceManager::getCurrentMode() const
{
    return currentMode;
}

// System status management
void DeviceManager::setSystemRunningStatus(uint8_t status)
{
    systemRunningStatus = status;
}
uint8_t DeviceManager::getSystemRunningStatus() const
{
    return systemRunningStatus;
}

// Interval configuration management
void DeviceManager::setConfiguredInterval(uint8_t intervalMs)
{
    configuredIntervalMs = intervalMs;
    elog_v("DeviceManager", "Configured interval set to %u ms", intervalMs);
}

uint8_t DeviceManager::getConfiguredInterval() const
{
    return configuredIntervalMs;
}

uint8_t DeviceManager::getEffectiveInterval() const
{
    return configuredIntervalMs > 0 ? configuredIntervalMs : DEFAULT_INTERVAL_MS;
}

// 数据采集管理
void DeviceManager::startDataCollection()
{
    elog_v("DeviceManager", "Starting data collection - mode: %d, total configs: %d", currentMode, slaveConfigs.size());

    // 检查是否有已配置且连接的从机
    bool hasConnectedSlaves = false;
    for (const auto &pair : slaveConfigs)
    {
        uint32_t slaveId = pair.first;
        if (isSlaveConnected(slaveId))
        {
            hasConnectedSlaves = true;
            elog_v("DeviceManager", "Slave 0x%08X is connected and configured", slaveId);
        }
    }

    dataCollectionActive = hasConnectedSlaves;
    cycleState = dataCollectionActive ? CollectionCycleState::COLLECTING : CollectionCycleState::IDLE;

    elog_v("DeviceManager", "Data collection started, mode: %d, active: %d", currentMode, dataCollectionActive ? 1 : 0);
}

void DeviceManager::resetDataCollection()
{
    dataCollectionActive = false;
    cycleState = CollectionCycleState::IDLE;

    elog_v("DeviceManager", "Data collection reset");
}

// 标记数据已接收 (简化版)
void DeviceManager::markDataReceived(uint32_t slaveId)
{
    elog_v("DeviceManager", "Data received from slave 0x%08X", slaveId);
}

// 获取当前采集周期状态
CollectionCycleState DeviceManager::getCycleState() const
{
    return cycleState;
}

// 是否有活跃的数据采集
bool DeviceManager::isDataCollectionActive() const
{
    return dataCollectionActive;
}

// 设备信息管理方法实现
void DeviceManager::addDeviceInfo(uint32_t deviceId, uint8_t versionMajor, uint8_t versionMinor, uint16_t versionPatch)
{
    uint32_t currentTime = getCurrentTimestampMs();

    auto it = deviceInfos.find(deviceId);
    if (it == deviceInfos.end())
    {
        // 新设备
        DeviceInfo info(deviceId, versionMajor, versionMinor, versionPatch);
        info.joinRequestTime = currentTime;
        info.joinRequestCount = 1;
        info.lastSeenTime = currentTime;
        deviceInfos[deviceId] = info;

        elog_i("DeviceManager", "Added new device 0x%08X (v%d.%d.%d)", deviceId, versionMajor, versionMinor,
               versionPatch);
    }
    else
    {
        // 已存在设备，更新信息
        it->second.lastSeenTime = currentTime;
        it->second.versionMajor = versionMajor;
        it->second.versionMinor = versionMinor;
        it->second.versionPatch = versionPatch;

        elog_v("DeviceManager", "Updated existing device 0x%08X", deviceId);
    }
}

void DeviceManager::updateDeviceJoinRequest(uint32_t deviceId)
{
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end())
    {
        it->second.joinRequestCount++;
        it->second.lastSeenTime = getCurrentTimestampMs();

        elog_v("DeviceManager", "Device 0x%08X joinRequest count: %d", deviceId, it->second.joinRequestCount);
    }
}

void DeviceManager::removeDeviceInfo(uint32_t deviceId)
{
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end())
    {
        // 如果设备已分配短ID，释放该短ID
        if (it->second.shortIdAssigned && it->second.shortId > 0)
        {
            uint8_t releasedId = it->second.shortId;
            availableShortIds.insert(releasedId);

            // 同时从slaveShortIds中移除
            slaveShortIds.erase(deviceId);

            elog_i("DeviceManager", "Released short ID %d from device 0x%08X (available IDs: %d)", releasedId, deviceId,
                   static_cast<int>(availableShortIds.size()));
        }

        elog_i("DeviceManager", "Removing device 0x%08X from device list", deviceId);
        deviceInfos.erase(it);

        // 同时从连接状态中移除
        connectedSlaves.erase(deviceId);

        elog_i("DeviceManager", "Device 0x%08X completely removed from all lists", deviceId);
    }
}

bool DeviceManager::shouldAssignShortId(uint32_t deviceId) const
{
    auto it = deviceInfos.find(deviceId);
    if (it == deviceInfos.end())
    {
        return false;
    }

    // 如果还没有分配短ID，且宣告次数在合理范围内
    return !it->second.shortIdAssigned && it->second.joinRequestCount <= ANNOUNCE_COUNT_LIMIT;
}

uint8_t DeviceManager::assignShortId(uint32_t deviceId)
{
    auto it = deviceInfos.find(deviceId);
    if (it == deviceInfos.end())
    {
        return 0;
    }

    // 检查是否有可用的短ID
    if (availableShortIds.empty())
    {
        elog_e("DeviceManager", "No available short IDs for device 0x%08X", deviceId);
        return 0;
    }

    // 从可用短ID池中取出最小的ID
    uint8_t assignedId = *availableShortIds.begin();
    availableShortIds.erase(availableShortIds.begin());

    it->second.shortId = assignedId;
    it->second.shortIdAssigned = true;
    it->second.lastSeenTime = getCurrentTimestampMs();

    elog_i("DeviceManager", "Assigned short ID %d to device 0x%08X (available IDs: %d)", assignedId, deviceId,
           static_cast<int>(availableShortIds.size()));

    return assignedId;
}

void DeviceManager::confirmShortId(uint32_t deviceId, uint8_t shortId)
{
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end())
    {
        it->second.shortId = shortId;
        it->second.shortIdAssigned = true;
        it->second.online = 1;
        it->second.lastSeenTime = getCurrentTimestampMs();

        // 同时更新旧的连接状态管理
        addSlave(deviceId, shortId);

        elog_i("DeviceManager", "Confirmed short ID %d for device 0x%08X", shortId, deviceId);
    }
}

void DeviceManager::updateDeviceBatteryLevel(uint32_t deviceId, uint8_t batteryLevel)
{
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end())
    {

        // update online status
        it->second.lastSeenTime = getCurrentTimestampMs();
        it->second.online = 1;

        // update battery level
        it->second.batteryLevel = batteryLevel;
        elog_v("DeviceManager", "Updated battery level for device 0x%08X: %d%%", deviceId, batteryLevel);
    }
}

std::vector<DeviceInfo> DeviceManager::getAllDeviceInfos() const
{
    std::vector<DeviceInfo> result;
    for (const auto &pair : deviceInfos)
    {
        result.push_back(pair.second);
    }
    return result;
}

bool DeviceManager::hasDeviceInfo(uint32_t deviceId) const
{
    return deviceInfos.find(deviceId) != deviceInfos.end();
}

DeviceInfo DeviceManager::getDeviceInfo(uint32_t deviceId) const
{
    auto it = deviceInfos.find(deviceId);
    return it != deviceInfos.end() ? it->second : DeviceInfo();
}

void DeviceManager::updateDeviceOnlineStatus(uint32_t timeoutMs)
{
}

void DeviceManager::markSlaveControlResponseReceived(uint32_t slaveId)
{
    elog_v("DeviceManager", "Marked slave control response received for slave 0x%08X", slaveId);
}

void DeviceManager::cleanupExpiredDevices(uint32_t timeoutMs)
{
    uint32_t currentTime = getCurrentTimestampMs();
    std::vector<uint32_t> devicesToRemove;

    // 收集需要删除的设备ID
    for (const auto &pair : deviceInfos)
    {
        const DeviceInfo &info = pair.second;
        if (currentTime - info.lastSeenTime > timeoutMs)
        {
            devicesToRemove.push_back(info.deviceId);
        }
    }

    // 删除超时的设备
    for (uint32_t deviceId : devicesToRemove)
    {
        elog_w("DeviceManager",
               "Device 0x%08X expired after %u ms of inactivity, removing from "
               "device list",
               deviceId, timeoutMs);
        removeDeviceInfo(deviceId);
    }

    if (!devicesToRemove.empty())
    {
        elog_i("DeviceManager", "Cleaned up %d expired devices", static_cast<int>(devicesToRemove.size()));
    }
}

// 从机复位状态管理方法实现
void DeviceManager::markSlaveForReset(uint32_t slaveId)
{
    slaveResetFlags[slaveId] = true;
    elog_v("DeviceManager", "Marked slave 0x%08X for reset", slaveId);
}

void DeviceManager::clearSlaveResetFlag(uint32_t slaveId)
{
    slaveResetFlags.erase(slaveId);
    elog_v("DeviceManager", "Cleared reset flag for slave 0x%08X", slaveId);
}

bool DeviceManager::isSlaveMarkedForReset(uint32_t slaveId) const
{
    auto it = slaveResetFlags.find(slaveId);
    return it != slaveResetFlags.end() && it->second;
}

void DeviceManager::clearAllResetFlags()
{
    slaveResetFlags.clear();
    elog_v("DeviceManager", "Cleared all slave reset flags");
}

void DeviceManager::clearAllDevices()
{
    // 清除所有设备信息
    size_t deviceCount = deviceInfos.size();
    deviceInfos.clear();

    // 清除连接的从机列表
    connectedSlaves.clear();

    // 清除从机短ID映射
    slaveShortIds.clear();

    // 重置短ID计数器和可用短ID池
    nextShortId = SHORT_ID_START;
    availableShortIds.clear();
    for (uint8_t id = SHORT_ID_START; id <= SHORT_ID_MAX; ++id)
    {
        availableShortIds.insert(id);
    }

    // 清除从机配置和顺序
    clearSlaveConfigs();

    // 清除复位标志
    clearAllResetFlags();

    elog_i("DeviceManager", "Cleared all device information (%d devices removed)", static_cast<int>(deviceCount));
}