#include "DeviceManager.h"

#include <algorithm>
#include <cstring>

#include "elog.h"

DeviceManager::DeviceManager()
    : currentMode(MODE_CONDUCTION), systemRunningStatus(SYSTEM_STATUS_STOP),
      configuredIntervalMs(0), // 0表示未配置，使用默认值
      nextShortId(SHORT_ID_START), dataCollectionActive(false), cycleState(CollectionCycleState::IDLE),
      offlineCheckEnabled(false) // 默认关闭掉线判断
{
    // 初始化短ID池，所有短ID都可用（虽然不再使用短ID，但保留代码以保持兼容性）
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

std::vector<uint32_t> DeviceManager::getAllSlavesInConfigOrder() const
{
    // 返回所有配置列表中的设备（包括离线设备）
    // 若设备掉线，不在设备列表中将其删除，下一次发送同步帧时依旧将该设备的设备配置发送出去
    return slaveConfigOrder;
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

void DeviceManager::updateSlaveHeartbeat(uint32_t deviceId, uint8_t batteryLevel)
{
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end())
    {

        // update online status
        it->second.lastSeenTime = getCurrentTimestampMs();
        // log.i deviceID and lastSeenTime
        elog_i("DeviceManager", "Updated heartbeat for device 0x%08X (lastSeenTime: %u)", deviceId,
               it->second.lastSeenTime);
        it->second.online = 1;

        // update battery level
        it->second.batteryLevel = batteryLevel;
        elog_v("DeviceManager", "Updated battery level for device 0x%08X: %d%%", deviceId, batteryLevel);
    }
}

void DeviceManager::updateDeviceLastSeenTime(uint32_t deviceId)
{
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end())
    {
        it->second.lastSeenTime = getCurrentTimestampMs();
        it->second.online = 1;
        elog_v("DeviceManager", "Updated lastSeenTime for device 0x%08X (lastSeenTime: %u)", deviceId,
               it->second.lastSeenTime);
    }
}

void DeviceManager::updateDeviceOnlineStatusFromDetectionData(uint32_t deviceId)
{
    // 通过检测数据更新设备在线状态
    // 设备是否在线只通过是否有检测数据上传来判断，并且收到检测数据后更新最后一次通信时间
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end())
    {
        uint32_t currentTime = getCurrentTimestampMs();
        it->second.lastSeenTime = currentTime;
        it->second.online = 1; // 收到检测数据，标记为在线
        elog_v("DeviceManager", "Updated device 0x%08X online status from detection data (lastSeenTime: %u)", deviceId,
               currentTime);
    }
    else
    {
        // 如果设备不在列表中，可能是新设备，但根据需求，设备列表由后端配置决定
        elog_w("DeviceManager", "Received detection data from unknown device 0x%08X", deviceId);
    }
}

void DeviceManager::resetAllDevicesLastSeenTime()
{
    // 在开始检测时，将设备列表里的所有设备的最后一次通信时间设置为当前时间
    uint32_t currentTime = getCurrentTimestampMs();
    for (auto &pair : deviceInfos)
    {
        pair.second.lastSeenTime = currentTime;
        pair.second.online = 1; // 重置时标记为在线
        elog_v("DeviceManager", "Reset lastSeenTime for device 0x%08X to %u", pair.first, currentTime);
    }
    elog_i("DeviceManager", "Reset lastSeenTime for all %d devices", static_cast<int>(deviceInfos.size()));
}

void DeviceManager::enableOfflineCheck()
{
    offlineCheckEnabled = true;
    elog_i("DeviceManager", "Offline check enabled");
}

void DeviceManager::disableOfflineCheck()
{
    offlineCheckEnabled = false;
    elog_i("DeviceManager", "Offline check disabled");
}

bool DeviceManager::isOfflineCheckEnabled() const
{
    return offlineCheckEnabled;
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
    // 只有在掉线判断启用时才执行检查
    if (!offlineCheckEnabled)
    {
        return;
    }

    uint32_t currentTime = getCurrentTimestampMs();

    // 检查所有设备，标记超时设备为离线（不删除）
    for (auto &pair : deviceInfos)
    {
        DeviceInfo &info = pair.second;
        if (currentTime - info.lastSeenTime > timeoutMs)
        {
            // 设备超时，标记为离线，但不删除
            if (info.online != 0)
            {
                info.online = 0;
                elog_w("DeviceManager", "Device 0x%08X marked as offline (timeout: %u ms)", pair.first, timeoutMs);
            }
        }
    }
}

void DeviceManager::markSlaveControlResponseReceived(uint32_t slaveId)
{
    elog_v("DeviceManager", "Marked slave control response received for slave 0x%08X", slaveId);
}

void DeviceManager::cleanupExpiredDevices(uint32_t timeoutMs)
{
    // 已废弃：不再删除设备，只标记为离线
    // 设备掉线时不在设备列表中将其删除，下一次发送同步帧时依旧将该设备的设备配置发送出去
    elog_v("DeviceManager",
           "cleanupExpiredDevices called but device removal is disabled - devices are only marked offline");
    updateDeviceOnlineStatus(timeoutMs);
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

    // 计算即将释放的buffer总大小
    size_t totalBufferSize = 0;
    for (const auto &pair : deviceInfos)
    {
        totalBufferSize += pair.second.conductionDataBuffer.size();
    }

    elog_i("DeviceManager", "Clearing all devices: %d device(s), releasing %d bytes of buffer memory",
           static_cast<int>(deviceCount), static_cast<int>(totalBufferSize));

    deviceInfos.clear(); // 自动释放所有 vector<uint8_t> buffer

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

    elog_i("DeviceManager", "All device information cleared successfully");
}

