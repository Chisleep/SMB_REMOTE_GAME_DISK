#include "VirtualDisk/MemoryCache.h"
#include "Common/Logger.h"

#include <algorithm>
#include <cstring>

namespace rgh {

MemoryCache& MemoryCache::instance() {
    static MemoryCache cache;
    return cache;
}

void MemoryCache::init(size_t maxSizeMB) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxSizeBytes_ = maxSizeMB * 1024 * 1024;
    RGH_LOG_INFO("MemoryCache", "内存缓存初始化，上限: " + std::to_string(maxSizeMB) + " MB");
}

int MemoryCache::read(const std::string& filePath, uint64_t offset, void* buffer, uint32_t size) {
    if (size == 0) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    stats_.totalReads++;

    auto* dst = static_cast<uint8_t*>(buffer);
    int bytesServed = 0;
    uint64_t currentOffset = offset;
    uint32_t remaining = size;

    while (remaining > 0) {
        uint64_t blockStart = blockAlign(currentOffset);
        uint64_t blockOffset = currentOffset - blockStart;
        uint32_t canRead = static_cast<uint32_t>(std::min(static_cast<size_t>(BlockSize - blockOffset),
                                                           static_cast<size_t>(remaining)));

        BlockKey key{filePath, blockStart};
        auto it = blockMap_.find(key);

        if (it != blockMap_.end() && it->second.first.size() > blockOffset) {
            // 缓存命中
            uint32_t available = static_cast<uint32_t>(it->second.first.size() - blockOffset);
            uint32_t toCopy = std::min(canRead, available);

            std::memcpy(dst + bytesServed, it->second.first.data() + blockOffset, toCopy);

            // 更新 LRU
            lruList_.erase(it->second.second);
            lruList_.push_front(key);
            it->second.second = lruList_.begin();

            bytesServed += toCopy;
            currentOffset += toCopy;
            remaining -= toCopy;
            stats_.cacheHits++;
            stats_.bytesServed += toCopy;

            if (toCopy < canRead) {
                // 部分命中，剩余部分需要从 SMB 读取
                break;
            }
        } else {
            // 缓存未命中
            stats_.cacheMisses++;
            break;
        }
    }

    return bytesServed;
}

void MemoryCache::put(const std::string& filePath, uint64_t offset, const void* buffer, uint32_t size) {
    if (size == 0) return;

    std::lock_guard<std::mutex> lock(mutex_);

    const auto* src = static_cast<const uint8_t*>(buffer);
    uint64_t currentOffset = offset;
    uint32_t remaining = size;

    while (remaining > 0) {
        uint64_t blockStart = blockAlign(currentOffset);
        uint64_t blockOffset = currentOffset - blockStart;
        uint32_t canWrite = static_cast<uint32_t>(std::min(static_cast<size_t>(BlockSize - blockOffset),
                                                            static_cast<size_t>(remaining)));

        BlockKey key{filePath, blockStart};
        auto it = blockMap_.find(key);

        if (it == blockMap_.end()) {
            // 新块
            std::vector<uint8_t> blockData(blockOffset + canWrite, 0);
            std::memcpy(blockData.data() + blockOffset, src + (size - remaining), canWrite);

            size_t addedSize = blockData.size();
            currentSizeBytes_ += addedSize;

            lruList_.push_front(key);
            blockMap_[key] = {std::move(blockData), lruList_.begin()};
        } else {
            // 更新已有块
            auto& blockData = it->second.first;
            if (blockData.size() < blockOffset + canWrite) {
                size_t oldSize = blockData.size();
                blockData.resize(blockOffset + canWrite, 0);
                currentSizeBytes_ += (blockData.size() - oldSize);
            }
            std::memcpy(blockData.data() + blockOffset, src + (size - remaining), canWrite);

            // 更新 LRU
            lruList_.erase(it->second.second);
            lruList_.push_front(key);
            it->second.second = lruList_.begin();
        }

        currentOffset += canWrite;
        remaining -= canWrite;

        evictIfNeeded();
    }

    stats_.bytesCached = currentSizeBytes_;
}

void MemoryCache::invalidate(const std::string& filePath) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 移除该文件的所有缓存块
    for (auto it = blockMap_.begin(); it != blockMap_.end();) {
        if (it->first.first == filePath) {
            currentSizeBytes_ -= it->second.first.size();
            lruList_.erase(it->second.second);
            it = blockMap_.erase(it);
        } else {
            ++it;
        }
    }

    // 移除目录缓存
    dirCache_.erase(filePath);
}

void MemoryCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    blockMap_.clear();
    lruList_.clear();
    dirCache_.clear();
    currentSizeBytes_ = 0;
    RGH_LOG_INFO("MemoryCache", "缓存已清空");
}

void MemoryCache::evictIfNeeded() {
    while (currentSizeBytes_ > maxSizeBytes_ && !lruList_.empty()) {
        BlockKey& oldest = lruList_.back();
        auto it = blockMap_.find(oldest);
        if (it != blockMap_.end()) {
            currentSizeBytes_ -= it->second.first.size();
            blockMap_.erase(it);
        }
        lruList_.pop_back();
        stats_.evictions++;
    }
}

double MemoryCache::hitRate() const {
    uint64_t total = stats_.totalReads.load();
    if (total == 0) return 0.0;
    return static_cast<double>(stats_.cacheHits.load()) / total * 100.0;
}

bool MemoryCache::getDirCache(const std::string& dirPath, std::vector<std::string>& entries, uint32_t ttlSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = dirCache_.find(dirPath);
    if (it == dirCache_.end()) return false;

    std::time_t now = std::time(nullptr);
    if (static_cast<uint32_t>(now - it->second.cachedAt) > ttlSec) {
        dirCache_.erase(it);
        return false;
    }

    entries = it->second.entries;
    return true;
}

void MemoryCache::putDirCache(const std::string& dirPath, const std::vector<std::string>& entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    DirCacheEntry entry;
    entry.entries = entries;
    entry.cachedAt = std::time(nullptr);
    dirCache_[dirPath] = std::move(entry);
}

} // namespace rgh
