#include "DeviceManager.h"

#include <algorithm>

#include "elog.h"

DeviceManager::DeviceManager()
    : currentMode(0),
      systemRunningStatus(0),
      configuredIntervalMs(0),    // 0表示未配置，使用默认值
      dataCollectionActive(false),
      cycleState(CollectionCycleState::IDLE),
      nextShortId(1) {}    // 短ID从1开始分配

void DeviceManager::addSlave(uint32_t slaveId, uint8_t shortId) {
    connectedSlaves[slaveId] = true;
    if (shortId > 0) {
        slaveShortIds[slaveId] = shortId;
    }
}

void DeviceManager::removeSlave(uint32_t slaveId) {
    connectedSlaves[slaveId] = false;
}

bool DeviceManager::isSlaveConnected(uint32_t slaveId) const {
    auto it = connectedSlaves.find(slaveId);
    return it != connectedSlaves.end() && it->second;
}

std::vector<uint32_t> DeviceManager::getConnectedSlaves() const {
    std::vector<uint32_t> result;
    for (const auto& pair : connectedSlaves) {
        if (pair.second) {
            result.push_back(pair.first);
        }
    }
    return result;
}

std::vector<uint32_t> DeviceManager::getConnectedSlavesInConfigOrder() const {
    std::vector<uint32_t> result;
    for (uint32_t slaveId : slaveConfigOrder) {
        if (isSlaveConnected(slaveId)) {
            result.push_back(slaveId);
        }
    }
    return result;
}

uint8_t DeviceManager::getSlaveShortId(uint32_t slaveId) const {
    auto it = slaveShortIds.find(slaveId);
    return it != slaveShortIds.end() ? it->second : 0;
}

// Configuration management
void DeviceManager::setSlaveConfig(
    uint32_t slaveId,
    const Backend2Master::SlaveConfigMessage::SlaveInfo& config) {
    slaveConfigs[slaveId] = config;

    // Add to configuration order if not already present
    if (std::find(slaveConfigOrder.begin(), slaveConfigOrder.end(), slaveId) ==
        slaveConfigOrder.end()) {
        slaveConfigOrder.push_back(slaveId);
    }
}

Backend2Master::SlaveConfigMessage::SlaveInfo DeviceManager::getSlaveConfig(
    uint32_t slaveId) const {
    auto it = slaveConfigs.find(slaveId);
    return it != slaveConfigs.end()
               ? it->second
               : Backend2Master::SlaveConfigMessage::SlaveInfo{};
}

bool DeviceManager::hasSlaveConfig(uint32_t slaveId) const {
    return slaveConfigs.find(slaveId) != slaveConfigs.end();
}

void DeviceManager::clearSlaveConfigs() {
    slaveConfigs.clear();
    slaveConfigOrder.clear();
}

// Mode management
void DeviceManager::setCurrentMode(uint8_t mode) { currentMode = mode; }
uint8_t DeviceManager::getCurrentMode() const { return currentMode; }

// System status management
void DeviceManager::setSystemRunningStatus(uint8_t status) {
    systemRunningStatus = status;
}
uint8_t DeviceManager::getSystemRunningStatus() const {
    return systemRunningStatus;
}

// Interval configuration management
void DeviceManager::setConfiguredInterval(uint8_t intervalMs) {
    configuredIntervalMs = intervalMs;
    elog_v("DeviceManager", "Configured interval set to %u ms", intervalMs);
}

uint8_t DeviceManager::getConfiguredInterval() const {
    return configuredIntervalMs;
}

uint8_t DeviceManager::getEffectiveInterval() const {
    return configuredIntervalMs > 0 ? configuredIntervalMs
                                    : DEFAULT_INTERVAL_MS;
}

// 数据采集管理
void DeviceManager::startDataCollection() {
    elog_v("DeviceManager",
           "Starting data collection - mode: %d, total configs: %d",
           currentMode, slaveConfigs.size());

    // 检查是否有已配置且连接的从机
    bool hasConnectedSlaves = false;
    for (const auto& pair : slaveConfigs) {
        uint32_t slaveId = pair.first;
        if (isSlaveConnected(slaveId)) {
            hasConnectedSlaves = true;
            elog_v("DeviceManager", "Slave 0x%08X is connected and configured",
                   slaveId);
        }
    }

    dataCollectionActive = hasConnectedSlaves;
    cycleState = dataCollectionActive ? CollectionCycleState::COLLECTING
                                      : CollectionCycleState::IDLE;

    elog_v("DeviceManager", "Data collection started, mode: %d, active: %d",
           currentMode, dataCollectionActive ? 1 : 0);
}