// 导通数据缓存管理方法实现
void DeviceManager::allocateConductionBuffers()
{
    // 计算总引脚数之和
    size_t totalPinCount = 0;
    for (const auto &pair : slaveConfigs)
    {
        totalPinCount += pair.second.conductionNum;
    }

    if (totalPinCount == 0)
    {
        elog_w("DeviceManager", "Total pin count is 0, no buffers allocated");
        return;
    }

    elog_i("DeviceManager", "========== Buffer Allocation Details ==========");
    elog_i("DeviceManager", "Total slaves: %d, Total pins (sum): %d", static_cast<int>(slaveConfigs.size()),
           static_cast<int>(totalPinCount));

    // 为每个从机分配缓存
    for (const auto &pair : slaveConfigs)
    {
        uint32_t slaveId = pair.first;
        uint8_t conductionNum = pair.second.conductionNum;

        // 缓存大小 = 从机引脚数 * 总引脚数 / 8，向上取整
        size_t bufferSize = (conductionNum * totalPinCount + 7) / 8;

        auto it = deviceInfos.find(slaveId);
        if (it != deviceInfos.end())
        {
            it->second.conductionDataSize = bufferSize;
            it->second.conductionDataBuffer.resize(bufferSize, 0);
            it->second.conductionDataReceived = false;
            it->second.deviceStatus = 0;

            elog_i("DeviceManager", "  Slave 0x%08X: pins=%d, buffer=%d bytes (formula: %d × %d ÷ 8 = %d)", slaveId,
                   conductionNum, static_cast<int>(bufferSize), conductionNum, static_cast<int>(totalPinCount),
                   static_cast<int>(bufferSize));
        }
        else
        {
            elog_w("DeviceManager", "  Slave 0x%08X not found in device list, skipping buffer allocation", slaveId);
        }
    }

    elog_i("DeviceManager", "========== Buffer Allocation Complete ==========");
}

void DeviceManager::storeConductionDataFragment(uint32_t slaveId, uint16_t deviceStatus, const uint8_t *data,
                                                size_t dataLen, uint8_t fragSeq, size_t mtu)
{
    auto it = deviceInfos.find(slaveId);
    if (it == deviceInfos.end())
    {
        elog_w("DeviceManager", "Cannot store conduction data: slave 0x%08X not found", slaveId);
        return;
    }

    DeviceInfo &info = it->second;

    // 更新设备状态（每个分片都携带最新的设备状态）
    info.deviceStatus = deviceStatus;

    // 计算偏移量
    // COND_DATA_MSG特殊处理：每个分片都包含完整的识别信息（Message ID + Slave ID + Device Status）
    // 这样即使丢失某些分片，也能独立识别和处理其他分片
    //
    // 帧格式：帧头(7字节) + Message ID(1) + Slave ID(4) + Device Status(2) + 导通数据
    // MTU 是完整帧的大小（帧头 + 载荷）
    //
    // 每个分片的导通数据长度 = MTU - 帧头(7) - 消息头部(7) = MTU - 14
    // 导通数据在缓存中的偏移量 = fragSeq × (MTU - 14)
    //
    // 无需判断fragSeq是否为0，所有分片使用统一的计算公式

    size_t maxDataPerFragment = (mtu > 14) ? (mtu - 14) : 0; // MTU - 帧头(7) - 消息头部(7)
    size_t offset = fragSeq * maxDataPerFragment;

    // 检查缓存边界
    if (offset + dataLen > info.conductionDataBuffer.size())
    {
        size_t overflowBytes = (offset + dataLen) - info.conductionDataBuffer.size();
        elog_e("DeviceManager",
               "Slave 0x%08X overflow: fragSeq=%d, offset=%d, Len=%d, bufferSize=%d, overflow=%d bytes", slaveId,
               fragSeq, static_cast<int>(offset), static_cast<int>(dataLen),
               static_cast<int>(info.conductionDataBuffer.size()), static_cast<int>(overflowBytes));
        return;
    }

    // 存储数据到缓存
    memcpy(info.conductionDataBuffer.data() + offset, data, dataLen);

    // 标记已接收数据
    info.conductionDataReceived = true;

    elog_i("DeviceManager", "Stored data for 0x%08X: fragSeq=%d, offset=%d, len=%d", slaveId, fragSeq,
           static_cast<int>(offset), static_cast<int>(dataLen));
}

void DeviceManager::resetConductionDataFlags()
{
    for (auto &pair : deviceInfos)
    {
        pair.second.conductionDataReceived = false;
        // 清空缓存（可选）
        std::fill(pair.second.conductionDataBuffer.begin(), pair.second.conductionDataBuffer.end(), 0);
    }
    elog_v("DeviceManager", "Reset conduction data flags for all devices");
}

bool DeviceManager::allConfiguredSlavesReceivedData() const
{
    // 检查所有配置的从机是否都已接收导通数据
    for (const auto &pair : slaveConfigs)
    {
        uint32_t slaveId = pair.first;
        auto it = deviceInfos.find(slaveId);
        if (it != deviceInfos.end())
        {
            if (!it->second.conductionDataReceived)
            {
                return false;
            }
        }
    }
    return !slaveConfigs.empty(); // 如果没有配置的从机，返回false
}