void DeviceManager::resetDataCollection() {
    dataCollectionActive = false;
    cycleState = CollectionCycleState::IDLE;

    elog_v("DeviceManager", "Data collection reset");
}

// 注意：以下方法已移除，因为推送式架构不需要复杂的周期管理：
// - startNewCycle, markSyncSent, shouldEnterReadingPhase, enterReadingPhase
// - markCollectionStarted, isSlaveCollectionComplete, markDataRequested

// 标记数据已接收 (简化版)
void DeviceManager::markDataReceived(uint32_t slaveId) {
    elog_v("DeviceManager", "Data received from slave 0x%08X", slaveId);
    // 在推送式架构中，这个方法主要用于日志记录
    // 不需要复杂的周期状态管理
}

// 获取当前采集周期状态
CollectionCycleState DeviceManager::getCycleState() const { return cycleState; }

// 是否有活跃的数据采集
bool DeviceManager::isDataCollectionActive() const {
    return dataCollectionActive;
}

// 设备信息管理方法实现
void DeviceManager::addDeviceInfo(uint32_t deviceId, uint8_t versionMajor,
                                  uint8_t versionMinor, uint16_t versionPatch) {
    uint32_t currentTime = getCurrentTimestampMs();

    auto it = deviceInfos.find(deviceId);
    if (it == deviceInfos.end()) {
        // 新设备
        DeviceInfo info(deviceId, versionMajor, versionMinor, versionPatch);
        info.announceTime = currentTime;
        info.announceCount = 1;
        info.lastSeenTime = currentTime;
        deviceInfos[deviceId] = info;

        elog_i("DeviceManager", "Added new device 0x%08X (v%d.%d.%d)", deviceId,
               versionMajor, versionMinor, versionPatch);
    } else {
        // 已存在设备，更新信息
        it->second.lastSeenTime = currentTime;
        it->second.versionMajor = versionMajor;
        it->second.versionMinor = versionMinor;
        it->second.versionPatch = versionPatch;

        elog_v("DeviceManager", "Updated existing device 0x%08X", deviceId);
    }
}

void DeviceManager::updateDeviceAnnounce(uint32_t deviceId) {
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end()) {
        it->second.announceCount++;
        it->second.lastSeenTime = getCurrentTimestampMs();

        elog_v("DeviceManager", "Device 0x%08X announce count: %d", deviceId,
               it->second.announceCount);
    }
}

void DeviceManager::removeDeviceInfo(uint32_t deviceId) {
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end()) {
        elog_i("DeviceManager", "Removing device 0x%08X from device list", deviceId);
        
        // only remove device info, not slave info
        deviceInfos.erase(it);
    
        // // 同时从连接的从机列表中删除
        // removeSlave(deviceId);
        
        // // 从从机配置中删除
        // auto configIt = slaveConfigs.find(deviceId);
        // if (configIt != slaveConfigs.end()) {
        //     slaveConfigs.erase(configIt);
        //     elog_v("DeviceManager", "Removed slave config for device 0x%08X", deviceId);
        // }
        
        // // 从配置顺序中删除
        // auto orderIt = std::find(slaveConfigOrder.begin(), slaveConfigOrder.end(), deviceId);
        // if (orderIt != slaveConfigOrder.end()) {
        //     slaveConfigOrder.erase(orderIt);
        //     elog_v("DeviceManager", "Removed device 0x%08X from config order", deviceId);
        // }
        
        elog_i("DeviceManager", "Device 0x%08X completely removed from all lists", deviceId);
    }
}

bool DeviceManager::shouldAssignShortId(uint32_t deviceId) const {
    auto it = deviceInfos.find(deviceId);
    if (it == deviceInfos.end()) {
        return false;
    }

    // 如果还没有分配短ID，且宣告次数在合理范围内（1-3次）
    return !it->second.shortIdAssigned && it->second.announceCount <= 3;
}

uint8_t DeviceManager::assignShortId(uint32_t deviceId) {
    auto it = deviceInfos.find(deviceId);
    if (it == deviceInfos.end()) {
        return 0;
    }

    // 分配短ID
    uint8_t assignedId = nextShortId++;
    it->second.shortId = assignedId;
    it->second.shortIdAssigned = true;
    it->second.lastSeenTime = getCurrentTimestampMs();

    // 避免短ID溢出，从1开始循环
    if (nextShortId > 254) {
        nextShortId = 1;
    }

    elog_i("DeviceManager", "Assigned short ID %d to device 0x%08X", assignedId,
           deviceId);

    return assignedId;
}

void DeviceManager::confirmShortId(uint32_t deviceId, uint8_t shortId) {
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end()) {
        it->second.shortId = shortId;
        it->second.shortIdAssigned = true;
        it->second.online = 1;
        it->second.lastSeenTime = getCurrentTimestampMs();

        // 同时更新旧的连接状态管理
        addSlave(deviceId, shortId);

        elog_i("DeviceManager", "Confirmed short ID %d for device 0x%08X",
               shortId, deviceId);
    }
}

void DeviceManager::updateDeviceLastSeen(uint32_t deviceId) {
    auto it = deviceInfos.find(deviceId);
    if (it != deviceInfos.end()) {
        it->second.lastSeenTime = getCurrentTimestampMs();
        it->second.online = 1;
    }
}

std::vector<DeviceInfo> DeviceManager::getAllDeviceInfos() const {
    std::vector<DeviceInfo> result;
    for (const auto& pair : deviceInfos) {
        result.push_back(pair.second);
    }
    return result;
}

bool DeviceManager::hasDeviceInfo(uint32_t deviceId) const {
    return deviceInfos.find(deviceId) != deviceInfos.end();
}

DeviceInfo DeviceManager::getDeviceInfo(uint32_t deviceId) const {
    auto it = deviceInfos.find(deviceId);
    return it != deviceInfos.end() ? it->second : DeviceInfo();
}

void DeviceManager::updateDeviceOnlineStatus(uint32_t timeoutMs) {
    // uint32_t currentTime = getCurrentTimestampMs();
    // int offlineCount = 0;
    // int onlineCount = 0;

    // for (auto& pair : deviceInfos) {
    //     DeviceInfo& info = pair.second;
    //     bool wasOnline = info.online;

    //     // 检查设备是否超时 - 这里只更新在线状态，不删除设备
    //     // 设备的实际删除由cleanupExpiredDevices()方法处理
    //     if (currentTime - info.lastSeenTime > timeoutMs) {
    //         if (wasOnline) {
    //             info.online = 0;
    //             offlineCount++;
    //             elog_w(
    //                 "DeviceManager",
    //                 "Device 0x%08X went offline in device info (timeout=%u ms)",
    //                 info.deviceId, timeoutMs);

    //             // 注意：不要调用removeSlave，保持现有的数据采集逻辑不受影响
    //             // 设备信息的在线状态仅用于后端查询，不影响数据采集
    //         }
    //     } else {
    //         if (!wasOnline) {
    //             info.online = 1;
    //             onlineCount++;
    //             elog_i("DeviceManager",
    //                    "Device 0x%08X came back online in device info",
    //                    info.deviceId);

    //             // 注意：不要调用addSlave，保持现有的数据采集逻辑不受影响
    //             // 设备重新在线的状态仅更新设备信息，不自动加入数据采集
    //         }
    //     }
    // }

    // if (offlineCount > 0 || onlineCount > 0) {
    //     elog_v("DeviceManager",
    //            "Device info status update: %d went offline, %d came online "
    //            "(device cleanup handled separately by cleanupExpiredDevices)",
    //            offlineCount, onlineCount);
    // }
}

void DeviceManager::markSlaveControlResponseReceived(uint32_t slaveId) {
    // 更新设备最后通信时间
    updateDeviceLastSeen(slaveId);

    elog_v("DeviceManager",
           "Marked slave control response received for slave 0x%08X", slaveId);
}

void DeviceManager::cleanupExpiredDevices(uint32_t timeoutMs) {
    uint32_t currentTime = getCurrentTimestampMs();
    std::vector<uint32_t> devicesToRemove;
    
    // 收集需要删除的设备ID
    for (const auto& pair : deviceInfos) {
        const DeviceInfo& info = pair.second;
        if (currentTime - info.lastSeenTime > timeoutMs) {
            devicesToRemove.push_back(info.deviceId);
        }
    }
    
    // 删除超时的设备
    for (uint32_t deviceId : devicesToRemove) {
        elog_w("DeviceManager", 
               "Device 0x%08X expired after %u ms of inactivity, removing from device list",
               deviceId, timeoutMs);
        removeDeviceInfo(deviceId);
    }
    
    if (!devicesToRemove.empty()) {
        elog_i("DeviceManager", "Cleaned up %d expired devices", 
               static_cast<int>(devicesToRemove.size()));
    }
}